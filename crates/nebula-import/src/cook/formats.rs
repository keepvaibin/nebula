//! The disc's texture containers, each rewritten with its images as texture records: a texture header (ResTIMG,
//! TPL image header, font sheet description) keeps its fields, its image offset names the record, and a
//! colour-indexed image becomes RGBA8 without a palette (the record holds the palette's colours).
//!
//! - BTI: one ResTIMG, then its data.
//! - BMD/BDL: the TEX1 block (ResTIMG array, names, data); the model's other blocks are kept.
//! - JPC (particles): each texture block (0x20 bytes, then a ResTIMG and its data).
//! - TPL (layouts): the image table, image and palette headers, data.
//! - BRFNT (fonts): the TGLP block's sheets; the records follow the file's last block.

use super::texture::{self, Format, Image, PaletteFormat};
use super::Stats;
use anyhow::{bail, Context, Result};
use std::collections::HashMap;

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(bytes.get(at..at + 4).context("a field past the file's end")?.try_into().unwrap()))
}

fn be16(bytes: &[u8], at: usize) -> Result<u16> {
    Ok(u16::from_be_bytes(bytes.get(at..at + 2).context("a field past the file's end")?.try_into().unwrap()))
}

fn put32(bytes: &mut [u8], at: usize, value: u32) {
    bytes[at..at + 4].copy_from_slice(&value.to_be_bytes());
}

fn put16(bytes: &mut [u8], at: usize, value: u16) {
    bytes[at..at + 2].copy_from_slice(&value.to_be_bytes());
}

fn pad32(bytes: &mut Vec<u8>) {
    bytes.resize((bytes.len() + 31) & !31, 0);
}

const RES_TIMG: usize = 0x20;
const RGBA8: u8 = 0x6;

/// One texture's image as a record, for a ResTIMG at `header` in `bytes` (offsets from the header).
fn timg_record(bytes: &[u8], header: usize, stats: &mut Stats) -> Result<Vec<u8>> {
    let code = bytes[header];
    let format = Format::from_code(code as u32).with_context(|| format!("texture format {code:#x}"))?;
    let width = be16(bytes, header + 2)? as u32;
    let height = be16(bytes, header + 4)? as u32;
    let palette_format = bytes[header + 9];
    let palette_entries = be16(bytes, header + 0x0A)? as usize;
    let palette_offset = be32(bytes, header + 0x0C)? as i32 as isize;
    let levels = bytes[header + 0x18].max(1) as u32;
    let image_offset = be32(bytes, header + 0x1C)? as i32 as isize;
    let image_at = (header as isize + if image_offset != 0 { image_offset } else { RES_TIMG as isize }) as usize;
    let data = bytes.get(image_at..).context("a texture's image past the file's end")?;
    let palette = if format.indexed() {
        let at = (header as isize + palette_offset) as usize;
        let entries = bytes.get(at..at + palette_entries * 2).context("a palette past the file's end")?;
        let palette_format = PaletteFormat::from_code(palette_format as u32).context("a palette format")?;
        Some((entries, palette_format))
    } else {
        None
    };
    let image = Image { format, width, height, levels, data, palette };
    stats.note(&image);
    texture::cook(&image)
}

/// Rewrites the ResTIMG at `header` (in `out`) for its record at `record_at` (offsets from the header).
fn point_timg(out: &mut [u8], header: usize, record_at: usize) {
    if Format::from_code(out[header] as u32).is_some_and(Format::indexed) {
        out[header] = RGBA8;
        out[header + 8] = 0; // no palette
        out[header + 9] = 0;
        put16(out, header + 0x0A, 0);
        put32(out, header + 0x0C, 0);
    }
    put32(out, header + 0x1C, (record_at - header) as u32);
}

/// A BTI file: its ResTIMG, then the record.
pub fn bti(bytes: &[u8], stats: &mut Stats) -> Result<Vec<u8>> {
    if bytes.len() < RES_TIMG {
        bail!("a texture file shorter than its header");
    }
    let record = timg_record(bytes, 0, stats)?;
    let mut out = bytes[..RES_TIMG].to_vec();
    point_timg(&mut out, 0, RES_TIMG);
    out.extend_from_slice(&record);
    Ok(out)
}

/// A ResTIMG array's records (images shared by several headers cooked once), appended to `out` from its length; the
/// headers in `out` at `headers[i]` point at them.
fn timg_array(source: &[u8], source_headers: &[usize], out: &mut Vec<u8>, headers: &[usize], stats: &mut Stats) -> Result<()> {
    let mut cooked: HashMap<Vec<u8>, usize> = HashMap::new();
    let mut records = Vec::with_capacity(source_headers.len());
    for &h in source_headers {
        // Two headers name one image when their image, palette and the fields that shape them agree.
        let mut key = source[h..h + 0x10].to_vec();
        key.extend_from_slice(&(h as i64 + be32(source, h + 0x0C)? as i32 as i64).to_le_bytes());
        key.extend_from_slice(&(h as i64 + be32(source, h + 0x1C)? as i32 as i64).to_le_bytes());
        key.push(source[h + 0x18]);
        let at = match cooked.get(&key) {
            Some(&at) => at,
            None => {
                let record = timg_record(source, h, stats)?;
                pad32(out);
                let at = out.len();
                out.extend_from_slice(&record);
                cooked.insert(key, at);
                at
            }
        };
        records.push(at);
    }
    for (&h, at) in headers.iter().zip(records) {
        point_timg(out, h, at);
    }
    pad32(out);
    Ok(())
}

/// A BMD or BDL model: the blocks before TEX1 as they are, TEX1 rebuilt (header, ResTIMG array, names, records),
/// the blocks after it.
pub fn j3d_model(bytes: &[u8], stats: &mut Stats) -> Result<Vec<u8>> {
    let blocks = be32(bytes, 0x0C)? as usize;
    let mut at = 0x20usize;
    let mut out = bytes[..0x20].to_vec();
    for _ in 0..blocks {
        let magic = bytes.get(at..at + 4).context("a model block past the file's end")?;
        let size = be32(bytes, at + 4)? as usize;
        let block = bytes.get(at..at + size).context("a model block past the file's end")?;
        if magic == b"TEX1" {
            let count = be16(block, 8)? as usize;
            let headers_at = be32(block, 0x0C)? as usize;
            let names_at = be32(block, 0x10)? as usize;
            // The names: a string table (count, entries, strings) whose size is its last string's end.
            let names_end = if names_at != 0 && count != 0 {
                let entries = be16(block, names_at)? as usize;
                let mut end = names_at + 4 + entries * 4;
                for e in 0..entries {
                    let s = names_at + be16(block, names_at + 4 + e * 4 + 2)? as usize;
                    let len = block[s..].iter().position(|b| *b == 0).context("a texture name without its end")?;
                    end = end.max(s + len + 1);
                }
                end
            } else {
                names_at
            };
            let start = out.len();
            let mut tex1 = block[..0x20].to_vec();
            put32(&mut tex1, 0x0C, 0x20);
            tex1.extend_from_slice(block.get(headers_at..headers_at + count * RES_TIMG).context("TEX1 headers")?);
            let new_names = tex1.len();
            if names_at != 0 {
                tex1.extend_from_slice(&block[names_at..names_end]);
                put32(&mut tex1, 0x10, new_names as u32);
            }
            let source_headers: Vec<usize> = (0..count).map(|i| headers_at + i * RES_TIMG).collect();
            let headers: Vec<usize> = (0..count).map(|i| 0x20 + i * RES_TIMG).collect();
            timg_array(block, &source_headers, &mut tex1, &headers, stats)?;
            let size = tex1.len() as u32;
            put32(&mut tex1, 4, size);
            debug_assert_eq!(start, out.len());
            out.extend_from_slice(&tex1);
        } else {
            out.extend_from_slice(block);
        }
        at += size;
    }
    let size = out.len() as u32;
    put32(&mut out, 0x08, size);
    Ok(out)
}

/// A JPC particle resource file: everything before its texture blocks, then each texture block rebuilt.
pub fn jpc(bytes: &[u8], stats: &mut Stats) -> Result<Vec<u8>> {
    let count = be16(bytes, 0x0A)? as usize;
    let mut at = be32(bytes, 0x0C)? as usize;
    let mut out = bytes.get(..at).context("JPC textures past the file's end")?.to_vec();
    pad32(&mut out);
    let textures_at = out.len() as u32;
    put32(&mut out, 0x0C, textures_at);
    for _ in 0..count {
        let size = be32(bytes, at + 4)? as usize;
        let block = bytes.get(at..at + size).context("a JPC texture past the file's end")?;
        let start = out.len();
        out.extend_from_slice(&block[..0x20 + RES_TIMG]);
        timg_array(block, &[0x20], &mut out, &[start + 0x20], stats)?;
        let new_size = (out.len() - start) as u32;
        put32(&mut out, start + 4, new_size);
        at += size;
    }
    Ok(out)
}

const TPL_IMAGE: usize = 0x24;

/// A TPL: its header, image table and image headers, then the records; palettes go.
pub fn tpl(bytes: &[u8], stats: &mut Stats) -> Result<Vec<u8>> {
    let count = be32(bytes, 4)? as usize;
    let table = be32(bytes, 8)? as usize;
    if count == 0 || count > 1024 {
        bail!("a texture palette file of {count} images");
    }
    let mut out = Vec::new();
    out.extend_from_slice(&bytes[..4]);
    out.extend_from_slice(&(count as u32).to_be_bytes());
    out.extend_from_slice(&12u32.to_be_bytes());
    let headers_at = 12 + count * 8;
    out.resize(headers_at + count * TPL_IMAGE, 0);
    for i in 0..count {
        let header = be32(bytes, table + i * 8)? as usize;
        put32(&mut out, 12 + i * 8, (headers_at + i * TPL_IMAGE) as u32);
        put32(&mut out, 12 + i * 8 + 4, 0);
        out[headers_at + i * TPL_IMAGE..headers_at + (i + 1) * TPL_IMAGE]
            .copy_from_slice(bytes.get(header..header + TPL_IMAGE).context("a TPL image header")?);
    }
    pad32(&mut out);
    for i in 0..count {
        let header = be32(bytes, table + i * 8)? as usize;
        let palette_header = be32(bytes, table + i * 8 + 4)? as usize;
        let height = be16(bytes, header)? as u32;
        let width = be16(bytes, header + 2)? as u32;
        let code = be32(bytes, header + 4)?;
        let format = Format::from_code(code).with_context(|| format!("texture format {code:#x}"))?;
        let data_at = be32(bytes, header + 8)? as usize;
        let levels = bytes[header + 34] as u32 + 1;
        let palette = if format.indexed() {
            if palette_header == 0 {
                bail!("a colour-indexed TPL image without a palette");
            }
            let entries = be16(bytes, palette_header)? as usize;
            let palette_format = PaletteFormat::from_code(be32(bytes, palette_header + 4)?).context("a palette format")?;
            let at = be32(bytes, palette_header + 8)? as usize;
            Some((bytes.get(at..at + entries * 2).context("a TPL palette")?, palette_format))
        } else {
            None
        };
        let data = bytes.get(data_at..).context("a TPL image past the file's end")?;
        let image = Image { format, width, height, levels, data, palette };
        stats.note(&image);
        let record = texture::cook(&image)?;
        let h = headers_at + i * TPL_IMAGE;
        if format.indexed() {
            put32(&mut out, h + 4, RGBA8 as u32);
        }
        let record_at = out.len() as u32;
        put32(&mut out, h + 8, record_at);
        out.extend_from_slice(&record);
        pad32(&mut out);
    }
    Ok(out)
}

/// A BRFNT font: the file as it is, with the TGLP block's sheets as records after its last block.
pub fn font(bytes: &[u8], stats: &mut Stats) -> Result<Vec<u8>> {
    let header_size = be16(bytes, 0x0C)? as usize;
    let blocks = be16(bytes, 0x0E)? as usize;
    let mut out = bytes.to_vec();
    let mut at = header_size;
    for _ in 0..blocks {
        let size = be32(bytes, at + 4)? as usize;
        if bytes.get(at..at + 4) == Some(b"TGLP") {
            let g = at + 8;
            let sheet_size = be32(bytes, g + 4)? as usize;
            let sheets = be16(bytes, g + 8)? as usize;
            let code = be16(bytes, g + 10)? as u32;
            let format = Format::from_code(code).with_context(|| format!("font sheet format {code:#x}"))?;
            let width = be16(bytes, g + 16)? as u32;
            let height = be16(bytes, g + 18)? as u32;
            let image_at = be32(bytes, g + 20)? as usize;
            pad32(&mut out);
            let first = out.len();
            // Sheets are found by index times one size: every record takes the widest layout among them.
            let images: Vec<Image> = (0..sheets)
                .map(|s| {
                    let start = image_at + s * sheet_size;
                    let data = bytes.get(start..start + sheet_size).context("a font sheet past the file's end")?;
                    Ok(Image { format, width, height, levels: 1, data, palette: None })
                })
                .collect::<Result<_>>()?;
            let mut wide = 1u8; // L8
            for image in &images {
                let l = texture::layout(image)?;
                if l == 0 || (l == 2 && wide == 1) {
                    wide = l;
                }
            }
            let mut stride = 0;
            for image in &images {
                stats.note(image);
                let mut record = texture::cook_widened(image, wide)?;
                record.resize((record.len() + 31) & !31, 0);
                stride = record.len();
                out.extend_from_slice(&record);
            }
            put32(&mut out, g + 4, stride as u32);
            put32(&mut out, g + 20, first as u32);
        }
        at += size;
    }
    let size = out.len() as u32;
    put32(&mut out, 0x08, size);
    Ok(out)
}
