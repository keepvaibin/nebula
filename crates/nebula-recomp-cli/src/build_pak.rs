use anyhow::{bail, Context, Result};
use nebula_recomp_core::{analyze_input, parse_fst, FstFile, RMGE01_DOL_SHA1, RMGE01_GAME_ID};
use sha1::{Digest, Sha1};
use std::{
    collections::HashSet,
    ffi::OsString,
    fs::{self, File, OpenOptions},
    io::{self, Read, Seek, SeekFrom, Write},
    os::windows::{ffi::OsStrExt, io::AsRawHandle},
    path::{Component, Path, PathBuf},
    sync::atomic::{AtomicU64, Ordering},
};
use windows_sys::Win32::{
    Storage::FileSystem::{MoveFileExW, MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH},
    System::{Ioctl::FSCTL_SET_SPARSE, IO::DeviceIoControl},
};

/// Disc-partition byte offsets for the fixed system files.
const BOOT_BIN_DISC_OFFSET: u64 = 0x0000_0000;
const BI2_BIN_DISC_OFFSET: u64 = 0x0000_0440;
const APPLOADER_DISC_OFFSET: u64 = 0x0000_2440;
const BOOT_BIN_SIZE: usize = 0x440;
const BI2_BIN_SIZE: usize = 0x2000;
const APPLOADER_HEADER_SIZE: usize = 0x20;
const TMD_SIZE: usize = 0x208;
const TMD_SIGNATURE_TYPE: u32 = 0x0001_0001;
const TMD_CONTENT_COUNT_OFFSET: usize = 0x1de;
const TMD_CONTENT_RECORD_OFFSET: usize = 0x1e4;
const TMD_CONTENT_RECORD_SIZE: usize = 36;
const RMGE01_PARTITION_DATA_BYTES: u64 = 0xff7c_0000;

/// DI commands carry a 32-bit word offset. The largest representable byte
/// address is therefore one byte below 2^34.
const PARTITION_ADDRESS_SPACE_BYTES: u64 = 1_u64 << 34;

static NEXT_TEMP_ID: AtomicU64 = AtomicU64::new(0);

struct SystemFile {
    label: &'static str,
    disc_offset: u64,
    bytes: Vec<u8>,
}

struct PlannedFile {
    fst_path: String,
    source_path: PathBuf,
    disc_offset: u64,
    size: u64,
}

struct PakPlan {
    system_files: Vec<SystemFile>,
    game_files: Vec<PlannedFile>,
    dol_disc_offset: u64,
    fst_disc_offset: u64,
    logical_size: u64,
}

#[derive(Debug)]
struct Extent {
    label: String,
    start: u64,
    end: u64,
}

struct TemporaryOutput {
    path: PathBuf,
    file: Option<File>,
    armed: bool,
}

impl TemporaryOutput {
    fn new(path: PathBuf, file: File) -> Self {
        Self {
            path,
            file: Some(file),
            armed: true,
        }
    }

    fn file(&self) -> &File {
        self.file.as_ref().expect("temporary file is open")
    }

    fn file_mut(&mut self) -> &mut File {
        self.file.as_mut().expect("temporary file is open")
    }

    fn close(&mut self) {
        drop(self.file.take());
    }

    fn disarm(&mut self) {
        self.armed = false;
    }
}

impl Drop for TemporaryOutput {
    fn drop(&mut self) {
        self.close();
        if self.armed {
            let _ = fs::remove_file(&self.path);
        }
    }
}

fn read_u32_be(data: &[u8], offset: usize) -> Result<u32> {
    let bytes = data
        .get(offset..offset + 4)
        .with_context(|| format!("big-endian u32 at offset {offset:#x} is out of bounds"))?;
    Ok(u32::from_be_bytes(
        bytes.try_into().expect("validated four-byte slice"),
    ))
}

fn read_u16_be(data: &[u8], offset: usize) -> Result<u16> {
    let bytes = data
        .get(offset..offset + 2)
        .with_context(|| format!("big-endian u16 at offset {offset:#x} is out of bounds"))?;
    Ok(u16::from_be_bytes(
        bytes.try_into().expect("validated two-byte slice"),
    ))
}

fn read_u64_be(data: &[u8], offset: usize) -> Result<u64> {
    let bytes = data
        .get(offset..offset + 8)
        .with_context(|| format!("big-endian u64 at offset {offset:#x} is out of bounds"))?;
    Ok(u64::from_be_bytes(
        bytes.try_into().expect("validated eight-byte slice"),
    ))
}

fn word_offset(value: u32) -> u64 {
    u64::from(value) * 4
}

fn resolve_data_root(input: &Path) -> Result<PathBuf> {
    let nested = input.join("DATA");
    if nested.join("sys").join("boot.bin").is_file() {
        return Ok(nested);
    }
    if input.join("sys").join("boot.bin").is_file() {
        return Ok(input.to_owned());
    }
    bail!("cannot find DATA/sys/boot.bin under {}", input.display())
}

fn canonical_directory(path: &Path, label: &str) -> Result<PathBuf> {
    let canonical = fs::canonicalize(path)
        .with_context(|| format!("failed to resolve {label} directory {}", path.display()))?;
    let metadata = fs::metadata(&canonical).with_context(|| {
        format!(
            "failed to inspect {label} directory {}",
            canonical.display()
        )
    })?;
    if !metadata.is_dir() {
        bail!("{label} path is not a directory: {}", canonical.display());
    }
    Ok(canonical)
}

fn canonical_subdirectory_under(root: &Path, relative: &Path, label: &str) -> Result<PathBuf> {
    let mut requested = root.to_owned();
    for component in relative.components() {
        let Component::Normal(name) = component else {
            bail!(
                "{label} has an unsafe relative path: {}",
                relative.display()
            );
        };
        requested.push(name);
        let metadata = fs::symlink_metadata(&requested)
            .with_context(|| format!("failed to inspect {label} {}", requested.display()))?;
        if metadata.file_type().is_symlink() {
            bail!(
                "{label} traverses a symbolic link/reparse path component: {}",
                requested.display()
            );
        }
    }

    let canonical = canonical_directory(&requested, label)?;
    if !canonical.starts_with(root) {
        bail!(
            "{label} resolves outside the DATA root: {}",
            canonical.display()
        );
    }
    Ok(canonical)
}

fn resolve_regular_file_under(root: &Path, relative: &Path, label: &str) -> Result<PathBuf> {
    let mut requested = root.to_owned();
    for component in relative.components() {
        let Component::Normal(name) = component else {
            bail!(
                "{label} has an unsafe relative path: {}",
                relative.display()
            );
        };
        requested.push(name);
        let metadata = fs::symlink_metadata(&requested)
            .with_context(|| format!("failed to inspect {label} {}", requested.display()))?;
        if metadata.file_type().is_symlink() {
            bail!(
                "{label} traverses a symbolic link/reparse path component: {}",
                requested.display()
            );
        }
    }

    let canonical = fs::canonicalize(&requested)
        .with_context(|| format!("failed to resolve {label} {}", requested.display()))?;
    if !canonical.starts_with(root) {
        bail!(
            "{label} resolves outside its source root: {}",
            canonical.display()
        );
    }
    let metadata = fs::metadata(&canonical)
        .with_context(|| format!("failed to inspect {label} {}", canonical.display()))?;
    if !metadata.is_file() {
        bail!("{label} is not a regular file: {}", canonical.display());
    }
    Ok(canonical)
}

fn read_source_file(root: &Path, relative: &Path, label: &str) -> Result<(PathBuf, Vec<u8>)> {
    let path = resolve_regular_file_under(root, relative, label)?;
    let bytes =
        fs::read(&path).with_context(|| format!("failed to read {label} {}", path.display()))?;
    Ok((path, bytes))
}

fn sha1_hex(bytes: &[u8]) -> String {
    format!("{:x}", Sha1::digest(bytes))
}

fn validate_rmge01_tmd(tmd: &[u8]) -> Result<u64> {
    if tmd.len() != TMD_SIZE {
        bail!(
            "tmd.bin has {} bytes; expected exactly {TMD_SIZE}",
            tmd.len()
        );
    }
    let signature_type = read_u32_be(tmd, 0)?;
    if signature_type != TMD_SIGNATURE_TYPE {
        bail!(
            "tmd.bin has signature type {signature_type:#010x}; expected {TMD_SIGNATURE_TYPE:#010x}"
        );
    }
    let content_count = read_u16_be(tmd, TMD_CONTENT_COUNT_OFFSET)?;
    if content_count != 1 {
        bail!("tmd.bin declares {content_count} content records; expected exactly one");
    }
    let table_end = TMD_CONTENT_RECORD_OFFSET
        .checked_add(usize::from(content_count) * TMD_CONTENT_RECORD_SIZE)
        .context("tmd.bin content table size overflows host memory")?;
    if table_end != tmd.len() {
        bail!(
            "tmd.bin content table ends at {table_end:#x}; expected exact file end {:#x}",
            tmd.len()
        );
    }

    let content_id = read_u32_be(tmd, TMD_CONTENT_RECORD_OFFSET)?;
    let content_index = read_u16_be(tmd, TMD_CONTENT_RECORD_OFFSET + 4)?;
    let content_type = read_u16_be(tmd, TMD_CONTENT_RECORD_OFFSET + 6)?;
    if content_id != 0 || content_index != 0 || content_type != 3 {
        bail!(
            "tmd.bin has unexpected RMGE01 content record: id={content_id:#x} index={content_index} type={content_type:#x}"
        );
    }
    let partition_size = read_u64_be(tmd, TMD_CONTENT_RECORD_OFFSET + 8)?;
    if partition_size != RMGE01_PARTITION_DATA_BYTES {
        bail!(
            "tmd.bin declares partition data size {partition_size:#x}; expected exact RMGE01 size {RMGE01_PARTITION_DATA_BYTES:#x}"
        );
    }
    Ok(partition_size)
}

fn validate_windows_component(component: &str, fst_path: &str) -> Result<()> {
    if component.is_empty()
        || component == "."
        || component == ".."
        || component.ends_with(' ')
        || component.ends_with('.')
        || component.chars().any(|character| {
            character <= '\u{1f}'
                || matches!(
                    character,
                    '<' | '>' | ':' | '"' | '/' | '\\' | '|' | '?' | '*'
                )
        })
    {
        bail!("FST path is not a canonical Windows relative path: {fst_path:?}");
    }

    let stem = component.split('.').next().unwrap_or(component);
    let upper = stem.to_ascii_uppercase();
    let device_number = upper
        .strip_prefix("COM")
        .or_else(|| upper.strip_prefix("LPT"));
    let reserved = matches!(upper.as_str(), "CON" | "PRN" | "AUX" | "NUL")
        || device_number.is_some_and(|number| {
            matches!(
                number,
                "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" | "¹" | "²" | "³"
            )
        });
    if reserved {
        bail!("FST path uses a reserved Windows device name: {fst_path:?}");
    }
    Ok(())
}

fn logical_windows_path(fst_path: &str) -> Result<(PathBuf, String)> {
    if fst_path.is_empty() || fst_path.starts_with('/') || fst_path.starts_with('\\') {
        bail!("FST path is empty or absolute: {fst_path:?}");
    }
    let mut relative = PathBuf::new();
    let mut canonical_components = Vec::new();
    for component in fst_path.split('/') {
        validate_windows_component(component, fst_path)?;
        relative.push(component);
        canonical_components.push(component.to_lowercase());
    }
    Ok((relative, canonical_components.join("\\")))
}

fn path_key(path: &Path) -> String {
    path.as_os_str().to_string_lossy().to_lowercase()
}

fn add_extent(
    extents: &mut Vec<Extent>,
    label: impl Into<String>,
    start: u64,
    size: u64,
    partition_size: u64,
) -> Result<()> {
    let label = label.into();
    let end = start
        .checked_add(size)
        .with_context(|| format!("partition extent for {label} overflows u64"))?;
    if partition_size > PARTITION_ADDRESS_SPACE_BYTES {
        bail!(
            "partition size {partition_size:#x} exceeds the DI word-addressable range {PARTITION_ADDRESS_SPACE_BYTES:#x}"
        );
    }
    if start > partition_size || end > partition_size {
        bail!(
            "partition extent for {label} is outside the exact RMGE01 partition: offset={start:#x} size={size:#x} limit={partition_size:#x}"
        );
    }
    extents.push(Extent { label, start, end });
    Ok(())
}

fn validate_extents(mut extents: Vec<Extent>) -> Result<u64> {
    extents.retain(|extent| extent.start != extent.end);
    extents.sort_by(|left, right| {
        left.start
            .cmp(&right.start)
            .then(left.end.cmp(&right.end))
            .then(left.label.cmp(&right.label))
    });

    for pair in extents.windows(2) {
        let previous = &pair[0];
        let current = &pair[1];
        if previous.end > current.start {
            bail!(
                "overlapping partition extents: {} [{:#x}, {:#x}) and {} [{:#x}, {:#x})",
                previous.label,
                previous.start,
                previous.end,
                current.label,
                current.start,
                current.end
            );
        }
    }

    Ok(extents.last().map_or(0, |extent| extent.end))
}

fn validate_destination(output: &Path, source_paths: &HashSet<String>) -> Result<()> {
    if output.file_name().is_none() {
        bail!("output path must name a file: {}", output.display());
    }
    match fs::symlink_metadata(output) {
        Ok(metadata) => {
            if metadata.file_type().is_symlink() {
                bail!(
                    "refusing to replace a symbolic-link output: {}",
                    output.display()
                );
            }
            if !metadata.is_file() {
                bail!(
                    "output exists and is not a regular file: {}",
                    output.display()
                );
            }
            let canonical = fs::canonicalize(output).with_context(|| {
                format!("failed to resolve existing output {}", output.display())
            })?;
            if source_paths.contains(&path_key(&canonical)) {
                bail!(
                    "output aliases an input file and would destroy source content: {}",
                    canonical.display()
                );
            }
        }
        Err(error) if error.kind() == io::ErrorKind::NotFound => {}
        Err(error) => {
            return Err(error)
                .with_context(|| format!("failed to inspect output {}", output.display()));
        }
    }
    Ok(())
}

fn preflight(input: &Path, output: &Path) -> Result<PakPlan> {
    let data_root = resolve_data_root(input)?;

    // This is the same exact game-ID and main.dol SHA-1 gate used by verify,
    // translation, and module generation. Packaging never accepts a merely
    // FST-shaped or lookalike dump.
    analyze_input(&data_root).with_context(|| {
        format!(
            "exact RMGE01 validation failed before packaging {}",
            data_root.display()
        )
    })?;

    let data_root = canonical_directory(&data_root, "DATA")?;
    let files_root = canonical_subdirectory_under(&data_root, Path::new("files"), "DATA/files")?;

    let (boot_path, boot_bin) =
        read_source_file(&data_root, Path::new("sys/boot.bin"), "boot.bin")?;
    let (bi2_path, bi2_bin) = read_source_file(&data_root, Path::new("sys/bi2.bin"), "bi2.bin")?;
    let (apploader_path, apploader) =
        read_source_file(&data_root, Path::new("sys/apploader.img"), "apploader.img")?;
    let (main_dol_path, main_dol) =
        read_source_file(&data_root, Path::new("sys/main.dol"), "main.dol")?;
    let (fst_path, fst_bin) = read_source_file(&data_root, Path::new("sys/fst.bin"), "fst.bin")?;
    let (tmd_path, tmd_bin) = read_source_file(&data_root, Path::new("tmd.bin"), "tmd.bin")?;

    if boot_bin.len() != BOOT_BIN_SIZE {
        bail!(
            "boot.bin has {} bytes; expected exactly {BOOT_BIN_SIZE}",
            boot_bin.len()
        );
    }
    if &boot_bin[..RMGE01_GAME_ID.len()] != RMGE01_GAME_ID.as_bytes() {
        bail!("boot.bin changed after exact RMGE01 validation");
    }
    if bi2_bin.len() != BI2_BIN_SIZE {
        bail!(
            "bi2.bin has {} bytes; expected exactly {BI2_BIN_SIZE}",
            bi2_bin.len()
        );
    }
    if apploader.len() < APPLOADER_HEADER_SIZE {
        bail!("apploader.img is shorter than its 0x20-byte header");
    }
    let apploader_code_size = usize::try_from(read_u32_be(&apploader, 0x14)?)
        .context("apploader code size does not fit host memory")?;
    let apploader_trailer_size = usize::try_from(read_u32_be(&apploader, 0x18)?)
        .context("apploader trailer size does not fit host memory")?;
    let expected_apploader_size = APPLOADER_HEADER_SIZE
        .checked_add(apploader_code_size)
        .and_then(|size| size.checked_add(apploader_trailer_size))
        .context("apploader.img declared size overflows host memory")?;
    if apploader.len() != expected_apploader_size {
        bail!(
            "apploader.img size mismatch: header declares {expected_apploader_size} bytes, source has {}",
            apploader.len()
        );
    }

    let packed_dol_hash = sha1_hex(&main_dol);
    if packed_dol_hash != RMGE01_DOL_SHA1 {
        bail!(
            "main.dol changed after exact validation: SHA-1 {packed_dol_hash}, expected {RMGE01_DOL_SHA1}"
        );
    }

    let dol_disc_offset = word_offset(read_u32_be(&boot_bin, 0x420)?);
    let fst_disc_offset = word_offset(read_u32_be(&boot_bin, 0x424)?);
    let fst_declared_size = word_offset(read_u32_be(&boot_bin, 0x428)?);
    let fst_max_size = word_offset(read_u32_be(&boot_bin, 0x42c)?);
    if dol_disc_offset == 0 || fst_disc_offset == 0 {
        bail!("boot.bin DOL/FST offsets are zero; input is not a valid RMGE01 dump");
    }
    if fst_declared_size != fst_bin.len() as u64 {
        bail!(
            "fst.bin size mismatch: boot.bin declares {fst_declared_size} bytes, source has {}",
            fst_bin.len()
        );
    }
    if fst_max_size < fst_declared_size {
        bail!(
            "boot.bin FST maximum {fst_max_size} is smaller than declared size {fst_declared_size}"
        );
    }
    let partition_size = validate_rmge01_tmd(&tmd_bin)?;

    let fst_files = parse_fst(&fst_bin).context("failed to validate fst.bin structure")?;
    let mut logical_paths = HashSet::new();
    let mut source_paths = HashSet::from([
        path_key(&boot_path),
        path_key(&bi2_path),
        path_key(&apploader_path),
        path_key(&main_dol_path),
        path_key(&fst_path),
        path_key(&tmd_path),
    ]);
    let mut game_files = Vec::with_capacity(fst_files.len());

    for FstFile {
        host_rel_path,
        disc_offset,
        size,
    } in fst_files
    {
        let (relative, logical_key) = logical_windows_path(&host_rel_path)?;
        if !logical_paths.insert(logical_key) {
            bail!("FST contains duplicate canonical Windows path {host_rel_path:?}");
        }
        let source_path = resolve_regular_file_under(
            &files_root,
            &relative,
            &format!("FST file {host_rel_path:?}"),
        )?;
        let source_key = path_key(&source_path);
        if !source_paths.insert(source_key) {
            bail!(
                "FST path {host_rel_path:?} aliases another canonical source file: {}",
                source_path.display()
            );
        }
        let source_size = fs::metadata(&source_path)
            .with_context(|| format!("failed to inspect FST file {}", source_path.display()))?
            .len();
        if source_size != u64::from(size) {
            bail!(
                "size mismatch for {host_rel_path:?}: FST says {size} bytes, source file is {source_size} bytes"
            );
        }
        game_files.push(PlannedFile {
            fst_path: host_rel_path,
            source_path,
            disc_offset,
            size: u64::from(size),
        });
    }

    let system_files = vec![
        SystemFile {
            label: "boot.bin",
            disc_offset: BOOT_BIN_DISC_OFFSET,
            bytes: boot_bin,
        },
        SystemFile {
            label: "bi2.bin",
            disc_offset: BI2_BIN_DISC_OFFSET,
            bytes: bi2_bin,
        },
        SystemFile {
            label: "apploader.img",
            disc_offset: APPLOADER_DISC_OFFSET,
            bytes: apploader,
        },
        SystemFile {
            label: "main.dol",
            disc_offset: dol_disc_offset,
            bytes: main_dol,
        },
        SystemFile {
            label: "fst.bin",
            disc_offset: fst_disc_offset,
            bytes: fst_bin,
        },
    ];

    let mut extents = Vec::with_capacity(system_files.len() + game_files.len());
    for system in &system_files {
        let size = u64::try_from(system.bytes.len())
            .with_context(|| format!("{} size does not fit u64", system.label))?;
        add_extent(
            &mut extents,
            system.label,
            system.disc_offset,
            size,
            partition_size,
        )?;
    }
    // Even if the current fst.bin is smaller, its boot-header maximum is a
    // reserved fixed-system region which game files must not overlap.
    if fst_max_size > fst_declared_size {
        add_extent(
            &mut extents,
            "fst.bin reserved tail",
            fst_disc_offset + fst_declared_size,
            fst_max_size - fst_declared_size,
            partition_size,
        )?;
    }
    for game_file in &game_files {
        add_extent(
            &mut extents,
            format!("FST file {:?}", game_file.fst_path),
            game_file.disc_offset,
            game_file.size,
            partition_size,
        )?;
    }
    let logical_size = validate_extents(extents)?;

    // Destination inspection is the final read-only preflight. No output
    // directory or temporary file has been created yet.
    validate_destination(output, &source_paths)?;

    Ok(PakPlan {
        system_files,
        game_files,
        dol_disc_offset,
        fst_disc_offset,
        logical_size,
    })
}

fn mark_sparse(file: &File) -> Result<()> {
    let mut bytes_returned = 0_u32;
    let succeeded = unsafe {
        DeviceIoControl(
            file.as_raw_handle() as _,
            FSCTL_SET_SPARSE,
            std::ptr::null_mut(),
            0,
            std::ptr::null_mut(),
            0,
            &mut bytes_returned,
            std::ptr::null_mut(),
        )
    };
    if succeeded == 0 {
        return Err(io::Error::last_os_error())
            .context("FSCTL_SET_SPARSE failed; build-pak requires a sparse NTFS destination");
    }
    Ok(())
}

fn write_at(file: &mut File, offset: u64, data: &[u8], label: &str) -> Result<()> {
    file.seek(SeekFrom::Start(offset))
        .with_context(|| format!("failed to seek to {offset:#x} for {label}"))?;
    file.write_all(data)
        .with_context(|| format!("failed to write {label} at {offset:#x}"))?;
    Ok(())
}

fn write_source_at(output: &mut File, planned: &PlannedFile) -> Result<()> {
    let mut source = File::open(&planned.source_path).with_context(|| {
        format!(
            "failed to reopen FST file {:?} from {}",
            planned.fst_path,
            planned.source_path.display()
        )
    })?;
    let before_size = source
        .metadata()
        .with_context(|| format!("failed to inspect {}", planned.source_path.display()))?
        .len();
    if before_size != planned.size {
        bail!(
            "source size changed after preflight for {:?}: expected {}, now {}",
            planned.fst_path,
            planned.size,
            before_size
        );
    }

    output
        .seek(SeekFrom::Start(planned.disc_offset))
        .with_context(|| {
            format!(
                "failed to seek to {:#x} for {:?}",
                planned.disc_offset, planned.fst_path
            )
        })?;
    let copied = {
        let mut bounded = (&mut source).take(planned.size);
        io::copy(&mut bounded, output).with_context(|| {
            format!(
                "failed to copy {:?} from {}",
                planned.fst_path,
                planned.source_path.display()
            )
        })?
    };
    if copied != planned.size {
        bail!(
            "source became short while copying {:?}: expected {}, copied {}",
            planned.fst_path,
            planned.size,
            copied
        );
    }
    let mut extra = [0_u8; 1];
    if source
        .read(&mut extra)
        .with_context(|| format!("failed to verify EOF for {:?}", planned.fst_path))?
        != 0
    {
        bail!("source grew while copying {:?}", planned.fst_path);
    }
    let after_size = source
        .metadata()
        .with_context(|| format!("failed to re-inspect {}", planned.source_path.display()))?
        .len();
    if after_size != planned.size {
        bail!(
            "source size changed while copying {:?}: expected {}, now {}",
            planned.fst_path,
            planned.size,
            after_size
        );
    }
    Ok(())
}

fn sibling_temp_name(output: &Path, sequence: u64) -> Result<PathBuf> {
    let file_name = output
        .file_name()
        .with_context(|| format!("output path must name a file: {}", output.display()))?;
    let mut temporary_name = OsString::from(file_name);
    temporary_name.push(format!(".tmp.{}.{}", std::process::id(), sequence));
    let parent = output
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    Ok(parent.join(temporary_name))
}

fn create_sibling_temp(output: &Path) -> Result<(PathBuf, File)> {
    for _ in 0..64 {
        let sequence = NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed) + 1;
        let temporary = sibling_temp_name(output, sequence)?;
        match OpenOptions::new()
            .write(true)
            .read(true)
            .create_new(true)
            .open(&temporary)
        {
            Ok(file) => return Ok((temporary, file)),
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => continue,
            Err(error) => {
                return Err(error).with_context(|| {
                    format!("failed to create sibling temporary {}", temporary.display())
                });
            }
        }
    }
    bail!(
        "failed to allocate a unique sibling temporary for {} after 64 attempts",
        output.display()
    )
}

fn wide_null(path: &Path) -> Vec<u16> {
    path.as_os_str()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect()
}

fn replace_atomically(temporary: &Path, output: &Path) -> Result<()> {
    let temporary_wide = wide_null(temporary);
    let output_wide = wide_null(output);
    let succeeded = unsafe {
        MoveFileExW(
            temporary_wide.as_ptr(),
            output_wide.as_ptr(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
        )
    };
    if succeeded == 0 {
        return Err(io::Error::last_os_error()).with_context(|| {
            format!(
                "failed to atomically replace {} with {}",
                output.display(),
                temporary.display()
            )
        });
    }
    Ok(())
}

fn write_atomic_output<F>(output: &Path, writer: F) -> Result<u64>
where
    F: FnOnce(&mut File) -> Result<()>,
{
    if let Some(parent) = output
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
    {
        fs::create_dir_all(parent)
            .with_context(|| format!("failed to create output directory {}", parent.display()))?;
    }

    let (temporary_path, temporary_file) = create_sibling_temp(output)?;
    let mut temporary = TemporaryOutput::new(temporary_path.clone(), temporary_file);
    mark_sparse(temporary.file())?;
    writer(temporary.file_mut())?;
    temporary
        .file_mut()
        .flush()
        .context("failed to flush temporary game.pak")?;
    temporary
        .file()
        .sync_all()
        .context("failed to durably flush temporary game.pak")?;
    let logical_size = temporary
        .file()
        .metadata()
        .context("failed to measure temporary game.pak")?
        .len();
    temporary.close();

    replace_atomically(&temporary_path, output)?;
    temporary.disarm();
    Ok(logical_size)
}

fn write_plan(output: &Path, plan: &PakPlan) -> Result<u64> {
    write_atomic_output(output, |pak| {
        for system in &plan.system_files {
            write_at(pak, system.disc_offset, &system.bytes, system.label)?;
        }
        println!("  Written: boot.bin, bi2.bin, apploader.img, main.dol, fst.bin");

        let total = plan.game_files.len();
        for (index, game_file) in plan.game_files.iter().enumerate() {
            write_source_at(pak, game_file)?;
            if (index + 1) % 200 == 0 || index + 1 == total {
                println!("  Progress: {}/{} files written", index + 1, total);
            }
        }
        pak.set_len(plan.logical_size)
            .context("failed to set validated game.pak logical size")?;
        let measured_size = pak
            .metadata()
            .context("failed to verify temporary game.pak logical size")?
            .len();
        if measured_size != plan.logical_size {
            bail!(
                "temporary game.pak size mismatch: expected {}, wrote {}",
                plan.logical_size,
                measured_size
            );
        }
        Ok(())
    })
}

pub fn run(input: &Path, output: &Path) -> Result<()> {
    let plan = preflight(input, output)?;

    println!(
        "Building game.pak: {} game files + 5 system files",
        plan.game_files.len()
    );
    println!("  DOL disc offset:  {:#010x}", plan.dol_disc_offset);
    println!("  FST disc offset:  {:#010x}", plan.fst_disc_offset);

    let pak_size = write_plan(output, &plan)?;
    println!(
        "game.pak written atomically: {} bytes logical, {} files",
        pak_size,
        plan.game_files.len() + 5
    );
    println!("Output: {}", output.display());
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::windows::fs::{MetadataExt, OpenOptionsExt};
    use windows_sys::Win32::Storage::FileSystem::{FILE_ATTRIBUTE_SPARSE_FILE, FILE_SHARE_READ};

    fn valid_tmd() -> Vec<u8> {
        let mut tmd = vec![0_u8; TMD_SIZE];
        tmd[0..4].copy_from_slice(&TMD_SIGNATURE_TYPE.to_be_bytes());
        tmd[TMD_CONTENT_COUNT_OFFSET..TMD_CONTENT_COUNT_OFFSET + 2]
            .copy_from_slice(&1_u16.to_be_bytes());
        tmd[TMD_CONTENT_RECORD_OFFSET..TMD_CONTENT_RECORD_OFFSET + 4]
            .copy_from_slice(&0_u32.to_be_bytes());
        tmd[TMD_CONTENT_RECORD_OFFSET + 4..TMD_CONTENT_RECORD_OFFSET + 6]
            .copy_from_slice(&0_u16.to_be_bytes());
        tmd[TMD_CONTENT_RECORD_OFFSET + 6..TMD_CONTENT_RECORD_OFFSET + 8]
            .copy_from_slice(&3_u16.to_be_bytes());
        tmd[TMD_CONTENT_RECORD_OFFSET + 8..TMD_CONTENT_RECORD_OFFSET + 16]
            .copy_from_slice(&RMGE01_PARTITION_DATA_BYTES.to_be_bytes());
        tmd
    }

    fn temp_artifacts(directory: &Path, output_name: &str) -> Vec<PathBuf> {
        let prefix = format!("{output_name}.tmp.");
        fs::read_dir(directory)
            .expect("read temporary directory")
            .map(|entry| entry.expect("directory entry").path())
            .filter(|path| {
                path.file_name()
                    .is_some_and(|name| name.to_string_lossy().starts_with(&prefix))
            })
            .collect()
    }

    #[test]
    fn late_write_failure_preserves_existing_output() {
        let directory = tempfile::tempdir().expect("temporary directory");
        let output = directory.path().join("game.pak");
        fs::write(&output, b"known-good-package").expect("seed existing package");

        let result = write_atomic_output(&output, |temporary| {
            temporary.write_all(b"replacement-prefix")?;
            bail!("synthetic late source failure")
        });
        assert!(result.is_err());
        assert_eq!(
            fs::read(&output).expect("existing output remains readable"),
            b"known-good-package"
        );
        assert!(temp_artifacts(directory.path(), "game.pak").is_empty());
    }

    #[test]
    fn late_source_size_change_preserves_existing_output() {
        let directory = tempfile::tempdir().expect("temporary directory");
        let output = directory.path().join("game.pak");
        let source = directory.path().join("source.bin");
        fs::write(&output, b"known-good-package").expect("seed existing package");
        fs::write(&source, b"four").expect("seed planned source");
        let planned = PlannedFile {
            fst_path: "source.bin".to_owned(),
            source_path: source.clone(),
            disc_offset: 0x100,
            size: 4,
        };

        fs::write(&source, b"changed-size").expect("mutate source after planning");
        let result = write_atomic_output(&output, |temporary| {
            temporary.write_all(b"temporary-prefix")?;
            write_source_at(temporary, &planned)
        });
        assert!(result.is_err());
        assert_eq!(
            fs::read(&output).expect("existing output remains readable"),
            b"known-good-package"
        );
        assert!(temp_artifacts(directory.path(), "game.pak").is_empty());
    }

    #[test]
    fn successful_write_atomically_replaces_existing_output() {
        let directory = tempfile::tempdir().expect("temporary directory");
        let output = directory.path().join("game.pak");
        fs::write(&output, b"old").expect("seed existing package");

        let size = write_atomic_output(&output, |temporary| {
            temporary.write_all(b"new-package")?;
            Ok(())
        })
        .expect("atomic replacement succeeds");
        assert_eq!(size, 11);
        assert_eq!(fs::read(&output).expect("read replacement"), b"new-package");
        assert_ne!(
            fs::metadata(&output)
                .expect("replacement metadata")
                .file_attributes()
                & FILE_ATTRIBUTE_SPARSE_FILE,
            0,
            "atomically installed package remains an NTFS sparse file"
        );
        assert!(temp_artifacts(directory.path(), "game.pak").is_empty());
    }

    #[test]
    fn failed_atomic_replace_preserves_existing_output() {
        let directory = tempfile::tempdir().expect("temporary directory");
        let output = directory.path().join("game.pak");
        fs::write(&output, b"known-good-package").expect("seed existing package");
        let lock = OpenOptions::new()
            .read(true)
            .share_mode(FILE_SHARE_READ)
            .open(&output)
            .expect("open destination without delete sharing");

        let result = write_atomic_output(&output, |temporary| {
            temporary.write_all(b"replacement")?;
            Ok(())
        });
        assert!(result.is_err());
        assert_eq!(
            fs::read(&output).expect("existing output remains readable"),
            b"known-good-package"
        );
        drop(lock);
        assert!(temp_artifacts(directory.path(), "game.pak").is_empty());
    }

    #[test]
    fn rejects_overlapping_and_out_of_partition_extents() {
        let mut overlapping = Vec::new();
        add_extent(
            &mut overlapping,
            "boot.bin",
            0,
            0x440,
            RMGE01_PARTITION_DATA_BYTES,
        )
        .unwrap();
        add_extent(
            &mut overlapping,
            "malformed FST file",
            0x400,
            0x100,
            RMGE01_PARTITION_DATA_BYTES,
        )
        .unwrap();
        assert!(validate_extents(overlapping)
            .unwrap_err()
            .to_string()
            .contains("overlapping partition extents"));

        let mut outside = Vec::new();
        assert!(add_extent(
            &mut outside,
            "malformed FST file",
            RMGE01_PARTITION_DATA_BYTES - 3,
            4,
            RMGE01_PARTITION_DATA_BYTES,
        )
        .is_err());
    }

    #[test]
    fn validates_exact_tmd_partition_bound() {
        assert_eq!(
            validate_rmge01_tmd(&valid_tmd()).expect("exact TMD is valid"),
            RMGE01_PARTITION_DATA_BYTES
        );

        let mut wrong_size = valid_tmd();
        wrong_size[TMD_CONTENT_RECORD_OFFSET + 8..TMD_CONTENT_RECORD_OFFSET + 16]
            .copy_from_slice(&(RMGE01_PARTITION_DATA_BYTES + 0x8000).to_be_bytes());
        assert!(validate_rmge01_tmd(&wrong_size).is_err());

        let mut wrong_count = valid_tmd();
        wrong_count[TMD_CONTENT_COUNT_OFFSET..TMD_CONTENT_COUNT_OFFSET + 2]
            .copy_from_slice(&2_u16.to_be_bytes());
        assert!(validate_rmge01_tmd(&wrong_count).is_err());
    }

    #[test]
    fn rejects_noncanonical_windows_fst_paths() {
        for path in [
            "../escape.bin",
            "/absolute.bin",
            "C:/absolute.bin",
            "directory/CON.txt",
            "directory/com1.arc",
            "LPT9/asset.bin",
            "directory/trailing. ",
        ] {
            assert!(logical_windows_path(path).is_err(), "accepted {path:?}");
        }
        let (relative, key) = logical_windows_path("AudioRes/Info/File.arc").unwrap();
        assert_eq!(relative, PathBuf::from("AudioRes/Info/File.arc"));
        assert_eq!(key, "audiores\\info\\file.arc");
    }

    #[test]
    fn rejects_superscript_device_names_in_any_path_component() {
        for prefix in ["COM", "com", "LPT", "lPt"] {
            for digit in ["¹", "²", "³"] {
                for extension in ["", ".arc", ".tar.gz"] {
                    let name = format!("{prefix}{digit}{extension}");
                    for path in [
                        format!("{name}/asset.bin"),
                        format!("directory/{name}/asset.bin"),
                        format!("directory/{name}"),
                    ] {
                        assert!(logical_windows_path(&path).is_err(), "accepted {path:?}");
                    }
                }
            }
        }
    }

    #[test]
    fn permits_non_reserved_device_like_path_components() {
        for name in [
            "COM0.arc",
            "COM10",
            "COM¹0.arc",
            "LPT0",
            "LPT⁴.arc",
            "acOM¹.arc",
        ] {
            let path = format!("directory/{name}");
            let (relative, key) = logical_windows_path(&path).expect("ordinary file name");
            assert_eq!(relative, PathBuf::from(&path));
            assert_eq!(key, format!("directory\\{}", name.to_lowercase()));
        }
    }
}
