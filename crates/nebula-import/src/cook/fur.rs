//! A fur's tuning (`<material>.fur.txt`: Shift-JIS C source, one value or `{a,b,c,d}` per line, named by the comment
//! after it) as the fixed record the game reads (FurParam.hpp FurFile).
//!
//! Layout, little-endian (132 bytes): "NFUR"; a mask with a bit per tuning value the file gave, in the order below
//! (the game keeps its default for the others); layer count s32; length, length curve, brightness tip, root and
//! curve, alpha tip, root and curve, skin alpha tip, root and curve, density map scale, body map scale (f32 x13);
//! blend colour u8 x4; density f32 x4; thickness f32 x4; length blend u8 x4; light 0 switch, material, ambient,
//! light 1 switch, material, ambient, colour source (s32 x7: the game sets these from every file, 0 when it lacks one).
//! The file's ズレ (indirect) values are left out: the game never draws indirect fur.

use anyhow::{bail, Context, Result};

pub const MAGIC: &[u8; 4] = b"NFUR";
pub const SIZE: usize = 132;

/// A file the cook turns into a fur record.
pub fn is_fur_text(name: &str) -> bool {
    name.to_ascii_lowercase().ends_with(".fur.txt")
}

/// The tuning values with a bit in the mask, in the record's order: (name, how many, integer).
const VALUES: [(&str, usize, Kind); 18] = [
    ("レイヤ数", 1, Kind::Int),
    ("毛長さ", 1, Kind::Float),
    ("長さ偏差", 1, Kind::Float),
    ("明るさ(毛先)", 1, Kind::Float),
    ("明るさ(毛元)", 1, Kind::Float),
    ("明るさ偏差", 1, Kind::Float),
    ("透明度(毛先)", 1, Kind::Float),
    ("透明度(毛元)", 1, Kind::Float),
    ("透明度偏差", 1, Kind::Float),
    ("透明度・地肌(毛先)", 1, Kind::Float),
    ("透明度・地肌(毛元)", 1, Kind::Float),
    ("透明度・地肌偏差", 1, Kind::Float),
    ("密度マップスケール", 1, Kind::Float),
    ("ベースマップスケール", 1, Kind::Float),
    ("混合カラー", 4, Kind::Byte),
    ("植毛密度", 4, Kind::Float),
    ("植毛太さ", 4, Kind::Float),
    ("混合比", 4, Kind::Byte),
];

/// The light values, always written.
const LIGHT: [&str; 7] = ["ライト0スイッチ", "ライト0マテリアル", "ライト0アンビエント", "ライト1スイッチ", "ライト1マテリアル", "ライト1アンビエント", "ライトカラーソース"];

#[derive(Clone, Copy, PartialEq)]
enum Kind {
    Int,
    Float,
    Byte,
}

/// The leading integer of a value ("6", "255", "6.0f" reads 6), as the game's reader takes it.
fn int(text: &str) -> Result<i32> {
    let end = text.char_indices().find(|&(i, c)| !(c.is_ascii_digit() || (i == 0 && (c == '-' || c == '+')))).map_or(text.len(), |(i, _)| i);
    text[..end].parse().with_context(|| format!("{text:?} is not a whole number"))
}

fn float(text: &str) -> Result<f32> {
    let text = text.strip_suffix(['f', 'F']).unwrap_or(text);
    text.parse().with_context(|| format!("{text:?} is not a number"))
}

/// The record for a fur file's bytes.
pub fn cook(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() == SIZE && bytes.starts_with(MAGIC) {
        return Ok(bytes.to_vec());
    }
    let (text, _, unmappable) = encoding_rs::SHIFT_JIS.decode(bytes);
    if unmappable {
        bail!("not Shift-JIS text");
    }
    let mut present = 0u32;
    let mut values = [[0u32; 4]; VALUES.len()];
    let mut light = [0i32; LIGHT.len()];
    for line in text.lines() {
        // The game reads a value on every line whose text holds its name, and passes over the other lines.
        let Some((value, name)) = line.split_once("//") else { continue };
        let value = value.trim().trim_end_matches(',').trim();
        for (index, &(key, count, kind)) in VALUES.iter().enumerate().filter(|(_, (key, _, _))| name.contains(key)) {
            let parts: Vec<&str> = if count == 1 {
                vec![value]
            } else {
                let inner = value.strip_prefix('{').and_then(|v| v.strip_suffix('}')).with_context(|| format!("{key}: {value:?} is not {{a,b,c,d}}"))?;
                inner.split(',').map(str::trim).collect()
            };
            if parts.len() != count {
                bail!("{key}: {count} values wanted, {value:?} has {}", parts.len());
            }
            for (slot, part) in values[index].iter_mut().zip(&parts) {
                *slot = match kind {
                    Kind::Int => int(part).with_context(|| key.to_string())? as u32,
                    Kind::Float => float(part).with_context(|| key.to_string())?.to_bits(),
                    // The game stores the whole number into a byte.
                    Kind::Byte => int(part).with_context(|| key.to_string())? as u8 as u32,
                };
            }
            present |= 1 << index;
        }
        for (index, key) in LIGHT.iter().enumerate().filter(|(_, key)| name.contains(*key)) {
            light[index] = int(value).with_context(|| key.to_string())?;
        }
    }

    let mut out = Vec::with_capacity(SIZE);
    out.extend_from_slice(MAGIC);
    out.extend_from_slice(&present.to_le_bytes());
    for (index, (_, count, kind)) in VALUES.iter().enumerate() {
        for value in &values[index][..*count] {
            match kind {
                Kind::Byte => out.push(*value as u8),
                _ => out.extend_from_slice(&value.to_le_bytes()),
            }
        }
    }
    for value in light {
        out.extend_from_slice(&value.to_le_bytes());
    }
    debug_assert_eq!(out.len(), SIZE);
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    const COLLAR: &str = "static\nFurParam sFurParam_FurCollar_v =\n{\n\t6, // レイヤ数\n\t6.000000f, // 毛長さ\n\t0.778809f, // 長さ偏差\n\t0.000000f, // ズレ(indirect)\n\t1.000000f, // ズレ偏差\n\t14.318848f, // 明るさ(毛先)\n\t9.525146f, // 明るさ(毛元)\n\t0.600000f, // 明るさ偏差  \n\t255.000000f, // 透明度(毛先)\n\t0.000000f, // 透明度(毛元)\n\t0.654297f, // 透明度偏差  \n\t89.772949f, // 透明度・地肌(毛先)\n\t10.334473f, // 透明度・地肌(毛元)\n\t4.267578f, // 透明度・地肌偏差  \n\t2.490234f, //  密度マップスケール\n\t1.000000f, // ベースマップスケール\n\t{0,0,0,0}, // 混合カラー\n\t{0.800000f,0.600000f,0.300000f,0.090000f}, // 植毛密度\n\t{0.517000f,0.386000f,0.364500f,0.171300f}, // 植毛太さ\n\t{0,0,0,0}, // 混合比\n\t3,\t// ライト0スイッチ\n\t255,\t// ライト0マテリアル\n\t45,\t// ライト0アンビエント\n\t0,\t// ライト1スイッチ\n\t255,\t// ライト1マテリアル\n\t0,\t// ライト1アンビエント\n\t2, // ライトカラーソース\n};\n";

    fn f32_at(record: &[u8], at: usize) -> f32 {
        f32::from_le_bytes(record[at..at + 4].try_into().unwrap())
    }

    fn i32_at(record: &[u8], at: usize) -> i32 {
        i32::from_le_bytes(record[at..at + 4].try_into().unwrap())
    }

    #[test]
    fn a_disc_fur_file_cooks_to_its_record() {
        let record = cook(&encoding_rs::SHIFT_JIS.encode(COLLAR).0).unwrap();
        assert_eq!(record.len(), SIZE);
        assert_eq!(&record[..4], MAGIC);
        assert_eq!(i32_at(&record, 4), (1 << 18) - 1);
        assert_eq!(i32_at(&record, 8), 6);
        assert_eq!(f32_at(&record, 12), 6.0);
        assert_eq!(f32_at(&record, 16), 0.778809);
        assert_eq!(f32_at(&record, 20), 14.318848);
        assert_eq!(f32_at(&record, 56), 2.490234);
        assert_eq!(f32_at(&record, 60), 1.0);
        assert_eq!(&record[64..68], &[0, 0, 0, 0]);
        assert_eq!(f32_at(&record, 68), 0.8);
        assert_eq!(f32_at(&record, 96), 0.1713);
        let light: Vec<i32> = (0..7).map(|i| i32_at(&record, 104 + 4 * i)).collect();
        assert_eq!(light, [3, 255, 45, 0, 255, 0, 2]);
    }

    #[test]
    fn a_missing_value_keeps_the_games_default_and_a_missing_light_value_is_0() {
        let text = COLLAR.replace("\t2.490234f, //  密度マップスケール\n", "").replace("\t2, // ライトカラーソース\n", "");
        let record = cook(&encoding_rs::SHIFT_JIS.encode(&text).0).unwrap();
        assert_eq!(i32_at(&record, 4), ((1 << 18) - 1) & !(1 << 12));
        assert_eq!(i32_at(&record, 128), 0);
    }

    #[test]
    fn other_lines_are_passed_over_and_a_bad_value_is_an_import_error() {
        let record = cook(&encoding_rs::SHIFT_JIS.encode(&COLLAR.replace("毛長さ", "毛の長さ")).0).unwrap();
        assert_eq!(i32_at(&record, 4), ((1 << 18) - 1) & !(1 << 1));
        assert!(cook(&encoding_rs::SHIFT_JIS.encode(&COLLAR.replace("{0.517000f,0.386000f,", "{0.517000f,")).0).is_err());
        assert!(cook(&encoding_rs::SHIFT_JIS.encode(&COLLAR.replace("6, // レイヤ数", "six, // レイヤ数")).0).is_err());
    }
}
