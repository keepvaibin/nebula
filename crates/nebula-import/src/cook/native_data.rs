//! The disc's data files in the host's byte order (the game reads its data natively, with no swap at
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
//! - BRFNT (fonts, after cook::formats::font made the sheets records): the flat layout "NFNT" (font_native): header,
//!   code pages, glyph table, sheets.
//! - J3D animations (BCK, BCA, BTK, BRK, BPK, BPA, BTP, BVA, BLK, BLA, BXK, BXA): headers, counts, offsets and the
//!   16-bit and float arrays by block type, the name tables (j3d_animation).
//! - J3D models and material tables (BMD, BDL, BMT; j3d_model): headers, block headers, INF1, JNT1, EVP1, DRW1 and
//!   the name tables, VTX1 (format list, arrays by their component type), SHP1 (shape data, tables, vertex
//!   descriptors, display lists' counts and 16-bit indices), MAT3 (every table by its record layout) and MDL3's
//!   tables (its command lists stay: the game does not read them), TEX1's image headers.
//! - BTI (texture files, after cook::formats::bti): the image header (host_timg).
//! - BAS (sound animations): native records, "NBAS" (bas).
//! - BRS/BMT (rhythm library remix and ME table files): words; the ME table as packed named records (rhythm).
//! - BCT/CSW (Wii Remote speaker table and waves): the table as packed named records; headers, samples (speaker).
//! - CIT (chord tables, "CITS"): size, counts, offset tables, scale note offsets (cit).
//!
//! The sound data (the audio archive, sequences, ME programs) is cooked to its own formats by cook::sound.

use super::distance;
use super::texture::Format;
use anyhow::{bail, Context, Result};
use std::collections::HashSet;

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

/// The font layout's version ("NFNT").
pub const FONT_VERSION: u32 = 1;
const FONT_HEADER: usize = 0x40;
const NO_GLYPH: u16 = 0xFFFF;

/// A font (BRFNT, after cook::formats::font made its sheets records) in the game's native layout "NFNT"
/// (nw4r/ut/fontResourceFormat.h FontFile), host order, every place an offset from the file's start:
/// - header (0x40 bytes): "NFNT", version, size; font type, line feed, u16 alternate glyph; the default widths (left,
///   glyph width, char width) and the encoding; height, width, ascent and the sheets' distance range (cook::distance,
///   0 for pictures); cell width and height, baseline, widest char; u16 sheet format, sheet count, sheet width and
///   height (source pixels), cells across and down; u32 sheet stride and glyph count; the places of the pages, the
///   page blocks, the glyphs and the sheets.
/// - pages: u16[256], a code's high byte to its page block (0xFFFF: no code there);
/// - page blocks: u16[256] each, the code's low byte to its glyph (0xFFFF: none);
/// - glyphs (10 bytes): u16 sheet, cell x and y (source pixels), then left, glyph width, char width, a spare byte;
/// - sheets: the records, the stride apart, from a 32-byte boundary.
/// A code's glyph is what the disc's code maps (CMAP, in their chain from FINF) gave it: the first map whose range
/// holds the code decides, as the console's lookup did. A glyph's widths are the first width table's (CWDH) holding
/// it, else the font's default. A glyph past the sheets reads as none.
pub fn font_native(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() < 0x10 || &bytes[0..4] != b"RFNT" {
        bail!("not a font");
    }
    let header = be16(bytes, 0xC)? as usize;
    let blocks = be16(bytes, 0xE)? as usize;
    let (mut finf, mut tglp) = (None, None);
    let mut at = header;
    for _ in 0..blocks {
        let kind: [u8; 4] = part(bytes, at, at + 4)?.try_into().unwrap();
        let size = be32(bytes, at + 4)? as usize;
        if size < 8 || at + size > bytes.len() {
            bail!("block {} at {at:#x}: size {size:#x}", String::from_utf8_lossy(&kind));
        }
        match &kind {
            b"FINF" => finf = Some(at + 8),
            b"TGLP" => tglp = Some(at + 8),
            b"CWDH" | b"CMAP" | b"GLGR" => {}
            _ => bail!("unknown font block {}", String::from_utf8_lossy(&kind)),
        }
        at += size;
    }
    let f = finf.context("a font without FINF")?;
    let g = tglp.context("a font without TGLP")?;
    part(bytes, f, f + 0x18)?;
    part(bytes, g, g + 0x18)?;
    let (cell_width, cell_height) = (bytes[g] as usize, bytes[g + 1] as usize);
    let sheet_size = be32(bytes, g + 4)? as usize;
    let sheet_count = be16(bytes, g + 8)? as usize;
    let format = be16(bytes, g + 10)?;
    let across = be16(bytes, g + 12)? as usize;
    let down = be16(bytes, g + 14)? as usize;
    let image = be32(bytes, g + 20)? as usize;
    let cells = across * down;
    let glyph_count = sheet_count * cells;
    if cells == 0 || glyph_count > NO_GLYPH as usize {
        bail!("a font of {sheet_count} sheets of {across} x {down} cells");
    }
    // Widths: the first table holding a glyph decides.
    let default: [u8; 3] = part(bytes, f + 4, f + 7)?.try_into().unwrap();
    let mut widths = vec![None::<[u8; 3]>; glyph_count];
    let mut next = be32(bytes, f + 0xC)? as usize;
    let mut walked = 0;
    while next != 0 {
        walked += 1;
        if walked > blocks {
            bail!("a font's width tables loop");
        }
        let (first, last) = (be16(bytes, next)? as usize, be16(bytes, next + 2)? as usize);
        for index in first..=last {
            let w = part(bytes, next + 8 + 3 * (index - first), next + 11 + 3 * (index - first))?;
            if let Some(slot) = widths.get_mut(index) {
                if slot.is_none() {
                    *slot = Some(w.try_into().unwrap());
                }
            }
        }
        next = be32(bytes, next + 4)? as usize;
    }
    // Codes: the first map whose range holds a code decides it (`owner`: that map's place in the chain).
    let mut owner = vec![u32::MAX; 0x10000];
    let mut glyph_of = vec![NO_GLYPH; 0x10000];
    let mut next = be32(bytes, f + 0x10)? as usize;
    let mut map = 0u32;
    while next != 0 {
        if map as usize >= blocks {
            bail!("a font's code maps loop");
        }
        let (first, last, method) = (be16(bytes, next)?, be16(bytes, next + 2)?, be16(bytes, next + 4)?);
        let info = next + 0xC;
        if method > 2 {
            bail!("code map method {method}");
        }
        for code in first..=last {
            if owner[code as usize] != u32::MAX {
                continue;
            }
            owner[code as usize] = map;
            glyph_of[code as usize] = match method {
                0 => be16(bytes, info)?.wrapping_add(code - first),
                1 => be16(bytes, info + 2 * (code - first) as usize)?,
                _ => NO_GLYPH, // a scan names its codes below
            };
        }
        if method == 2 {
            for k in 0..be16(bytes, info)? as usize {
                let (code, glyph) = (be16(bytes, info + 2 + 4 * k)?, be16(bytes, info + 4 + 4 * k)?);
                if owner[code as usize] == map && (first..=last).contains(&code) {
                    glyph_of[code as usize] = glyph;
                }
            }
        }
        next = be32(bytes, next + 8)? as usize;
        map += 1;
    }
    for glyph in glyph_of.iter_mut() {
        if *glyph as usize >= glyph_count {
            *glyph = NO_GLYPH;
        }
    }

    let mut out = vec![0u8; FONT_HEADER];
    out[0..4].copy_from_slice(b"NFNT");
    out[4..8].copy_from_slice(&FONT_VERSION.to_le_bytes());
    out[0xC] = bytes[f]; // font type
    out[0xD] = bytes[f + 1]; // line feed
    out[0xE..0x10].copy_from_slice(&be16(bytes, f + 2)?.to_le_bytes()); // alternate glyph
    out[0x10..0x14].copy_from_slice(part(bytes, f + 4, f + 8)?); // default widths, encoding
    out[0x14..0x17].copy_from_slice(part(bytes, f + 0x14, f + 0x17)?); // height, width, ascent
    out[0x17] = if Format::from_code(format as u32).is_some_and(distance::is_field_format) { distance::RANGE as u8 } else { 0 };
    out[0x18..0x1C].copy_from_slice(part(bytes, g, g + 4)?); // cell width, height, baseline, widest char
    for (k, v) in [format, sheet_count as u16, be16(bytes, g + 16)?, be16(bytes, g + 18)?, across as u16, down as u16]
        .iter()
        .enumerate()
    {
        out[0x1C + 2 * k..0x1E + 2 * k].copy_from_slice(&v.to_le_bytes());
    }
    out[0x28..0x2C].copy_from_slice(&(sheet_size as u32).to_le_bytes());
    out[0x2C..0x30].copy_from_slice(&(glyph_count as u32).to_le_bytes());
    // Pages, then a block for each high byte with a code.
    let pages_at = out.len();
    out.resize(pages_at + 512, 0xFF);
    let used: Vec<usize> = (0..256).filter(|&hi| glyph_of[hi * 256..hi * 256 + 256].iter().any(|&gl| gl != NO_GLYPH)).collect();
    let blocks_at = out.len();
    for (n, &hi) in used.iter().enumerate() {
        out[pages_at + 2 * hi..pages_at + 2 * hi + 2].copy_from_slice(&(n as u16).to_le_bytes());
        for lo in 0..256 {
            out.extend_from_slice(&glyph_of[hi * 256 + lo].to_le_bytes());
        }
    }
    let glyphs_at = out.len();
    for index in 0..glyph_count {
        let (sheet, cell) = (index / cells, index % cells);
        let x = (cell % across) * (cell_width + 1) + 1;
        let y = (cell / across) * (cell_height + 1) + 1;
        for v in [sheet, x, y] {
            out.extend_from_slice(&(v as u16).to_le_bytes());
        }
        out.extend_from_slice(&widths[index].unwrap_or(default));
        out.push(0);
    }
    out.resize((out.len() + 31) & !31, 0);
    let sheets_at = out.len();
    out.extend_from_slice(bytes.get(image..image + sheet_count * sheet_size).context("font sheets past the file's end")?);
    out.resize((out.len() + 31) & !31, 0);
    for (k, place) in [pages_at, blocks_at, glyphs_at, sheets_at].iter().enumerate() {
        out[0x30 + 4 * k..0x34 + 4 * k].copy_from_slice(&(*place as u32).to_le_bytes());
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

pub const BAS_MAGIC: &[u8; 4] = b"NBAS";
const BAS_SOUND: usize = 28;

/// A sound animation (BAS) as SoundAnimation reads it (NBAS, little-endian): "NBAS", the sound count (u32), then a
/// 28-byte record a sound: id, note-on and note-off times, base pitch, flags (u32 each), base volume, pitch delta,
/// play time, base pan, volume delta, repeat interval (bytes), 2 zero bytes. The disc's 0x20-byte sounds keep these at
/// the same offsets; its slot after the count (a control object's, 0 on disc) and each sound's bytes 0x1A-0x1F are
/// not read by the game and are left out.
pub fn bas(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.starts_with(BAS_MAGIC) {
        return Ok(bytes.to_vec());
    }
    if bytes.len() < 8 {
        bail!("a sound animation shorter than its header");
    }
    let count = be16(bytes, 0)? as usize;
    if 8 + 0x20 * count > bytes.len() {
        bail!("{count} sounds past the sound animation's end");
    }
    let mut out = Vec::with_capacity(8 + BAS_SOUND * count);
    out.extend_from_slice(BAS_MAGIC);
    out.extend_from_slice(&(count as u32).to_le_bytes());
    for i in 0..count {
        let s = 8 + 0x20 * i;
        for o in [0, 4, 8, 0xC, 0x10] {
            out.extend_from_slice(&be32(bytes, s + o)?.to_le_bytes());
        }
        out.extend_from_slice(part(bytes, s + 0x14, s + 0x1A)?);
        out.extend_from_slice(&[0, 0]);
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

/// A disc table of named records (an ME table, the speaker's sound table): the count, the record and name table
/// offsets, then 8-byte records and a name offset per record. Cooked: the count, the record and name table offsets (12
/// bytes), the records as `record` writes them, the name offsets (4-aligned) and the names, each ending in 0.
fn named_records(bytes: &[u8], record: impl Fn(&[u8], usize) -> Result<Vec<u8>>) -> Result<Vec<u8>> {
    let count = be32(bytes, 0)? as usize;
    let records = be32(bytes, 4)? as usize;
    let names = be32(bytes, 8)? as usize;
    let mut body = Vec::new();
    for i in 0..count {
        body.extend_from_slice(&record(bytes, records + 8 * i)?);
    }
    let names_at = (12 + body.len() + 3) & !3;
    let mut strings = Vec::new();
    let mut offsets = Vec::with_capacity(count);
    for i in 0..count {
        let at = be32(bytes, names + 4 * i)? as usize;
        let end = bytes.get(at..).and_then(|s| s.iter().position(|&b| b == 0)).context("a name past the file's end")?;
        offsets.push((names_at + 4 * count + strings.len()) as u32);
        strings.extend_from_slice(&bytes[at..at + end + 1]);
    }
    let mut out = Vec::with_capacity(names_at + 4 * count + strings.len());
    for word in [count as u32, 12, names_at as u32] {
        out.extend_from_slice(&word.to_le_bytes());
    }
    out.extend_from_slice(&body);
    out.resize(names_at, 0);
    for offset in offsets {
        out.extend_from_slice(&offset.to_le_bytes());
    }
    out.extend_from_slice(&strings);
    Ok(out)
}

/// The rhythm library's files (AudioRes/Info JaiMe.arc, JaiRemixSeq.arc): a remix sequence (BRS) is all 32-bit words, in
/// host order; an ME table (BMT) is named records of 6 bytes: priority, chord volume, pan, dolby, the fade time (u16).
/// ME programs (BME) are cook::sound's.
pub fn rhythm(bytes: &[u8], extension: &str) -> Result<Vec<u8>> {
    match extension {
        "brs" => {
            if bytes.len() % 4 != 0 {
                bail!("a remix sequence of {} bytes (not whole words)", bytes.len());
            }
            let mut out = bytes.to_vec();
            for at in (0..bytes.len()).step_by(4) {
                host32(bytes, &mut out, at)?;
            }
            Ok(out)
        }
        "bmt" => named_records(bytes, |b, at| {
            let mut r = part(b, at, at + 4)?.to_vec();
            r.extend_from_slice(&be16(b, at + 4)?.to_le_bytes());
            Ok(r)
        }),
        _ => bail!("not a rhythm library file"),
    }
}

/// The Wii Remote speaker's files (AudioRes/SpkRes/SpkRes.arc). BCT (the sound table): named records of 6 bytes: the
/// wave id and release time (u16), the volume (u8), 0. CSW (the waves), in host order: the size word, the wave count, the
/// offsets, and each wave's size, loop start, loop end and 16-bit samples up to the next wave.
pub fn speaker(bytes: &[u8], extension: &str) -> Result<Vec<u8>> {
    if extension == "bct" {
        return named_records(bytes, |b, at| {
            let mut r = be16(b, at)?.to_le_bytes().to_vec();
            r.extend_from_slice(&be16(b, at + 4)?.to_le_bytes());
            r.extend_from_slice(&[byte(b, at + 3)?, 0]);
            Ok(r)
        });
    }
    let mut out = bytes.to_vec();
    match extension {
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
mod font_tests {
    use super::*;

    fn put16(v: &mut Vec<u8>, x: u16) {
        v.extend_from_slice(&x.to_be_bytes());
    }
    fn put32(v: &mut Vec<u8>, x: u32) {
        v.extend_from_slice(&x.to_be_bytes());
    }
    fn block(v: &mut Vec<u8>, kind: &[u8; 4], body: &[u8]) -> usize {
        let at = v.len();
        v.extend_from_slice(kind);
        put32(v, (8 + body.len()) as u32);
        v.extend_from_slice(body);
        at + 8
    }

    // A BRFNT of one RGB5A3 sheet (2 x 2 cells of 4 x 4) with two width tables and three code maps whose ranges
    // overlap: the first holding a code or glyph decides, as the console's chain walk did.
    fn sample() -> Vec<u8> {
        let mut v = b"RFNT\xFE\xFF\x01\x04".to_vec();
        put32(&mut v, 0); // size
        put16(&mut v, 0x10);
        put16(&mut v, 6);
        let finf = v.len() + 8;
        let tglp = finf + 0x18 + 8;
        let cwdh1 = tglp + 0x18 + 8;
        let cwdh2 = cwdh1 + 8 + 6 + 8;
        let cmap1 = cwdh2 + 8 + 3 + 8;
        let cmap2 = cmap1 + 0xC + 2 + 8;
        let cmap3 = cmap2 + 0xC + 4 + 8;
        let sheet = cmap3 + 0xC + 2 + 8 + 8 + 4;
        let mut f = vec![1, 20];
        f.extend_from_slice(&3u16.to_be_bytes()); // alternate glyph
        f.extend_from_slice(&[0, 4, 5, 1]); // default widths, encoding
        f.extend_from_slice(&(tglp as u32).to_be_bytes());
        f.extend_from_slice(&(cwdh1 as u32).to_be_bytes());
        f.extend_from_slice(&(cmap1 as u32).to_be_bytes());
        f.extend_from_slice(&[16, 15, 12, 0]);
        assert_eq!(block(&mut v, b"FINF", &f), finf);
        let mut g = vec![4, 4, 3, 5];
        g.extend_from_slice(&32u32.to_be_bytes());
        for x in [1u16, 5, 2, 2, 10, 10] {
            g.extend_from_slice(&x.to_be_bytes());
        }
        g.extend_from_slice(&(sheet as u32).to_be_bytes());
        assert_eq!(block(&mut v, b"TGLP", &g), tglp);
        // Glyphs 1-2 widths, then 2-2 (glyph 2 already decided by the first).
        let mut w1 = vec![0, 1, 0, 2];
        w1.extend_from_slice(&(cwdh2 as u32).to_be_bytes());
        w1.extend_from_slice(&[1, 2, 3, 4, 5, 6]);
        assert_eq!(block(&mut v, b"CWDH", &w1), cwdh1);
        let mut w2 = vec![0, 2, 0, 2, 0, 0, 0, 0];
        w2.extend_from_slice(&[9, 9, 9]);
        assert_eq!(block(&mut v, b"CWDH", &w2), cwdh2);
        // 'A'-'B' direct from glyph 1; 'B'-'C' table [3, 0]; 'A'-0x3042 scan {'C' -> 2, 0x3042 -> 0}.
        let mut m1 = vec![0, 0x41, 0, 0x42, 0, 0, 0, 0];
        m1.extend_from_slice(&(cmap2 as u32).to_be_bytes());
        m1.extend_from_slice(&1u16.to_be_bytes());
        assert_eq!(block(&mut v, b"CMAP", &m1), cmap1);
        let mut m2 = vec![0, 0x42, 0, 0x43, 0, 1, 0, 0];
        m2.extend_from_slice(&(cmap3 as u32).to_be_bytes());
        m2.extend_from_slice(&[0, 3, 0, 0]);
        assert_eq!(block(&mut v, b"CMAP", &m2), cmap2);
        let mut m3 = vec![0, 0x41, 0x30, 0x42, 0, 2, 0, 0, 0, 0, 0, 0];
        m3.extend_from_slice(&[0, 2, 0, 0x43, 0, 2, 0x30, 0x42, 0, 0]);
        assert_eq!(block(&mut v, b"CMAP", &m3), cmap3);
        block(&mut v, b"GLGR", &[0; 4]);
        v.resize(sheet, 0);
        v.extend((0..32u8).map(|i| i ^ 0x5A));
        let size = v.len() as u32;
        v[8..12].copy_from_slice(&size.to_be_bytes());
        v
    }

    fn le16(b: &[u8], at: usize) -> u16 {
        u16::from_le_bytes([b[at], b[at + 1]])
    }
    fn le32(b: &[u8], at: usize) -> usize {
        u32::from_le_bytes(b[at..at + 4].try_into().unwrap()) as usize
    }
    fn glyph_of(n: &[u8], code: u16) -> u16 {
        let page = le16(n, le32(n, 0x30) + 2 * (code as usize >> 8));
        if page == 0xFFFF {
            return 0xFFFF;
        }
        le16(n, le32(n, 0x34) + 512 * page as usize + 2 * (code as usize & 255))
    }

    #[test]
    fn codes_and_widths_decide_as_the_chain_did() {
        let n = font_native(&sample()).unwrap();
        assert_eq!(&n[0..4], b"NFNT");
        assert_eq!(le32(&n, 8), n.len());
        assert_eq!((n[0xC], n[0xD], le16(&n, 0xE)), (1, 20, 3));
        assert_eq!(&n[0x10..0x17], &[0, 4, 5, 1, 16, 15, 12]);
        assert_eq!(n[0x17], 0, "a colour font has no distance range");
        assert_eq!(le32(&n, 0x2C), 4, "one sheet of 2 x 2 cells");
        // 'A' and 'B' from the direct map; 'C' from the table (the scan's 'C' comes later); 0x3042 from the scan.
        assert_eq!([glyph_of(&n, 0x41), glyph_of(&n, 0x42), glyph_of(&n, 0x43), glyph_of(&n, 0x3042)], [1, 2, 0, 0]);
        // In the scan's range but not in it, and outside every map.
        assert_eq!([glyph_of(&n, 0x44), glyph_of(&n, 0x20)], [0xFFFF, 0xFFFF]);
        // Glyph 3: sheet 0, cell (1, 1) -> (6, 6); default widths. Glyph 2: the first table's.
        let glyphs = le32(&n, 0x38);
        assert_eq!(&n[glyphs + 30..glyphs + 40], &[0, 0, 6, 0, 6, 0, 0, 4, 5, 0]);
        assert_eq!(&n[glyphs + 20..glyphs + 30], &[0, 0, 1, 0, 6, 0, 4, 5, 6, 0]);
        // The sheet, from a 32-byte boundary.
        let sheets = le32(&n, 0x3C);
        assert_eq!(sheets % 32, 0);
        assert_eq!(n[sheets..sheets + 32], (0..32u8).map(|i| i ^ 0x5A).collect::<Vec<_>>()[..]);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A disc table of two named records (8 bytes each) with the names after the name offsets.
    fn table(records: [[u8; 8]; 2]) -> Vec<u8> {
        let mut out = Vec::new();
        for word in [2u32, 0x10, 0x20, 0] {
            out.extend_from_slice(&word.to_be_bytes());
        }
        for r in records {
            out.extend_from_slice(&r);
        }
        for word in [0x28u32, 0x2B] {
            out.extend_from_slice(&word.to_be_bytes());
        }
        out.extend_from_slice(b"ab\0cde\0");
        out
    }

    #[test]
    fn me_and_speaker_tables_cook_to_packed_records() {
        let me = rhythm(&table([[1, 2, 3, 4, 0x01, 0x02, 9, 9], [5, 6, 7, 8, 0, 3, 9, 9]]), "bmt").unwrap();
        assert_eq!(&me[0..12], &[2, 0, 0, 0, 12, 0, 0, 0, 24, 0, 0, 0]);
        assert_eq!(&me[12..24], &[1, 2, 3, 4, 0x02, 0x01, 5, 6, 7, 8, 3, 0]);
        assert_eq!(&me[24..32], &[32, 0, 0, 0, 35, 0, 0, 0]);
        assert_eq!(&me[32..], b"ab\0cde\0");
        let spk = speaker(&table([[0, 7, 9, 100, 0, 40, 9, 9], [1, 0, 9, 50, 0, 80, 9, 9]]), "bct").unwrap();
        assert_eq!(&spk[12..24], &[7, 0, 40, 0, 100, 0, 0, 1, 80, 0, 50, 0]);
        assert_eq!(&spk[32..], b"ab\0cde\0");
    }

    #[test]
    fn sound_animations_cook_to_native_records() {
        let mut disc = vec![0, 2, 0, 0, 0, 0, 0, 0];
        for k in 0..2u8 {
            for word in [0x1000u32 + k as u32, 0x3F80_0000, 0x4000_0000, 0x4040_0000, 0x81] {
                disc.extend_from_slice(&word.to_be_bytes());
            }
            disc.extend_from_slice(&[10 + k, 0xFE, 3, 64, 0xFF, 5, 0x77, 0, 0xAA, 0xBB, 0xCC, 0xDD]);
        }
        let out = bas(&disc).unwrap();
        assert_eq!(out.len(), 8 + 2 * BAS_SOUND);
        assert_eq!(&out[0..8], b"NBAS\x02\0\0\0");
        let s = &out[8 + BAS_SOUND..];
        assert_eq!(&s[0..4], &0x1001u32.to_le_bytes());
        assert_eq!(&s[4..8], &1.0f32.to_le_bytes());
        assert_eq!(&s[12..16], &3.0f32.to_le_bytes());
        assert_eq!(&s[16..20], &0x81u32.to_le_bytes());
        assert_eq!(&s[20..], &[11, 0xFE, 3, 64, 0xFF, 5, 0, 0]);
        assert_eq!(bas(&out).unwrap(), out);
        assert!(bas(&disc[..8 + 0x20]).is_err());
    }
}
