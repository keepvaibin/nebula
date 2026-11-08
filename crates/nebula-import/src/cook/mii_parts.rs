//! The Mii parts: RFL_Res.dat in MiiFaceDatabase.arc, big-endian as the disc holds it, cooked into MiiParts.npt, which
//! the game's Mii system reads in place (native/petari/include/Game/Mii/MiiParts.hpp). Little-endian throughout.
//!
//! - Header (80 bytes): "NMPT", the version, 9 shape-table offsets (nose, forehead, face line, hair, cap, beard, nose
//!   line, mask, glasses) and 9 texture-table offsets (eye, eyebrow, mouth, mustache, mole, face, cap, nose line,
//!   glasses).
//! - A table: its count, then one offset per file; 0 for an empty file.
//! - A shape, 16-byte aligned: its vertex count, index count, flags (1: texture coordinates) and a zero word; the face
//!   line's nose, beard and hair places (three floats each, zero in other shapes) and three zero floats; then its
//!   vertices (position, normal, texture coordinate: eight floats) and its triangle list (32-bit indices), in the order
//!   the parts file draws them.
//! - A texture, 64-byte aligned: its NebulaTextureInfo (nebula_cooked_texture.h, as particles cook theirs), then its
//!   texture record (texture::cook) at image_at 64.
//!
//! The source file: a header of two 16-bit fields and one offset per archive (18), each archive a file count, a 16-bit
//! field and count + 1 file offsets from the table's end. A shape file: a 4-byte tag; a face line's three places (nine
//! floats); positions, normals and (but for the forehead, hair, beard and nose) texture coordinates, each a 16-bit count
//! and its 16-bit fixed-point values (8, 14 and 13 fraction bits); a primitive count; each primitive a vertex count, its
//! kind and each vertex's position, normal and texture coordinate indices (bytes).

use super::formats::timg_record;
use super::Stats;
use anyhow::{bail, Context, Result};
use std::collections::HashMap;

pub const MAGIC: &[u8; 4] = b"NMPT";
pub const VERSION: u32 = 1;
/// The cooked file's name next to the source's in the archive.
pub const NAME: &str = "MiiParts.npt";

const ARCHIVES: usize = 18;
// Archive numbers in the source: beard 0, eye 1, eyebrow 2, face line 3, face texture 4, forehead 5, glasses 6,
// glasses texture 7, hair 8, mask 9, mole 10, mouth 11, mustache 12, nose 13, nose line 14, nose line texture 15,
// cap 16, cap texture 17.
const SHAPES: [usize; 9] = [13, 5, 3, 8, 16, 0, 14, 9, 6];
const TEXTURES: [usize; 9] = [1, 2, 11, 12, 10, 4, 17, 15, 7];
const FACE_LINE: usize = 3;
const NO_COORDINATES: [usize; 4] = [5, 8, 0, 13];
const HEADER: usize = 8 + 4 * (SHAPES.len() + TEXTURES.len());
const SHAPE_HEADER: usize = 64;
const TEXTURE_INFO: usize = 64;
const TEXTURE_INFO_MAGIC: u32 = 0x4954_424E; // NBTI
const RES_TIMG: usize = 0x20;

/// True for the source file's name (its last part).
pub fn is_source(name: &str) -> bool {
    name.rsplit('/').next().is_some_and(|file| file.eq_ignore_ascii_case("RFL_Res.dat"))
}

fn be16(bytes: &[u8], at: usize) -> Result<u16> {
    Ok(u16::from_be_bytes(bytes.get(at..at + 2).context("a field past the file's end")?.try_into().unwrap()))
}

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(bytes.get(at..at + 4).context("a field past the file's end")?.try_into().unwrap()))
}

fn byte(bytes: &[u8], at: usize) -> Result<u8> {
    bytes.get(at).copied().context("a field past the file's end")
}

fn align(out: &mut Vec<u8>, to: usize) {
    out.resize(out.len().div_ceil(to) * to, 0);
}

fn put32(out: &mut [u8], at: usize, value: u32) {
    out[at..at + 4].copy_from_slice(&value.to_le_bytes());
}

/// An archive's files as byte ranges of the source.
fn files(bytes: &[u8], archive: usize) -> Result<Vec<&[u8]>> {
    let at = be32(bytes, (archive + 1) * 4)? as usize;
    let count = be16(bytes, at)? as usize;
    let table = at + 4;
    let data = table + (count + 1) * 4;
    if data > bytes.len() {
        bail!("archive {archive} past the file's end");
    }
    (0..count)
        .map(|i| {
            let start = data + be32(bytes, table + 4 * i)? as usize;
            let end = data + be32(bytes, table + 4 * i + 4)? as usize;
            bytes.get(start..end).with_context(|| format!("archive {archive} file {i} outside the file"))
        })
        .collect()
}

/// `count` 16-bit fixed-point values of `per` each, scaled.
fn fixed(file: &[u8], p: &mut usize, per: usize, scale: f32) -> Result<Vec<f32>> {
    let count = be16(file, *p)? as usize;
    *p += 2;
    let values = (0..count * per)
        .map(|i| Ok(be16(file, *p + 2 * i)? as i16 as f32 / scale))
        .collect::<Result<Vec<f32>>>()?;
    *p += count * per * 2;
    Ok(values)
}

/// One shape file as a cooked shape.
fn shape(file: &[u8], face_line: bool, coordinates: bool) -> Result<Vec<u8>> {
    let mut places = [0f32; 9];
    let mut p = 4;
    if face_line {
        for place in &mut places {
            *place = f32::from_bits(be32(file, p)?);
            p += 4;
        }
    }
    let mut vertices: Vec<[f32; 8]> = Vec::new();
    let mut indices: Vec<u32> = Vec::new();
    if be16(file, p)? != 0 {
        let positions = fixed(file, &mut p, 3, 256.0)?;
        let normals = fixed(file, &mut p, 3, 16384.0)?;
        let coords = if coordinates { fixed(file, &mut p, 2, 8192.0)? } else { Vec::new() };
        let primitives = byte(file, p)?;
        p += 1;
        let mut known: HashMap<(u8, u8, u8), u32> = HashMap::new();
        for primitive in 0..primitives {
            let count = byte(file, p)? as usize;
            let kind = byte(file, p + 1)? & 0xF8;
            p += 2;
            let mut corners = Vec::with_capacity(count);
            for _ in 0..count {
                let key = (byte(file, p)?, byte(file, p + 1)?, if coordinates { byte(file, p + 2)? } else { 0 });
                p += if coordinates { 3 } else { 2 };
                let (pi, ni, ti) = (key.0 as usize, key.1 as usize, key.2 as usize);
                if 3 * pi + 3 > positions.len() || 3 * ni + 3 > normals.len() || (coordinates && 2 * ti + 2 > coords.len()) {
                    bail!("primitive {primitive}: an index past its values");
                }
                let next = vertices.len() as u32;
                let index = *known.entry(key).or_insert_with(|| {
                    let (s, t) = if coordinates { (coords[2 * ti], coords[2 * ti + 1]) } else { (0.0, 0.0) };
                    vertices.push([
                        positions[3 * pi],
                        positions[3 * pi + 1],
                        positions[3 * pi + 2],
                        normals[3 * ni],
                        normals[3 * ni + 1],
                        normals[3 * ni + 2],
                        s,
                        t,
                    ]);
                    next
                });
                corners.push(index);
            }
            // As the renderer turns these primitives into triangles (TypedDraw writeIndices), so each faces the same way.
            match kind {
                0x80 => {
                    for q in corners.chunks_exact(4) {
                        indices.extend_from_slice(&[q[0], q[1], q[2], q[0], q[2], q[3]]);
                    }
                }
                0x90 => indices.extend(corners.chunks_exact(3).flatten()),
                0x98 => {
                    for t in 0..count.saturating_sub(2) {
                        let (a, b) = if t % 2 == 1 { (corners[t + 1], corners[t]) } else { (corners[t], corners[t + 1]) };
                        indices.extend_from_slice(&[a, b, corners[t + 2]]);
                    }
                }
                0xA0 => {
                    for t in 0..count.saturating_sub(2) {
                        indices.extend_from_slice(&[corners[0], corners[t + 1], corners[t + 2]]);
                    }
                }
                other => bail!("primitive {primitive}: kind {other:#x} is not a surface"),
            }
        }
    }
    let mut out = Vec::with_capacity(SHAPE_HEADER + vertices.len() * 32 + indices.len() * 4);
    for word in [vertices.len() as u32, indices.len() as u32, u32::from(coordinates), 0] {
        out.extend_from_slice(&word.to_le_bytes());
    }
    for value in places.iter().chain([0f32; 3].iter()) {
        out.extend_from_slice(&value.to_le_bytes());
    }
    for vertex in &vertices {
        for value in vertex {
            out.extend_from_slice(&value.to_le_bytes());
        }
    }
    for index in &indices {
        out.extend_from_slice(&index.to_le_bytes());
    }
    Ok(out)
}

/// One part texture (a ResTIMG-shaped header and its image) as its texture info and record.
fn texture(file: &[u8], stats: &mut Stats) -> Result<Vec<u8>> {
    if file.len() < RES_TIMG {
        bail!("a texture shorter than its header");
    }
    let record = timg_record(file, 0, stats)?;
    let mut out = Vec::with_capacity(TEXTURE_INFO + record.len());
    let words = [TEXTURE_INFO_MAGIC, 1, be16(file, 2)? as u32, be16(file, 4)? as u32, file[0x18] as u32, TEXTURE_INFO as u32];
    for word in words {
        out.extend_from_slice(&word.to_le_bytes());
    }
    for value in [file[0x16] as i8 as f32 * 0.125, file[0x17] as i8 as f32 * 0.125, be16(file, 0x1A)? as i16 as f32 * 0.01] {
        out.extend_from_slice(&value.to_le_bytes());
    }
    out.extend_from_slice(&[file[0], file[1], file[6], file[7], file[0x10], 1u8 << file[0x13].min(2), file[0x14], file[0x15]]);
    out.resize(TEXTURE_INFO, 0);
    out.extend_from_slice(&record);
    Ok(out)
}

/// RFL_Res.dat as MiiParts.npt.
pub fn cook(bytes: &[u8], stats: &mut Stats) -> Result<Vec<u8>> {
    if bytes.len() < (ARCHIVES + 1) * 4 {
        bail!("the Mii parts file is shorter than its header");
    }
    let mut out = vec![0u8; HEADER];
    out[0..4].copy_from_slice(MAGIC);
    put32(&mut out, 4, VERSION);
    for (kind, archive) in SHAPES.iter().chain(TEXTURES.iter()).enumerate() {
        let is_shape = kind < SHAPES.len();
        let parts = files(bytes, *archive)?;
        align(&mut out, 16);
        let table = out.len();
        put32(&mut out, 8 + 4 * kind, table as u32);
        out.extend_from_slice(&(parts.len() as u32).to_le_bytes());
        out.resize(table + 4 + 4 * parts.len(), 0);
        for (i, part) in parts.iter().enumerate() {
            if part.is_empty() {
                continue;
            }
            let cooked = if is_shape {
                shape(part, *archive == FACE_LINE, !NO_COORDINATES.contains(archive))
            } else {
                texture(part, stats)
            }
            .with_context(|| format!("archive {archive} file {i}"))?;
            align(&mut out, if is_shape { 16 } else { 64 });
            let at = out.len() as u32;
            put32(&mut out, table + 4 + 4 * i, at);
            out.extend_from_slice(&cooked);
        }
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn be16v(v: u16) -> [u8; 2] {
        v.to_be_bytes()
    }

    // A source with one face line (one quad and one strip of four, texture coordinates) and every other archive empty.
    fn sample() -> Vec<u8> {
        let mut face = b"FACE".to_vec();
        for place in [1.0f32, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0] {
            face.extend_from_slice(&place.to_bits().to_be_bytes());
        }
        face.extend_from_slice(&be16v(4));
        for v in [0i16, 0, 0, 256, 0, 0, 256, 256, 0, 0, 256, 0] {
            face.extend_from_slice(&v.to_be_bytes());
        }
        face.extend_from_slice(&be16v(1));
        for v in [0i16, 0, 16384] {
            face.extend_from_slice(&v.to_be_bytes());
        }
        face.extend_from_slice(&be16v(1));
        for v in [8192i16, 4096] {
            face.extend_from_slice(&v.to_be_bytes());
        }
        face.push(2);
        face.extend_from_slice(&[4, 0x80]);
        for i in 0..4u8 {
            face.extend_from_slice(&[i, 0, 0]);
        }
        face.extend_from_slice(&[4, 0x98]);
        for i in [0u8, 1, 3, 2] {
            face.extend_from_slice(&[i, 0, 0]);
        }
        let mut out = vec![0u8; (ARCHIVES + 1) * 4];
        for archive in 0..ARCHIVES {
            let at = out.len() as u32;
            out[(archive + 1) * 4..(archive + 2) * 4].copy_from_slice(&at.to_be_bytes());
            if archive == FACE_LINE {
                out.extend_from_slice(&be16v(1));
                out.extend_from_slice(&be16v(0));
                out.extend_from_slice(&0u32.to_be_bytes());
                out.extend_from_slice(&(face.len() as u32).to_be_bytes());
                out.extend_from_slice(&face);
            } else {
                out.extend_from_slice(&be16v(0));
                out.extend_from_slice(&be16v(0));
                out.extend_from_slice(&0u32.to_be_bytes());
            }
        }
        out
    }

    fn le32(bytes: &[u8], at: usize) -> u32 {
        u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap())
    }

    fn lef(bytes: &[u8], at: usize) -> f32 {
        f32::from_le_bytes(bytes[at..at + 4].try_into().unwrap())
    }

    #[test]
    fn face_line_cooks_to_triangles() {
        let mut stats = Stats::default();
        let cooked = cook(&sample(), &mut stats).unwrap();
        assert_eq!(&cooked[0..4], MAGIC);
        let table = le32(&cooked, 8 + 4 * 2) as usize; // the face line's kind
        assert_eq!(le32(&cooked, table), 1);
        let shape = le32(&cooked, table + 4) as usize;
        assert_eq!(shape % 16, 0);
        assert_eq!(le32(&cooked, shape), 4, "four distinct corners");
        assert_eq!(le32(&cooked, shape + 4), 12, "a quad and a strip of four: four triangles");
        assert_eq!(le32(&cooked, shape + 8), 1);
        assert_eq!(lef(&cooked, shape + 16), 1.0);
        assert_eq!(lef(&cooked, shape + 16 + 32), 9.0);
        let vertex = shape + SHAPE_HEADER + 32 * 2;
        assert_eq!((lef(&cooked, vertex), lef(&cooked, vertex + 4)), (1.0, 1.0), "positions at 8 fraction bits");
        assert_eq!(lef(&cooked, vertex + 20), 1.0, "normals at 14");
        assert_eq!((lef(&cooked, vertex + 24), lef(&cooked, vertex + 28)), (1.0, 0.5), "coordinates at 13");
        let indices = shape + SHAPE_HEADER + 32 * 4;
        let list: Vec<u32> = (0..12).map(|i| le32(&cooked, indices + 4 * i)).collect();
        assert_eq!(list, [0, 1, 2, 0, 2, 3, 0, 1, 3, 3, 1, 2]);
        let empty = le32(&cooked, 8) as usize; // the nose: no files
        assert_eq!(le32(&cooked, empty), 0);
    }

    #[test]
    fn part_textures_keep_their_fields_and_become_records() {
        // An I8 8x4 eye, clamped and repeating, nearest filters, LOD -1 to 1, bias -1.
        let mut file = vec![0u8; RES_TIMG];
        file[0] = 0x1;
        file[2..4].copy_from_slice(&8u16.to_be_bytes());
        file[4..6].copy_from_slice(&4u16.to_be_bytes());
        file[6] = 0;
        file[7] = 1;
        file[0x14] = 0;
        file[0x15] = 0;
        file[0x16] = (-8i8) as u8;
        file[0x17] = 8;
        file[0x18] = 1;
        file[0x1A..0x1C].copy_from_slice(&(-100i16).to_be_bytes());
        file[0x1C..0x20].copy_from_slice(&(RES_TIMG as u32).to_be_bytes());
        file.extend((0..32u8).map(|i| i * 7));
        let mut source = sample();
        // The eye archive (1) gets the file at the source's end.
        let end = source.len() as u32;
        source[8..12].copy_from_slice(&end.to_be_bytes());
        source.extend_from_slice(&1u16.to_be_bytes());
        source.extend_from_slice(&0u16.to_be_bytes());
        source.extend_from_slice(&0u32.to_be_bytes());
        source.extend_from_slice(&(file.len() as u32).to_be_bytes());
        source.extend_from_slice(&file);
        let cooked = cook(&source, &mut Stats::default()).unwrap();
        let table = le32(&cooked, 8 + 4 * SHAPES.len()) as usize; // the eye's kind
        assert_eq!(le32(&cooked, table), 1);
        let texture = le32(&cooked, table + 4) as usize;
        assert_eq!(texture % 64, 0);
        assert_eq!(le32(&cooked, texture), TEXTURE_INFO_MAGIC);
        assert_eq!((le32(&cooked, texture + 8), le32(&cooked, texture + 12)), (8, 4));
        assert_eq!(le32(&cooked, texture + 20), TEXTURE_INFO as u32, "the record follows the info");
        assert_eq!((lef(&cooked, texture + 24), lef(&cooked, texture + 28)), (-1.0, 1.0));
        assert_eq!(lef(&cooked, texture + 32), -1.0);
        assert_eq!(&cooked[texture + 36..texture + 44], &[1, 0, 0, 1, 0, 1, 0, 0]);
        assert!(super::super::texture::is_record(&cooked[texture + TEXTURE_INFO..]), "the image is a record");
    }

    #[test]
    fn lines_are_refused() {
        let mut bytes = sample();
        let at = bytes.windows(2).position(|w| w == [4, 0x98]).unwrap();
        bytes[at + 1] = 0xA8;
        assert!(cook(&bytes, &mut Stats::default()).is_err());
    }
}
