//! The disc's data files in the host's byte order (requirement B-02: the game reads its data natively, with no swap at
//! access). Each converter turns every field the game reads into little-endian and marks the file, so the game can
//! refuse a file that was not cooked.
//!
//! - BMG (messages): the header's sizes, INF1's counts and items (text offset, camera set), DAT1's text (UTF-16 units;
//!   a 0x1A escape's length and group bytes stay bytes, its id and parameters are 16-bit units), FLW1's counts, nodes
//!   (index; an event node's 32-bit value, the others' next index and group) and branch table, FLI1's header. Byte
//!   0x1F of the header becomes 2.
//! - KCL (collision): the four offsets and the scalars (0x0-0x38), positions and normals (whole vectors), prisms from
//!   entry 1 (a height and five indices), and the octree (s32 node words, u16 prism lists; a shared list once). A
//!   cooked file's first offset reads below 64 KB in host order (the disc's, big-endian, reads at least 64 KB).
//! - BCSV (tables: .bcsv, .tbl, .pa, .bcam, .banmt and the stages' extensionless placement tables): the header, the
//!   field table (hash, mask, offset) and each entry's values by their field's type (32-bit integers, floats and
//!   string offsets, 16-bit integers; bytes and embedded strings stay). A cooked table's field count reads below 64 K
//!   in host order.
//! - CANM/CKAN (camera animations): the header's six values, the eight components' counts, offsets (and key types),
//!   the value block's byte count and its floats. Byte 7 of a cooked file's type ("CANM"/"CKAN") becomes lower case.

use anyhow::{bail, Context, Result};
use std::collections::HashSet;

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
    for block in 0..blocks {
        let magic: [u8; 4] = bytes.get(at..at + 4).context("a block past the file's end")?.try_into().unwrap();
        host32(bytes, &mut out, at)?;
        let size = host32(bytes, &mut out, at + 4)? as usize;
        // The last block's size may count padding the archive does not store (the disc's FLI1 blocks).
        let last = block + 1 == blocks;
        if size < 8 || (at + size > bytes.len() && !last) {
            bail!("block {} at {at:#x}: size {size:#x} outside the file", String::from_utf8_lossy(&magic));
        }
        let end = (at + size).min(bytes.len());
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

/// A collision file (KCL) in host byte order.
pub fn kcl(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() < 0x38 {
        bail!("not a collision file");
    }
    if u32::from_le_bytes(bytes[0..4].try_into().unwrap()) < 0x10000 {
        return Ok(bytes.to_vec()); // cooked already
    }
    let mut out = bytes.to_vec();
    let pos = host32(bytes, &mut out, 0x0)? as usize;
    let norm = host32(bytes, &mut out, 0x4)? as usize;
    let prism = host32(bytes, &mut out, 0x8)? as usize;
    let octree = host32(bytes, &mut out, 0xC)? as usize;
    for at in (0x10..0x38).step_by(4) {
        host32(bytes, &mut out, at)?;
    }
    // Whole vectors (the last one's y and z too); the prisms are 1-based, entry 0 overlapping the normals.
    let mut at = pos;
    while at + 12 <= norm {
        for k in 0..3 {
            host32(bytes, &mut out, at + 4 * k)?;
        }
        at += 12;
    }
    let mut at = norm;
    while at + 12 <= prism + 0x10 {
        for k in 0..3 {
            host32(bytes, &mut out, at + 4 * k)?;
        }
        at += 12;
    }
    let mut at = prism + 0x10;
    while at + 0x10 <= octree {
        host32(bytes, &mut out, at)?; // height
        for k in (4..0x10).step_by(2) {
            host16(bytes, &mut out, at + k)?; // position, normal, three edges, attribute
        }
        at += 0x10;
    }
    let shift = be32(bytes, 0x2C)?;
    let roots = if be32(bytes, 0x34)? as i32 == -1 && be32(bytes, 0x30)? as i32 == -1 {
        1usize
    } else {
        let side = |mask_at: usize| -> Result<usize> { Ok(((!be32(bytes, mask_at)?) >> shift) as usize + 1) };
        side(0x20)? * side(0x24)? * side(0x28)?
    };
    let mut walk = Octree { source: bytes, base: octree, done: HashSet::new() };
    walk.block(&mut out, 0, roots, 0)?;
    Ok(out)
}

/// The octree's walk (as KCollision.cpp's KCLOctreeSwap): a word >= 0 is the offset of 8 child words from its block,
/// a negative word (low 31 bits) the offset from its block of a u16 list read from its second unit to a 0.
struct Octree<'a> {
    source: &'a [u8],
    base: usize,
    done: HashSet<usize>,
}

impl Octree<'_> {
    fn block(&mut self, out: &mut [u8], offs: usize, count: usize, depth: u32) -> Result<()> {
        if depth > 64 {
            bail!("octree deeper than 64 levels at {offs:#x}");
        }
        for i in 0..count {
            let word = self.base + offs + i * 4;
            let value = be32(self.source, word)? as i32;
            if self.done.insert((offs + i * 4) >> 1) {
                host32(self.source, out, word)?;
            }
            if value >= 0 {
                self.block(out, offs + value as usize, 8, depth + 1)?;
            } else {
                self.list(out, offs + (value as u32 & 0x7FFF_FFFF) as usize)?;
            }
        }
        Ok(())
    }

    fn list(&mut self, out: &mut [u8], offs: usize) -> Result<()> {
        let mut at = offs + 2;
        loop {
            let unit = be16(self.source, self.base + at)?;
            if self.done.insert(at >> 1) {
                host16(self.source, out, self.base + at)?;
            }
            if unit == 0 {
                return Ok(());
            }
            at += 2;
        }
    }
}

/// The bytes a BCSV field of `kind` occupies and swaps (0: none).
fn bcsv_width(kind: u8) -> Option<usize> {
    match kind {
        0 | 2 | 3 | 6 => Some(4), // long, float, long 2, string offset
        4 => Some(2),             // short
        1 | 5 => Some(0),         // embedded string, byte
        _ => None,
    }
}

/// A big-endian BCSV's layout (entries, fields, data offset, entry size) when every part lies in the file.
fn bcsv_layout(bytes: &[u8]) -> Option<(usize, usize, usize, usize)> {
    if bytes.len() < 16 {
        return None;
    }
    let entries = be32(bytes, 0).ok()? as i32;
    let fields = be32(bytes, 4).ok()? as usize;
    let data = be32(bytes, 8).ok()? as usize;
    let size = be32(bytes, 12).ok()? as usize;
    // An empty table (no fields, no entries) is a header alone.
    let empty = fields == 0 && entries == 0;
    if entries < 0 || fields > 1024 || data != 16 + 12 * fields || size > 0x10000 || (!empty && (fields == 0 || size == 0)) {
        return None;
    }
    let end = data.checked_add((entries as usize).checked_mul(size)?)?;
    if end > bytes.len() {
        return None;
    }
    let strings = bytes.len() - end;
    for f in 0..fields {
        let at = 16 + 12 * f;
        let offset = be16(bytes, at + 8).ok()? as usize;
        let kind = bytes[at + 11];
        let width = bcsv_width(kind)?;
        if bytes[at + 10] >= 32 || offset + width.max(1) > size {
            return None;
        }
        if kind == 6 {
            for e in 0..entries as usize {
                if be32(bytes, data + e * size + offset).ok()? as usize >= strings.max(1) {
                    return None;
                }
            }
        }
    }
    Some((entries as usize, fields, data, size))
}

/// A table (BCSV) in host byte order: Some(cooked) for a table, None for a file that is not one (or is cooked
/// already). `listed`: the name says it is a table, so one that does not read as one stops the cook.
pub fn bcsv(bytes: &[u8], listed: bool) -> Result<Option<Vec<u8>>> {
    let le = |at: usize| bytes.get(at..at + 4).map(|b| u32::from_le_bytes(b.try_into().unwrap()));
    if let (Some(fields), Some(data)) = (le(4), le(8)) {
        if fields < 0x10000 && data as usize == 16 + 12 * fields as usize && bcsv_layout(bytes).is_none() {
            return Ok(None); // cooked already
        }
    }
    let Some((entries, fields, data, size)) = bcsv_layout(bytes) else {
        if listed {
            bail!("a table that does not read as one");
        }
        return Ok(None);
    };
    let mut out = bytes.to_vec();
    for at in (0..16).step_by(4) {
        host32(bytes, &mut out, at)?;
    }
    let mut columns = Vec::with_capacity(fields);
    for f in 0..fields {
        let at = 16 + 12 * f;
        host32(bytes, &mut out, at)?; // hash
        host32(bytes, &mut out, at + 4)?; // mask
        let offset = host16(bytes, &mut out, at + 8)? as usize;
        columns.push((offset, bcsv_width(bytes[at + 11]).unwrap()));
    }
    // Fields packed into one word share its offset: each place is swapped once, at one width.
    // Widest first at each offset, so the place keeps the width it is swapped at.
    columns.sort_by(|a, b| a.0.cmp(&b.0).then(b.1.cmp(&a.1)));
    columns.dedup();
    // A field inside another's swapped word at another offset would move with the swap.
    for (i, a) in columns.iter().enumerate() {
        for b in &columns[i + 1..] {
            if a.1 > 0 && b.0 != a.0 && b.0 < a.0 + a.1 {
                bail!("a field at {:#x} lies inside the {}-byte field at {:#x}", b.0, a.1, a.0);
            }
        }
    }
    for pair in columns.windows(2) {
        if pair[0].0 == pair[1].0 && pair[0].1 != pair[1].1 && pair[0].1 != 0 && pair[1].1 != 0 {
            bail!("fields at offset {:#x} read at {} and {} bytes", pair[0].0, pair[0].1, pair[1].1);
        }
    }
    columns.dedup_by_key(|c| c.0);
    for e in 0..entries {
        let entry = data + e * size;
        for &(offset, width) in &columns {
            match width {
                4 => {
                    host32(bytes, &mut out, entry + offset)?;
                }
                2 => {
                    host16(bytes, &mut out, entry + offset)?;
                }
                _ => {}
            }
        }
    }
    Ok(Some(out))
}

/// A camera animation (CANM: frames, CKAN: keys) in host byte order.
pub fn canm(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() < 0x20 {
        bail!("not a camera animation");
    }
    let key = match &bytes[4..8] {
        b"CANM" => false,
        b"CKAN" => true,
        b"CANm" | b"CKAn" => return Ok(bytes.to_vec()), // cooked already
        _ => bail!("not a camera animation ({:?})", String::from_utf8_lossy(&bytes[4..8])),
    };
    let mut out = bytes.to_vec();
    for at in (0x8..0x20).step_by(4) {
        host32(bytes, &mut out, at)?;
    }
    let entry = 0x20;
    let info_words = if key { 24 } else { 16 }; // eight components of three (keys) or two words
    for w in 0..info_words {
        host32(bytes, &mut out, entry + 4 * w)?;
    }
    let values = entry + be32(bytes, 0x1C)? as usize;
    let size = host32(bytes, &mut out, values)? as usize;
    if values + 4 + size > bytes.len() || size % 4 != 0 {
        bail!("camera animation values ({size:#x} bytes at {values:#x}) outside the file");
    }
    for v in 0..size / 4 {
        host32(bytes, &mut out, values + 4 + 4 * v)?;
    }
    out[7] = out[7].to_ascii_lowercase();
    Ok(out)
}
