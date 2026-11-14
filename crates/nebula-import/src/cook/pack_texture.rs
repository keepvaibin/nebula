//! A texture pack's picture (`layers/<layer>/textures/**.png` or `.dds`) as the game's pack reader takes it
//! (runtime/src/render/texture_pack.cpp `parse_dds`, texture_pack_codecs.cpp `load_image`): the same files read, the
//! same refused. The check before a pack and the game's start ask this one function.

use super::refuse::refuse;
use anyhow::Result;

/// The longest side either form may have (the reader's STBI_MAX_DIMENSIONS, and its DDS limit).
pub const MAX_SIDE: u32 = 16384;

fn read32(bytes: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap())
}

const fn fourcc(code: &[u8; 4]) -> u32 {
    u32::from_le_bytes(*code)
}

/// Bytes of one 4x4 block of a DXGI format the reader takes; 0 for 4 bytes a pixel; None for one it does not take.
fn dxgi_block(dxgi: u32) -> Option<u32> {
    match dxgi {
        70..=72 | 79 | 80 => Some(8),
        73..=78 | 82 | 83 | 97..=99 => Some(16),
        27..=29 | 87 | 90 | 91 => Some(0),
        _ => None,
    }
}

fn side(image: &str, width: u32, height: u32) -> anyhow::Error {
    refuse("Cook.Image.Side", &[("image", image.to_string()), ("width", width.to_string()), ("height", height.to_string()), ("max", MAX_SIDE.to_string())])
}

fn unreadable(image: &str) -> anyhow::Error {
    refuse("Cook.Image.Unreadable", &[("image", image.to_string())])
}

/// A DDS as `parse_dds` reads it: the header, a form it decodes, a side of 1 to 16384, one picture deep, and at least
/// the first level's bytes in the file.
fn dds(image: &str, file: &[u8]) -> Result<()> {
    if file.len() < 128 || read32(file, 0) != fourcc(b"DDS ") || read32(file, 4) != 124 {
        return Err(unreadable(image));
    }
    let height = read32(file, 12);
    let width = read32(file, 16);
    let depth = read32(file, 24);
    let flags = read32(file, 80);
    let code = read32(file, 84);
    let bits = read32(file, 88);
    let r_mask = read32(file, 92);
    let a_mask = read32(file, 104);
    let mut offset = 128usize;
    let block = if flags & 0x4 != 0 {
        if code == fourcc(b"DX10") {
            if file.len() < 148 {
                return Err(unreadable(image));
            }
            if read32(file, 132) != 3 || read32(file, 140) > 1 {
                return Err(refuse("Cook.Texture.DdsForm", &[("image", image.to_string())]));
            }
            offset = 148;
            dxgi_block(read32(file, 128))
        } else if code == fourcc(b"DXT1") || code == fourcc(b"ATI1") || code == fourcc(b"BC4U") {
            Some(8)
        } else if [b"DXT2", b"DXT3", b"DXT4", b"DXT5", b"ATI2", b"BC5U"].iter().any(|c| code == fourcc(c)) {
            Some(16)
        } else {
            None
        }
    } else if flags & 0x40 != 0 && bits == 32 && a_mask == 0xFF00_0000 && (r_mask == 0x00FF_0000 || r_mask == 0x0000_00FF) {
        Some(0)
    } else {
        None
    };
    let Some(block) = block else { return Err(refuse("Cook.Texture.DdsForm", &[("image", image.to_string())])) };
    if depth > 1 {
        return Err(refuse("Cook.Texture.DdsForm", &[("image", image.to_string())]));
    }
    if width == 0 || height == 0 {
        return Err(unreadable(image));
    }
    if width > MAX_SIDE || height > MAX_SIDE {
        return Err(side(image, width, height));
    }
    let (row_bytes, rows) = if block != 0 { (width.div_ceil(4) * block, height.div_ceil(4)) } else { (width * 4, height) };
    if offset + row_bytes as usize * rows as usize > file.len() {
        return Err(unreadable(image));
    }
    Ok(())
}

/// A PNG as stb_image reads it (PNG only, no checksum checked, a side up to 16384): every row decoded.
fn png(image: &str, file: &[u8]) -> Result<()> {
    if file.len() > i32::MAX as usize {
        return Err(unreadable(image));
    }
    let mut decoder = png::Decoder::new_with_limits(file, png::Limits { bytes: usize::MAX });
    decoder.ignore_checksums(true);
    let mut reader = decoder.read_info().map_err(|_| unreadable(image))?;
    let (width, height) = (reader.info().width, reader.info().height);
    if width > MAX_SIDE || height > MAX_SIDE {
        return Err(side(image, width, height));
    }
    if width == 0 || height == 0 {
        return Err(unreadable(image));
    }
    while reader.next_row().map_err(|_| unreadable(image))?.is_some() {}
    Ok(())
}

/// Whether the pack reader takes this picture: Ok, or its refusal naming `image` (the file as the mod names it). A
/// file of another ending is no pack picture, and is not asked about.
pub fn check(image: &str, bytes: &[u8]) -> Result<()> {
    let lower = image.to_ascii_lowercase();
    if lower.ends_with(".dds") {
        dds(image, bytes)
    } else if lower.ends_with(".png") {
        png(image, bytes)
    } else {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The set of pictures the game's own test reads through its pack reader (runtime/src/render/texture_pack_tests.cpp,
    /// test_pack_pictures) with `answers.tsv`: each is taken or refused here as it is there.
    #[test]
    fn the_shared_pictures_are_taken_as_the_game_takes_them() {
        let folder = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("data/pack-pictures");
        let answers = std::fs::read_to_string(folder.join("answers.tsv")).unwrap();
        let mut count = 0;
        for line in answers.lines().skip(1).filter(|l| !l.is_empty()) {
            let (file, taken) = line.split_once('\t').unwrap();
            let bytes = std::fs::read(folder.join(file)).unwrap();
            assert_eq!(check(file, &bytes).is_ok(), taken == "1", "{file}: {}", said(file, &bytes));
            count += 1;
        }
        assert_eq!(count, 23);
    }

    /// The phrase a refusal names, or "" for a file taken.
    fn said(name: &str, bytes: &[u8]) -> &'static str {
        match check(name, bytes) {
            Ok(()) => "",
            Err(e) => match super::super::refuse::refusal_of(&e).and_then(|f| f.args.first()) {
                Some(("why", luma_validate::Arg::Phrase(why))) => *why,
                _ => "not a refusal",
            },
        }
    }

    fn dds_file(width: u32, height: u32, flags: u32, code: &[u8; 4], bits: u32, r: u32, a: u32, data: usize) -> Vec<u8> {
        let mut f = vec![0u8; 128];
        f[..4].copy_from_slice(b"DDS ");
        f[4..8].copy_from_slice(&124u32.to_le_bytes());
        f[12..16].copy_from_slice(&height.to_le_bytes());
        f[16..20].copy_from_slice(&width.to_le_bytes());
        f[80..84].copy_from_slice(&flags.to_le_bytes());
        f[84..88].copy_from_slice(code);
        f[88..92].copy_from_slice(&bits.to_le_bytes());
        f[92..96].copy_from_slice(&r.to_le_bytes());
        f[104..108].copy_from_slice(&a.to_le_bytes());
        f.resize(128 + data, 0);
        f
    }

    fn dx10(dxgi: u32, dimension: u32, array: u32, width: u32, data: usize) -> Vec<u8> {
        let mut f = dds_file(width, width, 4, b"DX10", 0, 0, 0, 0);
        for word in [dxgi, dimension, 0, array, 0] {
            f.extend_from_slice(&word.to_le_bytes());
        }
        f.resize(148 + data, 0);
        f
    }

    fn png_file(width: u32, height: u32) -> Vec<u8> {
        let mut out = Vec::new();
        let mut encoder = png::Encoder::new(&mut out, width, height);
        encoder.set_color(png::ColorType::Rgba);
        let mut writer = encoder.write_header().unwrap();
        writer.write_image_data(&vec![7u8; (width * height * 4) as usize]).unwrap();
        drop(writer);
        out
    }

    #[test]
    fn the_pack_reader_takes_what_it_decodes_and_refuses_the_rest() {
        // DDS: the forms parse_dds takes, each with its first level whole.
        assert_eq!(said("a.dds", &dds_file(4, 4, 4, b"DXT1", 0, 0, 0, 8)), "");
        assert_eq!(said("a.DDS", &dds_file(8, 4, 4, b"DXT5", 0, 0, 0, 32)), "");
        assert_eq!(said("a.dds", &dds_file(2, 2, 0x40, b"\0\0\0\0", 32, 0x00FF_0000, 0xFF00_0000, 16)), "");
        assert_eq!(said("a.dds", &dds_file(2, 2, 0x40, b"\0\0\0\0", 32, 0x0000_00FF, 0xFF00_0000, 16)), "");
        assert_eq!(said("a.dds", &dx10(98, 3, 1, 4, 16)), "");
        assert_eq!(said("a.dds", &dx10(28, 3, 0, 1, 4)), "");
        // A level cut short, a damaged header, no picture at all.
        assert_eq!(said("a.dds", &dds_file(4, 4, 4, b"DXT1", 0, 0, 0, 7)), "Cook.Image.Unreadable");
        assert_eq!(said("a.dds", &[0u8; 100]), "Cook.Image.Unreadable");
        assert_eq!(said("a.dds", b""), "Cook.Image.Unreadable");
        assert_eq!(said("a.dds", &dds_file(0, 4, 4, b"DXT1", 0, 0, 0, 8)), "Cook.Image.Unreadable");
        // A form it does not decode: 16 bits a pixel, another fourcc or DXGI form, a volume, an array, a cube.
        assert_eq!(said("a.dds", &dds_file(2, 2, 0x40, b"\0\0\0\0", 16, 0xF800, 0, 8)), "Cook.Texture.DdsForm");
        assert_eq!(said("a.dds", &dds_file(4, 4, 4, b"BC6H", 0, 0, 0, 16)), "Cook.Texture.DdsForm");
        assert_eq!(said("a.dds", &dx10(95, 3, 1, 4, 16)), "Cook.Texture.DdsForm");
        assert_eq!(said("a.dds", &dx10(98, 3, 2, 4, 16)), "Cook.Texture.DdsForm");
        assert_eq!(said("a.dds", &dx10(98, 4, 1, 4, 16)), "Cook.Texture.DdsForm");
        let mut volume = dds_file(4, 4, 4, b"DXT1", 0, 0, 0, 8);
        volume[24..28].copy_from_slice(&2u32.to_le_bytes());
        assert_eq!(said("a.dds", &volume), "Cook.Texture.DdsForm");
        assert_eq!(said("a.dds", &dds_file(16385, 4, 4, b"DXT1", 0, 0, 0, 8)), "Cook.Image.Side");
        // PNG: decoded whole; a checksum is not checked, as stb_image does not.
        let good = png_file(3, 2);
        assert_eq!(said("a.png", &good), "");
        let mut crc = good.clone();
        let end = crc.len() - 1;
        crc[end - 12] ^= 0xFF;
        assert_eq!(said("a.png", &crc), "");
        assert_eq!(said("a.png", &good[..good.len() - 20]), "Cook.Image.Unreadable");
        assert_eq!(said("a.png", b"GIF89a"), "Cook.Image.Unreadable");
        assert_eq!(said("a.png", b""), "Cook.Image.Unreadable");
        let mut wide = good.clone();
        wide[16..20].copy_from_slice(&16385u32.to_be_bytes());
        assert_eq!(said("a.png", &wide), "Cook.Image.Side");
        // Not a pack picture: not asked about.
        assert_eq!(said("a.txt", b""), "");
    }
}
