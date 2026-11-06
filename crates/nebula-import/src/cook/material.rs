//! A layout material's program as bytes, read by programs.rs: gone with the layout format in batch 4.

use anyhow::{bail, Context, Result};

pub const PROGRAM_BYTES: usize = 756;
#[path = "material_layout.rs"]
pub mod layout;

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

