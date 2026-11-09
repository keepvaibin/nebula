//! A disc texture cooked into Nebula's texture record (native/include/nebula_cooked_texture.h): its image decoded
//! once from the disc's tiled formats into texels the renderer samples as they are, palettes applied.
//!
//! The arithmetic is the renderer's own before cooking (texture_cache.cpp's scalar loops): bit replication for 3-, 4-,
//! 5- and 6-bit channels, CMPR's 5/8 + 3/8 colours and its averaged three-colour mode, RGBA8's AR/GB tile halves. The
//! texels are stored in the smallest layout that holds them exactly; the renderer widens them to RGBA8 as it uploads.

use anyhow::{bail, Result};

/// The disc's texel formats.
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

/// A palette's entry format (the file's format number).
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
const VERSION: u32 = 3;
pub const HEADER: usize = 64;
const MAX_LEVELS: u32 = 32;
const LAYOUT_RGBA8: u8 = 0;
const LAYOUT_L8: u8 = 1;
const LAYOUT_LA8: u8 = 2;
const LAYOUT_BLOCK4: u8 = 3;
/// The source format of a record that has no source image (nebula_cooked_texture.h NEBULA_TEXTURE_MADE).
const MADE: u8 = 0xFF;
/// Levels below the first were made here (nebula_cooked_texture.h NEBULA_TEXTURE_GENERATED_LEVELS).
const GENERATED_LEVELS: u8 = 1;

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

/// One 4x4 CMPR block at (ox, oy): DXT1 with big-endian colours, 5/8 + 3/8 interpolation, and its
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

/// xxh64 with seed 0 (texture packs' hash of a texture's level 0; rt_materials.cpp).
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
    while chain < 32 && ((image.width >> chain) | (image.height >> chain)) != 0 {
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

fn texel_count(width: u32, height: u32) -> Result<usize> {
    let count = u64::from(width) * u64::from(height);
    if width == 0 || height == 0 || count > (u32::MAX as u64 - HEADER as u64) / 4 {
        bail!("a {width}x{height} image exceeds the native texture block");
    }
    Ok(count as usize)
}

/// The texture record of a disc image.
pub fn cook(image: &Image) -> Result<Vec<u8>> {
    cook_with(image, LAYOUT_L8, true)
}

/// The record of one sheet of a font in at least layout `wider`: glyphs read it at its own size, so one level.
pub fn cook_sheet(image: &Image, wider: u8) -> Result<Vec<u8>> {
    cook_with(image, wider, false)
}

/// The first level of a disc image without a palette, as RGBA texels.
pub fn texels(image: &Image) -> Result<Vec<[u8; 4]>> {
    let count = texel_count(image.width, image.height)?;
    if image.format.indexed() || held_levels(image) == 0 {
        bail!("a {}x{} {:?} image without its first level, or with a palette", image.width, image.height, image.format);
    }
    let mut out = vec![[0u8; 4]; count];
    decode_level(image, image.data, image.width, image.height, &[], &mut out);
    Ok(out)
}

/// The record of a font sheet made into distance fields (cook::distance): one level, L8 with one field, LA8 with two.
/// It was never a disc image, so no texture pack names it.
pub fn field_record(width: u32, height: u32, two: bool, texels: Vec<[u8; 4]>) -> Result<Vec<u8>> {
    texel_count(width, height)?;
    let layout = if two { LAYOUT_LA8 } else { LAYOUT_L8 };
    record_of(width, height, MADE, 0, layout, [0, 0], &[], &[texels])
}

/// How wide a layout's texels are (L8 < LA8 < RGBA8).
fn rank(layout: u8) -> u8 {
    match layout {
        LAYOUT_L8 => 0,
        LAYOUT_LA8 => 1,
        _ => 2,
    }
}

/// The layout a font sheet's record takes (a font's sheets share the widest of theirs).
pub fn sheet_layout(image: &Image) -> Result<u8> {
    Ok(cook_sheet(image, LAYOUT_L8)?[28])
}

fn cook_with(image: &Image, wider: u8, minified: bool) -> Result<Vec<u8>> {
    texel_count(image.width, image.height)?;
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
    // A level dithered into a checker of two colours, or with one over its detail, loses it here, once.
    let mut filtered = false;
    for (l, level) in texels.iter_mut().enumerate() {
        let (w, h) = ((image.width >> l).max(1) as usize, (image.height >> l).max(1) as usize);
        filtered |= flatten_texel_checker(level, w, h) || remove_texel_checker(level, w, h);
    }
    // A colour image with one level, large enough for its minification to show: its lower levels, for the
    // enhanced-mipmaps option.
    let generate = minified && levels == 1 && matches!(image.format as u8, 4 | 5 | 6 | 8 | 9 | 10 | 14) && (image.width >= 64 || image.height >= 64);
    if generate {
        add_generated_levels(&mut texels, image.width, image.height);
    }
    let fits = fitting_layout(&texels);
    // CMPR stays 4 bits a texel when the GPU can sample its blocks as they are: opaque, whole blocks.
    let mut blocks = Vec::new();
    if image.format == Format::Cmpr && !filtered && image.width % 4 == 0 && image.height % 4 == 0 {
        let mut at = 0usize;
        for l in 0..levels {
            let (w, h) = ((image.width >> l).max(1), (image.height >> l).max(1));
            block4_level(&image.data[at..], w, h, &mut blocks);
            at += image.format.level_bytes(w, h);
        }
        if !blocks_opaque(&blocks) {
            blocks.clear();
        }
    }
    let layout = if !blocks.is_empty() {
        for l in levels..texels.len() as u32 {
            encode_blocks(&texels[l as usize], (image.width >> l).max(1) as usize, (image.height >> l).max(1) as usize, &mut blocks);
        }
        LAYOUT_BLOCK4
    } else if rank(wider) > rank(fits) {
        wider
    } else {
        fits
    };
    let flags = if generate { GENERATED_LEVELS } else { 0 };
    // Its image id groups a model's draws by image (Model.cpp); its name, which packs replace it by, comes from what holds
    // it (name_record).
    let image_id = xxh64(&image.data[..image.format.level_bytes(image.width, image.height)]);
    record_of(image.width, image.height, image.format as u8, flags, layout, [image_id, 0], &blocks, &texels)
}

/// Where a record keeps its image id and its name's hash (nebula_cooked_texture.h).
const IMAGE_ID_AT: usize = 32;
const NAME_HASH_AT: usize = 40;

/// A texture's name as packs and the dump name it, hashed: FNV-1a 64 of its bytes in ASCII lower case (the game's
/// galaxy::textures::name_hash).
pub fn name_hash(name: &str) -> u64 {
    let mut h = 0xcbf2_9ce4_8422_2325u64;
    for b in name.bytes() {
        h ^= u64::from(b.to_ascii_lowercase());
        h = h.wrapping_mul(0x0000_0100_0000_01b3);
    }
    h
}

/// A row of the store's texture names (NebulaData/TextureNames.tsv): the name, the image id, the hash of the palette
/// entries the source image used (0 for one without a palette: how a Dolphin pack's file names one), its size and source
/// format.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct NameRow {
    pub name: String,
    /// The file that holds it, as the store names it (the category of a pack's texture comes from it).
    pub file: String,
    pub image_id: u64,
    pub palette: u64,
    pub width: u32,
    pub height: u32,
    pub format: u8,
}

/// Where the store keeps the names table.
pub const NAMES_PATH: &str = "NebulaData/TextureNames.tsv";

thread_local! {
    static NAMING_FILE: std::cell::RefCell<String> = const { std::cell::RefCell::new(String::new()) };
}

/// While it lives, the file being cooked on this thread, for the names table (a member of an archive inside its own).
pub struct NamingFile(String);

impl NamingFile {
    pub fn set(path: &str) -> NamingFile {
        NamingFile(NAMING_FILE.with(|f| std::mem::replace(&mut *f.borrow_mut(), path.to_string())))
    }
}

impl Drop for NamingFile {
    fn drop(&mut self) {
        NAMING_FILE.with(|f| *f.borrow_mut() = std::mem::take(&mut self.0));
    }
}

/// Set while a store is cooked: every named record's row.
pub static NAMES: std::sync::Mutex<Option<Vec<NameRow>>> = std::sync::Mutex::new(None);

/// Names a record by what holds it: `<model stem>/<texture>` for a model's texture, `<file stem>` for a texture file.
/// `palette`: source_palette_hash of its source. A record the game makes (no source) keeps no name.
pub fn name_record(record: &mut [u8], name: &str, palette: u64) {
    if !is_record(record) || record[29] == MADE || name.is_empty() {
        return;
    }
    record[NAME_HASH_AT..NAME_HASH_AT + 8].copy_from_slice(&name_hash(name).to_le_bytes());
    if let Some(rows) = NAMES.lock().unwrap_or_else(|e| e.into_inner()).as_mut() {
        rows.push(NameRow {
            name: name.to_string(),
            file: NAMING_FILE.with(|f| f.borrow().clone()),
            image_id: u64::from_le_bytes(record[IMAGE_ID_AT..IMAGE_ID_AT + 8].try_into().unwrap()),
            palette,
            width: u32::from_le_bytes(record[16..20].try_into().unwrap()),
            height: u32::from_le_bytes(record[20..24].try_into().unwrap()),
            format: record[29],
        });
    }
}

/// The hash of the palette entries a colour-indexed image uses, as a Dolphin pack's file names it; 0 for the others.
pub fn source_palette_hash(image: &Image) -> u64 {
    let level0 = &image.data[..image.format.level_bytes(image.width, image.height).min(image.data.len())];
    image.palette.map_or(0, |(bytes, _)| palette_hash(image.format, level0, bytes))
}

/// The names table as the store keeps it: sorted, one row a line.
pub fn names_table(rows: &mut Vec<NameRow>) -> Vec<u8> {
    rows.sort();
    rows.dedup();
    let mut out = String::from("name\timage\tpalette\twidth\theight\tformat\tfile\n");
    for r in rows.iter() {
        out.push_str(&format!("{}\t{:016x}\t{:016x}\t{}\t{}\t{}\t{}\n", r.name, r.image_id, r.palette, r.width, r.height, r.format, r.file));
    }
    out.into_bytes()
}

/// The record of an image that was never in a disc format (a mod's PNG): its levels as they are.
pub fn cook_rgba(width: u32, height: u32, rgba: &[u8]) -> Result<Vec<u8>> {
    let pixels = texel_count(width, height)?;
    if rgba.len() != pixels * 4 { bail!("the RGBA image length does not match its dimensions"); }
    let mut texels = vec![rgba.chunks_exact(4).map(|p| [p[0], p[1], p[2], p[3]]).collect::<Vec<_>>()];
    let generate = width >= 64 || height >= 64;
    if generate {
        add_generated_levels(&mut texels, width, height);
    }
    let layout = fitting_layout(&texels);
    let flags = if generate { GENERATED_LEVELS } else { 0 };
    record_of(width, height, Format::Rgba8 as u8, flags, layout, [xxh64(rgba), 0], &[], &texels)
}

fn add_generated_levels(texels: &mut Vec<Vec<[u8; 4]>>, width: u32, height: u32) {
    let chain = 32 - width.max(height).leading_zeros();
    for l in 1..chain.min(MAX_LEVELS) {
        let source = (((width >> (l - 1)).max(1)) as usize, ((height >> (l - 1)).max(1)) as usize);
        let size = (((width >> l).max(1)) as usize, ((height >> l).max(1)) as usize);
        let next = generated_level(texels.last().unwrap(), source, size);
        texels.push(next);
    }
}

fn fitting_layout(texels: &[Vec<[u8; 4]>]) -> u8 {
    let all = texels.iter().flatten();
    if all.clone().all(|t| t[0] == t[1] && t[1] == t[2] && t[2] == t[3]) {
        LAYOUT_L8
    } else if all.clone().all(|t| t[0] == t[1] && t[1] == t[2]) {
        LAYOUT_LA8
    } else {
        LAYOUT_RGBA8
    }
}

#[allow(clippy::too_many_arguments)]
fn record_of(width: u32, height: u32, format: u8, flags: u8, layout: u8, hashes: [u64; 2], blocks: &[u8], texels: &[Vec<[u8; 4]>]) -> Result<Vec<u8>> {
    let mut record = Vec::with_capacity(HEADER + texels.iter().map(|t| t.len() * 4).sum::<usize>());
    record.extend_from_slice(MAGIC);
    record.extend_from_slice(&VERSION.to_le_bytes());
    record.extend_from_slice(&0u32.to_le_bytes()); // size, below
    record.extend_from_slice(&width.to_le_bytes());
    record.extend_from_slice(&height.to_le_bytes());
    record.extend_from_slice(&(texels.len() as u32).to_le_bytes());
    record.push(layout);
    record.push(format);
    record.push(flags);
    record.push(0);
    record.extend_from_slice(&hashes[0].to_le_bytes());
    record.extend_from_slice(&hashes[1].to_le_bytes());
    record.resize(HEADER, 0);
    record.extend_from_slice(blocks);
    for level in texels.iter().filter(|_| layout != LAYOUT_BLOCK4) {
        for t in level {
            match layout {
                LAYOUT_L8 => record.push(t[0]),
                LAYOUT_LA8 => record.extend_from_slice(&[t[0], t[3]]),
                _ => record.extend_from_slice(t),
            }
        }
    }
    let size = u32::try_from(record.len()).map_err(|_| anyhow::anyhow!("native texture block exceeds u32 byte offsets"))?;
    record[12..16].copy_from_slice(&size.to_le_bytes());
    Ok(record)
}

/// One level below `source`: a premultiplied-alpha 2x2 box filter with fixed rounding.
fn generated_level(source: &[[u8; 4]], (sw, sh): (usize, usize), (w, h): (usize, usize)) -> Vec<[u8; 4]> {
    let mut out = vec![[0u8; 4]; w * h];
    for y in 0..h {
        for x in 0..w {
            let (sx, sy) = (x * 2, y * 2);
            if sx + 1 < sw && sy + 1 < sh {
                let top = sy * sw + sx;
                let (p0, p1, p2, p3) = (source[top], source[top + 1], source[top + sw], source[top + sw + 1]);
                if p0[3] == p1[3] && p0[3] == p2[3] && p0[3] == p3[3] {
                    // Equal alpha cancels; fully transparent texels keep zero colour.
                    if p0[3] != 0 {
                        let mean = |c: usize| ((p0[c] as u32 + p1[c] as u32 + p2[c] as u32 + p3[c] as u32 + 2) >> 2) as u8;
                        out[y * w + x] = [mean(0), mean(1), mean(2), p0[3]];
                    }
                    continue;
                }
            }
            let (mut premul, mut alpha, mut count) = ([0u32; 3], 0u32, 0u32);
            for dy in 0..2 {
                for dx in 0..2 {
                    if sy + dy >= sh || sx + dx >= sw {
                        continue;
                    }
                    let t = source[(sy + dy) * sw + sx + dx];
                    for c in 0..3 {
                        premul[c] += t[c] as u32 * t[3] as u32;
                    }
                    alpha += t[3] as u32;
                    count += 1;
                }
            }
            let divisor = if alpha == 0 { count } else { alpha };
            out[y * w + x] = [
                ((premul[0] + divisor / 2) / divisor) as u8,
                ((premul[1] + divisor / 2) / divisor) as u8,
                ((premul[2] + divisor / 2) / divisor) as u8,
                ((alpha + count / 2) / count) as u8,
            ];
        }
    }
    out
}

/// An opaque level as four-colour blocks: end points along the block's main axis of colour.
fn encode_blocks(level: &[[u8; 4]], w: usize, h: usize, out: &mut Vec<u8>) {
    let to565 = |c: [f32; 3]| -> u16 {
        let q = |v: f32, max: f32| ((v / 255.0 * max + 0.5) as u16).min(max as u16);
        (q(c[0], 31.0) << 11) | (q(c[1], 63.0) << 5) | q(c[2], 31.0)
    };
    let from565 = |c: u16| -> [f32; 3] { [expand5(c >> 11) as f32, expand6(c >> 5) as f32, expand5(c) as f32] };
    for by in (0..h).step_by(4) {
        for bx in (0..w).step_by(4) {
            let mut texels = [[0f32; 3]; 16];
            let mut mean = [0f32; 3];
            for i in 0..16 {
                let t = level[(by + i / 4).min(h - 1) * w + (bx + i % 4).min(w - 1)];
                texels[i] = [t[0] as f32, t[1] as f32, t[2] as f32];
                for c in 0..3 {
                    mean[c] += texels[i][c] / 16.0;
                }
            }
            let mut cov = [0f32; 6];
            for t in &texels {
                let d = [t[0] - mean[0], t[1] - mean[1], t[2] - mean[2]];
                for (slot, (a, b)) in cov.iter_mut().zip([(0, 0), (0, 1), (0, 2), (1, 1), (1, 2), (2, 2)]) {
                    *slot += d[a] * d[b];
                }
            }
            let mut axis = [1f32, 1.0, 1.0];
            for _ in 0..4 {
                let next = [
                    cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2],
                    cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2],
                    cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2],
                ];
                let scale = next.iter().fold(0f32, |m, v| m.max(v.abs()));
                if scale <= f32::EPSILON {
                    break;
                }
                axis = [next[0] / scale, next[1] / scale, next[2] / scale];
            }
            let along = |t: &[f32; 3]| t[0] * axis[0] + t[1] * axis[1] + t[2] * axis[2];
            let (mut low, mut high) = (texels[0], texels[0]);
            for t in &texels {
                if along(t) < along(&low) {
                    low = *t;
                }
                if along(t) > along(&high) {
                    high = *t;
                }
            }
            let (mut c0, mut c1) = (to565(high), to565(low));
            if c0 < c1 {
                std::mem::swap(&mut c0, &mut c1);
            }
            let mut indices = 0u32;
            if c0 != c1 {
                let (a, b) = (from565(c0), from565(c1));
                let palette = [
                    a,
                    b,
                    [(2.0 * a[0] + b[0]) / 3.0, (2.0 * a[1] + b[1]) / 3.0, (2.0 * a[2] + b[2]) / 3.0],
                    [(a[0] + 2.0 * b[0]) / 3.0, (a[1] + 2.0 * b[1]) / 3.0, (a[2] + 2.0 * b[2]) / 3.0],
                ];
                for (i, t) in texels.iter().enumerate() {
                    let distance = |p: &[f32; 3]| (t[0] - p[0]).powi(2) + (t[1] - p[1]).powi(2) + (t[2] - p[2]).powi(2);
                    let mut best = 0u32;
                    for (n, p) in palette.iter().enumerate() {
                        if distance(p) < distance(&palette[best as usize]) {
                            best = n as u32;
                        }
                    }
                    indices |= best << (2 * i);
                }
            }
            out.extend_from_slice(&c0.to_le_bytes());
            out.extend_from_slice(&c1.to_le_bytes());
            out.extend_from_slice(&indices.to_le_bytes());
        }
    }
}

/// Whether no block's texel takes the transparent colour of a three-colour block.
fn blocks_opaque(blocks: &[u8]) -> bool {
    blocks.chunks_exact(8).all(|b| {
        let (c0, c1) = (u16::from_le_bytes([b[0], b[1]]), u16::from_le_bytes([b[2], b[3]]));
        let indices = u32::from_le_bytes([b[4], b[5], b[6], b[7]]);
        c0 > c1 || indices & (indices >> 1) & 0x5555_5555 == 0
    })
}

/// A level of nothing but two colours in a checker becomes their mean.
fn flatten_texel_checker(p: &mut [[u8; 4]], w: usize, h: usize) -> bool {
    if w < 4 || h < 4 || p.len() < w * h || p[0] == p[1] {
        return false;
    }
    let (even, odd) = (p[0], p[1]);
    for y in 0..h {
        for x in 0..w {
            if p[y * w + x] != if (x + y) & 1 == 0 { even } else { odd } {
                return false;
            }
        }
    }
    let mut flat = [0u8; 4];
    for c in 0..4 {
        flat[c] = ((even[c] as u32 + odd[c] as u32 + 1) >> 1) as u8;
    }
    p[..w * h].fill(flat);
    true
}

/// A level whose detail is mostly a checker in one phase loses the checker: each colour channel less
/// its local amplitude (alpha stays).
fn remove_texel_checker(p: &mut [[u8; 4]], w: usize, h: usize) -> bool {
    let count = w * h;
    if w < 4 || h < 4 || p.len() < count {
        return false;
    }
    fn detail(p: &[[u8; 4]], w: usize, h: usize, x: usize, y: usize, value: &dyn Fn(&[u8; 4]) -> i32) -> i32 {
        let at = |x: usize, y: usize| value(&p[y * w + x]);
        let sum = at(x.saturating_sub(1), y) + at((x + 1).min(w - 1), y) + at(x, y.saturating_sub(1)) + at(x, (y + 1).min(h - 1));
        let d = 4 * at(x, y) - sum;
        if (x + y) & 1 == 0 { d } else { -d }
    }
    let luma = |t: &[u8; 4]| 54 * t[0] as i32 + 183 * t[1] as i32 + 19 * t[2] as i32;
    let (mut strong, mut phase) = (0usize, 0i64);
    for y in 0..h {
        for x in 0..w {
            let d = detail(p, w, h, x, y, &luma);
            if d >= 8 * 256 || d <= -8 * 256 {
                strong += 1;
                phase += if d > 0 { 1 } else { -1 };
            }
        }
    }
    if strong * 5 < count || (phase.unsigned_abs() as usize) * 100 < strong * 95 {
        return false;
    }
    let mut out: Vec<[u8; 4]> = p[..count].to_vec();
    let mut details = vec![0i32; count];
    for c in 0..3 {
        let channel = move |t: &[u8; 4]| t[c] as i32;
        for y in 0..h {
            for x in 0..w {
                details[y * w + x] = detail(p, w, h, x, y, &channel);
            }
        }
        for y in 0..h {
            for x in 0..w {
                let (x0, x1, y0, y1) = (x.saturating_sub(1), (x + 1).min(w - 1), y.saturating_sub(1), (y + 1).min(h - 1));
                let mut sum = 0i32;
                for sy in y0..=y1 {
                    for sx in x0..=x1 {
                        sum += details[sy * w + sx];
                    }
                }
                let taps = ((x1 - x0 + 1) * (y1 - y0 + 1)) as i32;
                let scale = 8 * taps;
                let amplitude = (if sum >= 0 { sum + scale / 2 } else { sum - scale / 2 }) / scale;
                let value = p[y * w + x][c] as i32 - if (x + y) & 1 == 0 { amplitude } else { -amplitude };
                out[y * w + x][c] = value.clamp(0, 255) as u8;
            }
        }
    }
    p[..count].copy_from_slice(&out);
    true
}

/// One CMPR level as the record's BLOCK4 layout: its 4x4 blocks in row order, ceil(w / 4) across, each 8 bytes:
/// the two RGB565 colours (little-endian) and sixteen 2-bit indices (little-endian, texel (x, y) at bit 2 * (4y + x)),
/// BC1's block. The colours between them: 5/8 + 3/8 in four-colour blocks (c0 > c1), the average and
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

/// Whether `bytes` start with a texture record (an already cooked file).
pub fn is_record(bytes: &[u8]) -> bool {
    bytes.len() >= HEADER && &bytes[..8] == MAGIC && u32::from_le_bytes(bytes[8..12].try_into().unwrap()) == VERSION
}

/// The RGBA texels of a record's level 0 (tests and the cook check).
pub fn record_level0(record: &[u8]) -> Option<(u32, u32, Vec<[u8; 4]>)> {
    if !is_record(record) {
        return None;
    }
    let w = u32::from_le_bytes(record[16..20].try_into().ok()?);
    let h = u32::from_le_bytes(record[20..24].try_into().ok()?);
    let layout = record[28];
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
    fn native_texture_dimensions_do_not_stop_at_source_or_short_limits() {
        let rgba = [255, 0, 127, 255].repeat(70000);
        let record = cook_rgba(70000, 1, &rgba).unwrap();
        let (width, height, values) = record_level0(&record).unwrap();
        assert_eq!((width, height), (70000, 1));
        assert_eq!(values.len(), 70000);
        assert_eq!(values[69999], [255, 0, 127, 255]);
        assert!(cook_rgba(70000, 1, &rgba[..rgba.len()-4]).is_err());
    }

    #[test]
    fn names_hash_without_case() {
        assert_eq!(name_hash(""), 0xcbf2_9ce4_8422_2325);
        assert_eq!(name_hash("a"), 0xaf63_dc4c_8601_ec8c);
        assert_eq!(name_hash("Kuribo/KuriboBody"), name_hash("kuribo/kuribobody"));
        assert_ne!(name_hash("Kuribo/KuriboBody"), name_hash("Kuribo/KuriboBody2"));
    }

    #[test]
    fn a_named_record_holds_its_hash_and_gives_a_row() {
        let data: Vec<u8> = (0..32).collect();
        let image = Image { format: Format::I8, width: 8, height: 4, levels: 1, data: &data, palette: None };
        let mut record = cook(&image).unwrap();
        let image_id = u64::from_le_bytes(record[IMAGE_ID_AT..IMAGE_ID_AT + 8].try_into().unwrap());
        assert_eq!(image_id, xxh64(&data), "the image id is the source's level 0");
        *NAMES.lock().unwrap_or_else(|e| e.into_inner()) = Some(Vec::new());
        {
            let _file = NamingFile::set("ObjectData/NameTest.arc");
            name_record(&mut record, "NameTest/Body", 0);
        }
        let rows = NAMES.lock().unwrap_or_else(|e| e.into_inner()).take().unwrap_or_default();
        assert_eq!(u64::from_le_bytes(record[NAME_HASH_AT..NAME_HASH_AT + 8].try_into().unwrap()), name_hash("NameTest/Body"));
        // Other tests may cook at the same time: only this one's row is looked at.
        let mut mine: Vec<NameRow> = rows.into_iter().filter(|r| r.name == "NameTest/Body").collect();
        assert_eq!(mine.len(), 1);
        assert_eq!(mine[0], NameRow {
            name: "NameTest/Body".into(),
            file: "ObjectData/NameTest.arc".into(),
            image_id,
            palette: 0,
            width: 8,
            height: 4,
            format: Format::I8 as u8,
        });
        mine.push(mine[0].clone());
        let table = String::from_utf8(names_table(&mut mine)).unwrap();
        assert_eq!(
            table,
            format!("name\timage\tpalette\twidth\theight\tformat\tfile\nNameTest/Body\t{image_id:016x}\t0000000000000000\t8\t4\t{}\tObjectData/NameTest.arc\n", Format::I8 as u8),
            "one row (duplicates go), tab separated, the header first"
        );
    }

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
        assert_eq!(record[28], LAYOUT_L8);
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
        assert_eq!(source_palette_hash(&image), xxh64(&palette[6..12]), "the names table's palette: entries 3..5 used");
        assert_eq!(&record[NAME_HASH_AT..NAME_HASH_AT + 8], &[0u8; 8], "no name until what holds it names it");
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
