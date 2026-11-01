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
//! - JPC (particles, after cook::formats::jpc rewrote its textures): the header's counts and texture offset, each
//!   resource's index and block count, every block's size and fields (BEM1 dynamics, FLD1 fields, BSP1 base shape and
//!   its texture-coordinate table and colour keys, ESP1 extra shape, SSP1 child shape, ETX1 indirect matrix, KFA1
//!   keys, TDB1 texture indices) and the TEX1 blocks' sizes (their ResTIMG stays with the texture headers). Block
//!   magics stay bytes; byte 3 of a cooked file ("JPAC2-10") becomes 'c'.
//! - BRLYT/BRLAN (layouts and their animations): the file header's numbers (its byte-order mark reads 0xFEFF in host
//!   order once cooked), every block's size and numbers: layout size, texture/font name tables, the material list
//!   (colours, resource bits, texture indices, texture and indirect SRTs), panes (transform floats, vertex colours,
//!   texture coordinates, a text box's numbers and its UTF-16 text as messages' (tags kept), a window's inflation,
//!   content and frame table), groups, and the animation tag/share/animation blocks down to their keys. Block
//!   kinds and names stay characters.
//! - TPL (layout textures, after cook::formats::tpl rewrote the images): the version (2142000 reads in host order
//!   once cooked), the image count and table, each image header's size, format, data offset, wraps, filters and
//!   LOD bias, and a palette header where there is one.
//! - BRFNT (fonts, after cook::formats::font made the sheets records): the native layout "RFNN" (font_native).
//! - J3D animations (BCK, BCA, BTK, BRK, BPK, BPA, BTP, BVA, BLK, BLA, BXK, BXA): headers, counts, offsets and the
//!   16-bit and float arrays by block type (j3d_animation); name tables stay with the models'.

use anyhow::{bail, Context, Result};
use std::collections::{HashMap, HashSet};

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

/// The 32-bit words at [from, to) of a block at `at`.
fn host_words(src: &[u8], out: &mut [u8], at: usize, from: usize, to: usize) -> Result<()> {
    for o in (from..to).step_by(4) {
        host32(src, out, at + o)?;
    }
    Ok(())
}

/// The 16-bit values at [from, to) of a block at `at`.
fn host_halves(src: &[u8], out: &mut [u8], at: usize, from: usize, to: usize) -> Result<()> {
    for o in (from..to).step_by(2) {
        host16(src, out, at + o)?;
    }
    Ok(())
}

/// A particle file (JPC) in host byte order.
pub fn jpc_host(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.starts_with(b"JPAc2-10") {
        return Ok(bytes.to_vec());
    }
    if !bytes.starts_with(b"JPAC2-10") {
        bail!("not a particle file");
    }
    let mut out = bytes.to_vec();
    let resources = host16(bytes, &mut out, 8)? as usize;
    let textures = host16(bytes, &mut out, 0xA)? as usize;
    let tex_offset = host32(bytes, &mut out, 0xC)? as usize;
    let mut at = 0x10;
    for _ in 0..resources {
        host16(bytes, &mut out, at)?; // user index
        let blocks = host16(bytes, &mut out, at + 2)? as usize;
        at += 8;
        for _ in 0..blocks {
            let magic: [u8; 4] = bytes.get(at..at + 4).context("a block past the file's end")?.try_into().unwrap();
            let size = host32(bytes, &mut out, at + 4)? as usize;
            if size < 8 || at + size > bytes.len() {
                bail!("block {} at {at:#x}: size {size:#x}", String::from_utf8_lossy(&magic));
            }
            match &magic {
                b"BEM1" => {
                    host_words(bytes, &mut out, at, 0x8, 0x68)?; // flags, user work, three vectors, thirteen floats
                    host_halves(bytes, &mut out, at, 0x68, 0x78)?; // rotation, frames, life, volume size, divisions
                }
                b"FLD1" => host_words(bytes, &mut out, at, 0x8, 0x40)?,
                b"BSP1" => {
                    let flags = host32(bytes, &mut out, at + 0x8)?;
                    let prm = host16(bytes, &mut out, at + 0xC)? as usize;
                    let env = host16(bytes, &mut out, at + 0xE)? as usize;
                    host_words(bytes, &mut out, at, 0x10, 0x18)?; // base size x, y
                    host16(bytes, &mut out, at + 0x18)?; // blend mode
                    host16(bytes, &mut out, at + 0x24)?; // colour animation's last frame
                    if flags & 0x0100_0000 != 0 {
                        host_words(bytes, &mut out, at, 0x34, 0x34 + 0x28)?; // texture-coordinate animation table
                    }
                    let tables = [(bytes[at + 0x21] & 0x02 != 0, prm, bytes[at + 0x22]), (bytes[at + 0x21] & 0x08 != 0, env, bytes[at + 0x23])];
                    for (on, offset, keys) in tables {
                        if on {
                            for k in 0..keys as usize {
                                host16(bytes, &mut out, at + offset + 6 * k)?; // key frame; the colour is bytes
                            }
                        }
                    }
                }
                b"ESP1" => {
                    host_words(bytes, &mut out, at, 0x8, 0x28)?;
                    host_halves(bytes, &mut out, at, 0x28, 0x2C)?;
                    host_words(bytes, &mut out, at, 0x2C, 0x60)?;
                }
                b"SSP1" => {
                    host_words(bytes, &mut out, at, 0x8, 0x34)?;
                    host32(bytes, &mut out, at + 0x3C)?; // timing (the two colours are bytes)
                    host_halves(bytes, &mut out, at, 0x40, 0x44)?; // life, rate
                    host16(bytes, &mut out, at + 0x46)?; // rotation speed
                }
                b"ETX1" => host_words(bytes, &mut out, at, 0x8, 0x24)?,
                b"KFA1" => {
                    let keys = bytes[at + 9] as usize;
                    host_words(bytes, &mut out, at, 0xC, 0xC + 16 * keys)?;
                }
                b"TDB1" => host_halves(bytes, &mut out, at, 0x8, size & !1)?,
                _ => bail!("unknown block {} at {at:#x}", String::from_utf8_lossy(&magic)),
            }
            at += size;
        }
    }
    let mut at = tex_offset;
    for _ in 0..textures {
        let size = host32(bytes, &mut out, at + 4)? as usize;
        if size < 8 || at + size > bytes.len() {
            bail!("texture block at {at:#x}: size {size:#x}");
        }
        at += size;
    }
    out[3] = b'c';
    Ok(out)
}

/// `count` 32-bit words at `at`, as host order.
fn host_floats(src: &[u8], out: &mut [u8], at: usize, count: usize) -> Result<()> {
    for k in 0..count {
        host32(src, out, at + 4 * k)?;
    }
    Ok(())
}

/// UTF-16 text in place, as messages' (DAT1): a 0x1A escape's length and group bytes stay bytes.
fn host_text(src: &[u8], out: &mut [u8], at: usize, end: usize) -> Result<()> {
    let mut p = at;
    while p + 2 <= end {
        let unit = host16(src, out, p)?;
        p += 2;
        if unit != 0x1A || p + 2 > end {
            continue;
        }
        let tag_end = (p - 2 + src[p] as usize).min(end);
        let mut q = p + 2;
        while q + 2 <= tag_end {
            host16(src, out, q)?;
            q += 2;
        }
        if tag_end < p {
            bail!("text: a tag at {:#x} shorter than its own header", p - 2);
        }
        p = tag_end;
    }
    Ok(())
}

/// A pane's transform (translate, rotate, scale, size: ten floats at 0x24).
fn host_pane(src: &[u8], out: &mut [u8], at: usize) -> Result<()> {
    host_floats(src, out, at + 0x24, 10)
}

/// A layout (BRLYT) or layout animation (BRLAN) in host byte order.
pub fn layout(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() < 0x10 || !(bytes.starts_with(b"RLYT") || bytes.starts_with(b"RLAN")) {
        bail!("not a layout file");
    }
    if bytes[4] == 0xFF && bytes[5] == 0xFE {
        return Ok(bytes.to_vec()); // cooked already
    }
    let mut out = bytes.to_vec();
    host16(bytes, &mut out, 4)?; // byte-order mark
    host16(bytes, &mut out, 6)?; // version
    host32(bytes, &mut out, 8)?; // file size
    let header = host16(bytes, &mut out, 0xC)? as usize;
    let blocks = host16(bytes, &mut out, 0xE)? as usize;
    let mut at = header;
    for _ in 0..blocks {
        let kind: [u8; 4] = bytes.get(at..at + 4).context("a block past the file's end")?.try_into().unwrap();
        let size = host32(bytes, &mut out, at + 4)? as usize;
        if size < 8 || at + size > bytes.len() {
            bail!("block {} at {at:#x}: size {size:#x}", String::from_utf8_lossy(&kind));
        }
        match &kind {
            b"lyt1" => host_floats(bytes, &mut out, at + 0xC, 2)?,
            b"txl1" | b"fnl1" => {
                let n = host16(bytes, &mut out, at + 8)? as usize;
                for i in 0..n {
                    host32(bytes, &mut out, at + 0xC + 8 * i)?; // name offset (type and padding are bytes)
                }
            }
            b"mat1" => {
                let n = host16(bytes, &mut out, at + 8)? as usize;
                for i in 0..n {
                    let offset = host32(bytes, &mut out, at + 0xC + 4 * i)? as usize;
                    material(bytes, &mut out, at + offset)?;
                }
            }
            b"pan1" | b"bnd1" => host_pane(bytes, &mut out, at)?,
            b"pic1" => {
                host_pane(bytes, &mut out, at)?;
                host_floats(bytes, &mut out, at + 0x4C, 4)?; // vertex colours
                host16(bytes, &mut out, at + 0x5C)?; // material
                host_floats(bytes, &mut out, at + 0x60, 8 * bytes[at + 0x5E] as usize)?; // texture coordinates
            }
            b"txt1" => {
                host_pane(bytes, &mut out, at)?;
                host16(bytes, &mut out, at + 0x4C)?; // buffer bytes
                let text_bytes = host16(bytes, &mut out, at + 0x4E)? as usize;
                host16(bytes, &mut out, at + 0x50)?; // material
                host16(bytes, &mut out, at + 0x52)?; // font
                let text = host32(bytes, &mut out, at + 0x58)? as usize;
                host_floats(bytes, &mut out, at + 0x5C, 6)?; // two colours, font size, character and line spacing
                if text_bytes > 0 {
                    host_text(bytes, &mut out, at + text, at + text + text_bytes)?;
                }
            }
            b"wnd1" => {
                host_pane(bytes, &mut out, at)?;
                host_floats(bytes, &mut out, at + 0x4C, 4)?; // inflation
                let frames = bytes[at + 0x5C] as usize;
                let content = host32(bytes, &mut out, at + 0x60)? as usize;
                let table = host32(bytes, &mut out, at + 0x64)? as usize;
                let c = at + content;
                host_floats(bytes, &mut out, c, 4)?; // vertex colours
                host16(bytes, &mut out, c + 0x10)?; // material
                host_floats(bytes, &mut out, c + 0x14, 8 * bytes[c + 0x12] as usize)?;
                for f in 0..frames {
                    let frame = host32(bytes, &mut out, at + table + 4 * f)? as usize;
                    host16(bytes, &mut out, at + frame)?; // the frame's material
                }
            }
            b"grp1" => {
                host16(bytes, &mut out, at + 0x18)?; // pane count (the names are characters)
            }
            b"pas1" | b"pae1" | b"grs1" | b"gre1" | b"usd1" => {}
            b"pat1" => {
                host16(bytes, &mut out, at + 8)?;
                host16(bytes, &mut out, at + 0xA)?;
                host32(bytes, &mut out, at + 0xC)?;
                host32(bytes, &mut out, at + 0x10)?;
                host16(bytes, &mut out, at + 0x14)?;
                host16(bytes, &mut out, at + 0x16)?;
            }
            b"pah1" => {
                host32(bytes, &mut out, at + 8)?;
                host16(bytes, &mut out, at + 0xC)?;
            }
            b"pai1" => animation(bytes, &mut out, at)?,
            _ => bail!("unknown block {} at {at:#x}", String::from_utf8_lossy(&kind)),
        }
        at += size;
    }
    Ok(out)
}

/// A layout material at `at`: its colours, resource bits and the sections they count.
fn material(src: &[u8], out: &mut [u8], at: usize) -> Result<()> {
    for k in 0..12 {
        host16(src, out, at + 0x14 + 2 * k)?; // three signed colours
    }
    let bits = host32(src, out, at + 0x3C)?;
    let field = |shift: u32, width: u32| ((bits >> shift) & ((1 << width) - 1)) as usize;
    let mut p = at + 0x40;
    for _ in 0..field(0, 4) {
        host16(src, out, p)?; // texture index (wrap and filter bytes stay)
        p += 4;
    }
    for _ in 0..field(4, 4) {
        host_floats(src, out, p, 5)?;
        p += 20;
    }
    p += 4 * field(8, 4); // texture coordinate generators (bytes)
    p += 4 * field(25, 1); // channel control
    p += 4 * field(27, 1); // material colour
    p += 4 * field(12, 1); // swap table
    for _ in 0..field(13, 2) {
        host_floats(src, out, p, 5)?;
        p += 20;
    }
    Ok(())
}

/// The animation block (pai1) at `at`: header, file names' offsets, contents, their infos, targets and keys.
fn animation(src: &[u8], out: &mut [u8], at: usize) -> Result<()> {
    host16(src, out, at + 8)?; // frames
    let files = host16(src, out, at + 0xC)? as usize;
    let contents = host16(src, out, at + 0xE)? as usize;
    let table = host32(src, out, at + 0x10)? as usize;
    for f in 0..files {
        host32(src, out, at + 0x14 + 4 * f)?;
    }
    for c in 0..contents {
        let content = at + host32(src, out, at + table + 4 * c)? as usize;
        let infos = src[content + 20] as usize;
        for i in 0..infos {
            let info = content + host32(src, out, content + 24 + 4 * i)? as usize;
            host32(src, out, info)?; // kind (four characters read as a number)
            let targets = src[info + 4] as usize;
            for t in 0..targets {
                let target = info + host32(src, out, info + 8 + 4 * t)? as usize;
                let curve = src[target + 2];
                let keys = host16(src, out, target + 4)? as usize;
                let first = target + host32(src, out, target + 8)? as usize;
                for k in 0..keys {
                    if curve == 2 {
                        host_floats(src, out, first + 12 * k, 3)?; // hermite: frame, value, slope
                    } else {
                        host32(src, out, first + 8 * k)?; // step: frame, value
                        host16(src, out, first + 8 * k + 4)?;
                    }
                }
            }
        }
    }
    Ok(())
}

/// A layout texture file (TPL) in host byte order.
pub fn tpl_host(bytes: &[u8]) -> Result<Vec<u8>> {
    const VERSION: u32 = 2_142_000;
    if bytes.len() < 12 {
        bail!("not a texture palette file");
    }
    if u32::from_le_bytes(bytes[0..4].try_into().unwrap()) == VERSION {
        return Ok(bytes.to_vec()); // cooked already
    }
    if be32(bytes, 0)? != VERSION {
        bail!("not a texture palette file");
    }
    let mut out = bytes.to_vec();
    host32(bytes, &mut out, 0)?;
    let count = host32(bytes, &mut out, 4)? as usize;
    let table = host32(bytes, &mut out, 8)? as usize;
    for i in 0..count {
        let image = host32(bytes, &mut out, table + 8 * i)? as usize;
        let palette = host32(bytes, &mut out, table + 8 * i + 4)? as usize;
        host16(bytes, &mut out, image)?; // height
        host16(bytes, &mut out, image + 2)?; // width
        host_floats(bytes, &mut out, image + 4, 7)?; // format, data offset, wrap s/t, min/mag filter, LOD bias
        if palette != 0 {
            host16(bytes, &mut out, palette)?; // entries (then two bytes)
            host32(bytes, &mut out, palette + 4)?; // format
            host32(bytes, &mut out, palette + 8)?; // data offset
        }
    }
    Ok(out)
}

/// A font (BRFNT, after cook::formats::font made its sheets records) in the game's native layout: the header and block
/// kinds as host numbers, each structure with 8-byte pointer slots holding the file offset of what it points to (the
/// game adds the file's address once, ResFont::Rebuild), numbers in host order, the sheets after the blocks, signature
/// "RFNN". Old (disc) layouts: FINF {fontType, linefeed, u16 alter, 3-byte widths, encoding, u32 glyph/width/map, height,
/// width, ascent, pad}, TGLP {4 bytes, u32 sheet size, 6 x u16, u32 image}, CWDH {u16 begin, end, u32 next, 3-byte
/// widths}, CMAP {u16 begin, end, method, reserved, u32 next, u16 map info}.
pub fn font_native(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() < 0x10 || &bytes[0..4] != b"RFNT" {
        bail!("not a font");
    }
    let header = be16(bytes, 0xC)? as usize;
    let blocks = be16(bytes, 0xE)? as usize;
    let mut out = vec![0u8; 0x10];
    out[0..4].copy_from_slice(&u32::from_be_bytes(*b"RFNN").to_le_bytes());
    out[4..6].copy_from_slice(&0xFEFFu16.to_le_bytes());
    out[6..8].copy_from_slice(&be16(bytes, 6)?.to_le_bytes());
    out[0xC..0xE].copy_from_slice(&0x10u16.to_le_bytes());
    out[0xE..0x10].copy_from_slice(&(blocks as u16).to_le_bytes());
    let mut moved: HashMap<usize, usize> = HashMap::new(); // a structure's disc offset -> its new offset
    let mut slots: Vec<(usize, usize)> = Vec::new(); // (new slot offset, disc target offset)
    let mut sheets: Option<(usize, usize, usize)> = None; // (new TGLP structure, disc image offset, bytes)
    let put16 = |v: &mut Vec<u8>, x: u16| v.extend_from_slice(&x.to_le_bytes());
    let put32 = |v: &mut Vec<u8>, x: u32| v.extend_from_slice(&x.to_le_bytes());
    let mut at = header;
    for _ in 0..blocks {
        let kind: [u8; 4] = bytes.get(at..at + 4).context("a block past the file's end")?.try_into().unwrap();
        let size = be32(bytes, at + 4)? as usize;
        if size < 8 || at + size > bytes.len() {
            bail!("block {} at {at:#x}: size {size:#x}", String::from_utf8_lossy(&kind));
        }
        let s = at + 8; // the disc structure
        let block = out.len();
        put32(&mut out, u32::from_be_bytes(kind));
        put32(&mut out, 0); // the size, below
        let n = out.len(); // the new structure
        moved.insert(s, n);
        match &kind {
            b"FINF" => {
                out.extend_from_slice(&bytes[s..s + 2]);
                put16(&mut out, be16(bytes, s + 2)?);
                out.extend_from_slice(&bytes[s + 4..s + 8]);
                for k in 0..3 {
                    let target = be32(bytes, s + 8 + 4 * k)? as usize;
                    slots.push((out.len(), target));
                    out.extend_from_slice(&[0u8; 8]);
                }
                out.extend_from_slice(&bytes[s + 0x14..s + 0x18]);
                out.resize(out.len() + 4, 0);
            }
            b"TGLP" => {
                out.extend_from_slice(&bytes[s..s + 4]);
                let sheet_size = be32(bytes, s + 4)?;
                put32(&mut out, sheet_size);
                for k in 0..6 {
                    put16(&mut out, be16(bytes, s + 8 + 2 * k)?);
                }
                out.resize(out.len() + 4, 0);
                let count = be16(bytes, s + 8)? as usize;
                sheets = Some((out.len(), be32(bytes, s + 0x14)? as usize, count * sheet_size as usize));
                out.extend_from_slice(&[0u8; 8]);
            }
            b"CWDH" => {
                let begin = be16(bytes, s)?;
                let end = be16(bytes, s + 2)?;
                put16(&mut out, begin);
                put16(&mut out, end);
                out.resize(out.len() + 4, 0);
                slots.push((out.len(), be32(bytes, s + 4)? as usize));
                out.extend_from_slice(&[0u8; 8]);
                let count = (end as usize + 1).saturating_sub(begin as usize);
                out.extend_from_slice(bytes.get(s + 8..s + 8 + 3 * count).context("font widths past the block")?);
            }
            b"CMAP" => {
                let begin = be16(bytes, s)?;
                let end = be16(bytes, s + 2)?;
                let method = be16(bytes, s + 4)?;
                put16(&mut out, begin);
                put16(&mut out, end);
                put16(&mut out, method);
                put16(&mut out, be16(bytes, s + 6)?);
                slots.push((out.len(), be32(bytes, s + 8)? as usize));
                out.extend_from_slice(&[0u8; 8]);
                let units = match method {
                    0 => 1,                                          // direct: the first index
                    1 => (end as usize + 1).saturating_sub(begin as usize), // table
                    2 => 1 + 2 * be16(bytes, s + 0xC)? as usize,     // scan: count, then code/index pairs
                    _ => bail!("code map method {method}"),
                };
                for u in 0..units {
                    put16(&mut out, be16(bytes, s + 0xC + 2 * u)?);
                }
            }
            b"GLGR" => out.extend_from_slice(&bytes[s..at + size]), // glyph groups: the game reads none
            _ => bail!("unknown font block {}", String::from_utf8_lossy(&kind)),
        }
        out.resize((out.len() + 31) & !31, 0);
        let new_size = (out.len() - block) as u32;
        out[block + 4..block + 8].copy_from_slice(&new_size.to_le_bytes());
        at += size;
    }
    // The sheets (records), after the blocks.
    if let Some((slot, image, length)) = sheets {
        let start = out.len();
        out.extend_from_slice(bytes.get(image..image + length).context("font sheets past the file's end")?);
        out.resize((out.len() + 31) & !31, 0);
        out[slot..slot + 8].copy_from_slice(&(start as u64).to_le_bytes());
    } else {
        bail!("a font without sheets");
    }
    for (slot, target) in slots {
        let new = if target == 0 {
            0
        } else {
            *moved.get(&target).with_context(|| format!("a font pointer to {target:#x}, not a structure"))?
        };
        out[slot..slot + 8].copy_from_slice(&(new as u64).to_le_bytes());
    }
    let size = out.len() as u32;
    out[8..12].copy_from_slice(&size.to_le_bytes());
    Ok(out)
}

/// What an offset field of a J3D animation block points at.
#[derive(Clone, Copy, PartialEq)]
enum AnmData {
    Halves,    // u16/s16 values or tables made only of them
    Floats,    // f32 values (and texture SRT centres)
    Bytes,     // u8 values, texture matrix indices
    Names,     // a name table (JUTNameTab): stays big-endian with the models' until the model step
    RegTable,  // TRK1 colour register keys: four key tables (12 halves), the register id, three bytes
    Pattern,   // TPT1 tables: max frame, offset, texture map byte, a byte, a half
    VtxIndex,  // VCK1/VCF1 index entries: a count (half), two bytes, a 32-bit index
}

/// One J3D animation block's layout: its 16-bit fields, 32-bit fields, and offsets with what they point at; the
/// entry count for the mixed tables is a 16-bit field of the block.
struct AnmBlock {
    halves: &'static [usize],
    words: &'static [usize],
    offsets: &'static [(usize, AnmData, usize)],
}

fn anm_block(kind: &[u8]) -> Option<AnmBlock> {
    use AnmData::*;
    Some(match kind {
        // Transform keys (BCK) and frames (BCA): frames, joints, scale/rotation/translation counts.
        b"ANK1" | b"ANF1" => AnmBlock {
            halves: &[0xA, 0xC, 0xE, 0x10, 0x12],
            words: &[],
            offsets: &[(0x14, Halves, 0), (0x18, Floats, 0), (0x1C, Halves, 0), (0x20, Floats, 0)],
        },
        // Texture SRT keys (BTK), then the post-texture-matrix set.
        b"TTK1" => AnmBlock {
            halves: &[0xA, 0xC, 0xE, 0x10, 0x12, 0x34, 0x36, 0x38, 0x3A],
            words: &[0x5C],
            offsets: &[
                (0x14, Halves, 0), (0x18, Halves, 0), (0x1C, Names, 0), (0x20, Bytes, 0), (0x24, Floats, 0),
                (0x28, Floats, 0), (0x2C, Halves, 0), (0x30, Floats, 0),
                (0x3C, Halves, 0), (0x40, Halves, 0), (0x44, Names, 0), (0x48, Bytes, 0), (0x4C, Floats, 0),
                (0x50, Floats, 0), (0x54, Halves, 0), (0x58, Floats, 0),
            ],
        },
        // Material colour keys (BPK): s16 values; frames (BPA): byte values.
        b"PAK1" | b"PAF1" => {
            let values = if kind == b"PAK1" { Halves } else { Bytes };
            AnmBlock {
                halves: &[0xC, 0xE, 0x10, 0x12, 0x14, 0x16],
                words: &[],
                offsets: if values == Halves {
                    &[(0x18, Halves, 0), (0x1C, Halves, 0), (0x20, Names, 0), (0x24, Halves, 0), (0x28, Halves, 0), (0x2C, Halves, 0), (0x30, Halves, 0)]
                } else {
                    &[(0x18, Halves, 0), (0x1C, Halves, 0), (0x20, Names, 0), (0x24, Bytes, 0), (0x28, Bytes, 0), (0x2C, Bytes, 0), (0x30, Bytes, 0)]
                },
            }
        }
        // TEV register keys (BRK): colour and constant register sets.
        b"TRK1" => AnmBlock {
            halves: &[0xA, 0xC, 0xE, 0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E],
            words: &[],
            offsets: &[
                (0x20, RegTable, 0xC), (0x24, RegTable, 0xE), (0x28, Halves, 0), (0x2C, Halves, 0), (0x30, Names, 0),
                (0x34, Names, 0), (0x38, Halves, 0), (0x3C, Halves, 0), (0x40, Halves, 0), (0x44, Halves, 0),
                (0x48, Halves, 0), (0x4C, Halves, 0), (0x50, Halves, 0), (0x54, Halves, 0),
            ],
        },
        // Texture patterns (BTP).
        b"TPT1" => AnmBlock {
            halves: &[0xA, 0xC, 0xE],
            words: &[],
            offsets: &[(0x10, Pattern, 0xC), (0x14, Halves, 0), (0x18, Halves, 0), (0x1C, Names, 0)],
        },
        // Shape visibility (BVA).
        b"VAF1" => AnmBlock { halves: &[0xA, 0xC, 0xE], words: &[], offsets: &[(0x10, Halves, 0), (0x14, Bytes, 0)] },
        // Cluster weights, keys (BLK) and frames (BLA).
        b"CLK1" | b"CLF1" => AnmBlock { halves: &[0xA], words: &[0xC], offsets: &[(0x10, Halves, 0), (0x14, Floats, 0)] },
        // Vertex colours, keys (BXK, s16 values) and frames (BXA, byte values).
        b"VCK1" | b"VCF1" => {
            let values = if kind == b"VCK1" { Halves } else { Bytes };
            AnmBlock {
                halves: &[0xA, 0xC, 0xE],
                words: &[],
                offsets: if values == Halves {
                    &[(0x18, Halves, 0), (0x1C, Halves, 0), (0x20, VtxIndex, 0xC), (0x24, VtxIndex, 0xE), (0x28, Halves, 0), (0x2C, Halves, 0),
                      (0x30, Halves, 0), (0x34, Halves, 0), (0x38, Halves, 0), (0x3C, Halves, 0)]
                } else {
                    &[(0x18, Halves, 0), (0x1C, Halves, 0), (0x20, VtxIndex, 0xC), (0x24, VtxIndex, 0xE), (0x28, Halves, 0), (0x2C, Halves, 0),
                      (0x30, Bytes, 0), (0x34, Bytes, 0), (0x38, Bytes, 0), (0x3C, Bytes, 0)]
                },
            }
        }
        _ => return None,
    })
}

/// The J3D animation types (file header bytes 4-8) the game loads (J3DAnmLoaderDataBase::load).
const ANM_TYPES: [&[u8; 4]; 12] =
    [b"bck1", b"bpk1", b"blk1", b"btk1", b"brk1", b"bxk1", b"bca1", b"bpa1", b"btp1", b"bva1", b"bla1", b"bxa1"];

/// Whether these bytes are a J3D animation as the disc has it.
pub fn is_j3d_animation(bytes: &[u8]) -> bool {
    bytes.len() >= 0x20 && &bytes[0..4] == b"J3D1" && ANM_TYPES.iter().any(|t| &bytes[4..8] == *t)
}

/// A J3D animation (BCK, BCA, BTK, BRK, BPK, BPA, BTP, BVA, BLK, BLA, BXK, BXA) in host byte order: the file header's
/// magic, type, size, block count and sound offset; each block's kind and size, its counts and frames, its offsets;
/// what each offset points at by its type: 16-bit tables and values, floats (values, texture SRT centres), and the
/// mixed tables entry by entry (TRK1 register keys, TPT1 patterns, vertex-colour index entries). Bytes stay; the
/// name tables stay big-endian with the models'. The magic, type and block kinds become host-order numbers (a
/// cooked file starts "1D3J").
pub fn j3d_animation(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() >= 8 && &bytes[0..4] == b"1D3J" {
        return Ok(bytes.to_vec()); // cooked already
    }
    if !is_j3d_animation(bytes) {
        bail!("not a J3D animation");
    }
    let mut out = bytes.to_vec();
    out[0..4].copy_from_slice(&u32::from_be_bytes(*b"J3D1").to_le_bytes());
    out[4..8].copy_from_slice(&be32(bytes, 4)?.to_le_bytes());
    host32(bytes, &mut out, 8)?; // file size
    let blocks = host32(bytes, &mut out, 0xC)? as usize;
    host32(bytes, &mut out, 0x1C)?; // sound animation offset
    let mut at = 0x20;
    for b in 0..blocks {
        let kind: [u8; 4] = bytes.get(at..at + 4).context("an animation block past the file's end")?.try_into().unwrap();
        let mut size = be32(bytes, at + 4)? as usize;
        if size < 8 {
            bail!("animation block {} at {at:#x}: size {size:#x}", String::from_utf8_lossy(&kind));
        }
        if at + size > bytes.len() {
            if b + 1 == blocks {
                size = bytes.len() - at; // the last block's size may count padding the file does not have
            } else {
                bail!("animation block {} at {at:#x} runs past the file", String::from_utf8_lossy(&kind));
            }
        }
        out[at..at + 4].copy_from_slice(&u32::from_be_bytes(kind).to_le_bytes());
        host32(bytes, &mut out, at + 4)?;
        let spec = anm_block(&kind).with_context(|| format!("unknown animation block {}", String::from_utf8_lossy(&kind)))?;
        let end = at + size;
        let header_end = spec.offsets.iter().map(|o| o.0 + 4).chain(spec.words.iter().map(|w| w + 4)).max().unwrap_or(8);
        if at + header_end > end {
            bail!("animation block {} at {at:#x}: header past the block", String::from_utf8_lossy(&kind));
        }
        for &h in spec.halves {
            host16(bytes, &mut out, at + h)?;
        }
        for &w in spec.words {
            host32(bytes, &mut out, at + w)?;
        }
        // Each array runs to the next offset in the block (or the block's end).
        let mut starts: Vec<usize> = Vec::new();
        for &(o, _, _) in spec.offsets {
            let v = host32(bytes, &mut out, at + o)? as usize;
            if v != 0 {
                if v < header_end || v > size {
                    bail!("animation block {} at {at:#x}: offset {v:#x} at {o:#x} outside the block", String::from_utf8_lossy(&kind));
                }
                starts.push(v);
            }
        }
        starts.sort_unstable();
        starts.dedup();
        let mut done: HashSet<usize> = HashSet::new();
        for &(o, data, count_at) in spec.offsets {
            let start = be32(bytes, at + o)? as usize;
            if start == 0 || !done.insert(start) {
                continue;
            }
            let bound = starts.iter().copied().find(|&s| s > start).unwrap_or(size);
            let (from, to) = (at + start, at + bound);
            match data {
                AnmData::Halves => {
                    let mut p = from;
                    while p + 2 <= to {
                        host16(bytes, &mut out, p)?;
                        p += 2;
                    }
                }
                AnmData::Floats => {
                    let mut p = from;
                    while p + 4 <= to {
                        host32(bytes, &mut out, p)?;
                        p += 4;
                    }
                }
                AnmData::Bytes | AnmData::Names => {}
                AnmData::RegTable | AnmData::Pattern | AnmData::VtxIndex => {
                    let count = be16(bytes, at + count_at)? as usize;
                    let stride = match data {
                        AnmData::RegTable => 0x1C,
                        _ => 8,
                    };
                    if from + count * stride > to {
                        bail!("animation block {} at {at:#x}: {count} table entries past {bound:#x}", String::from_utf8_lossy(&kind));
                    }
                    for e in 0..count {
                        let p = from + e * stride;
                        match data {
                            AnmData::RegTable => {
                                for k in 0..12 {
                                    host16(bytes, &mut out, p + 2 * k)?;
                                }
                            }
                            AnmData::Pattern => {
                                host16(bytes, &mut out, p)?;
                                host16(bytes, &mut out, p + 2)?;
                                host16(bytes, &mut out, p + 6)?;
                            }
                            _ => {
                                host16(bytes, &mut out, p)?;
                                host32(bytes, &mut out, p + 4)?;
                            }
                        }
                    }
                }
            }
        }
        at += size;
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A BCK of one ANK1 block: the header, counts, table and arrays turn host order; the magic reads "1D3J".
    #[test]
    fn j3d_animation_turns_a_bck_host_order() {
        let mut f = b"J3D1bck1".to_vec();
        f.extend_from_slice(&0x60u32.to_be_bytes()); // file size
        f.extend_from_slice(&1u32.to_be_bytes()); // blocks
        f.extend_from_slice(b"SVR1");
        f.extend_from_slice(&[0xFF; 8]);
        f.extend_from_slice(&u32::MAX.to_be_bytes()); // no sound animation
        let block = f.len();
        f.extend_from_slice(b"ANK1");
        f.extend_from_slice(&0x40u32.to_be_bytes());
        f.extend_from_slice(&[1, 2]); // loop, rotation shift
        for v in [30u16, 1, 1, 1, 1] {
            f.extend_from_slice(&v.to_be_bytes()); // frames, joints, scale/rotation/translation counts
        }
        for v in [0x24u32, 0x38, 0x3C, 0x3E] {
            f.extend_from_slice(&v.to_be_bytes()); // table, scale, rotation, translation
        }
        for v in [7u16; 9] {
            f.extend_from_slice(&v.to_be_bytes()); // the first nine halves of the joint's key tables
        }
        f.resize(block + 0x38, 0);
        f.extend_from_slice(&1.5f32.to_be_bytes());
        f.extend_from_slice(&(-3i16).to_be_bytes());
        f.extend_from_slice(&[0x40, 0x00]); // translation: the block ends inside it (two bytes only)
        let out = j3d_animation(&f).unwrap();
        assert_eq!(u32::from_le_bytes(out[0..4].try_into().unwrap()), u32::from_be_bytes(*b"J3D1"));
        assert_eq!(u32::from_le_bytes(out[4..8].try_into().unwrap()), u32::from_be_bytes(*b"bck1"));
        assert_eq!(u32::from_le_bytes(out[block..block + 4].try_into().unwrap()), u32::from_be_bytes(*b"ANK1"));
        assert_eq!(u16::from_le_bytes(out[block + 0xA..block + 0xC].try_into().unwrap()), 30);
        assert_eq!(u32::from_le_bytes(out[block + 0x18..block + 0x1C].try_into().unwrap()), 0x38);
        assert_eq!(u16::from_le_bytes(out[block + 0x24..block + 0x26].try_into().unwrap()), 7);
        assert_eq!(f32::from_le_bytes(out[block + 0x38..block + 0x3C].try_into().unwrap()), 1.5);
        assert_eq!(i16::from_le_bytes(out[block + 0x3C..block + 0x3E].try_into().unwrap()), -3);
        assert_eq!(j3d_animation(&out).unwrap(), out); // cooked already
    }
}
