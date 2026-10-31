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

fn put32(bytes: &mut [u8], at: usize, value: u32) {
    bytes[at..at + 4].copy_from_slice(&value.to_be_bytes());
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

const FLAG_FILE: u8 = 0x01;
const FLAG_COMPRESSED: u8 = 0x04;
const FLAG_MRAM: u8 = 0x10;
const FLAG_ARAM: u8 = 0x20;
const FLAG_YAZ0: u8 = 0x80;

fn align32(n: usize) -> usize {
    (n + 31) & !31
}

/// An archive with each member file replaced by `cook(name, bytes)` (or kept when it returns None). Members keep
/// their order in the data area; the header's data, memory and auxiliary-memory sizes are summed again.
pub fn rebuild(bytes: &[u8], cook: &mut dyn FnMut(&str, Vec<u8>) -> Result<Option<Vec<u8>>>) -> Result<Vec<u8>> {
    let data_start = be32(bytes, 0x0C)? as usize + 0x20;
    let entry_count = be32(bytes, 0x28)? as usize;
    let entry_at = be32(bytes, 0x2C)? as usize + 0x20;
    let strings_at = be32(bytes, 0x34)? as usize + 0x20;
    if data_start > bytes.len() || entry_at + entry_count * 0x14 > data_start {
        bail!("an archive whose tables overlap its data");
    }
    struct Member {
        entry: usize,
        flags: u8,
        offset: usize,
        bytes: Vec<u8>,
    }
    let mut members = Vec::new();
    for e in 0..entry_count {
        let at = entry_at + e * 0x14;
        let flags = bytes[at + 4];
        if flags & FLAG_FILE == 0 {
            continue;
        }
        let name_at = strings_at + (be32(bytes, at + 4)? & 0x00FF_FFFF) as usize;
        let name_end = bytes[name_at..].iter().position(|b| *b == 0).context("an archive name without its end")?;
        let name = String::from_utf8_lossy(&bytes[name_at..name_at + name_end]).into_owned();
        let offset = be32(bytes, at + 8)? as usize;
        let size = be32(bytes, at + 12)? as usize;
        let start = data_start + offset;
        let mut member = bytes.get(start..start + size).with_context(|| format!("{name} lies past the archive's end"))?.to_vec();
        let mut flags = flags;
        if flags & FLAG_COMPRESSED != 0 || is_compressed(&member) {
            member = expand(&member).with_context(|| format!("{name} cannot be expanded"))?;
            flags &= !(FLAG_COMPRESSED | FLAG_YAZ0);
        }
        let member = match cook(&name, member.clone())? {
            Some(cooked) => cooked,
            None => member,
        };
        members.push(Member { entry: at, flags, offset, bytes: member });
    }
    members.sort_by_key(|m| m.offset);
    let mut out = bytes[..data_start].to_vec();
    let (mut mram, mut aram) = (0usize, 0usize);
    let mut placed: Vec<(usize, usize)> = Vec::new(); // original offset -> new offset (shared data stays shared)
    for m in &members {
        let new_offset = match placed.iter().find(|(old, _)| *old == m.offset) {
            Some((_, new)) => *new,
            None => {
                let new = out.len() - data_start;
                out.extend_from_slice(&m.bytes);
                out.resize(data_start + align32(out.len() - data_start), 0);
                let span = align32(m.bytes.len());
                if m.flags & FLAG_MRAM != 0 {
                    mram += span;
                } else if m.flags & FLAG_ARAM != 0 {
                    aram += span;
                }
                placed.push((m.offset, new));
                new
            }
        };
        out[m.entry + 4] = m.flags;
        put32(&mut out, m.entry + 8, new_offset as u32);
        put32(&mut out, m.entry + 12, m.bytes.len() as u32);
    }
    let total = out.len() - data_start;
    let file_size = out.len() as u32;
    put32(&mut out, 0x04, file_size);
    put32(&mut out, 0x10, total as u32);
    put32(&mut out, 0x14, mram as u32);
    put32(&mut out, 0x18, aram as u32);
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

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
