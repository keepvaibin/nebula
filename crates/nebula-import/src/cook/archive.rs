//! The disc's compression and archives: Yaz0 (SZS) and Yay0 (SZP) are expanded once, and an archive (RARC) is
//! rebuilt around its cooked members with their compression flags cleared, so the game reads every file as it is.

use anyhow::{bail, Context, Result};

/// "Yaz0" and "Yay0", as the file's first word.
const YAZ0: u32 = 0x5961_7A30;
const YAY0: u32 = 0x5961_7930;
/// "RARC".
const RARC: u32 = 0x5241_5243;

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(bytes.get(at..at + 4).context("a field past the data's end")?.try_into().unwrap()))
}

fn be16(bytes: &[u8], at: usize) -> Result<u16> {
    Ok(u16::from_be_bytes(bytes.get(at..at + 2).context("a field past the data's end")?.try_into().unwrap()))
}

pub fn is_compressed(bytes: &[u8]) -> bool {
    matches!(be32(bytes, 0), Ok(YAZ0) | Ok(YAY0)) && bytes.len() >= 16
}

/// The expanded bytes of a Yaz0 or Yay0 file.
pub fn expand(bytes: &[u8]) -> Result<Vec<u8>> {
    let size = be32(bytes, 4)? as usize;
    if size > 256 << 20 {
        bail!("a compressed file that expands to {size} bytes");
    }
    let mut out = Vec::with_capacity(size);
    if be32(bytes, 0)? == YAZ0 {
        let mut src = 16usize;
        let mut code = 0u8;
        let mut bits = 0;
        while out.len() < size {
            if bits == 0 {
                code = *bytes.get(src).context("Yaz0 data ends early")?;
                src += 1;
                bits = 8;
            }
            if code & 0x80 != 0 {
                out.push(*bytes.get(src).context("Yaz0 data ends early")?);
                src += 1;
            } else {
                let b1 = *bytes.get(src).context("Yaz0 data ends early")? as usize;
                let b2 = *bytes.get(src + 1).context("Yaz0 data ends early")? as usize;
                src += 2;
                let distance = ((b1 & 0xF) << 8 | b2) + 1;
                let length = if b1 >> 4 == 0 {
                    let n = *bytes.get(src).context("Yaz0 data ends early")? as usize + 0x12;
                    src += 1;
                    n
                } else {
                    (b1 >> 4) + 2
                };
                copy_back(&mut out, distance, length, size)?;
            }
            code <<= 1;
            bits -= 1;
        }
    } else {
        // Yay0: flag words, then back-references, then literal bytes, each stream at its header's offset.
        let mut links = be32(bytes, 8)? as usize;
        let mut chunks = be32(bytes, 12)? as usize;
        let mut flags_at = 16usize;
        let mut flags = 0u32;
        let mut bits = 0;
        while out.len() < size {
            if bits == 0 {
                flags = be32(bytes, flags_at)?;
                flags_at += 4;
                bits = 32;
            }
            if flags & 0x8000_0000 != 0 {
                out.push(*bytes.get(chunks).context("Yay0 data ends early")?);
                chunks += 1;
            } else {
                let link = be16(bytes, links)? as usize;
                links += 2;
                let distance = (link & 0xFFF) + 1;
                let length = if link >> 12 == 0 {
                    let n = *bytes.get(chunks).context("Yay0 data ends early")? as usize + 0x12;
                    chunks += 1;
                    n
                } else {
                    (link >> 12) + 2
                };
                copy_back(&mut out, distance, length, size)?;
            }
            flags <<= 1;
            bits -= 1;
        }
    }
    Ok(out)
}

fn copy_back(out: &mut Vec<u8>, distance: usize, length: usize, size: usize) -> Result<()> {
    if distance > out.len() {
        bail!("compressed data refers before its start");
    }
    for _ in 0..length.min(size - out.len()) {
        out.push(out[out.len() - distance]);
    }
    Ok(())
}

pub fn is_archive(bytes: &[u8]) -> bool {
    bytes.len() >= 0x40 && be32(bytes, 0).ok() == Some(RARC)
}

const FLAG_FOLDER: u8 = 0x02;
const FLAG_COMPRESSED: u8 = 0x04;

/// A file of an archive: its path below the archive, where it lay in the archive's data, its bytes expanded.
pub struct Member {
    pub path: String,
    pub offset: usize,
    pub bytes: Vec<u8>,
}

/// An archive's files, folder by folder as its directory lists them (a sub-folder's files where the folder is listed).
pub fn members(bytes: &[u8]) -> Result<Vec<Member>> {
    let data_start = be32(bytes, 0x0C)? as usize + 0x20;
    let folders_at = be32(bytes, 0x24)? as usize + 0x20;
    let folder_count = be32(bytes, 0x20)? as usize;
    let entry_at = be32(bytes, 0x2C)? as usize + 0x20;
    let entry_count = be32(bytes, 0x28)? as usize;
    let strings_at = be32(bytes, 0x34)? as usize + 0x20;
    if data_start > bytes.len() || entry_at + entry_count * 0x14 > data_start || folders_at + folder_count * 0x10 > data_start {
        bail!("an archive whose tables overlap its data");
    }
    let mut out = Vec::new();
    if folder_count != 0 {
        let layout = Layout { data_start, folders_at, folder_count, entry_at, entry_count, strings_at };
        let mut walked = Walked { folders: vec![false; folder_count], bytes: 0 };
        walk(bytes, &layout, 0, "", 0, &mut walked, &mut out)?;
    }
    Ok(out)
}

/// Past this an archive's members are refused: entries can share bytes, so the table alone can ask for any amount.
const MEMBERS_MAX: usize = 4 << 30;

/// What a walk has been through: each folder once, however the entries point at them, and the members' bytes.
struct Walked {
    folders: Vec<bool>,
    bytes: usize,
}

struct Layout {
    data_start: usize,
    folders_at: usize,
    folder_count: usize,
    entry_at: usize,
    entry_count: usize,
    strings_at: usize,
}

fn walk(bytes: &[u8], layout: &Layout, folder: usize, prefix: &str, depth: usize, walked: &mut Walked, out: &mut Vec<Member>) -> Result<()> {
    if folder >= layout.folder_count || depth > 32 || std::mem::replace(&mut walked.folders[folder], true) {
        bail!("an archive's folders do not form a tree");
    }
    let at = layout.folders_at + folder * 0x10;
    let count = u16::from_be_bytes(bytes[at + 0x0A..at + 0x0C].try_into().unwrap()) as usize;
    let first = be32(bytes, at + 0x0C)? as usize;
    if first + count > layout.entry_count {
        bail!("an archive folder lists past the file table");
    }
    for e in first..first + count {
        let at = layout.entry_at + e * 0x14;
        let flags = bytes[at + 4];
        let name_at = layout.strings_at + (be32(bytes, at + 4)? & 0x00FF_FFFF) as usize;
        let name_end = bytes.get(name_at..).and_then(|s| s.iter().position(|b| *b == 0)).context("an archive name without its end")?;
        let name = String::from_utf8_lossy(&bytes[name_at..name_at + name_end]).into_owned();
        if name == "." || name == ".." {
            continue;
        }
        let path = if prefix.is_empty() { name.clone() } else { format!("{prefix}/{name}") };
        let offset = be32(bytes, at + 8)? as usize;
        if flags & FLAG_FOLDER != 0 {
            walk(bytes, layout, offset, &path, depth + 1, walked, out)?;
            continue;
        }
        let size = be32(bytes, at + 12)? as usize;
        let start = layout.data_start + offset;
        let mut member = bytes.get(start..start + size).with_context(|| format!("{name} lies past the archive's end"))?.to_vec();
        if flags & FLAG_COMPRESSED != 0 || is_compressed(&member) {
            member = expand(&member).with_context(|| format!("{name} cannot be expanded"))?;
        }
        walked.bytes += member.len();
        if walked.bytes > MEMBERS_MAX {
            bail!("an archive whose files come to more than {MEMBERS_MAX} bytes");
        }
        out.push(Member { path, offset, bytes: member });
    }
    Ok(())
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    // An archive: a.bin, then the folder sub with b.bin; a.bin's bytes lie after b.bin's.
    pub(crate) fn small_archive() -> Vec<u8> {
        let names = b".\0..\0a.bin\0sub\0b.bin\0";
        let (folders_at, entries_at, strings_at, data_at) = (0x40usize, 0x60usize, 0xECusize, 0x120usize);
        let mut out = vec![0u8; data_at];
        out[..4].copy_from_slice(b"RARC");
        for (at, value) in [(0x0C, data_at - 0x20), (0x20, 2), (0x24, folders_at - 0x20), (0x28, 7), (0x2C, entries_at - 0x20), (0x34, strings_at - 0x20)] {
            put32(&mut out, at, value as u32);
        }
        out[folders_at + 0x0B] = 4;
        out[folders_at + 0x10 + 0x0B] = 3;
        put32(&mut out, folders_at + 0x10 + 0x0C, 4);
        // Name offsets: "." 0, ".." 2, a.bin 5, sub 11, b.bin 15.
        let entries: [(u32, u32, u32, u32); 7] = [(0x01, 5, 16, 2), (0x02, 11, 1, 0), (0x02, 0, 0, 0), (0x02, 2, 0, 0), (0x01, 15, 0, 3), (0x02, 0, 1, 0), (0x02, 2, 0, 0)];
        for (i, (flags, name, offset, size)) in entries.into_iter().enumerate() {
            let at = entries_at + i * 0x14;
            put32(&mut out, at + 4, flags << 24 | name);
            put32(&mut out, at + 8, offset);
            put32(&mut out, at + 12, size);
        }
        out[strings_at..strings_at + names.len()].copy_from_slice(names);
        out.extend_from_slice(b"bbb");
        out.resize(data_at + 16, 0);
        out.extend_from_slice(b"aa");
        out
    }

    fn put32(out: &mut [u8], at: usize, value: u32) {
        out[at..at + 4].copy_from_slice(&value.to_be_bytes());
    }

    #[test]
    fn members_in_their_folders_order() {
        let members = members(&small_archive()).unwrap();
        let listed: Vec<(&str, usize, &[u8])> = members.iter().map(|m| (m.path.as_str(), m.offset, m.bytes.as_slice())).collect();
        assert_eq!(listed, [("a.bin", 16, &b"aa"[..]), ("sub/b.bin", 0, &b"bbb"[..])]);
    }

    #[test]
    fn folders_that_list_each_other_are_refused_at_once() {
        // Each of 32 folders' two entries points at the next: 2^32 walks if a folder could be walked twice.
        let folders = 32usize;
        let (folders_at, entries_at) = (0x40usize, 0x40 + folders * 0x10);
        let strings_at = entries_at + folders * 2 * 0x14;
        let data_at = strings_at + 0x10;
        let mut out = vec![0u8; data_at];
        out[..4].copy_from_slice(b"RARC");
        for (at, value) in [(0x0C, data_at - 0x20), (0x20, folders), (0x24, folders_at - 0x20), (0x28, folders * 2), (0x2C, entries_at - 0x20), (0x34, strings_at - 0x20)] {
            put32(&mut out, at, value as u32);
        }
        out[strings_at..strings_at + 2].copy_from_slice(b"d\0");
        for folder in 0..folders {
            let at = folders_at + folder * 0x10;
            out[at + 0x0B] = 2;
            put32(&mut out, at + 0x0C, (folder * 2) as u32);
            for entry in 0..2 {
                let at = entries_at + (folder * 2 + entry) * 0x14;
                put32(&mut out, at + 4, 0x02 << 24);
                put32(&mut out, at + 8, ((folder + 1) % folders) as u32);
            }
        }
        assert!(members(&out).err().unwrap().to_string().contains("do not form a tree"));
    }

    #[test]
    fn an_archive_is_a_folder_of_the_store_with_its_files_side_by_side() {
        let mut stats = crate::cook::Stats::default();
        let items = crate::cook::cook_entry("x.arc", small_archive(), &mut stats).unwrap();
        let paths: Vec<&str> = items.iter().map(|i| i.path.as_str()).collect();
        assert_eq!(paths, ["a.bin", "sub/b.bin"]);
        let orders: Vec<usize> = items.iter().map(|i| i.order).collect();
        assert_eq!(orders, [16, 0]);
    }

    #[test]
    fn yaz0_literals_and_back_references() {
        // "abcabcabc": three literals, then a 6-byte copy from 3 back.
        let mut data = b"Yaz0".to_vec();
        data.extend_from_slice(&9u32.to_be_bytes());
        data.extend_from_slice(&[0; 8]);
        data.push(0b1110_0000);
        data.extend_from_slice(b"abc");
        data.extend_from_slice(&[(6 - 2) << 4, 2]);
        assert_eq!(expand(&data).unwrap(), b"abcabcabc");
    }
}
