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
//!   keys, TDB1 texture indices) and the TEX1 blocks' sizes and image headers (ResTIMG, host_timg). Block
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
//!   16-bit and float arrays by block type, the name tables (j3d_animation).
//! - J3D models and material tables (BMD, BDL, BMT; j3d_model): headers, block headers, INF1, JNT1, EVP1, DRW1 and
//!   the name tables, VTX1 (format list, arrays by their component type), SHP1 (shape data, tables, vertex
//!   descriptors, display lists' counts and 16-bit indices), MAT3 (every table by its record layout) and MDL3's
//!   tables (its command lists stay: the game does not read them), TEX1's image headers.
//! - BTI (texture files, after cook::formats::bti): the image header (host_timg).
//! - BAS (sound animations): counts, ids, times, pitches, flags (bas).
//! - BRS/BME/BMT (rhythm library remix and ME files): words, tables, parameters (rhythm).
//! - BCT/CSW (Wii Remote speaker table and waves): headers, parameters, samples (speaker).
//! - CIT (chord tables, "CITS"): size, counts, offset tables, scale note offsets (cit).
//! - RFL_Res.dat (Mii parts, after cook::formats::rfl_res): header, section tables, part texture headers, shapes'
//!   counts and values (rfl_res_host).
//! - The audio archive (BAA): the command stream, BST, BSTN, BSC, the wave systems (WSYS) and the banks (IBNK)
//!   (baa).

use anyhow::{bail, Context, Result};
use std::collections::{HashMap, HashSet};

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(bytes.get(at..at + 4).context("a field past the file's end")?.try_into().unwrap()))
}

fn be16(bytes: &[u8], at: usize) -> Result<u16> {
    Ok(u16::from_be_bytes(bytes.get(at..at + 2).context("a field past the file's end")?.try_into().unwrap()))
}

fn byte(bytes: &[u8], at: usize) -> Result<u8> {
    bytes.get(at).copied().context("a field past the file's end")
}

fn part(bytes: &[u8], from: usize, to: usize) -> Result<&[u8]> {
    bytes.get(from..to).context("a field past the file's end")
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
        let side = |mask_at: usize| -> Result<usize> { Ok((!be32(bytes, mask_at)?).checked_shr(shift).unwrap_or(0) as usize + 1) };
        side(0x20)?.saturating_mul(side(0x24)?).saturating_mul(side(0x28)?)
    };
    let mut walk = Octree { source: bytes, base: octree, done: HashSet::new(), walked: HashSet::new() };
    walk.block(&mut out, 0, roots, 0)?;
    Ok(out)
}

/// The octree's walk (as KCollision.cpp's KCLOctreeSwap): a word >= 0 is the offset of 8 child words from its block,
/// a negative word (low 31 bits) the offset from its block of a u16 list read from its second unit to a 0.
struct Octree<'a> {
    source: &'a [u8],
    base: usize,
    done: HashSet<usize>,
    /// Each block and list once (block or list, offset): words that point back would fan out 8 times a level.
    walked: HashSet<(bool, usize)>,
}

impl Octree<'_> {
    fn block(&mut self, out: &mut [u8], offs: usize, count: usize, depth: u32) -> Result<()> {
        if depth > 64 {
            bail!("octree deeper than 64 levels at {offs:#x}");
        }
        if !self.walked.insert((true, offs)) {
            return Ok(());
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
        if !self.walked.insert((false, offs)) {
            return Ok(());
        }
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
                    let keys = byte(bytes, at + 9)? as usize;
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
        host_timg(bytes, &mut out, at + 0x20)?; // after the block's kind, size and name
        at += size;
    }
    out[3] = b'c';
    Ok(out)
}

/// An image header (ResTIMG) at `at` in host order: width, height, LOD bias, image offset (the bytes between stay
/// bytes). Palettes are applied, so their eight bytes are cleared: they are the game's image pointer, null in a file.
fn host_timg(src: &[u8], out: &mut [u8], at: usize) -> Result<()> {
    host16(src, out, at + 2)?;
    host16(src, out, at + 4)?;
    out.get_mut(at + 8..at + 0x10).context("an image header past the file's end")?.fill(0);
    host16(src, out, at + 0x1A)?;
    host32(src, out, at + 0x1C)?;
    Ok(())
}

/// A texture file (BTI, after cook::formats::bti) in host order: its image header.
pub fn bti_host(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() < 0x20 {
        bail!("a texture file shorter than its header");
    }
    let mut out = bytes.to_vec();
    host_timg(bytes, &mut out, 0)?;
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
        let tag_end = (p - 2 + byte(src, p)? as usize).min(end);
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
                host_floats(bytes, &mut out, at + 0x60, 8 * byte(bytes, at + 0x5E)? as usize)?; // texture coordinates
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
                let frames = byte(bytes, at + 0x5C)? as usize;
                let content = host32(bytes, &mut out, at + 0x60)? as usize;
                let table = host32(bytes, &mut out, at + 0x64)? as usize;
                let c = at + content;
                host_floats(bytes, &mut out, c, 4)?; // vertex colours
                host16(bytes, &mut out, c + 0x10)?; // material
                host_floats(bytes, &mut out, c + 0x14, 8 * byte(bytes, c + 0x12)? as usize)?;
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
        let infos = byte(src, content + 20)? as usize;
        for i in 0..infos {
            let info = content + host32(src, out, content + 24 + 4 * i)? as usize;
            host32(src, out, info)?; // kind (four characters read as a number)
            let targets = byte(src, info + 4)? as usize;
            for t in 0..targets {
                let target = info + host32(src, out, info + 8 + 4 * t)? as usize;
                let curve = byte(src, target + 2)?;
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
                out.extend_from_slice(part(bytes, s, s + 2)?);
                put16(&mut out, be16(bytes, s + 2)?);
                out.extend_from_slice(part(bytes, s + 4, s + 8)?);
                for k in 0..3 {
                    let target = be32(bytes, s + 8 + 4 * k)? as usize;
                    slots.push((out.len(), target));
                    out.extend_from_slice(&[0u8; 8]);
                }
                out.extend_from_slice(part(bytes, s + 0x14, s + 0x18)?);
                out.resize(out.len() + 4, 0);
            }
            b"TGLP" => {
                out.extend_from_slice(part(bytes, s, s + 4)?);
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
    Names,     // a name table (JUTNameTab)
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
        // Register colour keys (BRK): colour and constant register sets.
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
/// mixed tables entry by entry (TRK1 register keys, TPT1 patterns, vertex-colour index entries), the name tables.
/// Bytes stay. The magic, type and block kinds become host-order numbers (a cooked file starts "1D3J").
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
                AnmData::Bytes => {}
                AnmData::Names => host_name_table(bytes, &mut out, from)?,
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

/// A name table (JUTNameTab: a count, two bytes, then each name's hash and string offset) in host order.
fn host_name_table(src: &[u8], out: &mut [u8], at: usize) -> Result<()> {
    let count = host16(src, out, at)? as usize;
    host16(src, out, at + 2)?;
    for k in 0..count {
        host16(src, out, at + 4 + 4 * k)?;
        host16(src, out, at + 6 + 4 * k)?;
    }
    Ok(())
}

/// The model file types (header bytes 4-8) the game loads (J3DModelLoaderDataBase).
const MODEL_TYPES: [&[u8; 4]; 3] = [b"bmd3", b"bdl4", b"bmt3"];

/// Whether these bytes are a J3D model or material table as the disc has it.
pub fn is_j3d_model(bytes: &[u8]) -> bool {
    bytes.len() >= 0x20 && &bytes[0..4] == b"J3D2" && MODEL_TYPES.iter().any(|t| &bytes[4..8] == *t)
}

/// A J3D model (BMD, BDL) or material table (BMT), after cook::formats::j3d_model, with these parts in host order:
/// the file header (magic and type become host-order character constants: a cooked file starts "2D3J"), every
/// block's kind, size, count and offsets; INF1's flags, counts and hierarchy; JNT1's joint data (kind, transform,
/// radius, bounds) and index table; EVP1's indices, weights and inverse matrices; DRW1's indices; every name table;
/// VTX1's format list and arrays (16-bit and float components by the list's type; 8-bit ones and RGB8/RGBX8/RGBA6/
/// RGBA8 colours stay bytes); SHP1's shape data, index and matrix tables, vertex descriptors, matrix and draw
/// entries, and in its display lists each primitive's count and 16-bit indices (the layout as J3DNativeShape reads
/// it); MAT3's tables by record (material entries' indices, IDs, cull modes, lights, texture matrices, texture
/// numbers, register colours, fog, NBT scales, indirect matrices; byte records stay); MDL3's list entries, patching
/// offsets and matrix registers (its command lists stay bytes, unread); TEX1's image headers (host_timg).
pub fn j3d_model(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() >= 8 && &bytes[0..4] == b"2D3J" {
        return Ok(bytes.to_vec()); // cooked already
    }
    if !is_j3d_model(bytes) {
        bail!("not a J3D model");
    }
    let mut out = bytes.to_vec();
    out[0..4].copy_from_slice(&u32::from_be_bytes(*b"J3D2").to_le_bytes());
    out[4..8].copy_from_slice(&be32(bytes, 4)?.to_le_bytes());
    host32(bytes, &mut out, 8)?; // file size
    let blocks = host32(bytes, &mut out, 0xC)? as usize;
    // VTX1's normal format count (three unless listed; an NBT entry counts as the normal's), for SHP1's layouts.
    let mut normal_count = 0u32;
    let mut at = 0x20;
    for b in 0..blocks {
        let kind: [u8; 4] = bytes.get(at..at + 4).context("a model block past the file's end")?.try_into().unwrap();
        let mut size = be32(bytes, at + 4)? as usize;
        if size < 0xC {
            bail!("model block {} at {at:#x}: size {size:#x}", String::from_utf8_lossy(&kind));
        }
        if at + size > bytes.len() {
            if b + 1 == blocks {
                size = bytes.len() - at;
            } else {
                bail!("model block {} at {at:#x} runs past the file", String::from_utf8_lossy(&kind));
            }
        }
        out[at..at + 4].copy_from_slice(&u32::from_be_bytes(kind).to_le_bytes());
        host32(bytes, &mut out, at + 4)?;
        // (offset fields' range, the name table's field)
        let (offsets, names): (std::ops::Range<usize>, Option<usize>) = match &kind {
            b"INF1" => (0x14..0x18, None),
            b"VTX1" => (0x8..0x40, None),
            b"EVP1" => (0xC..0x1C, None),
            b"DRW1" => (0xC..0x14, None),
            b"JNT1" => (0xC..0x18, Some(0x14)),
            b"MAT3" => (0xC..0x84, Some(0x14)),
            b"MDL3" => (0xC..0x24, Some(0x20)),
            b"SHP1" => (0xC..0x2C, Some(0x14)),
            b"TEX1" => (0xC..0x14, Some(0x10)),
            _ => bail!("model block {} at {at:#x}: not one the game reads", String::from_utf8_lossy(&kind)),
        };
        if at + offsets.end > at + size {
            bail!("model block {} at {at:#x}: header past the block", String::from_utf8_lossy(&kind));
        }
        if &kind != b"VTX1" {
            host16(bytes, &mut out, at + 8)?; // the count (INF1: flags)
        }
        let mut starts: Vec<usize> = Vec::new();
        for o in offsets.clone().step_by(4) {
            let v = host32(bytes, &mut out, at + o)? as usize;
            if v != 0 {
                if v > size {
                    bail!("model block {} at {at:#x}: offset {v:#x} past the block", String::from_utf8_lossy(&kind));
                }
                starts.push(v);
            }
        }
        starts.sort_unstable();
        starts.dedup();
        let field = |o: usize| -> Result<usize> { Ok(be32(bytes, at + o)? as usize) };
        let bound = |start: usize| starts.iter().copied().find(|&s| s > start).unwrap_or(size);
        if let Some(n) = names {
            let t = field(n)?;
            // An empty block's table may sit at the block's end (TEX1 without textures): nothing to turn.
            if t != 0 && t + 4 <= size {
                host_name_table(bytes, &mut out, at + t)?;
            }
        }
        let halves = |out: &mut Vec<u8>, from: usize, to: usize| -> Result<()> {
            let mut p = from;
            while p + 2 <= to {
                host16(bytes, out, p)?;
                p += 2;
            }
            Ok(())
        };
        let words = |out: &mut Vec<u8>, from: usize, to: usize| -> Result<()> {
            let mut p = from;
            while p + 4 <= to {
                host32(bytes, out, p)?;
                p += 4;
            }
            Ok(())
        };
        match &kind {
            b"INF1" => {
                host32(bytes, &mut out, at + 0xC)?; // packets
                host32(bytes, &mut out, at + 0x10)?; // vertices
                let h = field(0x14)?;
                if h != 0 {
                    halves(&mut out, at + h, at + size)?; // (type, value) pairs to the end
                }
            }
            b"EVP1" => {
                let (index, weight, inverse) = (field(0x10)?, field(0x14)?, field(0x18)?);
                if index != 0 {
                    halves(&mut out, at + index, at + bound(index))?;
                }
                if weight != 0 {
                    words(&mut out, at + weight, at + bound(weight))?;
                }
                if inverse != 0 {
                    words(&mut out, at + inverse, at + bound(inverse))?;
                }
            }
            b"VTX1" => {
                // The format list: (attribute, count, type: 32-bit; fraction byte, three pad) to the end marker.
                let list = field(0x8)?;
                let mut formats: Vec<(u32, u32)> = Vec::new(); // (attribute, type)
                if list != 0 {
                    let mut e = at + list;
                    loop {
                        if e + 16 > at + size {
                            bail!("model block VTX1 at {at:#x}: format list past the block");
                        }
                        let attr = host32(bytes, &mut out, e)?;
                        host32(bytes, &mut out, e + 4)?;
                        let kind = host32(bytes, &mut out, e + 8)?;
                        if attr == 0xFF {
                            break;
                        }
                        if attr == 10 || attr == 25 {
                            normal_count = be32(bytes, e + 4)?;
                        }
                        formats.push((attr, kind));
                        e += 16;
                    }
                }
                let type_of = |attr: u32, default: u32| formats.iter().find(|f| f.0 == attr).map_or(default, |f| f.1);
                // (offset field, attribute): position, normal, NBT, colour 0/1, texture coordinates 0-7.
                let mut arrays: Vec<(usize, u32)> = vec![(0xC, 9), (0x10, 10), (0x14, 25), (0x18, 11), (0x1C, 12)];
                arrays.extend((0..8).map(|t| (0x20 + 4 * t, 13 + t as u32)));
                let mut done: HashSet<usize> = HashSet::new();
                for (o, attr) in arrays {
                    let start = field(o)?;
                    if start == 0 || !done.insert(start) {
                        continue; // none, or an array another attribute shares (turned once)
                    }
                    let (from, to) = (at + start, at + bound(start));
                    let width = if attr == 11 || attr == 12 {
                        // The two 16-bit formats (565 and 4444) are 16-bit; the others are bytes.
                        match type_of(attr, 5) {
                            0 | 3 => 2,
                            _ => 1,
                        }
                    } else {
                        let kind = if attr == 25 { type_of(25, type_of(10, 4)) } else { type_of(attr, 4) };
                        match kind {
                            0 | 1 => 1, // U8, S8
                            2 | 3 => 2, // U16, S16
                            _ => 4,     // F32
                        }
                    };
                    match width {
                        2 => halves(&mut out, from, to)?,
                        4 => words(&mut out, from, to)?,
                        _ => {}
                    }
                }
            }
            b"SHP1" => {
                let (init, index, descs, table, lists, mtx, draw) =
                    (field(0xC)?, field(0x10)?, field(0x18)?, field(0x1C)?, field(0x20)?, field(0x24)?, field(0x28)?);
                for t in [index, table] {
                    if t != 0 {
                        halves(&mut out, at + t, at + bound(t))?;
                    }
                }
                for t in [descs, draw] {
                    if t != 0 {
                        words(&mut out, at + t, at + bound(t))?; // (attribute, type) pairs; (size, offset) pairs
                    }
                }
                if mtx != 0 {
                    // (matrix index, count: 16-bit; first table entry: 32-bit)
                    let mut m = at + mtx;
                    while m + 8 <= at + bound(mtx) {
                        host16(bytes, &mut out, m)?;
                        host16(bytes, &mut out, m + 2)?;
                        host32(bytes, &mut out, m + 4)?;
                        m += 8;
                    }
                }
                if init != 0 {
                    let mut done: HashSet<usize> = HashSet::new();
                    let mut e = at + init;
                    // 0x28 bytes each: matrix type, pad, group count, descriptor/matrix/draw indices (16-bit), pad,
                    // radius, minimum, maximum.
                    while e + 0x28 <= at + bound(init) {
                        halves(&mut out, e + 2, e + 0xA)?;
                        words(&mut out, e + 0xC, e + 0x28)?;
                        let groups = be16(bytes, e + 2)? as usize;
                        let desc = be16(bytes, e + 4)? as usize;
                        let first_draw = be16(bytes, e + 8)? as usize;
                        e += 0x28;
                        if descs == 0 || draw == 0 || lists == 0 {
                            continue;
                        }
                        // The vertex layout: attributes in the file's order, NBT as the normal; matrix indices one byte,
                        // indices one or two (three normal indices for an NBT3 shape).
                        let mut kinds = [0u32; 21];
                        let mut nbt = normal_count != 0;
                        let mut d = at + descs + desc;
                        loop {
                            let attr = be32(bytes, d)?;
                            if attr == 0xFF {
                                break;
                            }
                            let kind = be32(bytes, d + 4)?;
                            let slot = if attr == 25 { 10 } else { attr };
                            nbt |= attr == 25;
                            if slot <= 20 && kind != 0 {
                                if slot >= 9 && kind == 1 {
                                    bail!("model block SHP1 at {at:#x}: a shape with direct vertex values");
                                }
                                kinds[slot as usize] = kind;
                            }
                            d += 8;
                        }
                        let mut stride = 0usize;
                        let mut wide: Vec<usize> = Vec::new(); // offsets of 16-bit indices in a vertex
                        for (attr, &kind) in kinds.iter().enumerate() {
                            if kind == 0 {
                                continue;
                            }
                            let one = if attr < 9 || kind == 2 { 1 } else { 2 };
                            let n = if attr == 10 && nbt && normal_count == 2 { 3 } else { 1 };
                            if one == 2 {
                                wide.extend((0..n).map(|k| stride + 2 * k));
                            }
                            stride += one * n;
                        }
                        for g in 0..groups {
                            let entry = at + draw + 8 * (first_draw + g);
                            let length = be32(bytes, entry)? as usize;
                            let offset = be32(bytes, entry + 4)? as usize;
                            let from = at + lists + offset;
                            if !done.insert(from) {
                                continue;
                            }
                            let to = from + length;
                            if to > at + bound(lists) {
                                bail!("model block SHP1 at {at:#x}: a display list past the block");
                            }
                            let mut p = from;
                            while p < to {
                                let op = bytes[p];
                                p += 1;
                                if op == 0 {
                                    continue;
                                }
                                if op & 0x80 == 0 {
                                    bail!("model block SHP1 at {at:#x}: display list command {op:#04x} at {:#x}", p - 1 - at);
                                }
                                let count = host16(bytes, &mut out, p)? as usize;
                                p += 2;
                                if p + count * stride > to {
                                    bail!("model block SHP1 at {at:#x}: {count} vertices past the display list");
                                }
                                for v in 0..count {
                                    for &w in &wide {
                                        host16(bytes, &mut out, p + v * stride + w)?;
                                    }
                                }
                                p += count * stride;
                            }
                        }
                    }
                }
            }
            b"MAT3" => {
                // (offset field, record size, the record's parts: (from, to, width) with width 2 or 4)
                const TEX_MTX: &[(usize, usize, usize)] = &[(0x4, 0x18, 4), (0x18, 0x1A, 2), (0x1C, 0x64, 4)];
                let tables: [(usize, usize, &[(usize, usize, usize)]); 11] = [
                    (0xC, 0x14C, &[(0x8, 0x9C, 2), (0xBC, 0x14C, 2)]), // material entries: table indices
                    (0x10, 2, &[(0, 2, 2)]),                           // material IDs
                    (0x18, 0x138, &[(0x14, 0x2C, 4), (0x30, 0x48, 4), (0x4C, 0x64, 4)]), // indirect: matrices
                    (0x1C, 4, &[(0, 4, 4)]),                           // cull modes
                    (0x30, 0x34, &[(0, 0x18, 4), (0x1C, 0x34, 4)]),    // lights (the colour stays bytes)
                    (0x40, 0x64, TEX_MTX),                             // texture matrices
                    (0x44, 0x64, TEX_MTX),                             // post-transform texture matrices
                    (0x48, 2, &[(0, 2, 2)]),                           // texture numbers
                    (0x50, 8, &[(0, 8, 2)]),                           // register colours (s16 x4)
                    (0x68, 0x2C, &[(0x2, 0x4, 2), (0x4, 0x14, 4), (0x18, 0x2C, 2)]), // fog
                    (0x80, 0x10, &[(0x4, 0x10, 4)]),                   // NBT scales
                ];
                for (o, record, parts) in tables {
                    let start = field(o)?;
                    if start == 0 {
                        continue;
                    }
                    let end = at + bound(start);
                    let mut r = at + start;
                    while r + record <= end {
                        for &(from, to, width) in parts {
                            if width == 2 {
                                halves(&mut out, r + from, r + to)?;
                            } else {
                                words(&mut out, r + from, r + to)?;
                            }
                        }
                        r += record;
                    }
                }
            }
            b"MDL3" => {
                // One entry per material each (the command lists between them stay bytes).
                let materials = be16(bytes, at + 8)? as usize;
                let (lists, patching, registers) = (field(0xC)?, field(0x10)?, field(0x14)?);
                if lists != 0 {
                    words(&mut out, at + lists, at + lists + 8 * materials)?; // (offset, size)
                }
                if registers != 0 {
                    words(&mut out, at + registers, at + registers + 8 * materials)?; // two matrix index registers
                }
                if patching != 0 {
                    for m in 0..materials {
                        let r = at + patching + 0x10 * m;
                        halves(&mut out, r, r + 0xC)?; // six 16-bit offsets, four bytes
                    }
                }
            }
            b"TEX1" => {
                let headers = field(0xC)?;
                let count = be16(bytes, at + 8)? as usize;
                if headers != 0 {
                    for i in 0..count {
                        host_timg(bytes, &mut out, at + headers + 0x20 * i)?;
                    }
                }
            }
            b"DRW1" => {
                let index = field(0x10)?;
                if index != 0 {
                    halves(&mut out, at + index, at + bound(index))?;
                }
            }
            b"JNT1" => {
                let (init, index) = (field(0xC)?, field(0x10)?);
                if index != 0 {
                    halves(&mut out, at + index, at + bound(index))?;
                }
                if init != 0 {
                    // 0x40 bytes each: kind, compensation byte, pad; scale, rotation (s16 x3, pad), translation; radius;
                    // minimum and maximum.
                    let mut j = at + init;
                    while j + 0x40 <= at + bound(init) {
                        host16(bytes, &mut out, j)?;
                        words(&mut out, j + 4, j + 0x10)?;
                        halves(&mut out, j + 0x10, j + 0x18)?;
                        words(&mut out, j + 0x18, j + 0x40)?;
                        j += 0x40;
                    }
                }
            }
            _ => {}
        }
        at += size;
    }
    Ok(out)
}

/// RFL_Res.dat (the Mii parts, after cook::formats::rfl_res) in host order: the header (two 16-bit fields, one
/// offset per section), each section's file count, 16-bit field and file offsets, each part texture's header
/// (host_timg: Mii_Texture has ResTIMG's layout) and each shape's numbers (a face line's nine 32-bit values; then
/// positions, normals and, but for the forehead, hair, beard and nose, texture coordinates: a count and its 16-bit
/// values). Texels, palettes and the shapes' primitive bytes stay bytes. The rules are the ones the Mii library
/// applied at load before .
pub fn rfl_res_host(bytes: &[u8]) -> Result<Vec<u8>> {
    const SECTIONS: usize = 18; // Mii_ArcID_Max
    const TEXTURES: [usize; 9] = [1, 2, 4, 7, 10, 11, 12, 15, 17]; // Eye, Eyebrow, FaceTex, GlassTex, Mole, Mouth, Mustache, NlineTex, CapTex
    const FACELINE: usize = 3;
    const NO_COORDINATES: [usize; 4] = [5, 8, 0, 13]; // ForeHead, Hair, Beard, Nose
    if bytes.len() < (SECTIONS + 1) * 4 {
        bail!("RFL_Res.dat shorter than its header");
    }
    let mut out = bytes.to_vec();
    host16(bytes, &mut out, 0)?;
    host16(bytes, &mut out, 2)?;
    for section in 0..SECTIONS {
        let at = host32(bytes, &mut out, (section + 1) * 4)? as usize;
        let count = host16(bytes, &mut out, at)? as usize;
        host16(bytes, &mut out, at + 2)?;
        let table = at + 4;
        let data = table + (count + 1) * 4;
        if data > bytes.len() {
            bail!("RFL_Res.dat section {section} past the file's end");
        }
        for i in 0..=count {
            host32(bytes, &mut out, table + 4 * i)?;
        }
        for i in 0..count {
            let start = data + be32(bytes, table + 4 * i)? as usize;
            let end = data + be32(bytes, table + 4 * i + 4)? as usize;
            if end < start || end > bytes.len() {
                bail!("RFL_Res.dat section {section} file {i} outside the file");
            }
            if end == start {
                continue;
            }
            if TEXTURES.contains(&section) {
                if end - start < 0x20 {
                    bail!("RFL_Res.dat section {section} file {i}: a texture shorter than its header");
                }
                host_timg(bytes, &mut out, start)?;
                continue;
            }
            let mut p = start + 4; // its tag
            if section == FACELINE {
                if p + 36 > end {
                    bail!("RFL_Res.dat face line {i} shorter than its values");
                }
                for _ in 0..9 {
                    host32(bytes, &mut out, p)?;
                    p += 4;
                }
            }
            let parts = if NO_COORDINATES.contains(&section) { 2 } else { 3 };
            for part in 0..parts {
                let per = if part == 2 { 2 } else { 3 };
                if p + 2 > end {
                    bail!("RFL_Res.dat section {section} file {i}: a count past the file");
                }
                let n = host16(bytes, &mut out, p)? as usize;
                p += 2;
                if part == 0 && n == 0 {
                    break;
                }
                if p + n * per * 2 > end {
                    bail!("RFL_Res.dat section {section} file {i}: values past the file");
                }
                for _ in 0..n * per {
                    host16(bytes, &mut out, p)?;
                    p += 2;
                }
            }
        }
    }
    Ok(out)
}

/// The audio archive (BAA, AudioRes/SMR.szs expanded) with these parts in host order: the command stream (commands as
/// host-order character constants, a cooked archive starts "<_AA"; their arguments; 'sect' keeps its four bytes), the
/// sound table (BST: header words, sections, groups, item offsets with their type byte, items by type: SE, BGM,
/// stream), the sound name table (BSTN: header words, sections, groups, name offsets) and the sequence collection
/// (BSC: group count, size, table offsets, each table's count and sequence offsets; the sequences stay bytes) and the
/// wave systems (WSYS: header, wave archive bank, archives, waves, control groups, scenes, controls and their wave
/// entries) and the instrument banks (IBNK version 1: bank).
pub fn baa(bytes: &[u8]) -> Result<Vec<u8>> {
    let waves = super::audio_data::wave_layout(bytes).context("the wave systems")?;
    if bytes.len() >= 4 && &bytes[0..4] == b"<_AA" {
        return Ok(bytes.to_vec()); // cooked already
    }
    if bytes.len() < 8 || &bytes[0..4] != b"AA_<" {
        bail!("not an audio archive");
    }
    let mut out = bytes.to_vec();
    let constant = |out: &mut Vec<u8>, at: usize| -> Result<[u8; 4]> {
        let c: [u8; 4] = bytes.get(at..at + 4).context("an audio archive command past the file's end")?.try_into().unwrap();
        out[at..at + 4].copy_from_slice(&u32::from_be_bytes(c).to_le_bytes());
        Ok(c)
    };
    constant(&mut out, 0)?;
    let mut turned: HashSet<usize> = HashSet::new(); // records two wave systems share: turned once
    let mut p = 4;
    loop {
        let cmd = constant(&mut out, p)?;
        p += 4;
        let args = match &cmd {
            b">_AA" => break,
            b"ws  " | b"bms " => 3,
            b"bnk " | b"bl_<" | b"bsc " | b"bst " | b"bstn" | b"vbnk" => 2,
            b"bmsa" | b"dsqb" | b"bsft" => 1,
            b">_bl" => 0,
            b"sect" => {
                p += 4; // four bytes
                continue;
            }
            _ => bail!("audio archive command {:?} at {:#x}", String::from_utf8_lossy(&cmd), p - 4),
        };
        let mut v = [0usize; 3];
        for k in 0..args {
            v[k] = host32(bytes, &mut out, p + 4 * k)? as usize;
        }
        p += 4 * args;
        match &cmd {
            b"bst " => sound_table(bytes, &mut out, v[0], v[1]).context("the sound table (BST)")?,
            b"bstn" => sound_name_table(bytes, &mut out, v[0], v[1]).context("the sound name table (BSTN)")?,
            b"bsc " => seq_collection(bytes, &mut out, v[0]).context("the sequence collection (BSC)")?,
            b"bsft" => {
                let files = host32(bytes, &mut out, v[0] + 4)? as usize;
                for f in 0..files {
                    host32(bytes, &mut out, v[0] + 8 + 4 * f)?;
                }
            }
            b"ws  " => wave_system(bytes, &mut out, v[1], &mut turned, &waves).with_context(|| format!("wave system {}", v[0]))?,
            b"bnk " => bank(bytes, &mut out, v[1], &mut turned).with_context(|| format!("bank {}", v[0]))?,
            _ => {}
        }
    }
    Ok(out)
}

/// The sound table (BST) at `base` (to `end`): magic and header words, root, sections, groups and items, offsets from
/// `base`.
fn sound_table(src: &[u8], out: &mut [u8], base: usize, end: usize) -> Result<()> {
    if src.get(base..base + 4) != Some(b"BST ".as_slice()) {
        bail!("no BST magic");
    }
    host32(src, out, base)?; // the magic, as its host-order character constant
    host32(src, out, base + 4)?;
    host32(src, out, base + 8)?;
    let root = base + host32(src, out, base + 0xC)? as usize;
    let sections = host32(src, out, root)? as usize;
    let mut items: HashSet<usize> = HashSet::new();
    let mut seen: HashSet<usize> = HashSet::new(); // sections and groups another entry shares: turned once
    for s in 0..sections {
        let section = host32(src, out, root + 4 + 4 * s)? as usize;
        if section == 0 || !seen.insert(section) {
            continue;
        }
        let groups = host32(src, out, base + section)? as usize;
        for g in 0..groups {
            let group = host32(src, out, base + section + 4 + 4 * g)? as usize;
            if group == 0 || !seen.insert(group) {
                continue;
            }
            let at = base + group;
            let count = host32(src, out, at)? as usize;
            host32(src, out, at + 4)?; // global offset
            for i in 0..count {
                let word = host32(src, out, at + 8 + 4 * i)?;
                let item = base + (word & 0xFF_FFFF) as usize;
                if word & 0xFF_FFFF == 0 || !items.insert(item) {
                    continue;
                }
                if item + 8 > end {
                    bail!("a sound item at {item:#x} past the table");
                }
                match (word >> 24) & 0xF0 {
                    0x50 => {
                        host16(src, out, item + 2)?; // audible switch
                        host16(src, out, item + 4)?; // sound switch
                    }
                    0x60 => {
                        host16(src, out, item + 2)?; // resource
                        host16(src, out, item + 4)?; // chord resource
                        host16(src, out, item + 6)?; // sound switch
                    }
                    0x70 => {
                        host16(src, out, item + 2)?; // channel control
                        host32(src, out, item + 4)?; // stream file name offset
                    }
                    t => bail!("a sound item of type {t:#x} at {item:#x}"),
                }
            }
        }
    }
    Ok(())
}

/// The sound name table (BSTN) at `base`: header words, root, sections (count, global offset, groups), groups (count,
/// category name offset, name offsets).
fn sound_name_table(src: &[u8], out: &mut [u8], base: usize, _end: usize) -> Result<()> {
    if src.get(base..base + 4) != Some(b"BSTN".as_slice()) {
        bail!("no BSTN magic");
    }
    host32(src, out, base)?; // the magic, as its host-order character constant
    host32(src, out, base + 4)?;
    host32(src, out, base + 8)?;
    let root = base + host32(src, out, base + 0xC)? as usize;
    let sections = host32(src, out, root)? as usize;
    let mut seen: HashSet<usize> = HashSet::new();
    for s in 0..sections {
        let section = host32(src, out, root + 4 + 4 * s)? as usize;
        if section == 0 || !seen.insert(section) {
            continue;
        }
        let groups = host32(src, out, base + section)? as usize;
        host32(src, out, base + section + 4)?;
        for g in 0..groups {
            let group = host32(src, out, base + section + 8 + 4 * g)? as usize;
            if group == 0 || !seen.insert(group) {
                continue;
            }
            let count = host32(src, out, base + group)? as usize;
            host32(src, out, base + group + 4)?;
            for i in 0..count {
                host32(src, out, base + group + 8 + 4 * i)?;
            }
        }
    }
    Ok(())
}

/// A wave system (WSYS) at `base`: header (size, id, wave table size, archive bank and control group offsets), the
/// wave archive bank (count, archive offsets), each archive (count after its 0x70-byte name, wave offsets), each wave
/// (sample rate, AW offset and length, loop flags and points, sample count: 32-bit; the two last samples: 16-bit;
/// format and key bytes stay), the control group (count, scene offsets), each scene's three control offsets, each
/// control (count, wave entry offsets) and its wave entries (32-bit: group and wave id halves). Magics stay
/// characters (the game compares none).
fn wave_system(
    src: &[u8],
    out: &mut [u8],
    base: usize,
    turned: &mut HashSet<usize>,
    waves: &std::collections::BTreeMap<String, Vec<super::audio_data::WavePlace>>,
) -> Result<()> {
    if src.get(base..base + 4) != Some(b"WSYS".as_slice()) {
        bail!("no WSYS magic");
    }
    if !turned.insert(base) {
        return Ok(());
    }
    for o in [4, 8, 0xC] {
        host32(src, out, base + o)?;
    }
    let bank = host32(src, out, base + 0x10)? as usize;
    let group = host32(src, out, base + 0x14)? as usize;
    if bank != 0 {
        let archives = host32(src, out, base + bank + 4)? as usize;
        for a in 0..archives {
            let archive = host32(src, out, base + bank + 8 + 4 * a)? as usize;
            if archive == 0 || !turned.insert(base + archive) {
                continue;
            }
            let name = super::audio_data::archive_name(src.get(base + archive..base + archive + 0x70).context("a wave archive's name")?);
            let count = host32(src, out, base + archive + 0x70)? as usize;
            for w in 0..count {
                let wave = host32(src, out, base + archive + 0x74 + 4 * w)? as usize;
                if wave == 0 || !turned.insert(base + wave) {
                    continue;
                }
                let at = base + wave;
                for o in (4..0x20).step_by(4) {
                    host32(src, out, at + o)?;
                }
                host16(src, out, at + 0x20)?;
                host16(src, out, at + 0x22)?;
                // The wave's place in the cooked AW file, PCM16 whatever the format byte says (it stays: the game takes a
                // voice nobody names as music when its wave was PCM16 on the disc).
                let word = |o: usize| u32::from_be_bytes(src[at + o..at + o + 4].try_into().unwrap()) as usize;
                if let Some(place) = super::audio_data::place_of(waves, &name, word(8), word(0xC), src[at + 1]) {
                    out[at + 8..at + 0xC].copy_from_slice(&(place.new_offset as u32).to_le_bytes());
                    out[at + 0xC..at + 0x10].copy_from_slice(&(place.new_length as u32).to_le_bytes());
                }
            }
        }
    }
    if group != 0 {
        let scenes = host32(src, out, base + group + 8)? as usize;
        for c in 0..scenes {
            let scene = host32(src, out, base + group + 0xC + 4 * c)? as usize;
            if scene == 0 || !turned.insert(base + scene) {
                continue;
            }
            for o in [0xC, 0x10, 0x14] {
                let ctrl = host32(src, out, base + scene + o)? as usize;
                if ctrl == 0 || !turned.insert(base + ctrl) {
                    continue;
                }
                let entries = host32(src, out, base + ctrl + 4)? as usize;
                for e in 0..entries {
                    let entry = host32(src, out, base + ctrl + 8 + 4 * e)? as usize;
                    if entry != 0 && turned.insert(base + entry) {
                        host32(src, out, base + entry)?;
                    }
                }
            }
        }
    }
    Ok(())
}

/// An instrument bank (IBNK, version 1) at `base`: the header's size, word and version; each chunk's kind (a host
/// character constant) and size (ENVT's envelope points stay big-endian with the sequences'), OSCT's count and oscillators (id, rate, table offsets,
/// scales; the target byte stays), LIST's count and offsets, the other chunks' counts; then from LIST every
/// instrument ('Inst': oscillator indices, effect offsets, key regions with their top-byte key, velocity regions,
/// volume and pitch) and drum set ('Perc': its percussion map offsets, each 'Pmap': volume, pitch, release, effects,
/// velocity regions), and every effect they name ('Rand', 'Sens': floats; type bytes stay). Velocity regions keep their
/// first byte. The parser's reads: JASBNKParser::Ver1.
fn bank(src: &[u8], out: &mut [u8], base: usize, turned: &mut HashSet<usize>) -> Result<()> {
    if src.get(base..base + 4) != Some(b"IBNK".as_slice()) {
        bail!("no IBNK magic");
    }
    if !turned.insert(base) {
        return Ok(());
    }
    let size = host32(src, out, base + 4)? as usize;
    host32(src, out, base + 8)?;
    let version = host32(src, out, base + 0xC)?;
    if version != 1 {
        bail!("bank version {version}");
    }
    let end = base + size;
    if end > src.len() || base % 4 != 0 {
        bail!("bank of {size:#x} bytes at {base:#x} outside the archive");
    }
    let words = |out: &mut [u8], from: usize, to: usize| -> Result<()> {
        let mut p = from;
        while p + 4 <= to {
            host32(src, out, p)?;
            p += 4;
        }
        Ok(())
    };
    let mut list: Vec<usize> = Vec::new();
    // The chunks as JASBNKParser::Ver1::findChunk walks them: each that starts before the bank's stated end (a last
    // chunk may run past it: bank 5's LIST does by four bytes).
    let mut at = base + 0x20;
    while at < end {
        let kind: [u8; 4] = src.get(at..at + 4).context("a bank chunk past the archive")?.try_into().unwrap();
        host32(src, out, at)?;
        let length = host32(src, out, at + 4)? as usize;
        let data = at + 8;
        if data + length > src.len() {
            bail!("bank chunk {:?} at {:#x} past the archive", String::from_utf8_lossy(&kind), at - base);
        }
        match &kind {
            // Envelope points: three 16-bit numbers each.
            b"ENVT" => {
                for o in (data..data + (length & !1)).step_by(2) {
                    host16(src, out, o)?;
                }
            }
            b"OSCT" => {
                let count = host32(src, out, data)? as usize;
                for i in 0..count {
                    let r = data + 4 + 0x1C * i;
                    host32(src, out, r)?; // id
                    words(out, r + 8, r + 0x1C)?; // rate, table offset, release table offset, scale, word
                }
            }
            b"LIST" => {
                let count = host32(src, out, data)? as usize;
                for i in 0..count {
                    let o = host32(src, out, data + 4 + 4 * i)? as usize;
                    if o != 0 {
                        list.push(base + o);
                    }
                }
            }
            b"INST" | b"PMAP" | b"PERC" | b"RAND" | b"SENS" => {
                host32(src, out, data)?; // the count (the records turn from LIST)
            }
            [0, 0, 0, 0] => {}
            _ => bail!("bank chunk {:?} at {:#x}", String::from_utf8_lossy(&kind), at - base),
        }
        at = (at + 0xB + length) & !3;
    }
    let effect = |out: &mut [u8], turned: &mut HashSet<usize>, at: usize| -> Result<()> {
        if !turned.insert(at) {
            return Ok(());
        }
        match src.get(at..at + 4) {
            Some(b"Rand") | Some(b"Sens") => {
                host32(src, out, at)?;
                words(out, at + 8, at + 16) // base and range, or start and end levels
            }
            _ => bail!("a bank effect at {:#x} of neither kind", at - base),
        }
    };
    // A velocity region list at `p`: the count, then 16 bytes each (a byte, a wave id word, volume, pitch).
    let velocities = |out: &mut [u8], p: usize| -> Result<usize> {
        let count = host32(src, out, p)? as usize;
        let mut q = p + 4;
        for _ in 0..count {
            words(out, q + 4, q + 16)?;
            q += 16;
        }
        Ok(q)
    };
    for record in list {
        if !turned.insert(record) {
            continue;
        }
        let magic: [u8; 4] = src.get(record..record + 4).context("a bank record past the archive")?.try_into().unwrap();
        host32(src, out, record)?;
        match &magic {
            b"Inst" => {
                let mut p = record + 4;
                let oscillators = host32(src, out, p)? as usize;
                words(out, p + 4, p + 4 + 4 * oscillators)?;
                p += 4 + 4 * oscillators;
                let effects = host32(src, out, p)? as usize;
                for e in 0..effects {
                    let o = host32(src, out, p + 4 + 4 * e)? as usize;
                    effect(out, turned, base + o)?;
                }
                p += 4 + 4 * effects;
                let keys = host32(src, out, p)? as usize;
                p += 4;
                for _ in 0..keys {
                    host32(src, out, p)?; // the high key in the top byte
                    p = velocities(out, p + 4)?;
                }
                words(out, p, p + 8)?; // volume, pitch
            }
            b"Perc" => {
                let count = host32(src, out, record + 4)? as usize;
                for i in 0..count {
                    let o = host32(src, out, record + 8 + 4 * i)? as usize;
                    if o == 0 || !turned.insert(base + o) {
                        continue;
                    }
                    let m = base + o;
                    if src.get(m..m + 4) != Some(b"Pmap".as_slice()) {
                        bail!("a drum entry at {o:#x} that is no Pmap");
                    }
                    host32(src, out, m)?;
                    words(out, m + 4, m + 0xC)?; // volume, pitch
                    host16(src, out, m + 0xE)?; // release (the pan byte stays)
                    let effects = host32(src, out, m + 0x10)? as usize;
                    for e in 0..effects {
                        let eo = host32(src, out, m + 0x14 + 4 * e)? as usize;
                        effect(out, turned, base + eo)?;
                    }
                    velocities(out, m + 0x14 + 4 * effects)?;
                }
            }
            _ => bail!("a bank record at {:#x} of kind {:?}", record - base, String::from_utf8_lossy(&magic)),
        }
    }
    Ok(())
}

/// The sequence collection (BSC) at `base`: group count, size, table offsets; each table's count and sequence offsets.
fn seq_collection(src: &[u8], out: &mut [u8], base: usize) -> Result<()> {
    if src.get(base..base + 2) != Some(b"SC".as_slice()) {
        bail!("no SC magic");
    }
    let groups = host16(src, out, base + 2)? as usize;
    host32(src, out, base + 4)?;
    let mut seen: HashSet<usize> = HashSet::new();
    for g in 0..groups {
        let table = host32(src, out, base + 8 + 4 * g)? as usize;
        if table == 0 || !seen.insert(table) {
            continue;
        }
        let count = host32(src, out, base + table)? as usize;
        for w in 0..count {
            host32(src, out, base + table + 4 + 4 * w)?;
        }
    }
    Ok(())
}

/// A sound animation (BAS) in host order: the sound count (16-bit), the 32-bit slot after it, and each 0x20-byte sound:
/// its id, note-on and note-off times, base pitch, flags and last word (32-bit); the volume, pitch, pan and repeat
/// bytes stay.
pub fn bas(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() < 8 {
        bail!("a sound animation shorter than its header");
    }
    let mut out = bytes.to_vec();
    let count = host16(bytes, &mut out, 0)? as usize;
    host32(bytes, &mut out, 4)?;
    if 8 + 0x20 * count > bytes.len() {
        bail!("{count} sounds past the sound animation's end");
    }
    for i in 0..count {
        let s = 8 + 0x20 * i;
        for o in [0, 4, 8, 0xC, 0x10, 0x1C] {
            host32(bytes, &mut out, s + o)?;
        }
    }
    Ok(out)
}

/// A chord table (CIT, "CITS", the rhythm library's JaiChord.arc members) in host order: the size word, the chord and
/// scale counts, the chord and scale offset tables and each scale's up and down note offsets (all from the file's
/// start); the first word (0) and the note bytes stay.
pub fn cit(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() < 16 || &bytes[4..8] != b"CITS" {
        bail!("not a chord table");
    }
    let mut out = bytes.to_vec();
    host32(bytes, &mut out, 8)?;
    let chords = host16(bytes, &mut out, 12)? as usize;
    let scales = host16(bytes, &mut out, 14)? as usize;
    for i in 0..chords {
        host32(bytes, &mut out, 16 + 4 * i)?;
    }
    let mut seen: HashSet<usize> = HashSet::new();
    for i in 0..scales {
        let scale = host32(bytes, &mut out, 16 + 4 * (chords + i))? as usize;
        if seen.insert(scale) {
            host32(bytes, &mut out, scale)?;
            host32(bytes, &mut out, scale + 4)?;
        }
    }
    Ok(out)
}

/// The rhythm library's files (AudioRes/Info JaiMe.arc, JaiRemixSeq.arc) in host order: a remix sequence (BRS) is all
/// 32-bit words; an ME sequence (BME) starts with its count and start positions (the sequence bytecode after them stays
/// bytes); an ME table (BMT) has the entry count, entry and name table offsets, a word, 8-byte playing parameters (a
/// 16-bit field at 4) and the name offsets.
pub fn rhythm(bytes: &[u8], extension: &str) -> Result<Vec<u8>> {
    let mut out = bytes.to_vec();
    match extension {
        "brs" => {
            if bytes.len() % 4 != 0 {
                bail!("a remix sequence of {} bytes (not whole words)", bytes.len());
            }
            for at in (0..bytes.len()).step_by(4) {
                host32(bytes, &mut out, at)?;
            }
        }
        "bme" => {
            let count = host32(bytes, &mut out, 0)? as usize;
            for i in 0..count {
                host32(bytes, &mut out, 4 + 4 * i)?;
            }
        }
        "bmt" => {
            let count = host32(bytes, &mut out, 0)? as usize;
            let entries = host32(bytes, &mut out, 4)? as usize;
            let names = host32(bytes, &mut out, 8)? as usize;
            host32(bytes, &mut out, 0xC)?;
            for i in 0..count {
                host16(bytes, &mut out, entries + 8 * i + 4)?;
                host32(bytes, &mut out, names + 4 * i)?;
            }
        }
        _ => bail!("not a rhythm library file"),
    }
    Ok(out)
}

/// The Wii Remote speaker's files (AudioRes/SpkRes/SpkRes.arc) in host order. BCT (the sound table): count, parameter
/// and name table offsets, a word, 8-byte parameters (wave id, release time, a spare: 16-bit; two bytes stay) and the
/// name offsets. CSW (the waves): the size word, the wave count, the offsets, and each wave's size, loop start, loop end
/// and 16-bit samples up to the next wave.
pub fn speaker(bytes: &[u8], extension: &str) -> Result<Vec<u8>> {
    let mut out = bytes.to_vec();
    match extension {
        "bct" => {
            let count = host32(bytes, &mut out, 0)? as usize;
            let parameters = host32(bytes, &mut out, 4)? as usize;
            let names = host32(bytes, &mut out, 8)? as usize;
            host32(bytes, &mut out, 0xC)?;
            for i in 0..count {
                let p = parameters + 8 * i;
                host16(bytes, &mut out, p)?;
                host16(bytes, &mut out, p + 4)?;
                host16(bytes, &mut out, p + 6)?;
                host32(bytes, &mut out, names + 4 * i)?;
            }
        }
        "csw" => {
            host32(bytes, &mut out, 0)?;
            let count = host32(bytes, &mut out, 4)? as usize;
            let mut starts: Vec<usize> = Vec::with_capacity(count);
            for i in 0..count {
                starts.push(host32(bytes, &mut out, 8 + 4 * i)? as usize);
            }
            let mut sorted = starts.clone();
            sorted.sort_unstable();
            sorted.dedup();
            for (k, &at) in sorted.iter().enumerate() {
                let end = sorted.get(k + 1).copied().unwrap_or(bytes.len());
                if at + 12 > end {
                    bail!("a speaker wave at {at:#x} shorter than its header");
                }
                for o in [0, 4, 8] {
                    host32(bytes, &mut out, at + o)?;
                }
                let mut p = at + 12;
                while p + 2 <= end {
                    host16(bytes, &mut out, p)?;
                    p += 2;
                }
            }
        }
        _ => bail!("not a speaker file"),
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
