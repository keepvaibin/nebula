//! A disc texture cooked into Nebula's texture record (native/include/nebula_cooked_texture.h): its image decoded
//! once from the console's tiled formats into texels the renderer samples as they are, palettes applied.
//!
//! The arithmetic is the renderer's own before cooking (texture_cache.cpp's scalar loops): bit replication for 3-, 4-,
//! 5- and 6-bit channels, CMPR's 5/8 + 3/8 colours and its averaged three-colour mode, RGBA8's AR/GB tile halves. The
//! texels are stored in the smallest layout that holds them exactly; the renderer widens them to RGBA8 as it uploads.

use anyhow::{bail, Result};

/// The console's texel formats (GXTexFmt).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Format {
    I4 = 0x0,
    I8 = 0x1,
    Ia4 = 0x2,
    Ia8 = 0x3,
    Rgb565 = 0x4,
    Rgb5a3 = 0x5,
    Rgba8 = 0x6,
    C4 = 0x8,
    C8 = 0x9,
    C14x2 = 0xA,
    Cmpr = 0xE,
}

impl Format {
    pub fn from_code(code: u32) -> Option<Format> {
        Some(match code {
            0x0 => Format::I4,
            0x1 => Format::I8,
            0x2 => Format::Ia4,
            0x3 => Format::Ia8,
            0x4 => Format::Rgb565,
            0x5 => Format::Rgb5a3,
            0x6 => Format::Rgba8,
            0x8 => Format::C4,
            0x9 => Format::C8,
            0xA => Format::C14x2,
            0xE => Format::Cmpr,
            _ => return None,
        })
    }

    pub fn indexed(self) -> bool {
        matches!(self, Format::C4 | Format::C8 | Format::C14x2)
    }

    /// Texels per tile across and down, and the tile's bytes.
    fn tile(self) -> (u32, u32, u32) {
        match self {
            Format::I4 | Format::C4 | Format::Cmpr => (8, 8, 32),
            Format::I8 | Format::Ia4 | Format::C8 => (8, 4, 32),
            Format::Ia8 | Format::Rgb565 | Format::Rgb5a3 | Format::C14x2 => (4, 4, 32),
            Format::Rgba8 => (4, 4, 64),
        }
    }

    /// The bytes of one level, in whole tiles.
    pub fn level_bytes(self, width: u32, height: u32) -> usize {
        let (tw, th, bytes) = self.tile();
        (width.div_ceil(tw) * height.div_ceil(th) * bytes) as usize
    }

    /// The palette entries an indexed format addresses.
    pub fn palette_entries(self) -> usize {
        match self {
            Format::C4 => 16,
            Format::C8 => 256,
            Format::C14x2 => 16384,
            _ => 0,
        }
    }
}

/// A palette's entry format (GXTlutFmt).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PaletteFormat {
    Ia8 = 0,
    Rgb565 = 1,
    Rgb5a3 = 2,
}

impl PaletteFormat {
    pub fn from_code(code: u32) -> Option<PaletteFormat> {
        Some(match code {
            0 => PaletteFormat::Ia8,
            1 => PaletteFormat::Rgb565,
            2 => PaletteFormat::Rgb5a3,
            _ => return None,
        })
    }
}

/// A disc image to cook: its format, size, level count and bytes (every level, in order), and its palette.
pub struct Image<'a> {
    pub format: Format,
    pub width: u32,
    pub height: u32,
    pub levels: u32,
    pub data: &'a [u8],
    pub palette: Option<(&'a [u8], PaletteFormat)>,
}

const MAGIC: &[u8; 8] = b"NbTextur";
const VERSION: u32 = 1;
pub const HEADER: usize = 64;
const MAX_LEVELS: u32 = 11;
const LAYOUT_RGBA8: u8 = 0;
const LAYOUT_L8: u8 = 1;
const LAYOUT_LA8: u8 = 2;
const LAYOUT_BLOCK4: u8 = 3;
const FILTER_AT_UPLOAD: u8 = 1;

fn expand5(v: u16) -> u8 {
    let v = (v & 0x1F) as u8;
    (v << 3) | (v >> 2)
}

fn expand6(v: u16) -> u8 {
    let v = (v & 0x3F) as u8;
    (v << 2) | (v >> 4)
}

fn expand4(v: u16) -> u8 {
    let v = (v & 0xF) as u8;
    (v << 4) | v
}

fn be16(bytes: &[u8], at: usize) -> u16 {
    u16::from_be_bytes([bytes[at], bytes[at + 1]])
}

/// One 16-bit colour of a palette or a direct-colour texel.
fn colour(value: u16, format: PaletteFormat) -> [u8; 4] {
    match format {
        PaletteFormat::Ia8 => {
            let i = (value & 0xFF) as u8;
            [i, i, i, (value >> 8) as u8]
        }
        PaletteFormat::Rgb565 => [expand5(value >> 11), expand6(value >> 5), expand5(value), 0xFF],
        PaletteFormat::Rgb5a3 => {
            if value & 0x8000 != 0 {
                [expand5(value >> 10), expand5(value >> 5), expand5(value), 0xFF]
            } else {
                let a3 = ((value >> 12) & 7) as u8;
                [expand4(value >> 8), expand4(value >> 4), expand4(value), (a3 << 5) | (a3 << 2) | (a3 >> 1)]
            }
        }
    }
}

/// Decodes one level (`src` holds at least its whole tiles) into `out` (width * height texels, RGBA).
fn decode_level(image: &Image, src: &[u8], width: u32, height: u32, palette: &[[u8; 4]], out: &mut [[u8; 4]]) {
    let (tw, th, tile_bytes) = image.format.tile();
    let (w, h) = (width as usize, height as usize);
    let tiles_x = width.div_ceil(tw) as usize;
    let tiles_y = height.div_ceil(th) as usize;
    let (tw, th, tile_bytes) = (tw as usize, th as usize, tile_bytes as usize);
    let mut at = 0usize;
    for ty in 0..tiles_y {
        for tx in 0..tiles_x {
            let tile = &src[at..at + tile_bytes];
            at += tile_bytes;
            if image.format == Format::Cmpr {
                for sub in 0..4 {
                    let ox = tx * 8 + (sub & 1) * 4;
                    let oy = ty * 8 + (sub >> 1) * 4;
                    cmpr_block(&tile[sub * 8..sub * 8 + 8], ox, oy, w, h, out);
                }
                continue;
            }
            for py in 0..th {
                let y = ty * th + py;
                if y >= h {
                    break;
                }
                for px in 0..tw {
                    let x = tx * tw + px;
                    if x >= w {
                        break;
                    }
                    let i = py * tw + px;
                    out[y * w + x] = match image.format {
                        Format::I4 => {
                            let nibble = (tile[i / 2] >> if i % 2 == 0 { 4 } else { 0 }) & 0xF;
                            let v = nibble * 17;
                            [v, v, v, v]
                        }
                        Format::I8 => {
                            let v = tile[i];
                            [v, v, v, v]
                        }
                        Format::Ia4 => {
                            let byte = tile[i];
                            let v = (byte & 0xF) * 17;
                            [v, v, v, (byte >> 4) * 17]
                        }
                        Format::Ia8 => {
                            let v = tile[i * 2 + 1];
                            [v, v, v, tile[i * 2]]
                        }
                        Format::Rgb565 => colour(be16(tile, i * 2), PaletteFormat::Rgb565),
                        Format::Rgb5a3 => colour(be16(tile, i * 2), PaletteFormat::Rgb5a3),
                        Format::Rgba8 => [tile[i * 2 + 1], tile[32 + i * 2], tile[32 + i * 2 + 1], tile[i * 2]],
                        Format::C4 => palette[((tile[i / 2] >> if i % 2 == 0 { 4 } else { 0 }) & 0xF) as usize],
                        Format::C8 => palette[tile[i] as usize],
                        Format::C14x2 => palette[(be16(tile, i * 2) & 0x3FFF) as usize],
                        Format::Cmpr => unreachable!(),
                    };
                }
            }
        }
    }
}

/// One 4x4 CMPR block at (ox, oy): DXT1 with big-endian colours, the console's 5/8 + 3/8 interpolation, and its
/// three-colour mode's fourth entry the average with zero alpha.
fn cmpr_block(block: &[u8], ox: usize, oy: usize, w: usize, h: usize, out: &mut [[u8; 4]]) {
    if ox >= w || oy >= h {
        return;
    }
    let c0 = be16(block, 0);
    let c1 = be16(block, 2);
    let a = colour(c0, PaletteFormat::Rgb565);
    let b = colour(c1, PaletteFormat::Rgb565);
    let mix = |p: u8, s: u8| ((5 * p as u32 + 3 * s as u32) >> 3) as u8;
    let half = |p: u8, s: u8| ((p as u32 + s as u32) / 2) as u8;
    let entries = if c0 > c1 {
        [a, b, [mix(a[0], b[0]), mix(a[1], b[1]), mix(a[2], b[2]), 0xFF], [mix(b[0], a[0]), mix(b[1], a[1]), mix(b[2], a[2]), 0xFF]]
    } else {
        let m = [half(a[0], b[0]), half(a[1], b[1]), half(a[2], b[2])];
        [a, b, [m[0], m[1], m[2], 0xFF], [m[0], m[1], m[2], 0]]
    };
    for py in 0..4.min(h - oy) {
        let selectors = block[4 + py];
        for px in 0..4.min(w - ox) {
            out[(oy + py) * w + ox + px] = entries[((selectors >> (6 - px * 2)) & 3) as usize];
        }
    }
}

/// xxh64 with seed 0 (texture packs' hash of a texture's level 0, Dolphin's naming; rt_materials.cpp).
pub fn xxh64(data: &[u8]) -> u64 {
    const K1: u64 = 0x9E37_79B1_85EB_CA87;
    const K2: u64 = 0xC2B2_AE3D_27D4_EB4F;
    const K3: u64 = 0x1656_67B1_9E37_79F9;
    const K4: u64 = 0x85EB_CA77_C2B2_AE63;
    const K5: u64 = 0x27D4_EB2F_1656_67C5;
    let read64 = |p: &[u8]| u64::from_le_bytes(p[..8].try_into().unwrap());
    let round = |acc: u64, lane: u64| acc.wrapping_add(lane.wrapping_mul(K2)).rotate_left(31).wrapping_mul(K1);
    let n = data.len();
    let mut p = 0usize;
    let mut acc;
    if n >= 32 {
        let mut v = [K1.wrapping_add(K2), K2, 0u64, 0u64.wrapping_sub(K1)];
        while p + 32 <= n {
            for (lane, value) in v.iter_mut().enumerate() {
                *value = round(*value, read64(&data[p + lane * 8..]));
            }
            p += 32;
        }
        acc = v[0]
            .rotate_left(1)
            .wrapping_add(v[1].rotate_left(7))
            .wrapping_add(v[2].rotate_left(12))
            .wrapping_add(v[3].rotate_left(18));
        for value in v {
            acc = (acc ^ round(0, value)).wrapping_mul(K1).wrapping_add(K4);
        }
    } else {
        acc = K5;
    }
    acc = acc.wrapping_add(n as u64);
    while p + 8 <= n {
        acc = (acc ^ round(0, read64(&data[p..]))).rotate_left(27).wrapping_mul(K1).wrapping_add(K4);
        p += 8;
    }
    if p + 4 <= n {
        let v = u32::from_le_bytes(data[p..p + 4].try_into().unwrap()) as u64;
        acc = (acc ^ v.wrapping_mul(K1)).rotate_left(23).wrapping_mul(K2).wrapping_add(K3);
        p += 4;
    }
    while p < n {
        acc = (acc ^ (data[p] as u64).wrapping_mul(K5)).rotate_left(11).wrapping_mul(K1);
        p += 1;
    }
    acc ^= acc >> 33;
    acc = acc.wrapping_mul(K2);
    acc ^= acc >> 29;
    acc = acc.wrapping_mul(K3);
    acc ^= acc >> 32;
    acc
}

/// The hash texture packs name an indexed texture's palette by: xxh64 of the entries from the lowest to the highest
/// index its level 0 uses (texture_pack.cpp's texture_pack_tlut_hash).
fn palette_hash(format: Format, level0: &[u8], palette: &[u8]) -> u64 {
    let (mut lo, mut hi) = (u32::MAX, 0u32);
    let mut see = |v: u32| {
        lo = lo.min(v);
        hi = hi.max(v);
    };
    match format {
        Format::C4 => level0.iter().for_each(|b| {
            see((b >> 4) as u32);
            see((b & 0xF) as u32);
        }),
        Format::C8 => level0.iter().for_each(|b| see(*b as u32)),
        Format::C14x2 => level0.chunks_exact(2).for_each(|p| see((u16::from_be_bytes([p[0], p[1]]) & 0x3FFF) as u32)),
        _ => return 0,
    }
    if lo > hi {
        return 0;
    }
    let (start, end) = (lo as usize * 2, (hi as usize + 1) * 2);
    if end > palette.len() {
        // The renderer read entries past the palette's end from whatever followed it; cooked, they hash as zeros.
        let mut padded = palette[start.min(palette.len())..].to_vec();
        padded.resize(end - start, 0);
        return xxh64(&padded);
    }
    xxh64(&palette[start..end])
}

/// The levels a disc image holds (at most `image.levels`, the full chain and what its bytes cover).
pub fn held_levels(image: &Image) -> u32 {
    let mut chain = 1;
    while (image.width >> chain) | (image.height >> chain) != 0 {
        chain += 1;
    }
    let wanted = image.levels.clamp(1, chain.min(MAX_LEVELS));
    let mut levels = 0;
    let mut used = 0usize;
    while levels < wanted {
        let bytes = image.format.level_bytes((image.width >> levels).max(1), (image.height >> levels).max(1));
        if used + bytes > image.data.len() {
            break;
        }
        used += bytes;
        levels += 1;
    }
    levels
}

/// The texture record of a disc image.
pub fn cook(image: &Image) -> Result<Vec<u8>> {
    cook_widened(image, LAYOUT_L8)
}

/// How wide a layout's texels are (L8 < LA8 < RGBA8).
fn rank(layout: u8) -> u8 {
    match layout {
        LAYOUT_L8 => 0,
        LAYOUT_LA8 => 1,
        _ => 2,
    }
}

/// The layout a record of `image` takes (font sheets share the widest of theirs).
pub fn layout(image: &Image) -> Result<u8> {
    Ok(cook(image)?[21])
}

/// The texture record of a disc image in at least layout `wider` (a layout of `layout`).
pub fn cook_widened(image: &Image, wider: u8) -> Result<Vec<u8>> {
    if image.width == 0 || image.height == 0 || image.width > 1024 || image.height > 1024 {
        bail!("a {}x{} texture", image.width, image.height);
    }
    let levels = held_levels(image);
    if levels == 0 {
        bail!("a {}x{} {:?} texture whose file holds less than its first level", image.width, image.height, image.format);
    }
    let mut palette = Vec::new();
    if image.format.indexed() {
        let Some((bytes, format)) = image.palette else { bail!("a colour-indexed texture without its palette") };
        let entries = image.format.palette_entries();
        palette = (0..entries)
            .map(|e| if e * 2 + 1 < bytes.len() { colour(be16(bytes, e * 2), format) } else { [0, 0, 0, 0] })
            .collect();
    }
    let mut texels: Vec<Vec<[u8; 4]>> = Vec::with_capacity(levels as usize);
    let mut at = 0usize;
    for l in 0..levels {
        let (w, h) = ((image.width >> l).max(1), (image.height >> l).max(1));
        let mut out = vec![[0u8; 4]; (w * h) as usize];
        decode_level(image, &image.data[at..], w, h, &palette, &mut out);
        at += image.format.level_bytes(w, h);
        texels.push(out);
    }
    let all = texels.iter().flatten();
    let fits = if all.clone().all(|t| t[0] == t[1] && t[1] == t[2] && t[2] == t[3]) {
        LAYOUT_L8
    } else if all.clone().all(|t| t[0] == t[1] && t[1] == t[2]) {
        LAYOUT_LA8
    } else {
        LAYOUT_RGBA8
    };
    // CMPR stays 4 bits a texel: its blocks in row order (block4_level), expanded by the renderer as it uploads.
    let layout = if image.format == Format::Cmpr {
        LAYOUT_BLOCK4
    } else if rank(wider) > rank(fits) {
        wider
    } else {
        fits
    };
    let level0 = &image.data[..image.format.level_bytes(image.width, image.height)];
    let pack_hash = xxh64(level0);
    let pack_palette_hash = image.palette.map_or(0, |(bytes, _)| palette_hash(image.format, level0, bytes));

    let mut record = Vec::with_capacity(HEADER + texels.iter().map(|t| t.len() * 4).sum::<usize>());
    record.extend_from_slice(MAGIC);
    record.extend_from_slice(&VERSION.to_le_bytes());
    record.extend_from_slice(&0u32.to_le_bytes()); // size, below
    record.extend_from_slice(&(image.width as u16).to_le_bytes());
    record.extend_from_slice(&(image.height as u16).to_le_bytes());
    record.push(levels as u8);
    record.push(layout);
    record.push(image.format as u8);
    record.push(FILTER_AT_UPLOAD);
    record.extend_from_slice(&pack_hash.to_le_bytes());
    record.extend_from_slice(&pack_palette_hash.to_le_bytes());
    record.resize(HEADER, 0);
    let mut at = 0usize;
    for (l, level) in texels.iter().enumerate() {
        if layout == LAYOUT_BLOCK4 {
            let (w, h) = ((image.width >> l).max(1), (image.height >> l).max(1));
            block4_level(&image.data[at..], w, h, &mut record);
            at += image.format.level_bytes(w, h);
            continue;
        }
        for t in level {
            match layout {
                LAYOUT_L8 => record.push(t[0]),
                LAYOUT_LA8 => record.extend_from_slice(&[t[0], t[3]]),
                _ => record.extend_from_slice(t),
            }
        }
    }
    let size = record.len() as u32;
    record[12..16].copy_from_slice(&size.to_le_bytes());
    dump(image, levels, &record);
    Ok(record)
}

/// One CMPR level as the record's BLOCK4 layout: its 4x4 blocks in row order, ceil(w / 4) across, each 8 bytes:
/// the two RGB565 colours (little-endian) and sixteen 2-bit indices (little-endian, texel (x, y) at bit 2 * (4y + x)),
/// BC1's block. The colours between them are the console's: 5/8 + 3/8 in four-colour blocks (c0 > c1), the average and
/// transparent black in three-colour ones (nebula_cooked_texture.h).
fn block4_level(src: &[u8], width: u32, height: u32, out: &mut Vec<u8>) {
    let tiles_x = width.div_ceil(8) as usize;
    for by in 0..height.div_ceil(4) as usize {
        for bx in 0..width.div_ceil(4) as usize {
            // The 8x8 tile holding the block, then its 4x4 subblock (top-left, top-right, bottom-left, bottom-right).
            let tile = (by / 2) * tiles_x + bx / 2;
            let sub = (by % 2) * 2 + bx % 2;
            let b = &src[tile * 32 + sub * 8..tile * 32 + sub * 8 + 8];
            out.extend_from_slice(&[b[1], b[0], b[3], b[2]]);
            let mut indices = 0u32;
            for row in 0..4 {
                for column in 0..4 {
                    let index = (b[4 + row] >> (6 - column * 2)) & 3;
                    indices |= (index as u32) << (2 * (row * 4 + column));
                }
            }
            out.extend_from_slice(&indices.to_le_bytes());
        }
    }
}

/// With NEBULA_COOK_DUMP=<file>, each cooked image is appended to the file for the cook check
/// (runtime/src/render/texture_cache_cook_check.cpp): format, width, height, levels, palette format (0xFF: none),
/// palette length, image length, record length (u32 LE each), then the palette, image and record bytes.
fn dump(image: &Image, levels: u32, record: &[u8]) {
    use std::io::Write;
    use std::sync::Mutex;
    static FILE: Mutex<Option<std::fs::File>> = Mutex::new(None);
    let Some(path) = std::env::var_os("NEBULA_COOK_DUMP") else { return };
    let mut file = FILE.lock().unwrap();
    if file.is_none() {
        *file = std::fs::OpenOptions::new().create(true).append(true).open(path).ok();
    }
    let Some(file) = file.as_mut() else { return };
    let mut bytes_used = 0usize;
    for l in 0..levels {
        bytes_used += image.format.level_bytes((image.width >> l).max(1), (image.height >> l).max(1));
    }
    let (palette, palette_format) = image.palette.map_or((&[][..], 0xFFu32), |(p, f)| (p, f as u32));
    let mut head = Vec::new();
    for v in [image.format as u32, image.width, image.height, levels, palette_format, palette.len() as u32,
              bytes_used as u32, record.len() as u32] {
        head.extend_from_slice(&v.to_le_bytes());
    }
    let _ = file.write_all(&head);
    let _ = file.write_all(palette);
    let _ = file.write_all(&image.data[..bytes_used]);
    let _ = file.write_all(record);
}

/// Whether `bytes` start with a texture record (an already cooked file).
pub fn is_record(bytes: &[u8]) -> bool {
    bytes.len() >= HEADER && &bytes[..8] == MAGIC
}

/// The RGBA texels of a record's level 0 (tests and the cook check).
pub fn record_level0(record: &[u8]) -> Option<(u32, u32, Vec<[u8; 4]>)> {
    if !is_record(record) {
        return None;
    }
    let w = u16::from_le_bytes([record[16], record[17]]) as u32;
    let h = u16::from_le_bytes([record[18], record[19]]) as u32;
    let layout = record[21];
    let texels = &record[HEADER..];
    let n = (w * h) as usize;
    let out = match layout {
        LAYOUT_BLOCK4 => {
            let mut out = vec![[0u8; 4]; n];
            for by in 0..h.div_ceil(4) as usize {
                for bx in 0..w.div_ceil(4) as usize {
                    let b = texels.get((by * w.div_ceil(4) as usize + bx) * 8..)?;
                    let c0 = u16::from_le_bytes([b[0], b[1]]);
                    let c1 = u16::from_le_bytes([b[2], b[3]]);
                    let a = colour(c0, PaletteFormat::Rgb565);
                    let z = colour(c1, PaletteFormat::Rgb565);
                    let mix = |p: u8, q: u8| ((5 * p as u32 + 3 * q as u32) >> 3) as u8;
                    let half = |p: u8, q: u8| ((p as u32 + q as u32) / 2) as u8;
                    let entries = if c0 > c1 {
                        [a, z, [mix(a[0], z[0]), mix(a[1], z[1]), mix(a[2], z[2]), 255], [mix(z[0], a[0]), mix(z[1], a[1]), mix(z[2], a[2]), 255]]
                    } else {
                        let m = [half(a[0], z[0]), half(a[1], z[1]), half(a[2], z[2])];
                        [a, z, [m[0], m[1], m[2], 255], [m[0], m[1], m[2], 0]]
                    };
                    let indices = u32::from_le_bytes(b[4..8].try_into().unwrap());
                    for row in 0..4 {
                        for column in 0..4 {
                            let (x, y) = (bx * 4 + column, by * 4 + row);
                            if x < w as usize && y < h as usize {
                                out[y * w as usize + x] = entries[((indices >> (2 * (row * 4 + column))) & 3) as usize];
                            }
                        }
                    }
                }
            }
            out
        }
        LAYOUT_L8 => texels.get(..n)?.iter().map(|&v| [v, v, v, v]).collect(),
        LAYOUT_LA8 => texels.get(..n * 2)?.chunks_exact(2).map(|p| [p[0], p[0], p[0], p[1]]).collect(),
        _ => texels.get(..n * 4)?.chunks_exact(4).map(|p| [p[0], p[1], p[2], p[3]]).collect(),
    };
    Some((w, h, out))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn xxh64_matches_the_reference_values() {
        // python-xxhash 3.x, xxh64(data, seed=0) (texture_pack_tests.cpp).
        assert_eq!(xxh64(b""), 0xef46db3751d8e999);
        assert_eq!(xxh64(b"abc"), 0x44bc2cf5ad770999);
        assert_eq!(xxh64(b"Nobody inspects the spammish repetition"), 0xfbcea83c8a378bf1);
    }

    #[test]
    fn i8_tile_lands_row_by_row() {
        let data: Vec<u8> = (0..32).collect();
        let image = Image { format: Format::I8, width: 8, height: 4, levels: 1, data: &data, palette: None };
        let record = cook(&image).unwrap();
        assert_eq!(record[21], LAYOUT_L8);
        assert_eq!(&record[HEADER..HEADER + 32], &data[..]);
        assert_eq!(u32::from_le_bytes(record[12..16].try_into().unwrap()) as usize, record.len());
    }

    #[test]
    fn cmpr_uses_five_eighths_and_three_colour_alpha() {
        // c0 white > c1 black: entry 2 = 5/8 white.
        let mut data = vec![0u8; 32];
        data[0..2].copy_from_slice(&0xFFFFu16.to_be_bytes());
        data[4] = 0b1000_0000;
        let image = Image { format: Format::Cmpr, width: 8, height: 8, levels: 1, data: &data, palette: None };
        let (_, _, t) = record_level0(&cook(&image).unwrap()).unwrap();
        assert_eq!(t[0], [159, 159, 159, 255]);
        // c0 == c1: three-colour mode, index 3 transparent.
        let mut data = vec![0u8; 32];
        data[4] = 0b1100_0000;
        let image = Image { format: Format::Cmpr, width: 8, height: 8, levels: 1, data: &data, palette: None };
        let (_, _, t) = record_level0(&cook(&image).unwrap()).unwrap();
        assert_eq!(t[0], [0, 0, 0, 0]);
    }

    #[test]
    fn c8_takes_its_palette_and_hashes_the_used_range() {
        let mut data = vec![3u8; 32];
        data[1] = 5;
        let mut palette = vec![0u8; 512];
        palette[6..8].copy_from_slice(&0xF800u16.to_be_bytes()); // entry 3: red (RGB565)
        let image = Image {
            format: Format::C8,
            width: 8,
            height: 4,
            levels: 1,
            data: &data,
            palette: Some((&palette, PaletteFormat::Rgb565)),
        };
        let record = cook(&image).unwrap();
        let (_, _, t) = record_level0(&record).unwrap();
        assert_eq!(t[0], [255, 0, 0, 255]);
        assert_eq!(u64::from_le_bytes(record[32..40].try_into().unwrap()), xxh64(&palette[6..12]));
    }

    #[test]
    fn palette_hash_covers_the_used_index_range() {
        // The renderer's cases before cooking (texture_pack_tests.cpp): C8 indices 3..7, C4 nibbles 1..14, C14X2's
        // indices 0x10..0x12 with the top two bits ignored.
        let palette: Vec<u8> = (0..512u32).map(|i| (i * 7 + 3) as u8).collect();
        let mut c8 = vec![5u8; 32];
        c8[0] = 3;
        c8[31] = 7;
        assert_eq!(palette_hash(Format::C8, &c8, &palette), xxh64(&palette[6..16]));
        let mut c4 = vec![0x55u8; 32];
        c4[3] = 0x1E;
        assert_eq!(palette_hash(Format::C4, &c4, &palette), xxh64(&palette[2..30]));
        let mut c14 = vec![0u8; 32];
        for pair in c14.chunks_exact_mut(2) {
            pair.copy_from_slice(&[0xC0, 0x10]);
        }
        c14[1] = 0x12;
        let big: Vec<u8> = (0..32768u32).map(|i| (i ^ (i >> 8)) as u8).collect();
        assert_eq!(palette_hash(Format::C14x2, &c14, &big), xxh64(&big[0x20..0x26]));
        assert_eq!(palette_hash(Format::Rgba8, &c8, &palette), 0);
    }
}
