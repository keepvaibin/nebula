//! The disc's data files in the host's byte order (requirement B-02: the game reads its data natively, with no swap at
//! access). Each converter turns every field the game reads into little-endian and marks the file, so the game can
//! refuse a file that was not cooked.
//!
//! - BMG (messages): the header's sizes, INF1's counts and items (text offset, camera set), DAT1's text (UTF-16 units;
//!   a 0x1A escape's length and group bytes stay bytes, its id and parameters are 16-bit units), FLW1's counts, nodes
//!   (index; an event node's 32-bit value, the others' next index and group) and branch table, FLI1's header. Byte
//!   0x1F of the header becomes 2.

use anyhow::{bail, Context, Result};

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(bytes.get(at..at + 4).context("a field past the file's end")?.try_into().unwrap()))
}

fn be16(bytes: &[u8], at: usize) -> Result<u16> {
    Ok(u16::from_be_bytes(bytes.get(at..at + 2).context("a field past the file's end")?.try_into().unwrap()))
}

/// The big-endian 32-bit field at `at` of `source`, written to `out` in host order.
fn host32(source: &[u8], out: &mut [u8], at: usize) -> Result<u32> {
    let value = be32(source, at)?;
    out[at..at + 4].copy_from_slice(&value.to_le_bytes());
    Ok(value)
}

/// The big-endian 16-bit field at `at` of `source`, written to `out` in host order.
fn host16(source: &[u8], out: &mut [u8], at: usize) -> Result<u16> {
    let value = be16(source, at)?;
    out[at..at + 2].copy_from_slice(&value.to_le_bytes());
    Ok(value)
}

/// The mark in a BMG header's last padding byte: the whole file is in host order.
pub const BMG_HOST_ORDER: u8 = 2;

/// A message file (BMG) in host byte order.
pub fn bmg(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() < 0x20 || !bytes.starts_with(b"MESGbmg1") {
        bail!("not a message file");
    }
    if bytes[0x1F] == BMG_HOST_ORDER {
        return Ok(bytes.to_vec());
    }
    let mut out = bytes.to_vec();
    host32(bytes, &mut out, 0x8)?; // file size
    let blocks = host32(bytes, &mut out, 0xC)?;
    let mut at = 0x20usize;
    for _ in 0..blocks {
        let magic: [u8; 4] = bytes.get(at..at + 4).context("a block past the file's end")?.try_into().unwrap();
        host32(bytes, &mut out, at)?;
        let size = host32(bytes, &mut out, at + 4)? as usize;
        if size < 8 || at + size > bytes.len() {
            bail!("block {} at {at:#x}: size {size:#x} outside the file", String::from_utf8_lossy(&magic));
        }
        let end = at + size;
        match &magic {
            b"INF1" => {
                let count = host16(bytes, &mut out, at + 8)? as usize;
                let item = host16(bytes, &mut out, at + 10)? as usize;
                host32(bytes, &mut out, at + 12)?;
                if item < 6 || at + 16 + count * item > end {
                    bail!("INF1: {count} items of {item} bytes do not fit");
                }
                for i in 0..count {
                    let entry = at + 16 + i * item;
                    host32(bytes, &mut out, entry)?; // the text's offset in DAT1
                    host16(bytes, &mut out, entry + 4)?; // the camera set (the other attributes are bytes)
                }
            }
            b"DAT1" => {
                // As the game converted it at load (MessageHolder): every unit, except that the unit after a 0x1A
                // escape holds the tag's length and group as bytes; the tag's id and parameters are 16-bit units.
                let mut p = at + 8;
                while p + 2 <= end {
                    let unit = host16(bytes, &mut out, p)?;
                    p += 2;
                    if unit != 0x1A || p + 2 > end {
                        continue;
                    }
                    let tag_end = (p - 2 + bytes[p] as usize).min(end);
                    let mut q = p + 2;
                    while q + 2 <= tag_end {
                        host16(bytes, &mut out, q)?;
                        q += 2;
                    }
                    if tag_end < p {
                        bail!("DAT1: a tag at {:#x} shorter than its own header", p - 2);
                    }
                    p = tag_end;
                }
            }
            b"FLW1" => {
                let nodes = host16(bytes, &mut out, at + 8)? as usize;
                let branches = host16(bytes, &mut out, at + 10)? as usize;
                host32(bytes, &mut out, at + 12)?;
                if at + 16 + nodes * 8 + branches * 2 > end {
                    bail!("FLW1: {nodes} nodes and {branches} branches do not fit");
                }
                for i in 0..nodes {
                    let node = at + 16 + i * 8;
                    host16(bytes, &mut out, node + 2)?; // index
                    if bytes[node] == 3 {
                        host32(bytes, &mut out, node + 4)?; // an event node's value
                    } else {
                        host16(bytes, &mut out, node + 4)?; // next index
                        host16(bytes, &mut out, node + 6)?; // next group
                    }
                }
                for b in 0..branches {
                    host16(bytes, &mut out, at + 16 + nodes * 8 + b * 2)?;
                }
            }
            b"FLI1" => {} // the game finds the block and reads nothing in it
            _ => bail!("unknown block {} at {at:#x}", String::from_utf8_lossy(&magic)),
        }
        at = end;
    }
    out[0x1F] = BMG_HOST_ORDER;
    Ok(out)
}
