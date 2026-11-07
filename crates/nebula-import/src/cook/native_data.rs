//! The disc's data files in the host's byte order (requirement B-02: the game reads its data natively, with no swap at
//! access). Each converter turns every field the game reads into little-endian and marks the file, so the game can
//! refuse a file that was not cooked.
//!
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
//! - BRS/BMT (rhythm library remix and ME table files): words, tables, parameters (rhythm).
//! - BCT/CSW (Wii Remote speaker table and waves): headers, parameters, samples (speaker).
//! - CIT (chord tables, "CITS"): size, counts, offset tables, scale note offsets (cit).
//! - RFL_Res.dat (Mii parts, after cook::formats::rfl_res): header, section tables, part texture headers, shapes'
//!   counts and values (rfl_res_host).
//!
//! The sound data (the audio archive, sequences, ME programs) is cooked to its own formats by cook::sound.

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
    if bytes.starts_with(b"NBTI") { return Ok(bytes.to_vec()); }
    if bytes.len() < 0x20 { bail!("a texture file shorter than its header"); }
    let image_at = be32(bytes, 0x1C)? as usize;
    let image_at = if image_at == 0 { 32 } else { image_at };
    let image = bytes.get(image_at..).context("texture image past its file")?;
    let mut out = Vec::with_capacity(64 + image.len());
    let word = |out: &mut Vec<u8>, value: u32| out.extend_from_slice(&value.to_le_bytes());
    for value in [0x4954_424E, 1, be16(bytes, 2)? as u32, be16(bytes, 4)? as u32, bytes[0x18] as u32, 64] {
        word(&mut out, value);
    }
    for value in [bytes[0x16] as i8 as f32 * 0.125, bytes[0x17] as i8 as f32 * 0.125,
                  be16(bytes, 0x1A)? as i16 as f32 * 0.01] { word(&mut out, value.to_bits()); }
    out.extend_from_slice(&[bytes[0], bytes[1], bytes[6], bytes[7], bytes[0x10], 1u8 << bytes[0x13].min(2), bytes[0x14], bytes[0x15]]);
    out.resize(64, 0);
    out.extend_from_slice(image);
    Ok(out)
}

/// `count` 32-bit words at `at`, as host order.
fn host_floats(src: &[u8], out: &mut [u8], at: usize, count: usize) -> Result<()> {
    for k in 0..count {
        host32(src, out, at + 4 * k)?;
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

/// The J3D animation types (file header bytes 4-8) the game loads (J3DAnmLoaderDataBase::load).
const ANM_TYPES: [&[u8; 4]; 12] =
    [b"bck1", b"bpk1", b"blk1", b"btk1", b"brk1", b"bxk1", b"bca1", b"bpa1", b"btp1", b"bva1", b"bla1", b"bxa1"];

/// Whether these bytes are a J3D animation as the disc has it.
pub fn is_animation_file(bytes: &[u8]) -> bool {
    bytes.len() >= 0x20 && &bytes[0..4] == b"J3D1" && ANM_TYPES.iter().any(|t| &bytes[4..8] == *t)
}

/// Whether these bytes are a J3D model or material table as the disc has it.
const MODEL_TYPES: [&[u8; 4]; 3] = [b"bmd3", b"bdl4", b"bmt3"];

pub fn is_model_file(bytes: &[u8]) -> bool {
    bytes.len() >= 0x20 && &bytes[0..4] == b"J3D2" && MODEL_TYPES.iter().any(|t| &bytes[4..8] == *t)
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
/// 32-bit words; an ME table (BMT) has the entry count, entry and name table offsets, a word, 8-byte playing parameters
/// (a 16-bit field at 4) and the name offsets. ME programs (BME) are cook::sound's.
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
