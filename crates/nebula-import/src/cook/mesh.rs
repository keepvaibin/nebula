//! Native meshes in a cooked J3D model (host order already, native_data::j3d_model): the vertex arrays become values
//! (floats and colour words), each shape group's display list becomes its distinct corners and triangles. No vertex
//! format list, vertex description or display list reaches the game.
//!
//! VTX1: the header, `counts` (13 u32: entries of position, normal, NBT, colour 0, colour 1, texture coordinates 0-7),
//! `normal_floats` (u32: 3, or 9 where the format list named NBT normals), then the arrays at 32-byte boundaries.
//! SHP1: a group's draw entry (size, offset) names its mesh: `attributes`, `corner_count`, `index_count`, `places`
//! (u32 each), the corners (NebulaShapeCorner, 40 bytes), the indices (u16). The field that named the display lists
//! names the meshes; the description field is 0.
use anyhow::{bail, Context, Result};
use std::collections::HashMap;

const ATTR_PNMTX: usize = 0;
const ATTR_POS: usize = 9;
const ATTR_NRM: usize = 10;
const ATTR_CLR0: usize = 11;
const ATTR_TEX0: usize = 13;
const ATTR_NBT: u32 = 25;
const ATTR_END: u32 = 0xFF;

const SHAPE_NORMAL: u32 = 1 << 0;
const SHAPE_COLOR0: u32 = 1 << 1;
const SHAPE_TEXCOORD0: u32 = 1 << 3;
const SHAPE_NBT: u32 = 1 << 11;
const SHAPE_NBT_THREE: u32 = 1 << 12;
const SHAPE_POSITION_MATRIX: u32 = 1 << 13;
const SHAPE_TEXTURE_MATRIX0: u32 = 1 << 14;

const CORNER_BYTES: usize = 40;

fn r32(b: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(b.get(at..at + 4).context("model field past its end")?.try_into()?))
}

fn r16(b: &[u8], at: usize) -> Result<u16> {
    Ok(u16::from_le_bytes(b.get(at..at + 2).context("model field past its end")?.try_into()?))
}

fn w32(b: &mut [u8], at: usize, v: u32) {
    b[at..at + 4].copy_from_slice(&v.to_le_bytes());
}

/// A fixed-point array's scale; a fraction past any type's bits still gives a number.
fn fraction_scale(frac: u32) -> f32 {
    1.0 / 2f32.powi(frac as i32)
}

#[derive(Clone, Copy)]
struct Format {
    cnt: u32,
    kind: u32,
    frac: u32,
}

/// The format a model lists for `attr`, or J3D's default without one (an NBT entry counts as the normal's).
fn format_of(list: &[(u32, Format)], attr: u32) -> Format {
    let mut f = match attr {
        9 => Format { cnt: 1, kind: 4, frac: 0 },
        10 => Format { cnt: 0, kind: 4, frac: 0 },
        11 | 12 => Format { cnt: 1, kind: 5, frac: 0 },
        _ => Format { cnt: 1, kind: 4, frac: 0 },
    };
    for &(a, g) in list {
        let listed = if a == ATTR_NBT { 10 } else { a };
        if listed == attr {
            f = g;
        }
    }
    f
}

fn component(p: &[u8], at: usize, kind: u32, scale: f32) -> f32 {
    let two = |at: usize| -> u16 {
        p.get(at..at + 2).map_or(0, |s| u16::from_le_bytes([s[0], s[1]]))
    };
    match kind {
        0 => f32::from(p.get(at).copied().unwrap_or(0)) * scale,
        1 => f32::from(p.get(at).copied().unwrap_or(0) as i8) * scale,
        2 => f32::from(two(at)) * scale,
        3 => f32::from(two(at) as i16) * scale,
        _ => p.get(at..at + 4).map_or(0.0, |s| f32::from_le_bytes([s[0], s[1], s[2], s[3]])),
    }
}

/// A colour entry as R << 24 | G << 16 | B << 8 | A, each format widened by repeating its high bits.
fn color(p: &[u8], kind: u32) -> u32 {
    let (a, b, c, d) = (p[0] as u32, p[1] as u32, p[2] as u32, p[3] as u32);
    match kind {
        0 => {
            let v = a | (b << 8);
            let (r, g, bl) = ((v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F);
            ((r << 3 | r >> 2) << 24) | ((g << 2 | g >> 4) << 16) | ((bl << 3 | bl >> 2) << 8) | 0xFF
        }
        1 | 2 => (a << 24) | (b << 16) | (c << 8) | 0xFF,
        3 => {
            let v = a | (b << 8);
            let (r, g, bl, al) = ((v >> 12) & 0xF, (v >> 8) & 0xF, (v >> 4) & 0xF, v & 0xF);
            ((r | r << 4) << 24) | ((g | g << 4) << 16) | ((bl | bl << 4) << 8) | (al | al << 4)
        }
        4 => {
            let v = (a << 16) | (b << 8) | c;
            let (r, g, bl, al) = ((v >> 18) & 0x3F, (v >> 12) & 0x3F, (v >> 6) & 0x3F, v & 0x3F);
            ((r << 2 | r >> 4) << 24) | ((g << 2 | g >> 4) << 16) | ((bl << 2 | bl >> 4) << 8) | (al << 2 | al >> 4)
        }
        _ => (a << 24) | (b << 16) | (c << 8) | d,
    }
}

fn type_bytes(kind: u32) -> usize {
    match kind {
        0 | 1 => 1,
        2 | 3 => 2,
        _ => 4,
    }
}

struct Vertices {
    block: Vec<u8>,
    formats: Vec<(u32, Format)>,
}

fn pad32(out: &mut Vec<u8>) {
    while out.len() % 32 != 0 {
        out.push(0);
    }
}

/// VTX1 as values: J3D's strides for each array, the file's dequantisation (a position or texture coordinate by its fraction,
/// a normal by its type).
fn vertices(b: &[u8]) -> Result<Vertices> {
    // A last block's size may count padding the file does not have.
    let size = (r32(b, 4)? as usize).min(b.len());
    let mut formats = Vec::new();
    let list = r32(b, 8)? as usize;
    if list != 0 {
        let mut e = list;
        loop {
            let attr = r32(b, e)?;
            if attr == ATTR_END {
                break;
            }
            formats.push((attr, Format { cnt: r32(b, e + 4)?, kind: r32(b, e + 8)?, frac: u32::from(*b.get(e + 12).context("format list")?) }));
            e += 16;
        }
    }
    let mut starts: Vec<usize> = (0xC..0x40).step_by(4).map(|o| r32(b, o).unwrap_or(0) as usize).filter(|&v| v != 0).collect();
    starts.sort_unstable();
    starts.dedup();
    let bound = |s: usize| starts.iter().copied().find(|&x| x > s).unwrap_or(size);
    let normal = format_of(&formats, 10);
    let normal_floats = if normal.cnt != 0 { 9u32 } else { 3 };
    let mut out = vec![0u8; 0x40 + 13 * 4 + 4];
    out[0..8].copy_from_slice(&b[0..8]);
    w32(&mut out, 0x40 + 13 * 4, normal_floats);
    pad32(&mut out);
    let mut placed: HashMap<usize, (u32, u32)> = HashMap::new();
    for array in 0..13usize {
        let field = 0xC + array * 4;
        let start = r32(b, field)? as usize;
        if start == 0 {
            continue;
        }
        if let Some(&(offset, count)) = placed.get(&start) {
            w32(&mut out, field, offset);
            w32(&mut out, 0x40 + array * 4, count);
            continue;
        }
        let region = b.get(start..bound(start)).context("vertex array past its block")?;
        let to = out.len() as u32;
        let count;
        match array {
            3 | 4 => {
                let f = format_of(&formats, 11 + array as u32 - 3);
                count = region.len() / 4;
                for e in region.chunks_exact(4) {
                    out.extend_from_slice(&color(e, f.kind).to_le_bytes());
                }
            }
            0 => {
                let f = format_of(&formats, 9);
                let stride = if f.kind == 4 { 12 } else { 6 };
                let scale = fraction_scale(f.frac);
                let (bytes, n) = (type_bytes(f.kind), if f.cnt == 1 { 3 } else { 2 });
                count = region.len() / stride;
                for i in 0..count {
                    for c in 0..3 {
                        let v = if c < n { component(region, i * stride + c * bytes, f.kind, scale) } else { 0.0 };
                        out.extend_from_slice(&v.to_le_bytes());
                    }
                }
            }
            1 | 2 => {
                let f = normal;
                let nbt = array == 2;
                let stride = (if f.kind == 4 { 12 } else { 6 }) * if nbt { 3 } else { 1 };
                let shift = match f.kind {
                    0 => 7,
                    1 => 6,
                    2 => 15,
                    _ => 14,
                };
                let scale = if f.kind == 4 { 1.0 } else { 1.0 / (1u32 << shift) as f32 };
                let bytes = type_bytes(f.kind);
                let vectors = if nbt || f.cnt != 0 { 3 } else { 1 };
                count = region.len() / stride;
                let whole = b.get(start..size).context("vertex array past its block")?;
                for i in 0..count {
                    for v in 0..vectors {
                        for c in 0..3 {
                            let x = component(whole, i * stride + (v * 3 + c) * bytes, f.kind, scale);
                            out.extend_from_slice(&x.to_le_bytes());
                        }
                    }
                }
            }
            _ => {
                let f = format_of(&formats, 13 + array as u32 - 5);
                let stride = if f.kind == 4 { 8 } else { 4 };
                let scale = fraction_scale(f.frac);
                let (bytes, n) = (type_bytes(f.kind), if f.cnt == 1 { 2 } else { 1 });
                count = region.len() / stride;
                for i in 0..count {
                    for c in 0..2 {
                        let v = if c < n { component(region, i * stride + c * bytes, f.kind, scale) } else { 0.0 };
                        out.extend_from_slice(&v.to_le_bytes());
                    }
                }
            }
        }
        w32(&mut out, field, to);
        w32(&mut out, 0x40 + array * 4, count as u32);
        placed.insert(start, (to, count as u32));
        pad32(&mut out);
    }
    let total = out.len() as u32;
    w32(&mut out, 4, total);
    Ok(Vertices { block: out, formats })
}

struct Layout {
    stride: usize,
    offset: [i32; 21],
    bytes: [usize; 21],
    nbt: bool,
    nbt_three: bool,
}

fn layout_of(desc: &[(u32, u32)], formats: &[(u32, Format)]) -> Result<Layout> {
    let mut kind = [0u32; 21];
    let mut nbt = false;
    for &(attr, ty) in desc {
        let a = if attr == ATTR_NBT { 10 } else { attr };
        if a > 20 || ty == 0 {
            continue;
        }
        if a >= 9 && ty == 1 {
            bail!("a shape with values inside its display list");
        }
        kind[a as usize] = ty;
        nbt |= attr == ATTR_NBT;
    }
    let normal = format_of(formats, 10);
    let nbt_three = normal.cnt == 2;
    let wide_nbt = nbt || normal.cnt != 0;
    let mut l = Layout { stride: 0, offset: [-1; 21], bytes: [0; 21], nbt: wide_nbt, nbt_three };
    let mut at = 0;
    for a in 0..21 {
        if kind[a] == 0 {
            continue;
        }
        let mut n = if a < 9 || kind[a] == 2 { 1 } else { 2 };
        if a == ATTR_NRM && wide_nbt && nbt_three {
            n *= 3;
        }
        l.offset[a] = at as i32;
        l.bytes[a] = n;
        at += n;
    }
    l.stride = at;
    if l.offset[ATTR_POS] < 0 {
        bail!("a shape without positions");
    }
    Ok(l)
}

fn index_at(v: &[u8], l: &Layout, attr: usize, which: usize) -> u16 {
    if l.offset[attr] < 0 {
        return 0;
    }
    let width = if attr == ATTR_NRM && l.nbt_three && l.nbt { l.bytes[attr] / 3 } else { l.bytes[attr] };
    let p = l.offset[attr] as usize + which * width;
    if width == 1 {
        u16::from(v[p])
    } else {
        u16::from_le_bytes([v[p], v[p + 1]])
    }
}

fn corner_bytes(v: &[u8], l: &Layout) -> [u8; CORNER_BYTES] {
    let mut c = [0u8; CORNER_BYTES];
    let mut put = |slot: usize, x: u16| c[slot * 2..slot * 2 + 2].copy_from_slice(&x.to_le_bytes());
    put(0, index_at(v, l, ATTR_POS, 0));
    if l.offset[ATTR_NRM] >= 0 {
        let three = l.nbt && l.nbt_three;
        put(1, index_at(v, l, ATTR_NRM, 0));
        put(2, if three { index_at(v, l, ATTR_NRM, 1) } else { index_at(v, l, ATTR_NRM, 0) });
        put(3, if three { index_at(v, l, ATTR_NRM, 2) } else { index_at(v, l, ATTR_NRM, 0) });
    }
    for k in 0..2 {
        put(4 + k, index_at(v, l, ATTR_CLR0 + k, 0));
    }
    for k in 0..8 {
        put(6 + k, index_at(v, l, ATTR_TEX0 + k, 0));
    }
    c[28] = if l.offset[ATTR_PNMTX] >= 0 { v[l.offset[ATTR_PNMTX] as usize] / 3 } else { 0 };
    for k in 0..8 {
        let at = l.offset[1 + k];
        if at >= 0 && v[at as usize] >= 30 {
            c[29 + k] = (v[at as usize] - 30) / 3;
        }
    }
    c
}

/// A group's display list as a mesh: its quads, strips and fans as the renderer has always made them (a strip's odd
/// triangles turned to keep its winding), each distinct corner once in the order first met.
fn mesh(list: &[u8], l: &Layout) -> Result<Vec<u8>> {
    let mut corners: Vec<[u8; CORNER_BYTES]> = Vec::new();
    let mut seen: HashMap<[u8; CORNER_BYTES], u16> = HashMap::new();
    let mut indices: Vec<u16> = Vec::new();
    let mut at = 0;
    while at < list.len() {
        let op = list[at];
        at += 1;
        if op == 0 {
            continue;
        }
        let primitive = op & 0xF8;
        if op & 0x80 == 0 || op & 7 != 0 || primitive == 0xA8 || primitive == 0xB0 || primitive == 0xB8 {
            bail!("a display list holds more than triangles of one vertex format (command {op:#04x})");
        }
        let count = r16(list, at)? as usize;
        at += 2;
        let vertices = list.get(at..at + count * l.stride).context("vertices past the display list")?;
        at += count * l.stride;
        let triangles = match primitive {
            0x90 => count / 3,
            0x80 | 0x88 => count / 4 * 2,
            _ if count >= 3 => count - 2,
            _ => 0,
        };
        for t in 0..triangles {
            let v: [usize; 3] = match primitive {
                0x90 => [3 * t, 3 * t + 1, 3 * t + 2],
                0x80 | 0x88 => {
                    let q = 4 * (t / 2);
                    [q, q + 1 + (t & 1), q + 2 + (t & 1)]
                }
                0x98 => [if t & 1 != 0 { t + 1 } else { t }, if t & 1 != 0 { t } else { t + 1 }, t + 2],
                _ => [0, t + 1, t + 2],
            };
            for k in v {
                let c = corner_bytes(&vertices[k * l.stride..(k + 1) * l.stride], l);
                let index = match seen.get(&c) {
                    Some(&i) => i,
                    None => {
                        if corners.len() >= 0x10000 {
                            bail!("a shape group of over 65,536 corners");
                        }
                        let i = corners.len() as u16;
                        corners.push(c);
                        seen.insert(c, i);
                        i
                    }
                };
                indices.push(index);
            }
        }
    }
    let mut attributes = 0u32;
    if l.offset[ATTR_NRM] >= 0 {
        attributes |= SHAPE_NORMAL | if l.nbt { SHAPE_NBT } else { 0 } | if l.nbt && l.nbt_three { SHAPE_NBT_THREE } else { 0 };
    }
    for k in 0..2 {
        if l.offset[ATTR_CLR0 + k] >= 0 {
            attributes |= SHAPE_COLOR0 << k;
        }
    }
    if l.offset[ATTR_PNMTX] >= 0 {
        attributes |= SHAPE_POSITION_MATRIX;
    }
    for t in 0..8 {
        if l.offset[ATTR_TEX0 + t] >= 0 {
            attributes |= SHAPE_TEXCOORD0 << t;
        }
        if l.offset[1 + t] >= 0 {
            attributes |= SHAPE_TEXTURE_MATRIX0 << t;
        }
    }
    let mut places = 0u32;
    if l.offset[ATTR_PNMTX] >= 0 {
        for c in &corners {
            places |= 1u32.checked_shl(u32::from(c[28])).unwrap_or(0);
        }
    }
    let mut out = Vec::with_capacity(16 + corners.len() * CORNER_BYTES + indices.len() * 2);
    out.extend_from_slice(&attributes.to_le_bytes());
    out.extend_from_slice(&(corners.len() as u32).to_le_bytes());
    out.extend_from_slice(&(indices.len() as u32).to_le_bytes());
    out.extend_from_slice(&places.to_le_bytes());
    for c in &corners {
        out.extend_from_slice(c);
    }
    for i in &indices {
        out.extend_from_slice(&i.to_le_bytes());
    }
    Ok(out)
}

/// SHP1 with meshes for display lists; the other tables keep their place.
fn shapes(b: &[u8], formats: &[(u32, Format)]) -> Result<Vec<u8>> {
    // A last block's size may count padding the file does not have.
    let size = (r32(b, 4)? as usize).min(b.len());
    let (init, descs, lists, draw) = (r32(b, 0xC)? as usize, r32(b, 0x18)? as usize, r32(b, 0x20)? as usize, r32(b, 0x28)? as usize);
    if init == 0 || descs == 0 || lists == 0 || draw == 0 {
        return Ok(b.to_vec());
    }
    if lists < 0x2C {
        bail!("display lists inside the block's header");
    }
    let mut starts: Vec<usize> = (0xC..0x2C).step_by(4).map(|o| r32(b, o).unwrap_or(0) as usize).filter(|&v| v != 0).collect();
    starts.sort_unstable();
    starts.dedup();
    let bound = |s: usize| starts.iter().copied().find(|&x| x > s).unwrap_or(size);
    let mut out = b.get(..lists).context("display lists past the block")?.to_vec();
    // The tables after the lists move up to where the lists were.
    let tail = bound(lists);
    let moved = tail.checked_sub(lists).context("display lists past the block")?;
    out.extend_from_slice(b.get(tail..size).context("display lists past the block")?);
    for o in (0xC..0x2C).step_by(4) {
        let v = r32(b, o)? as usize;
        if v > lists {
            w32(&mut out, o, (v - moved) as u32);
        }
    }
    w32(&mut out, 0x18, 0);
    pad32(&mut out);
    let draw_at = r32(&out, 0x28)? as usize;
    let meshes_at = out.len();
    w32(&mut out, 0x20, meshes_at as u32);
    let mut blob: Vec<u8> = Vec::new();
    let mut made: HashMap<usize, (u32, u32)> = HashMap::new();
    let mut e = init;
    let init_end = bound(init);
    while e + 0x28 <= init_end {
        let groups = r16(b, e + 2)? as usize;
        let desc = r16(b, e + 4)? as usize;
        let first_draw = r16(b, e + 8)? as usize;
        e += 0x28;
        let mut d = descs + desc;
        let mut list = Vec::new();
        loop {
            let attr = r32(b, d)?;
            if attr == ATTR_END {
                break;
            }
            list.push((attr, r32(b, d + 4)?));
            d += 8;
        }
        let layout = layout_of(&list, formats)?;
        for g in 0..groups {
            let entry = draw + 8 * (first_draw + g);
            let length = r32(b, entry)? as usize;
            let offset = r32(b, entry + 4)? as usize;
            let key = entry;
            let (at, n) = match made.get(&key) {
                Some(&v) => v,
                None => {
                    let dl = b.get(lists + offset..lists + offset + length).context("a display list past its block")?;
                    let m = mesh(dl, &layout)?;
                    while blob.len() % 4 != 0 {
                        blob.push(0);
                    }
                    let at = blob.len() as u32;
                    blob.extend_from_slice(&m);
                    (at, m.len() as u32)
                }
            };
            made.insert(key, (at, n));
            let out_entry = draw_at + 8 * (first_draw + g);
            if out_entry + 8 > out.len() {
                bail!("a draw entry in the display lists' place");
            }
            w32(&mut out, out_entry, n);
            w32(&mut out, out_entry + 4, at);
        }
    }
    out.extend_from_slice(&blob);
    pad32(&mut out);
    let total = out.len() as u32;
    w32(&mut out, 4, total);
    Ok(out)
}

/// The cooked model with native vertex arrays and meshes (a material table, with neither block, comes back as it is).
pub fn native(model: Vec<u8>) -> Result<Vec<u8>> {
    let blocks = r32(&model, 0xC)? as usize;
    let tag = |s: &[u8; 4]| u32::from_be_bytes(*s).to_le_bytes();
    let mut out = model[..0x20].to_vec();
    let mut formats = Vec::new();
    let mut at = 0x20;
    for _ in 0..blocks {
        let size = (r32(&model, at + 4)? as usize).min(model.len() - at);
        let block = &model[at..at + size];
        if block[..4] == tag(b"VTX1") {
            let v = vertices(block).context("model vertices")?;
            formats = v.formats;
            out.extend_from_slice(&v.block);
        } else if block[..4] == tag(b"SHP1") {
            out.extend_from_slice(&shapes(block, &formats).context("model shapes")?);
        } else {
            out.extend_from_slice(block);
        }
        at += size;
    }
    if at < model.len() {
        out.extend_from_slice(&model[at..]);
    }
    let total = out.len() as u32;
    w32(&mut out, 8, total);
    Ok(out)
}
