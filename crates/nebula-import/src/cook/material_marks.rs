//! What the generated programs need to know of a material's maps that its data does not say: which maps read through a
//! normal-made coordinate hold a colour ramp (they keep their own fetch), which are painted highlight sprites (no
//! environment's), and which materials drawn blended by alpha stay opaque. A material's stages are run here on the CPU
//! (the same combiner the generated programs run) with chosen inputs.

use super::model_ir::{MaterialIr, ModelIr};

const FRACTION: [f32; 8] = [1.0, 223.0 / 255.0, 191.0 / 255.0, 159.0 / 255.0, 128.0 / 255.0, 96.0 / 255.0, 64.0 / 255.0, 32.0 / 255.0];

fn constant_colour(m: &MaterialIr, sel: u8) -> [f32; 3] {
    let k = |n: usize, c: usize| f32::from(m.constants[n & 3][c]) / 255.0;
    match sel {
        0..=7 => [FRACTION[sel as usize]; 3],
        0x0C..=0x0F => [k((sel - 0x0C) as usize, 0), k((sel - 0x0C) as usize, 1), k((sel - 0x0C) as usize, 2)],
        0x10..=0x1F => [k(((sel - 0x10) % 4) as usize, ((sel - 0x10) / 4) as usize); 3],
        _ => [0.0; 3],
    }
}

fn constant_alpha(m: &MaterialIr, sel: u8) -> f32 {
    match sel {
        0..=7 => FRACTION[sel as usize],
        0x10..=0x1F => f32::from(m.constants[((sel - 0x10) % 4) as usize][((sel - 0x10) / 4) as usize]) / 255.0,
        _ => 0.0,
    }
}

fn swapped(v: [f32; 4], table: &[u8; 4]) -> [f32; 4] {
    [v[table[0] as usize & 3], v[table[1] as usize & 3], v[table[2] as usize & 3], v[table[3] as usize & 3]]
}

/// A register read as an operand: its low 8 bits (a value an unclamped stage left past 1 or below 0 wraps).
fn low8(x: f32) -> f32 {
    x - (x * 255.0 / 256.0).floor() * 256.0 / 255.0
}

/// The material's stages run once: `map(n)` is map n's sample, `raster(n)` colour channel n as the rasteriser hands it
/// over. The generated programs' combiner (programs.rs `color_stage`, `alpha_stage`), without warps.
pub fn evaluate(m: &MaterialIr, map: &dyn Fn(u8) -> [f32; 4], raster: &dyn Fn(u8) -> [f32; 4]) -> [f32; 4] {
    let mut reg = [[1.0f32; 4]; 4];
    for r in 1..4 {
        for c in 0..4 {
            reg[r][c] = f32::from(m.registers[r - 1][c]) / 255.0;
        }
    }
    let count = (m.pixel_stage_count as usize).min(m.stages.len());
    for s in &m.stages[..count] {
        let t = if s.map != 0xFF { swapped(map(s.map), &m.swaps[s.texture_swap as usize & 3]) } else { [0.0; 4] };
        let ras = swapped(raster(s.channel), &m.swaps[s.raster_swap as usize & 3]);
        let k = constant_colour(m, s.color_constant);
        let ka = constant_alpha(m, s.alpha_constant);
        let colour = |sel: u8, operand: bool| -> [f32; 3] {
            let of = |r: usize| {
                let v = [reg[r][0], reg[r][1], reg[r][2]];
                if operand { v.map(low8) } else { v }
            };
            let alpha_of = |r: usize| [if operand { low8(reg[r][3]) } else { reg[r][3] }; 3];
            match sel & 15 {
                0 => of(0),
                1 => alpha_of(0),
                2 => of(1),
                3 => alpha_of(1),
                4 => of(2),
                5 => alpha_of(2),
                6 => of(3),
                7 => alpha_of(3),
                8 => [t[0], t[1], t[2]],
                9 => [t[3]; 3],
                10 => [ras[0], ras[1], ras[2]],
                11 => [ras[3]; 3],
                12 => [1.0; 3],
                13 => [128.0 / 255.0; 3],
                14 => k,
                _ => [0.0; 3],
            }
        };
        let alpha = |sel: u8, operand: bool| -> f32 {
            match sel & 7 {
                r @ 0..=3 => {
                    let v = reg[r as usize][3];
                    if operand { low8(v) } else { v }
                }
                4 => t[3],
                5 => ras[3],
                6 => ka,
                _ => 0.0,
            }
        };
        let finish = |v: f32, bias: u8, scale: u8, clamp: u8| {
            let v = match bias % 3 {
                1 => v + 128.0 / 255.0,
                2 => v - 128.0 / 255.0,
                _ => v,
            };
            let v = match scale & 3 {
                1 => v * 2.0,
                2 => v * 4.0,
                3 => v * 0.5,
                _ => v,
            };
            if clamp != 0 { v.clamp(0.0, 1.0) } else { v }
        };
        let (a, b, c, d) = (colour(s.color_in[0], true), colour(s.color_in[1], true), colour(s.color_in[2], true), colour(s.color_in[3], false));
        let mut out = [0.0f32; 4];
        if s.color_op <= 1 {
            for i in 0..3 {
                let mix = a[i] + (b[i] - a[i]) * c[i];
                out[i] = finish(if s.color_op == 1 { d[i] - mix } else { d[i] + mix }, s.color_bias, s.color_scale, s.color_clamp);
            }
        } else {
            // Compare ops: on the red channel, on green and red, on all three, or on each channel.
            let round = |x: f32| (x * 255.0).round();
            let pack = |x: [f32; 3]| match s.color_op & !1 {
                8 => round(x[0]),
                10 => round(x[1]) * 256.0 + round(x[0]),
                12 => round(x[0]) + round(x[1]) * 256.0 + round(x[2]) * 65536.0,
                _ => 0.0,
            };
            for i in 0..3 {
                let passes = if s.color_op >= 14 {
                    if s.color_op == 14 { round(a[i]) > round(b[i]) } else { round(a[i]) == round(b[i]) }
                } else if s.color_op % 2 == 0 {
                    pack(a) > pack(b)
                } else {
                    pack(a) == pack(b)
                };
                let v = d[i] + if passes { c[i] } else { 0.0 };
                out[i] = if s.color_clamp != 0 { v.clamp(0.0, 1.0) } else { v };
            }
        }
        let (aa, ab, ac, ad) = (alpha(s.alpha_in[0], true), alpha(s.alpha_in[1], true), alpha(s.alpha_in[2], true), alpha(s.alpha_in[3], false));
        out[3] = if s.alpha_op <= 1 {
            let mix = aa + (ab - aa) * ac;
            finish(if s.alpha_op == 1 { ad - mix } else { ad + mix }, s.alpha_bias, s.alpha_scale, s.alpha_clamp)
        } else {
            let round = |x: f32| (x * 255.0).round();
            let passes = if s.alpha_op % 2 == 0 { round(aa) > round(ab) } else { round(aa) == round(ab) };
            let v = ad + if passes { ac } else { 0.0 };
            if s.alpha_clamp != 0 { v.clamp(0.0, 1.0) } else { v }
        };
        let (to_colour, to_alpha) = (s.color_out as usize & 3, s.alpha_out as usize & 3);
        reg[to_colour][..3].copy_from_slice(&out[..3]);
        reg[to_alpha][3] = out[3];
    }
    reg[0]
}

/// What a map read through a normal-made coordinate holds. Each
/// texel's colour against the means of its row, its column and its ring (16 rings about the centre): the share of the
/// map's colour variance each explains; the texels' mean saturation; the span of their hues (360 less the widest run of
/// empty tens of degrees among the saturated texels, a ten under 2 % of them empty); the share that is dark.
#[derive(Clone, Copy, Default, Debug)]
pub struct MapShape {
    pub rows: f64,
    pub columns: f64,
    pub rings: f64,
    pub saturation: f64,
    pub hue_span: f64,
    pub dark: f64,
    pub spread: f64,
    /// The brightest texel's luminance.
    pub peak: f64,
}

pub fn map_shape(width: u32, height: u32, texels: &[[u8; 4]]) -> Option<MapShape> {
    let (w, h) = (width as usize, height as usize);
    if w == 0 || h == 0 || texels.len() < w * h {
        return None;
    }
    const RINGS: usize = 16;
    let rgb = |t: [u8; 4]| [f64::from(t[0]) / 255.0, f64::from(t[1]) / 255.0, f64::from(t[2]) / 255.0];
    let ring = |x: usize, y: usize| {
        let (dx, dy) = ((x as f64 + 0.5) / w as f64 - 0.5, (y as f64 + 0.5) / h as f64 - 0.5);
        (((dx * dx + dy * dy).sqrt() / 0.7072) * RINGS as f64).min(RINGS as f64 - 1.0) as usize
    };
    let mut all = [0.0f64; 3];
    let mut row = vec![[0.0f64; 3]; h];
    let mut column = vec![[0.0f64; 3]; w];
    let mut rings = vec![[0.0f64; 4]; RINGS];
    for y in 0..h {
        for x in 0..w {
            let c = rgb(texels[y * w + x]);
            let r = ring(x, y);
            for k in 0..3 {
                all[k] += c[k];
                row[y][k] += c[k];
                column[x][k] += c[k];
                rings[r][k] += c[k];
            }
            rings[r][3] += 1.0;
        }
    }
    let n = (w * h) as f64;
    let mean = all.map(|v| v / n);
    let (mut total, mut by_row, mut by_column, mut by_ring) = (0.0f64, 0.0f64, 0.0f64, 0.0f64);
    let mut hues = [0.0f64; 36];
    let (mut saturation, mut saturated, mut dark) = (0.0f64, 0.0f64, 0.0f64);
    for y in 0..h {
        for x in 0..w {
            let c = rgb(texels[y * w + x]);
            let r = ring(x, y);
            for k in 0..3 {
                total += (c[k] - mean[k]).powi(2);
                by_row += (c[k] - row[y][k] / w as f64).powi(2);
                by_column += (c[k] - column[x][k] / h as f64).powi(2);
                by_ring += (c[k] - rings[r][k] / rings[r][3]).powi(2);
            }
            let cf = [c[0] as f32, c[1] as f32, c[2] as f32];
            let (hue, sat) = hue_saturation(cf);
            saturation += f64::from(sat);
            if sat > 0.3 {
                hues[(hue as usize / 10).min(35)] += 1.0;
                saturated += 1.0;
            }
            if luminance(cf) < 0.1 {
                dark += 1.0;
            }
        }
    }
    let explained = |rest: f64| if total > 1e-9 { 1.0 - rest / total } else { 1.0 };
    let lum = |t: [u8; 4]| (0.2126 * f64::from(t[0]) + 0.7152 * f64::from(t[1]) + 0.0722 * f64::from(t[2])) / 255.0;
    let peak = texels[..w * h].iter().map(|&t| lum(t)).fold(0.0f64, f64::max);
    let hue_span = if saturated < 1.0 {
        0.0
    } else {
        let full: Vec<bool> = hues.iter().map(|&c| c >= 0.02 * saturated).collect();
        let (mut widest, mut run) = (0usize, 0usize);
        for i in 0..72 {
            if full[i % 36] {
                run = 0;
            } else {
                run += 1;
                widest = widest.max(run.min(36));
            }
        }
        f64::from(360 - 10 * widest as u32)
    };
    Some(MapShape {
        rows: explained(by_row),
        columns: explained(by_column),
        rings: explained(by_ring),
        saturation: saturation / n,
        hue_span,
        dark: dark / n,
        spread: (total / (3.0 * n)).sqrt(),
        peak,
    })
}

/// A painted highlight sprite: a light spot on a dark ground (at least 0.6 of it dark), not a ramp.
pub fn is_highlight(s: &MapShape) -> bool {
    const SPRITE_DARK: f64 = 0.6;
    s.dark >= SPRITE_DARK && !is_ramp(s) && s.peak > 0.05
}

/// Whether a map is ADDED to the stages' colour (a highlight) and not multiplied in (a mask, which hiding would
/// blacken): the result with the map black keeps a quarter or more of its brightness with the map mid grey (the other
/// maps and the rasterised colours mid grey).
fn sprite_adds(m: &MaterialIr, sprite: u8) -> bool {
    let raster = |_: u8| [0.5f32, 0.5, 0.5, 1.0];
    let at = |v: f32| {
        let c = evaluate(m, &move |n: u8| if n == sprite { [v, v, v, 1.0] } else { [0.5, 0.5, 0.5, 1.0] }, &raster);
        luminance([c[0], c[1], c[2]])
    };
    let (off, on) = (at(0.0), at(0.5));
    on <= 1e-3 || off >= 0.25 * on
}

/// Each material's normal-indexed maps that are painted highlight sprites (MaterialIr highlight_maps).
pub fn mark_highlights(ir: &mut ModelIr) {
    let found: Vec<Vec<u8>> = ir
        .materials
        .iter()
        .map(|m| {
            normal_maps(m)
                .into_iter()
                .filter(|map| !m.ramp_maps.contains(map))
                .filter(|&map| sprite_adds(m, map))
                .filter(|&map| map_texels(ir, m, map).and_then(|(w, h, t)| map_shape(w, h, &t)).is_some_and(|s| is_highlight(&s)))
                .collect()
        })
        .collect();
    for (m, sprites) in ir.materials.iter_mut().zip(found) {
        m.highlight_maps = sprites;
    }
}

/// A colour ramp, not a picture. One axis or the radius explains kRampExplained of its colour, its hues
/// span kRampHues degrees, under kRampDark of it is dark, and it varies at all (a flat map is neither).
pub fn is_ramp(s: &MapShape) -> bool {
    const RAMP_EXPLAINED: f64 = 0.9;
    const RAMP_HUES: f64 = 60.0;
    const RAMP_DARK: f64 = 0.2;
    s.rows.max(s.columns).max(s.rings) >= RAMP_EXPLAINED && s.hue_span >= RAMP_HUES && s.dark < RAMP_DARK && s.spread > 0.02
}

/// A normal-indexed map's texels: level 0 of the record of the material's map `map`.
fn map_texels(ir: &ModelIr, m: &MaterialIr, map: u8) -> Option<(u32, u32, Vec<[u8; 4]>)> {
    let t = ir.textures.get(*m.textures.get(usize::from(map))? as usize)?;
    super::texture::record_level0(ir.records.get(t.record)?)
}

/// Each material's normal-indexed maps that hold a colour ramp (MaterialIr ramp_maps).
pub fn mark_ramps(ir: &mut ModelIr) {
    let found: Vec<Vec<u8>> = ir
        .materials
        .iter()
        .map(|m| {
            normal_maps(m)
                .into_iter()
                .filter(|&map| map_texels(ir, m, map).and_then(|(w, h, t)| map_shape(w, h, &t)).is_some_and(|s| is_ramp(&s)))
                .collect()
        })
        .collect();
    for (m, ramps) in ir.materials.iter_mut().zip(found) {
        m.ramp_maps = ramps;
    }
}

/// The maps a material reads through a coordinate made from the normal.
fn normal_maps(m: &MaterialIr) -> Vec<u8> {
    let mut maps = Vec::new();
    let count = (m.pixel_stage_count as usize).min(m.stages.len());
    for s in &m.stages[..count] {
        if s.map == 0xFF {
            continue;
        }
        if let Some(c) = m.coords.get(s.coord as usize) {
            if c.kind != 10 && c.source == 1 && !maps.contains(&s.map) {
                maps.push(s.map);
            }
        }
    }
    maps
}

fn luminance(c: [f32; 3]) -> f32 {
    0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
}

fn hue_saturation(c: [f32; 3]) -> (f32, f32) {
    let (high, low) = (c[0].max(c[1]).max(c[2]), c[0].min(c[1]).min(c[2]));
    if high <= 1e-4 || high - low <= 1e-4 {
        return (0.0, 0.0);
    }
    let span = high - low;
    let hue = if high == c[0] {
        60.0 * (((c[1] - c[2]) / span) % 6.0)
    } else if high == c[1] {
        60.0 * ((c[2] - c[0]) / span + 2.0)
    } else {
        60.0 * ((c[0] - c[1]) / span + 4.0)
    };
    (if hue < 0.0 { hue + 360.0 } else { hue }, span / high)
}

/// Each material drawn blended by alpha whose alpha stays at 0.9 or more (MaterialIr fade_opaque): its stages run
/// with every map's alpha at the low or high end of its texels' and the vertices' alpha at the low or high end of the
/// model's colour arrays (four ways: maps low or high by vertices low or high); the least of them. A map whose texels
/// cannot be read counts 0 to 1. A lit alpha channel is taken at full light (the game's lights carry alpha 255).
pub fn mark_fades(ir: &mut ModelIr) {
    let range = |colours: &[u32]| {
        if colours.is_empty() {
            return [1.0f32, 1.0];
        }
        colours.iter().fold([1.0f32, 0.0], |[lo, hi], &c| {
            let a = (c & 0xFF) as f32 / 255.0;
            [lo.min(a), hi.max(a)]
        })
    };
    let vertex = [range(&ir.arrays.colors[0]), range(&ir.arrays.colors[1])];
    let found: Vec<Option<f32>> = ir
        .materials
        .iter()
        .map(|m| {
            if (m.blend_type, m.blend_src, m.blend_dst) != (1, 4, 5) {
                return None;
            }
            let channels = 2 * (m.channel_count as usize).min(2);
            let maps: Vec<[f32; 2]> = (0..m.textures.len())
                .map(|map| {
                    map_texels(ir, m, map as u8).map_or([0.0, 1.0], |(_, _, t)| {
                        t.iter().fold([1.0f32, 0.0], |[lo, hi], p| {
                            let a = f32::from(p[3]) / 255.0;
                            [lo.min(a), hi.max(a)]
                        })
                    })
                })
                .collect();
            let at = |map_end: usize, vertex_end: usize| {
                let raster = move |code: u8| -> [f32; 4] {
                    let n = match code {
                        0 | 4 => 0usize,
                        1 | 5 => 1usize,
                        _ => return [0.0; 4],
                    };
                    if n * 2 >= channels {
                        return [1.0; 4];
                    }
                    let alpha = if m.channels[n * 2 + 1].material_source != 0 {
                        vertex[n][vertex_end]
                    } else {
                        f32::from(m.material_color[n][3]) / 255.0
                    };
                    [0.5, 0.5, 0.5, alpha]
                };
                let map = |n: u8| [0.5, 0.5, 0.5, maps.get(usize::from(n)).map_or(1.0, |r| r[map_end])];
                evaluate(m, &map, &raster)[3]
            };
            let ends: Vec<f32> = [(0, 0), (0, 1), (1, 0), (1, 1)].iter().map(|&(a, b)| at(a, b)).collect();
            Some(ends.iter().copied().fold(f32::MAX, f32::min))
        })
        .collect();
    for (m, fade) in ir.materials.iter_mut().zip(found) {
        m.fade_opaque = fade.is_some_and(|low| low >= 0.9);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::cook::model_ir::StageIr;

    fn stage(colour: [u8; 4], alpha: [u8; 4]) -> StageIr {
        StageIr { coord: 0xFF, map: 0xFF, channel: 0xFF, color_in: colour, alpha_in: alpha, color_clamp: 1, alpha_clamp: 1, ..Default::default() }
    }

    #[test]
    fn a_stage_passes_its_raster_colour() {
        // d = raster colour, a = b = c = zero: the stage's result is the raster colour.
        let mut m = MaterialIr { pixel_stage_count: 1, ..Default::default() };
        let mut s = stage([15, 15, 15, 10], [7, 7, 7, 5]);
        s.channel = 0;
        m.stages.push(s);
        m.swaps = [[0, 1, 2, 3]; 4];
        let out = evaluate(&m, &|_| [0.0; 4], &|_| [0.25, 0.5, 0.75, 1.0]);
        assert_eq!(out, [0.25, 0.5, 0.75, 1.0]);
    }

    #[test]
    fn a_stage_modulates_a_map_by_a_constant() {
        // lerp(0, map, constant) with the fixed constant 1/2 (selection 4: 128/255).
        let mut m = MaterialIr { pixel_stage_count: 1, ..Default::default() };
        let mut s = stage([15, 8, 14, 15], [7, 4, 6, 7]);
        s.map = 0;
        s.color_constant = 4;
        s.alpha_constant = 4;
        m.stages.push(s);
        m.swaps = [[0, 1, 2, 3]; 4];
        let out = evaluate(&m, &|_| [1.0, 0.5, 0.0, 1.0], &|_| [0.0; 4]);
        assert!((out[0] - 128.0 / 255.0).abs() < 1e-6 && (out[1] - 64.0 / 255.0).abs() < 1e-6 && out[2] == 0.0);
    }
}
