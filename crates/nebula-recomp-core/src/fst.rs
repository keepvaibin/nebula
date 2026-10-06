use std::{collections::HashSet, fmt};

/// One file entry extracted from a Wii/GameCube FST.
#[derive(Debug, Clone)]
pub struct FstFile {
    /// Path relative to the partition root, using `'/'` as separator.
    ///
    /// Example: `"ObjectData/MarioFaceShipPlanet.arc"` (the packer resolves
    /// this beneath the extracted `DATA/files/` directory).
    pub host_rel_path: String,
    /// Byte offset of this file within the disc partition image.
    pub disc_offset: u64,
    /// File size in bytes.
    pub size: u32,
}

/// Errors that can occur while parsing an FST image.
#[derive(Debug, PartialEq, Eq)]
pub enum FstError {
    /// The data slice is shorter than the minimum 12-byte root entry.
    TooShort,
    /// The root entry is not the required directory entry.
    InvalidRootType { flags: u8 },
    /// The root entry's name offset is not zero.
    InvalidRootNameOffset { name_offset: u32 },
    /// The root entry's parent index is not zero.
    InvalidRootParent { parent_index: u32 },
    /// The root entry count cannot describe an entry table in `data`.
    InvalidRootNextIndex {
        next_index: u32,
        available_entries: usize,
    },
    /// `root.next_idx * 12` exceeds the size of `data`.
    StringTableOutOfBounds,
    /// A name offset points past the end of the string table.
    NameOutOfBounds { entry: usize, name_offset: u32 },
    /// A name offset points into the middle of another string-table name.
    NameNotAtBoundary { entry: usize, name_offset: u32 },
    /// A file or directory name is not valid UTF-8.
    InvalidUtf8 { entry: usize, name_offset: u32 },
    /// A string-table entry is not terminated before the end of the table.
    MissingNameTerminator { entry: usize, name_offset: u32 },
    /// A file or directory has an empty name.
    EmptyName { entry: usize },
    /// A file or directory name can escape or reshape its parent path.
    UnsafeName { entry: usize, name: String },
    /// An entry uses a type/flags byte other than file (0) or directory (1).
    UnknownFlags { entry: usize, flags: u8 },
    /// A directory's parent index does not identify the active parent.
    InvalidDirectoryParent {
        entry: usize,
        parent_index: u32,
        expected_parent: usize,
    },
    /// A directory's next index does not form a non-empty, nested subtree.
    InvalidDirectoryNext {
        entry: usize,
        next_index: u32,
        parent_end: usize,
    },
    /// Two entries resolve to the same exact relative path.
    DuplicatePath { entry: usize, path: String },
}

impl fmt::Display for FstError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FstError::TooShort => write!(f, "FST data is too short (need at least 12 bytes)"),
            FstError::InvalidRootType { flags } => write!(
                f,
                "FST root has flags {flags:#x}; expected the directory flag 0x1"
            ),
            FstError::InvalidRootNameOffset { name_offset } => write!(
                f,
                "FST root has name offset {name_offset:#x}; expected zero"
            ),
            FstError::InvalidRootParent { parent_index } => write!(
                f,
                "FST root has parent index {parent_index}; expected zero"
            ),
            FstError::InvalidRootNextIndex {
                next_index,
                available_entries,
            } => write!(
                f,
                "FST root next index {next_index} cannot fit in the available data ({available_entries} complete entries)"
            ),
            FstError::StringTableOutOfBounds => {
                write!(f, "FST root next_idx overflows the available data")
            }
            FstError::NameOutOfBounds { entry, name_offset } => write!(
                f,
                "FST entry {entry}: name offset {name_offset:#x} is past the end of the string table"
            ),
            FstError::NameNotAtBoundary { entry, name_offset } => write!(
                f,
                "FST entry {entry}: name offset {name_offset:#x} points into another string-table name"
            ),
            FstError::InvalidUtf8 { entry, name_offset } => write!(
                f,
                "FST entry {entry}: name at string table offset {name_offset:#x} is not valid UTF-8"
            ),
            FstError::MissingNameTerminator { entry, name_offset } => write!(
                f,
                "FST entry {entry}: name at string table offset {name_offset:#x} is not NUL-terminated"
            ),
            FstError::EmptyName { entry } => {
                write!(f, "FST entry {entry}: file/directory name is empty")
            }
            FstError::UnsafeName { entry, name } => {
                write!(f, "FST entry {entry}: unsafe path component {name:?}")
            }
            FstError::UnknownFlags { entry, flags } => write!(
                f,
                "FST entry {entry}: unknown flags/type byte {flags:#x}"
            ),
            FstError::InvalidDirectoryParent {
                entry,
                parent_index,
                expected_parent,
            } => write!(
                f,
                "FST directory entry {entry}: parent index {parent_index} does not match active parent {expected_parent}"
            ),
            FstError::InvalidDirectoryNext {
                entry,
                next_index,
                parent_end,
            } => write!(
                f,
                "FST directory entry {entry}: next index {next_index} must be greater than the entry and no greater than parent end {parent_end}"
            ),
            FstError::DuplicatePath { entry, path } => {
                write!(f, "FST entry {entry}: duplicate relative path {path:?}")
            }
        }
    }
}

impl std::error::Error for FstError {}

/// Read a big-endian `u32` from `data` at `offset`.
fn read_u32_be(data: &[u8], offset: usize) -> u32 {
    u32::from_be_bytes(
        data[offset..offset + 4]
            .try_into()
            .expect("slice has at least 4 bytes"),
    )
}

/// Read a big-endian 24-bit unsigned integer (3 bytes) from `data` at `offset`.
fn read_u24_be(data: &[u8], offset: usize) -> u32 {
    (u32::from(data[offset]) << 16)
        | (u32::from(data[offset + 1]) << 8)
        | u32::from(data[offset + 2])
}

/// Return the slice of `data` starting at `start` up to (but not including)
/// the first `\0` byte. Unterminated names are malformed.
fn read_cstring(data: &[u8], start: usize) -> Option<&[u8]> {
    let length = data[start..].iter().position(|&byte| byte == 0)?;
    Some(&data[start..start + length])
}

fn is_unsafe_host_component(name: &str) -> bool {
    if name.is_empty()
        || name == "."
        || name == ".."
        || name.ends_with(' ')
        || name.ends_with('.')
        || name.chars().any(|character| {
            character <= '\u{1f}'
                || matches!(
                    character,
                    '<' | '>' | ':' | '"' | '/' | '\\' | '|' | '?' | '*'
                )
        })
    {
        return true;
    }

    let stem = name.split('.').next().unwrap_or(name);
    let upper = stem.to_ascii_uppercase();
    let device_number = upper
        .strip_prefix("COM")
        .or_else(|| upper.strip_prefix("LPT"));
    matches!(upper.as_str(), "CON" | "PRN" | "AUX" | "NUL")
        || device_number.is_some_and(|number| {
            matches!(
                number,
                "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" | "¹" | "²" | "³"
            )
        })
}

/// Parse a raw FST binary image and return the complete list of file entries.
///
/// The FST format (12 bytes per entry):
/// * `[0]`     - flags: `0` = file, `1` = directory
/// * `[1..4]`  - big-endian 24-bit name offset into the string table
/// * `[4..8]`  - big-endian u32: disc-word-offset (files) or parent-dir-index (dirs)
/// * `[8..12]` - big-endian u32: file size (files) or next-index past the dir subtree (dirs)
///
/// The string table immediately follows all `num_entries * 12` bytes of entries.
/// The root entry (index 0) is a directory whose `next_idx` equals `num_entries`.
pub fn parse_fst(data: &[u8]) -> Result<Vec<FstFile>, FstError> {
    if data.len() < 12 {
        return Err(FstError::TooShort);
    }

    let root_flags = data[0];
    if root_flags != 1 {
        return Err(FstError::InvalidRootType { flags: root_flags });
    }
    let root_name_offset = read_u24_be(data, 1);
    if root_name_offset != 0 {
        return Err(FstError::InvalidRootNameOffset {
            name_offset: root_name_offset,
        });
    }
    let root_parent = read_u32_be(data, 4);
    if root_parent != 0 {
        return Err(FstError::InvalidRootParent {
            parent_index: root_parent,
        });
    }
    let root_next = read_u32_be(data, 8);
    let available_entries = data.len() / 12;
    let total_entries = usize::try_from(root_next).map_err(|_| FstError::InvalidRootNextIndex {
        next_index: root_next,
        available_entries,
    })?;
    if total_entries < 1 || total_entries > available_entries {
        return Err(FstError::InvalidRootNextIndex {
            next_index: root_next,
            available_entries,
        });
    }
    let string_table_offset = total_entries
        .checked_mul(12)
        .ok_or(FstError::StringTableOutOfBounds)?;
    if string_table_offset > data.len() {
        return Err(FstError::StringTableOutOfBounds);
    }
    let string_table = &data[string_table_offset..];

    let mut files = Vec::new();
    // (directory index, end index, cumulative path prefix). The root remains
    // active through the complete declared entry table.
    let mut dir_stack = vec![(0_usize, total_entries, String::new())];
    let mut entry_paths = HashSet::new();

    for idx in 1..total_entries {
        while dir_stack.last().is_some_and(|&(_, end, _)| idx >= end) {
            dir_stack.pop();
        }
        let Some((expected_parent, parent_end, prefix)) = dir_stack.last() else {
            return Err(FstError::InvalidDirectoryNext {
                entry: idx,
                next_index: u32::try_from(idx).unwrap_or(u32::MAX),
                parent_end: total_entries,
            });
        };

        let entry_base = idx * 12;
        if entry_base + 12 > data.len() {
            return Err(FstError::TooShort);
        }

        let flags = data[entry_base];
        let name_off = read_u24_be(data, entry_base + 1);
        let word4 = read_u32_be(data, entry_base + 4);
        let word8 = read_u32_be(data, entry_base + 8);

        let name_start = name_off as usize;
        if name_start >= string_table.len() {
            return Err(FstError::NameOutOfBounds {
                entry: idx,
                name_offset: name_off,
            });
        }
        if name_start > 0 && string_table[name_start - 1] != 0 {
            return Err(FstError::NameNotAtBoundary {
                entry: idx,
                name_offset: name_off,
            });
        }
        let name_bytes =
            read_cstring(string_table, name_start).ok_or(FstError::MissingNameTerminator {
                entry: idx,
                name_offset: name_off,
            })?;
        let name = std::str::from_utf8(name_bytes).map_err(|_| FstError::InvalidUtf8 {
            entry: idx,
            name_offset: name_off,
        })?;
        if name.is_empty() {
            return Err(FstError::EmptyName { entry: idx });
        }
        if is_unsafe_host_component(name) {
            return Err(FstError::UnsafeName {
                entry: idx,
                name: name.to_owned(),
            });
        }

        let entry_path = format!("{prefix}{name}");
        if !entry_paths.insert(entry_path.to_lowercase()) {
            return Err(FstError::DuplicatePath {
                entry: idx,
                path: entry_path,
            });
        }

        match flags {
            1 => {
                if usize::try_from(word4).ok() != Some(*expected_parent) {
                    return Err(FstError::InvalidDirectoryParent {
                        entry: idx,
                        parent_index: word4,
                        expected_parent: *expected_parent,
                    });
                }
                let next_idx = usize::try_from(word8).unwrap_or(usize::MAX);
                if next_idx <= idx || next_idx > *parent_end {
                    return Err(FstError::InvalidDirectoryNext {
                        entry: idx,
                        next_index: word8,
                        parent_end: *parent_end,
                    });
                }
                dir_stack.push((idx, next_idx, format!("{entry_path}/")));
            }
            0 => files.push(FstFile {
                host_rel_path: entry_path,
                disc_offset: u64::from(word4) * 4,
                size: word8,
            }),
            _ => return Err(FstError::UnknownFlags { entry: idx, flags }),
        }
    }

    Ok(files)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(flags: u8, name_off: u32, w4: u32, w8: u32) -> [u8; 12] {
        let mut entry = [0_u8; 12];
        entry[0] = flags;
        entry[1] = (name_off >> 16) as u8;
        entry[2] = (name_off >> 8) as u8;
        entry[3] = name_off as u8;
        entry[4..8].copy_from_slice(&w4.to_be_bytes());
        entry[8..12].copy_from_slice(&w8.to_be_bytes());
        entry
    }

    fn flat_fst(entries: &[[u8; 12]], strings: &[u8]) -> Vec<u8> {
        let mut data = Vec::new();
        data.extend_from_slice(&entry(1, 0, 0, (entries.len() + 1) as u32));
        for item in entries {
            data.extend_from_slice(item);
        }
        data.extend_from_slice(strings);
        data
    }

    #[test]
    fn rejects_too_short() {
        assert!(matches!(parse_fst(&[0_u8; 11]), Err(FstError::TooShort)));
        assert!(matches!(parse_fst(&[]), Err(FstError::TooShort)));
    }

    #[test]
    fn parses_flat_fst() {
        let data = flat_fst(
            &[entry(0, 0, 8, 512), entry(0, 6, 16, 1024)],
            b"a.arc\0b.arc\0",
        );
        let files = parse_fst(&data).unwrap();
        assert_eq!(files.len(), 2);
        assert_eq!(files[0].host_rel_path, "a.arc");
        assert_eq!(files[0].disc_offset, 32);
        assert_eq!(files[0].size, 512);
        assert_eq!(files[1].host_rel_path, "b.arc");
        assert_eq!(files[1].disc_offset, 64);
        assert_eq!(files[1].size, 1024);
    }

    #[test]
    fn parses_nested_fst() {
        let mut data = Vec::new();
        data.extend_from_slice(&entry(1, 0, 0, 5));
        data.extend_from_slice(&entry(1, 0, 0, 4));
        data.extend_from_slice(&entry(0, 6, 100, 200));
        data.extend_from_slice(&entry(0, 15, 200, 300));
        data.extend_from_slice(&entry(0, 25, 400, 500));
        data.extend_from_slice(b"files\0data.arc\0other.bin\0root.bin\0");

        let files = parse_fst(&data).unwrap();
        assert_eq!(files.len(), 3);
        assert_eq!(files[0].host_rel_path, "files/data.arc");
        assert_eq!(files[0].disc_offset, 400);
        assert_eq!(files[0].size, 200);
        assert_eq!(files[1].host_rel_path, "files/other.bin");
        assert_eq!(files[1].disc_offset, 800);
        assert_eq!(files[2].host_rel_path, "root.bin");
        assert_eq!(files[2].disc_offset, 1600);
    }

    #[test]
    fn parses_two_level_nesting() {
        let mut data = Vec::new();
        data.extend_from_slice(&entry(1, 0, 0, 6));
        data.extend_from_slice(&entry(1, 0, 0, 5));
        data.extend_from_slice(&entry(1, 6, 1, 4));
        data.extend_from_slice(&entry(0, 12, 10, 100));
        data.extend_from_slice(&entry(0, 21, 20, 200));
        data.extend_from_slice(&entry(0, 31, 30, 300));
        data.extend_from_slice(b"outer\0inner\0deep.arc\0after.bin\0top.bin\0");

        let files = parse_fst(&data).unwrap();
        assert_eq!(files.len(), 3);
        assert_eq!(files[0].host_rel_path, "outer/inner/deep.arc");
        assert_eq!(files[0].disc_offset, 40);
        assert_eq!(files[1].host_rel_path, "outer/after.bin");
        assert_eq!(files[1].disc_offset, 80);
        assert_eq!(files[2].host_rel_path, "top.bin");
        assert_eq!(files[2].disc_offset, 120);
    }

    #[test]
    fn rejects_malformed_root_fields() {
        let mut wrong_type = flat_fst(&[], b"");
        wrong_type[0] = 0;
        assert!(matches!(
            parse_fst(&wrong_type),
            Err(FstError::InvalidRootType { .. })
        ));

        let mut wrong_name = flat_fst(&[], b"");
        wrong_name[3] = 1;
        assert!(matches!(
            parse_fst(&wrong_name),
            Err(FstError::InvalidRootNameOffset { .. })
        ));

        let mut wrong_parent = flat_fst(&[], b"");
        wrong_parent[7] = 1;
        assert!(matches!(
            parse_fst(&wrong_parent),
            Err(FstError::InvalidRootParent { .. })
        ));

        let mut wrong_end = flat_fst(&[], b"");
        wrong_end[8..12].copy_from_slice(&0_u32.to_be_bytes());
        assert!(matches!(
            parse_fst(&wrong_end),
            Err(FstError::InvalidRootNextIndex { .. })
        ));
    }

    #[test]
    fn rejects_unknown_entry_flags() {
        let data = flat_fst(&[entry(2, 0, 0, 0)], b"x\0");
        assert!(matches!(
            parse_fst(&data),
            Err(FstError::UnknownFlags { entry: 1, flags: 2 })
        ));
    }

    #[test]
    fn rejects_invalid_directory_indices() {
        let mut bad_parent = Vec::new();
        bad_parent.extend_from_slice(&entry(1, 0, 0, 3));
        bad_parent.extend_from_slice(&entry(1, 0, 1, 3));
        bad_parent.extend_from_slice(&entry(0, 4, 1, 1));
        bad_parent.extend_from_slice(b"dir\0x\0");
        assert!(matches!(
            parse_fst(&bad_parent),
            Err(FstError::InvalidDirectoryParent { entry: 1, .. })
        ));

        let mut bad_end = Vec::new();
        bad_end.extend_from_slice(&entry(1, 0, 0, 2));
        bad_end.extend_from_slice(&entry(1, 0, 0, 1));
        bad_end.extend_from_slice(b"dir\0");
        assert!(matches!(
            parse_fst(&bad_end),
            Err(FstError::InvalidDirectoryNext { entry: 1, .. })
        ));
    }

    #[test]
    fn rejects_unsafe_duplicate_and_unterminated_names() {
        for unsafe_name in [
            b"..\0".as_slice(),
            b"a/b\0",
            b"a\\b\0",
            b"C:\0",
            b"CON.txt\0",
            b"com1.arc\0",
            b"LPT9\0",
            b"trailing.\0",
        ] {
            let data = flat_fst(&[entry(0, 0, 0, 0)], unsafe_name);
            assert!(matches!(
                parse_fst(&data),
                Err(FstError::UnsafeName { entry: 1, .. })
            ));
        }

        let duplicate = flat_fst(&[entry(0, 0, 0, 0), entry(0, 0, 0, 0)], b"same\0");
        assert!(matches!(
            parse_fst(&duplicate),
            Err(FstError::DuplicatePath { entry: 2, .. })
        ));

        let case_duplicate = flat_fst(&[entry(0, 0, 0, 0), entry(0, 5, 0, 0)], b"same\0SAME\0");
        assert!(matches!(
            parse_fst(&case_duplicate),
            Err(FstError::DuplicatePath { entry: 2, .. })
        ));

        let unterminated = flat_fst(&[entry(0, 0, 0, 0)], b"name");
        assert!(matches!(
            parse_fst(&unterminated),
            Err(FstError::MissingNameTerminator { entry: 1, .. })
        ));
    }

    #[test]
    fn rejects_name_out_of_bounds() {
        let data = flat_fst(&[entry(0, 9999, 0, 0)], b"x\0");
        assert!(matches!(
            parse_fst(&data),
            Err(FstError::NameOutOfBounds { entry: 1, .. })
        ));

        let middle_of_name = flat_fst(&[entry(0, 1, 0, 0)], b"name\0");
        assert!(matches!(
            parse_fst(&middle_of_name),
            Err(FstError::NameNotAtBoundary { entry: 1, .. })
        ));
    }

    #[test]
    fn rejects_windows_superscript_device_names_for_files_and_directories() {
        for prefix in ["COM", "com", "LPT", "lPt"] {
            for digit in ["¹", "²", "³"] {
                for extension in ["", ".arc", ".tar.gz"] {
                    let name = format!("{prefix}{digit}{extension}");
                    let strings = format!("{name}\0");
                    for flags in [0, 1] {
                        let data = flat_fst(&[entry(flags, 0, 0, 2)], strings.as_bytes());
                        assert_eq!(
                            parse_fst(&data).unwrap_err(),
                            FstError::UnsafeName {
                                entry: 1,
                                name: name.clone(),
                            }
                        );
                    }
                }
            }
        }
    }

    #[test]
    fn permits_non_reserved_device_like_names() {
        for name in [
            "COM0.arc",
            "COM10",
            "COM¹0.arc",
            "LPT0",
            "LPT⁴.arc",
            "acOM¹.arc",
        ] {
            let strings = format!("{name}\0");
            let data = flat_fst(&[entry(0, 0, 0, 2)], strings.as_bytes());
            assert_eq!(parse_fst(&data).unwrap()[0].host_rel_path, name);
        }
    }
}
