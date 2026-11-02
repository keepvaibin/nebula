//! Cook-time material identity. Legacy program bytes stay inside the importer.
//!
//! This is the compatibility key used to prove the MAT3 cooker against captured
//! game materials before replacing their runtime construction.

use anyhow::{bail, Context, Result};
use std::collections::BTreeMap;

pub const PROGRAM_BYTES: usize = 756;

/// Normalize captures made before native lighting and texgen encodings landed.
pub fn normalize_program(bytes: &[u8]) -> Result<[u8; PROGRAM_BYTES]> {
    let mut p: [u8; PROGRAM_BYTES] = bytes
        .try_into()
        .context("material program must contain 756 bytes")?;
    if p[0] > 16 || p[1] > 8 || p[2] > 2 || p[3] > 4 {
        bail!("material counts exceed the source format limits");
    }
    if p[662] == 0 {
        for c in 0..4 {
            let b = 656 + c * 8;
            let attenuation = p[b + 5];
            p[b + 4] = if attenuation == 0 { 0 } else { p[b + 4] & 3 };
            p[b + 5] = match attenuation {
                0 => 1,
                1 => 3,
                _ => 2,
            };
        }
        p[662] = 1;
    }
    for g in 0..usize::from(p[1]) {
        let b = 592 + g * 8;
        if p[b + 5] != 0 {
            continue;
        }
        let (kind, source) = (p[b], p[b + 1]);
        let mut row = 5;
        let mut emboss = 5;
        match source {
            0..=3 => row = [0, 1, 3, 4][usize::from(source)],
            4..=11 => row = source + 1,
            12..=18 => emboss = source - 12,
            19..=20 => row = 2,
            _ => (),
        }
        let (mode, light) = match kind {
            2..=9 => (1, kind - 2),
            10 => (if source == 19 { 2 } else { 3 }, 0),
            _ => (0, 0),
        };
        p[b] = mode | (u8::from(kind == 0) << 2) | (u8::from(source <= 3) << 3) | (light << 4);
        p[b + 1] = row | ((emboss & 7) << 4);
        p[b + 5] = 1;
    }
    Ok(p)
}

/// Exact compatibility with nativeShaderOf and gen_j3d_materials.lookup_bytes.
/// Do not reduce unused stages here: the existing table keys retain those bytes.
pub fn program_key(bytes: &[u8]) -> Result<u64> {
    let mut p = normalize_program(bytes)?;
    for (start, end) in [(4, 8), (12, 16), (689, 712), (716, 724), (724, 756)] {
        p[start..end].fill(0);
    }
    Ok(p.iter().fold(0xcbf29ce484222325u64, |h, &b| {
        (h ^ u64::from(b)).wrapping_mul(0x100000001b3)
    }))
}

/// The build's shader table, passed explicitly so a cook cannot silently use a
/// table from another build. IDs here are the generated, one-based J3D IDs.
pub struct ShaderTable(BTreeMap<u64, u32>);

impl ShaderTable {
    pub fn parse(text: &str) -> Result<Self> {
        let mut entries = BTreeMap::new();
        for line in text
            .lines()
            .map(str::trim)
            .filter(|l| !l.is_empty() && !l.starts_with("//"))
        {
            let (hash, id) = line
                .strip_prefix("{0x")
                .and_then(|s| s.strip_suffix("u},"))
                .and_then(|s| s.split_once("ull, "))
                .context("invalid shader table entry")?;
            let hash = u64::from_str_radix(hash, 16).context("invalid material key")?;
            let id: u32 = id.parse().context("invalid material shader id")?;
            if id == 0 || entries.insert(hash, id).is_some() {
                bail!("zero shader id or duplicate material key");
            }
        }
        if entries.is_empty() {
            bail!("empty material shader table");
        }
        Ok(Self(entries))
    }

    pub fn find(&self, program: &[u8]) -> Result<Option<u32>> {
        Ok(self.0.get(&program_key(program)?).copied())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn state_is_not_shader_identity() {
        let p = [0u8; PROGRAM_BYTES];
        let key = program_key(&p).unwrap();
        for (a, b) in [(4, 8), (12, 16), (689, 712), (716, 756)] {
            for i in a..b {
                let mut q = p;
                q[i] = 255;
                assert_eq!(program_key(&q).unwrap(), key, "byte {i}");
            }
        }
        let mut q = p;
        q[16] = 1;
        assert_ne!(program_key(&q).unwrap(), key);
    }

    #[test]
    fn rejects_bad_inputs() {
        assert!(program_key(&[0; 755]).is_err());
        let mut p = [0; PROGRAM_BYTES];
        p[0] = 17;
        assert!(program_key(&p).is_err());
        for bad in [
            "",
            "{0x00ull, 0u},",
            "{0x00ull, 1u},\n{0x00ull, 2u},",
            "garbage",
        ] {
            assert!(ShaderTable::parse(bad).is_err());
        }
    }
}
