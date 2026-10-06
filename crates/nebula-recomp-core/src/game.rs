use crate::{rmge01_function_map, DolImage, DolSection, FunctionRange};
use powerpc::{Extensions, Ins, Opcode};
use serde::Serialize;
use sha1::{Digest, Sha1};
use std::{
    collections::BTreeMap,
    fs,
    path::{Path, PathBuf},
};
use thiserror::Error;
use walkdir::WalkDir;

pub const RMGE01_GAME_ID: &str = "RMGE01";
pub const RMGE01_DOL_SHA1: &str = "9a71008ae1ee9010e267fa67d1f0b0d4f0e895dd";

#[derive(Debug, Error)]
pub enum GameError {
    #[error("input does not contain DATA/sys/main.dol, sys/main.dol, or a DOL file")]
    MainDolNotFound,
    #[error("failed to read {path}: {source}")]
    Read {
        path: PathBuf,
        source: std::io::Error,
    },
    #[error("failed to inspect extracted files under {path}: {source}")]
    Walk {
        path: PathBuf,
        source: walkdir::Error,
    },
    #[error(transparent)]
    InvalidDol(#[from] crate::dol::DolError),
    #[error("unsupported game ID {actual}; expected {expected}")]
    WrongGameId {
        actual: String,
        expected: &'static str,
    },
    #[error("main.dol SHA-1 {actual} does not match supported RMGE01 hash {expected}")]
    WrongDolHash {
        actual: String,
        expected: &'static str,
    },
    #[error("{0}")]
    InvalidDspUcode(String),
}

#[derive(Clone, Copy, Debug, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum GameInputKind {
    ExtractedDirectory,
    MainDol,
}

#[derive(Clone, Debug, Serialize)]
pub struct ExecutableAudit {
    pub raw_word_count: u64,
    pub recognized_word_count: u64,
    pub unrecognized_word_count: u64,
    pub unrecognized_addresses: Vec<u32>,
    pub opcode_histogram: BTreeMap<String, u64>,
}

#[derive(Clone, Debug, Serialize)]
pub struct FunctionAudit {
    pub function_count: u64,
    pub function_word_count: u64,
    pub recognized_word_count: u64,
    pub unrecognized_word_count: u64,
    pub unrecognized_addresses: Vec<u32>,
    pub invalid_function_ranges: Vec<FunctionRange>,
}

#[derive(Clone, Debug, Serialize)]
pub struct InputAssets {
    pub file_count: u64,
    pub total_bytes: u64,
    pub home_button_rso_present: bool,
}

#[derive(Clone, Debug, Serialize)]
pub struct GameAnalysis {
    pub schema_version: u32,
    pub game_id: String,
    pub input_kind: GameInputKind,
    pub main_dol_sha1: String,
    pub main_dol_size: u64,
    pub entry_point: u32,
    pub bss_address: u32,
    pub bss_size: u32,
    pub sections: Vec<DolSection>,
    pub raw_text_audit: ExecutableAudit,
    pub function_audit: FunctionAudit,
    pub assets: InputAssets,
}

pub fn analyze_input(input: &Path) -> Result<GameAnalysis, GameError> {
    let loaded = load_verified_game(input)?;
    let located = loaded.located;
    let dol_bytes = loaded.dol_bytes;
    let dol = loaded.dol;
    let raw_text_audit = audit_executable(&dol, &dol_bytes);
    let function_audit = audit_functions(&dol, &dol_bytes, &rmge01_function_map());
    let assets = inspect_assets(located.content_root.as_deref())?;

    Ok(GameAnalysis {
        schema_version: 1,
        game_id: loaded.game_id,
        input_kind: located.kind,
        main_dol_sha1: RMGE01_DOL_SHA1.to_owned(),
        main_dol_size: dol_bytes.len() as u64,
        entry_point: dol.entry_point,
        bss_address: dol.bss_address,
        bss_size: dol.bss_size,
        sections: dol.sections,
        raw_text_audit,
        function_audit,
        assets,
    })
}

pub(crate) struct LoadedGame {
    pub located: LocatedInput,
    pub game_id: String,
    pub dol_bytes: Vec<u8>,
    pub dol: DolImage,
}

pub(crate) struct LocatedInput {
    kind: GameInputKind,
    dol_path: PathBuf,
    content_root: Option<PathBuf>,
}

pub(crate) fn load_verified_game(input: &Path) -> Result<LoadedGame, GameError> {
    let located = locate_input(input)?;
    let dol_bytes = read_file(&located.dol_path)?;
    let dol_hash = sha1_hex(&dol_bytes);
    if dol_hash != RMGE01_DOL_SHA1 {
        return Err(GameError::WrongDolHash {
            actual: dol_hash,
            expected: RMGE01_DOL_SHA1,
        });
    }

    let game_id =
        read_game_id(located.content_root.as_deref())?.unwrap_or_else(|| RMGE01_GAME_ID.to_owned());
    if game_id != RMGE01_GAME_ID {
        return Err(GameError::WrongGameId {
            actual: game_id,
            expected: RMGE01_GAME_ID,
        });
    }
    let dol = DolImage::parse(&dol_bytes)?;
    Ok(LoadedGame {
        located,
        game_id,
        dol_bytes,
        dol,
    })
}

fn locate_input(input: &Path) -> Result<LocatedInput, GameError> {
    if input.is_file() {
        return Ok(LocatedInput {
            kind: GameInputKind::MainDol,
            dol_path: input.to_owned(),
            content_root: None,
        });
    }

    let candidates = [input.join("DATA"), input.to_owned()];
    for root in candidates {
        let dol_path = root.join("sys").join("main.dol");
        if dol_path.is_file() {
            return Ok(LocatedInput {
                kind: GameInputKind::ExtractedDirectory,
                dol_path,
                content_root: Some(root),
            });
        }
    }

    Err(GameError::MainDolNotFound)
}

fn read_game_id(content_root: Option<&Path>) -> Result<Option<String>, GameError> {
    let Some(root) = content_root else {
        return Ok(None);
    };
    let boot_path = root.join("sys").join("boot.bin");
    if !boot_path.is_file() {
        return Ok(None);
    }
    let boot = read_file(&boot_path)?;
    if boot.len() < 6 {
        return Ok(None);
    }
    Ok(Some(String::from_utf8_lossy(&boot[..6]).into_owned()))
}

fn inspect_assets(content_root: Option<&Path>) -> Result<InputAssets, GameError> {
    inspect_assets_with_file_size(content_root, |entry| {
        entry.metadata().map(|metadata| metadata.len())
    })
}

fn inspect_assets_with_file_size(
    content_root: Option<&Path>,
    mut file_size: impl FnMut(&walkdir::DirEntry) -> Result<u64, walkdir::Error>,
) -> Result<InputAssets, GameError> {
    let Some(root) = content_root else {
        return Ok(InputAssets {
            file_count: 1,
            total_bytes: 0,
            home_button_rso_present: false,
        });
    };

    let mut file_count = 0_u64;
    let mut total_bytes = 0_u64;
    let mut home_button_rso_present = false;
    for entry in WalkDir::new(root).follow_links(false) {
        let entry = entry.map_err(|source| GameError::Walk {
            path: root.to_owned(),
            source,
        })?;
        if entry.file_type().is_file() {
            let bytes = file_size(&entry).map_err(|source| GameError::Walk {
                path: entry.path().to_owned(),
                source,
            })?;
            file_count += 1;
            total_bytes = total_bytes.saturating_add(bytes);
            if entry
                .path()
                .to_string_lossy()
                .replace('\\', "/")
                .ends_with("/files/ModuleData/HomeButtonMenuWrapperRSO.rso")
            {
                home_button_rso_present = true;
            }
        }
    }

    Ok(InputAssets {
        file_count,
        total_bytes,
        home_button_rso_present,
    })
}

fn audit_executable(dol: &DolImage, data: &[u8]) -> ExecutableAudit {
    let mut audit = ExecutableAudit {
        raw_word_count: 0,
        recognized_word_count: 0,
        unrecognized_word_count: 0,
        unrecognized_addresses: Vec::new(),
        opcode_histogram: BTreeMap::new(),
    };
    let extensions = Extensions::gekko_broadway();

    for section in dol.text_sections() {
        let bytes = &data[section.file_range()];
        for (index, chunk) in bytes.chunks_exact(4).enumerate() {
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte chunk"));
            let instruction = Ins::new(word, extensions);
            let address = section.address + (index as u32 * 4);
            audit.raw_word_count += 1;
            if instruction.op == Opcode::Illegal {
                audit.unrecognized_word_count += 1;
                audit.unrecognized_addresses.push(address);
            } else {
                audit.recognized_word_count += 1;
                *audit
                    .opcode_histogram
                    .entry(format!("{:?}", instruction.op))
                    .or_default() += 1;
            }
        }
    }
    audit
}

fn audit_functions(dol: &DolImage, data: &[u8], functions: &[FunctionRange]) -> FunctionAudit {
    let extensions = Extensions::gekko_broadway();
    let mut audit = FunctionAudit {
        function_count: functions.len() as u64,
        function_word_count: 0,
        recognized_word_count: 0,
        unrecognized_word_count: 0,
        unrecognized_addresses: Vec::new(),
        invalid_function_ranges: Vec::new(),
    };

    for function in functions {
        let function_end = match function.address.checked_add(function.size) {
            Some(end) => end,
            None => {
                audit.invalid_function_ranges.push(*function);
                continue;
            }
        };
        let section = dol.text_sections().find(|section| {
            let section_end = section.address + section.size;
            function.address >= section.address && function_end <= section_end
        });
        let Some(section) = section else {
            audit.invalid_function_ranges.push(*function);
            continue;
        };

        let section_relative = (function.address - section.address) as usize;
        let file_start = section.file_offset as usize + section_relative;
        let file_end = file_start + function.size as usize;
        let bytes = &data[file_start..file_end];
        for (index, chunk) in bytes.chunks_exact(4).enumerate() {
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte chunk"));
            let instruction = Ins::new(word, extensions);
            let address = function.address + (index as u32 * 4);
            audit.function_word_count += 1;
            if instruction.op == Opcode::Illegal {
                audit.unrecognized_word_count += 1;
                audit.unrecognized_addresses.push(address);
            } else {
                audit.recognized_word_count += 1;
            }
        }
    }

    audit
}

fn read_file(path: &Path) -> Result<Vec<u8>, GameError> {
    fs::read(path).map_err(|source| GameError::Read {
        path: path.to_owned(),
        source,
    })
}

fn sha1_hex(data: &[u8]) -> String {
    format!("{:x}", Sha1::digest(data))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::tempdir;

    #[test]
    fn rejects_unknown_hash_before_parsing() {
        let dir = tempdir().expect("temp dir");
        let dol_path = dir.path().join("main.dol");
        fs::write(&dol_path, vec![0_u8; 0x100]).expect("write");
        assert!(matches!(
            analyze_input(&dol_path),
            Err(GameError::WrongDolHash { .. })
        ));
    }

    #[test]
    fn reads_game_id_from_boot_bin() {
        let dir = tempdir().expect("temp dir");
        let sys = dir.path().join("sys");
        fs::create_dir_all(&sys).expect("create sys");
        let mut boot = fs::File::create(sys.join("boot.bin")).expect("boot");
        boot.write_all(b"RMGE01").expect("write game id");
        assert_eq!(
            read_game_id(Some(dir.path())).expect("read"),
            Some("RMGE01".to_owned())
        );
    }

    #[test]
    fn counts_asset_bytes_including_empty_files() {
        let dir = tempdir().expect("temp dir");
        let module_dir = dir.path().join("files/ModuleData");
        fs::create_dir_all(&module_dir).expect("create modules");
        fs::write(dir.path().join("five.bin"), [0_u8; 5]).expect("write asset");
        fs::write(dir.path().join("empty.bin"), b"").expect("write empty asset");
        fs::write(module_dir.join("HomeButtonMenuWrapperRSO.rso"), [0_u8; 3])
            .expect("write module");

        let assets = inspect_assets(Some(dir.path())).expect("inspect assets");
        assert_eq!(assets.file_count, 3);
        assert_eq!(assets.total_bytes, 8);
        assert!(assets.home_button_rso_present);
    }

    #[test]
    fn asset_size_error_rejects_partial_inventory_with_file_path() {
        let dir = tempdir().expect("temp dir");
        fs::write(dir.path().join("first.bin"), [0_u8; 5]).expect("write first asset");
        fs::write(dir.path().join("second.bin"), [0_u8; 7]).expect("write second asset");
        let missing = dir.path().join("missing");
        let mut size_error = Some(
            WalkDir::new(&missing)
                .into_iter()
                .next()
                .expect("missing-path result")
                .expect_err("missing path must fail"),
        );
        let mut calls = 0;
        let mut failed_path = None;

        // Windows walkdir entries cache metadata. Inject the error at the size
        // boundary so this regression also exercises error propagation there.
        let result = inspect_assets_with_file_size(Some(dir.path()), |entry| {
            calls += 1;
            if calls == 2 {
                failed_path = Some(entry.path().to_owned());
                Err(size_error.take().expect("one size error"))
            } else {
                entry.metadata().map(|metadata| metadata.len())
            }
        });

        match result {
            Err(GameError::Walk { path, source }) => {
                assert_eq!(calls, 2);
                assert_eq!(Some(path), failed_path);
                assert_eq!(source.path(), Some(missing.as_path()));
                assert_eq!(
                    source.io_error().unwrap().kind(),
                    std::io::ErrorKind::NotFound
                );
            }
            other => panic!("expected asset-size failure, got {other:?}"),
        }
    }

    #[cfg(not(windows))]
    #[test]
    fn disappearing_asset_metadata_is_an_error() {
        let dir = tempdir().expect("temp dir");
        let asset = dir.path().join("asset.bin");
        fs::write(&asset, [0_u8; 5]).expect("write asset");

        let result = inspect_assets_with_file_size(Some(dir.path()), |entry| {
            fs::remove_file(entry.path()).expect("remove discovered asset");
            entry.metadata().map(|metadata| metadata.len())
        });

        match result {
            Err(GameError::Walk { path, source }) => {
                assert_eq!(path, asset);
                assert_eq!(
                    source.io_error().unwrap().kind(),
                    std::io::ErrorKind::NotFound
                );
            }
            other => panic!("expected missing metadata, got {other:?}"),
        }
    }
}
