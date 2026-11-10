//! A layout material's program, read by programs.rs.

pub const STAGE_BYTES: usize = 34;
#[path = "material_layout.rs"]
pub mod layout;

/// A layout material's program: its stages, coordinate generators and indirect orders, as many of each as the material has.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct LayoutProgram {
    /// Per stage: coordinate, map, channel; the colour inputs (4), op, bias, scale, clamp, output; the alpha inputs (4), op,
    /// bias, scale, clamp, output; raster swap, texture swap, colour constant, alpha constant; the nine warp bytes.
    pub stages: Vec<[u8; STAGE_BYTES]>,
    /// Per generator: mode bits, row and emboss, the matrix place, then fields the reader does not use (see `normalize`).
    pub generators: Vec<[u8; 8]>,
    /// The indirect orders, four bytes each.
    pub orders: Vec<[u8; 4]>,
    pub swaps: [[u8; 4]; 4],
    pub channel_count: u8,
    /// The first two channels' material sources.
    pub channel_sources: [u8; 2],
    /// Alpha comparison: first, operation, second.
    pub alpha: [u8; 3],
}

impl LayoutProgram {
    /// A stage with nothing set: no coordinate, map or channel, constant 2.
    pub fn new_stage() -> [u8; STAGE_BYTES] {
        let mut stage = [0u8; STAGE_BYTES];
        stage[..3].fill(255);
        stage[23] = 2;
        stage
    }

    /// The generators in the coding the reader expects, and a position or normal source as sent: rows 13 and 14.
    pub fn normalize(&mut self) {
        for g in &mut self.generators {
            if g[5] == 0 {
                let (kind, source) = (g[0], g[1]);
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
                g[0] = mode | (u8::from(kind == 0) << 2) | (u8::from(source <= 3) << 3) | (light << 4);
                g[1] = row | ((emboss & 7) << 4);
                g[5] = 1;
            }
            if g[1] & 15 <= 1 {
                g[1] = (g[1] & 0xF0) | (13 + (g[1] & 15));
            }
        }
    }
}
