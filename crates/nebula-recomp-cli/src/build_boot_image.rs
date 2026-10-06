use anyhow::{bail, Context, Result};
use nebula_recomp_core::{build_rmge01_boot_image, parse_rmge01_boot_image};
use std::{
    ffi::OsString,
    fs::{self, File, OpenOptions},
    io::{self, Write},
    os::windows::ffi::OsStrExt,
    path::{Path, PathBuf},
    sync::atomic::{AtomicU64, Ordering},
};
use windows_sys::Win32::Storage::FileSystem::{
    MoveFileExW, MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH,
};

static NEXT_TEMP_ID: AtomicU64 = AtomicU64::new(0);

struct TemporaryOutput {
    path: PathBuf,
    file: Option<File>,
    armed: bool,
}

impl Drop for TemporaryOutput {
    fn drop(&mut self) {
        drop(self.file.take());
        if self.armed {
            let _ = fs::remove_file(&self.path);
        }
    }
}

fn sibling_temp_name(output: &Path, sequence: u64) -> Result<PathBuf> {
    let file_name = output
        .file_name()
        .context("boot-image output must have a file name")?;
    let mut temporary_name = OsString::from(file_name);
    temporary_name.push(format!(".tmp.{}.{}", std::process::id(), sequence));
    Ok(output
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."))
        .join(temporary_name))
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
                "failed to atomically publish boot image {} from {}",
                output.display(),
                temporary.display()
            )
        });
    }
    Ok(())
}

pub fn run(input: &Path, output: &Path) -> Result<()> {
    if output.file_name().and_then(|name| name.to_str()) != Some("RMGE01_boot_image.bin") {
        bail!("boot-image output file name must be RMGE01_boot_image.bin");
    }
    let artifact = build_rmge01_boot_image(input)
        .with_context(|| format!("failed to build boot image from {}", input.display()))?;
    if let Some(parent) = output
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
    {
        fs::create_dir_all(parent)
            .with_context(|| format!("failed to create output directory {}", parent.display()))?;
    }
    let (temporary_path, temporary_file) = create_sibling_temp(output)?;
    let mut temporary = TemporaryOutput {
        path: temporary_path.clone(),
        file: Some(temporary_file),
        armed: true,
    };
    let file = temporary.file.as_mut().expect("temporary file is open");
    file.write_all(&artifact.bytes)
        .context("failed to write temporary boot image")?;
    file.flush()
        .context("failed to flush temporary boot image")?;
    file.sync_all()
        .context("failed to durably flush temporary boot image")?;
    drop(temporary.file.take());
    // Validate the actual durable temporary bytes before replacing a user's
    // previous artifact. Retain the final readback to diagnose publication.
    let staged = fs::read(&temporary_path)
        .context("failed to re-read temporary boot image before publication")?;
    parse_rmge01_boot_image(&staged)
        .context("temporary boot image failed canonical revalidation")?;
    if staged != artifact.bytes {
        bail!("temporary boot image bytes differ from the validated installer artifact");
    }
    replace_atomically(&temporary_path, output)?;
    temporary.armed = false;

    let published = fs::read(output).with_context(|| {
        format!(
            "failed to re-read published boot image {}",
            output.display()
        )
    })?;
    let manifest = parse_rmge01_boot_image(&published)
        .context("published boot image failed canonical revalidation")?;
    if published != artifact.bytes {
        bail!("published boot image bytes differ from the validated installer artifact");
    }
    println!(
        "Built RMGE01 boot image: {} sections, {} payload bytes, {} total bytes, digest {}, file sha256 {} -> {}",
        manifest.sections.len(),
        manifest.payload_size,
        manifest.image_size,
        manifest.digest_sha256,
        artifact.file_sha256,
        output.display()
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_unsafe_or_ambiguous_output_name_before_touching_disk() {
        let directory = tempfile::tempdir().expect("tempdir");
        let output = directory.path().join("main.dol");
        let result = run(directory.path(), &output);
        assert!(result.is_err());
        assert!(!output.exists());
    }

    #[test]
    fn temporary_name_is_an_owned_sibling() {
        let output = Path::new("generated").join("RMGE01_boot_image.bin");
        let temporary = sibling_temp_name(&output, 7).expect("temporary name");
        assert_eq!(temporary.parent(), output.parent());
        assert!(temporary
            .file_name()
            .expect("file name")
            .to_string_lossy()
            .starts_with("RMGE01_boot_image.bin.tmp."));
    }
}
