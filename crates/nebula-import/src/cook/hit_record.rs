//! What ray tracing reads of a material: its class and numbers, from its own data and never its name.
//!
//! A material's stages are evaluated here on the CPU (the same combiner the generated programs run) with chosen
//! inputs: every map mid grey, an environment map white or black, the lit channel on or off. What the result does
//! under those changes is the evidence: how much of the look an environment map carries (metal against gloss), what
//! the surface shows with no light at all (emission), what light adds (its base colour).
//! `nebula-import rt-materials CONTENT OUT.tsv` writes the table for every material of the game (or a mod's files).

use super::model_ir::{read_file, MaterialIr, ModelIr, Source, StageIr, NONE};
use super::Stats;
use anyhow::{Context, Result};
use std::collections::BTreeMap;
use std::io::Write;
use std::path::Path;

/// A material's class at a ray's hit. The numbers are the model file's.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u8)]
pub enum Class {
    /// Lit, opaque, no painted reflection: ground, wood, cloth, skin, most characters.
    Dielectric = 0,
    /// A lit base with a painted reflection or an authored highlight over it: plastic, paint, polished things.
    Gloss = 1,
    /// A painted reflection carries the look over a dark base: chrome, gold, steel.
    Metal = 2,
    /// Blended, with a painted reflection or a warp: glass, crystal, bubbles.
    Glass = 3,
    /// Alpha-tested: leaves, fences, grates. Two-sided ones are thin sheets.
    Cutout = 4,
    /// Shaded through a ramp map: keeps its own look.
    Ramp = 5,
    /// Not lit and added to what is behind it, or bright with no light: glows, beams, halos, lava.
    Emissive = 6,
    /// Not lit, opaque: the vertex colour or the texture is the light (painted).
    Painted = 7,
    /// Blended without a reflection: soft translucents (smoke sheets, decals, shadows' blobs).
    Translucent = 8,
}

impl Class {
    pub fn name(self) -> &'static str {
        match self {
            Class::Dielectric => "dielectric",
            Class::Gloss => "gloss",
            Class::Metal => "metal",
            Class::Glass => "glass",
            Class::Cutout => "cutout",
            Class::Ramp => "ramp",
            Class::Emissive => "emissive",
            Class::Painted => "painted",
            Class::Translucent => "translucent",
        }
    }
}

/// What the data shows (the design's evidence signals that need nothing but the material).
#[derive(Clone, Copy, Debug, Default)]
pub struct Signals {
    pub lit: bool,
    pub specular: bool,
    pub vertex_colour: bool,
    pub environment: bool,
    pub ramp: bool,
    pub bump: bool,
    pub warp: bool,
    pub alpha_test: bool,
    /// 0 opaque, 1 blended by alpha, 2 added, 3 other blends.
    pub blend: u8,
    pub two_sided: bool,
    pub depth_write: bool,
    pub animated_coords: bool,
    /// How far the result moves between a white and a black environment map, 0 to 1.
    pub environment_share: f32,
    /// The result with no light and black environment maps: what the surface shows by itself.
    pub unlit: [f32; 3],
    /// What full light adds to it.
    pub lit_gain: [f32; 3],
    /// What a white environment map adds (the colour it is multiplied by), its hue, 0 to 360, and its saturation.
    pub environment_tint: [f32; 3],
    pub environment_hue: f32,
    pub environment_saturation: f32,
}

/// The class, its numbers and the rules that chose it.
#[derive(Clone, Debug)]
pub struct Reading {
    pub class: Class,
    pub signals: Signals,
    pub base: [f32; 3],
    pub emission: [f32; 3],
    pub roughness: f32,
    pub metal: f32,
    pub ior: f32,
    pub transmission: f32,
    pub rule: &'static str,
}

/// The models that glow (data/rt_glow.tsv): a model's file stem to its light's colour (None: the material's own) and
/// strength.
const GLOW_TABLE: &str = include_str!("../../data/rt_glow.tsv");

/// The light a material of the model `stem` gives: its colour and, last, its strength (0: none). From the table by
/// the model's name; else a surface that is added to what is behind it and bright lights by its own colour.
/// Whether a material is a shadow drawn by hand: a model made to be something's shadow (the player's shadow, a
/// launch star's, a boss's: the file is named so), one of the drop shadows' volumes, or a shadow decal among a
/// model's materials (named so, and blended or painted). `kagenasi` is "without shadow": not one.
pub fn drawn_shadow(stem: &str, material: &str, class: Class) -> bool {
    let stem = stem.to_ascii_lowercase();
    let material = material.to_ascii_lowercase();
    if stem.contains("withoutshadow") || material.contains("kagenasi") {
        return false;
    }
    if stem.ends_with("shadow") || stem.starts_with("shadowvolume") {
        return true;
    }
    (material.starts_with("shadow") || material.ends_with("shadow") || material.contains("shadow_") || material.contains("shadow0"))
        && matches!(class, Class::Translucent | Class::Painted | Class::Glass)
}

pub fn glow_of(stem: &str, r: &Reading) -> [f32; 4] {
    let stem = stem.to_ascii_lowercase();
    let own = || {
        // The material's bright colour, its strongest channel at 1.
        let c = if luminance(r.emission) > 0.05 { r.emission } else { r.base };
        let top = c[0].max(c[1]).max(c[2]).max(1e-3);
        [c[0] / top, c[1] / top, c[2] / top]
    };
    for line in GLOW_TABLE.lines() {
        let mut fields = line.split('\t');
        let (Some(name), Some(colour), Some(strength)) = (fields.next(), fields.next(), fields.next()) else { continue };
        if line.starts_with('#') || name != stem {
            continue;
        }
        let strength: f32 = strength.trim().parse().unwrap_or(0.0);
        let parts: Vec<f32> = colour.split_whitespace().filter_map(|v| v.parse().ok()).collect();
        let c = if parts.len() == 3 { [parts[0], parts[1], parts[2]] } else { own() };
        // A model's dark or see-through parts (eyes, shadows' blobs) are not what glows.
        if r.class == Class::Translucent {
            return [0.0; 4];
        }
        return [c[0], c[1], c[2], strength];
    }
    if r.class == Class::Emissive && r.signals.blend == 2 && luminance(r.emission) > 0.3 {
        // A surface added to what is behind it: a light by its size. Below zero: a luminance (candela a square
        // metre of the surface as the eye sees it), not an intensity; the renderer knows the mesh's size.
        let c = own();
        return [c[0], c[1], c[2], -120.0];
    }
    [0.0; 4]
}

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

/// The maps a material reads through a coordinate made from the normal: its painted reflections.
fn environment_maps(m: &MaterialIr) -> Vec<u8> {
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

fn ramp_maps(m: &MaterialIr) -> bool {
    let count = (m.pixel_stage_count as usize).min(m.stages.len());
    m.stages[..count].iter().any(|s: &StageIr| s.map != 0xFF && m.coords.get(s.coord as usize).is_some_and(|c| c.kind == 10 || c.source == 32 || c.source == 33))
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

/// The signals of one material.
pub fn signals(m: &MaterialIr) -> Signals {
    let channels = 2 * (m.channel_count as usize).min(2);
    let lit_channel = |k: usize| m.channels[k].enable != 0 && m.channels[k].light_mask != 0;
    let lit = (0..channels).any(lit_channel);
    let environment = environment_maps(m);
    let grey = [0.5f32, 0.5, 0.5, 1.0];
    let material = |n: usize| {
        let c = m.material_color[n & 1];
        [f32::from(c[0]) / 255.0, f32::from(c[1]) / 255.0, f32::from(c[2]) / 255.0, f32::from(c[3]) / 255.0]
    };
    let ambient = |n: usize| {
        let c = m.ambient_color[n & 1];
        [f32::from(c[0]) / 255.0, f32::from(c[1]) / 255.0, f32::from(c[2]) / 255.0]
    };
    // Colour channel n as the rasteriser hands it over, with `light` of the lights' sum (0 none, 1 full): the material
    // colour (the register, or mid grey where it is the vertex's) times ambient plus light, clamped.
    let raster = |light: f32| {
        move |code: u8| -> [f32; 4] {
            let n = match code {
                0 | 4 => 0usize,
                1 | 5 => 1usize,
                _ => return [0.0; 4],
            };
            if n * 2 >= channels {
                return [1.0; 4];
            }
            let colour = &m.channels[n * 2];
            let base = if colour.material_source != 0 { grey } else { material(n) };
            let mut out = base;
            if colour.enable != 0 {
                let amb = ambient(n);
                for c in 0..3 {
                    let sum = if colour.light_mask != 0 { (amb[c] + light).min(1.0) } else { amb[c] };
                    out[c] = base[c] * sum;
                }
            }
            out
        }
    };
    let map = |environment_value: f32| {
        let environment = environment.clone();
        move |n: u8| -> [f32; 4] { if environment.contains(&n) { [environment_value, environment_value, environment_value, 1.0] } else { grey } }
    };
    let unlit = evaluate(m, &map(0.0), &raster(0.0));
    let full = evaluate(m, &map(0.0), &raster(1.0));
    let mirrored = evaluate(m, &map(1.0), &raster(1.0));
    let tint = [mirrored[0] - full[0], mirrored[1] - full[1], mirrored[2] - full[2]].map(|v| v.max(0.0));
    let share = luminance(tint) / luminance([mirrored[0], mirrored[1], mirrored[2]].map(|v| v.clamp(0.0, 1.0))).max(1e-3);
    let (hue, saturation) = hue_saturation(tint);
    let count = (m.pixel_stage_count as usize).min(m.stages.len());
    Signals {
        lit,
        specular: (0..channels).any(|k| lit_channel(k) && m.channels[k].attenuation == 0),
        vertex_colour: channels != 0 && m.channels[0].material_source != 0,
        environment: !environment.is_empty(),
        ramp: ramp_maps(m),
        bump: m.coords.iter().any(|c| c.source == 2 || c.source == 3),
        warp: m.has_indirect && m.stages[..count].iter().any(|s| s.warp.iter().any(|&b| b != 0)),
        alpha_test: !alpha_test_always(m),
        blend: match (m.blend_type, m.blend_src, m.blend_dst) {
            (0, _, _) => 0,
            (1, 4, 5) => 1,
            (1, _, 1) => 2,
            _ => 3,
        },
        two_sided: m.cull == 0,
        depth_write: m.depth_write != 0,
        animated_coords: m.matrices.len() > 0 && m.coords.iter().any(|c| c.matrix != NONE),
        environment_share: if environment.is_empty() { 0.0 } else { share.clamp(0.0, 1.0) },
        unlit: [unlit[0], unlit[1], unlit[2]].map(|v| v.clamp(0.0, 4.0)),
        lit_gain: [full[0] - unlit[0], full[1] - unlit[1], full[2] - unlit[2]].map(|v| v.clamp(0.0, 4.0)),
        environment_tint: tint.map(|v| v.clamp(0.0, 1.0)),
        environment_hue: hue,
        environment_saturation: saturation,
    }
}

/// Whether the alpha test passes whatever the alpha (nebula_model.h `nebula_alpha_test_always`).
fn alpha_test_always(m: &MaterialIr) -> bool {
    let (a, b) = (m.alpha_comp0, m.alpha_comp1);
    match m.alpha_op {
        0 => a == 7 && b == 7,
        1 => a == 7 || b == 7,
        2 => (a == 0 && b == 7) || (a == 7 && b == 0),
        3 => (a == 0 && b == 0) || (a == 7 && b == 7),
        _ => false,
    }
}

/// The class and numbers the signals give (the design's 2b.3, the rules that need the material alone).
pub fn read(m: &MaterialIr) -> Reading {
    let s = signals(m);
    let full = [s.unlit[0] + s.lit_gain[0], s.unlit[1] + s.lit_gain[1], s.unlit[2] + s.lit_gain[2]];
    // Maps were mid grey: twice the result is the colour under a white map, the nearest thing to a base colour the
    // material alone gives (the textures' own means refine it at the cook).
    let base = full.map(|v| (v * 2.0).clamp(0.0, 1.0));
    let dark_base = luminance(full) < 0.2;
    let mut r = Reading { class: Class::Dielectric, signals: s, base, emission: [0.0; 3], roughness: 0.45, metal: 0.0, ior: 1.5, transmission: 0.0, rule: "" };
    let metal_hue = s.environment_saturation < 0.15 || (8.0..=60.0).contains(&s.environment_hue);
    if s.blend == 2 {
        // Only what is added to the picture behind it is a glow by the data alone: a blended, bright, unlit surface
        // may as well be water or cracked crystal (the first audit's mistakes). The game's draw category settles
        // the rest at run time (bloom and glare buffers).
        r.class = Class::Emissive;
        r.emission = s.unlit.map(|v| (v * 2.0).min(4.0));
        r.rule = "added to what is behind it";
    } else if s.blend != 0 && (s.environment || s.warp) {
        r.class = Class::Glass;
        r.roughness = 0.02;
        r.transmission = 1.0;
        r.rule = "blended with a painted reflection or a warp";
    } else if s.blend != 0 {
        r.class = Class::Translucent;
        r.transmission = 0.5;
        r.rule = "blended, no reflection";
    } else if s.alpha_test {
        r.class = Class::Cutout;
        r.roughness = 0.5;
        r.transmission = if s.two_sided { 0.25 } else { 0.0 };
        r.rule = if s.two_sided { "alpha test, two-sided: a thin sheet" } else { "alpha test" };
    } else if s.ramp {
        r.class = Class::Ramp;
        r.rule = "a ramp map";
    } else if s.environment && s.environment_share >= 0.5 && dark_base && metal_hue {
        r.class = Class::Metal;
        r.metal = 1.0;
        r.roughness = 0.15;
        r.base = s.environment_tint;
        r.rule = "the painted reflection carries half the look or more over a dark base";
    } else if s.environment && !s.lit && s.environment_share >= 0.3 && metal_hue {
        // Not lit at all and a third or more of it a painted reflection, in a metal's hue: the art's way to draw
        // metal that keeps its brightness everywhere (coins).
        r.class = Class::Metal;
        r.metal = 1.0;
        r.roughness = 0.2;
        r.rule = "unlit, a third or more a painted reflection in a metal's hue";
    } else if s.environment && s.environment_share >= 0.5 && !dark_base {
        r.class = Class::Gloss;
        r.roughness = 0.1;
        r.rule = "a strong painted reflection over a bright base";
    } else if s.environment || s.specular {
        r.class = Class::Gloss;
        r.roughness = if s.environment { 0.2 } else { 0.3 };
        r.rule = if s.environment { "a painted reflection over a lit base" } else { "an authored highlight" };
    } else if !s.lit {
        r.class = Class::Painted;
        r.roughness = 0.9;
        r.rule = "not lit, opaque";
    } else {
        r.rule = "lit, opaque, no painted reflection";
    }
    r
}

/// Every material of every model under CONTENT, one row each, and the classes' counts.
pub fn audit(input: &Path, output: &Path) -> Result<()> {
    let mut out = std::io::BufWriter::new(std::fs::File::create(output)?);
    writeln!(
        out,
        "model\tmaterial\tname\tclass\trule\tlit\tspecular\tvertex colour\tenvironment\tenvironment share\tenvironment hue\tenvironment saturation\t\
         ramp\tbump\twarp\talpha test\tblend\ttwo-sided\tdepth write\tanimated coordinates\tunlit\tlit gain\tbase\temission\troughness\tmetal\ttransmission\tstages\tmaps\tglow"
    )?;
    let mut classes: BTreeMap<Class, (u32, u32)> = BTreeMap::new();
    let mut stats = Stats::default();
    let (mut models, mut materials, mut unread, mut glowing) = (0u32, 0u32, 0u32, 0u32);
    super::programs::walk_files(input, &mut |name, bytes| {
        let source = Source::of(&bytes);
        if source == Source::Table || bytes.starts_with(b"RLYT") {
            return Ok(());
        }
        // A file the reader does not take (the cook leaves it as it is) has no materials to read.
        let ir: ModelIr = match read_file(&bytes, source, &mut stats).with_context(|| name.to_string()) {
            Ok(ir) => ir,
            Err(error) => {
                unread += 1;
                println!("not read: {error:#}");
                return Ok(());
            }
        };
        models += 1;
        for (i, m) in ir.materials.iter().enumerate() {
            let r = read(m);
            let s = r.signals;
            let stem = name.rsplit('/').next().unwrap_or(name);
            let glow = glow_of(stem.split('.').next().unwrap_or(stem), &r);
            let triple = |v: [f32; 3]| format!("{:.2} {:.2} {:.2}", v[0], v[1], v[2]);
            let flag = |b: bool| u8::from(b);
            writeln!(
                out,
                "{name}\t{i}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{:.2}\t{:.0}\t{:.2}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{:.2}\t{:.0}\t{:.2}\t{}\t{}\t{}",
                m.name,
                r.class.name(),
                r.rule,
                flag(s.lit),
                flag(s.specular),
                flag(s.vertex_colour),
                flag(s.environment),
                s.environment_share,
                s.environment_hue,
                s.environment_saturation,
                flag(s.ramp),
                flag(s.bump),
                flag(s.warp),
                flag(s.alpha_test),
                s.blend,
                flag(s.two_sided),
                flag(s.depth_write),
                flag(s.animated_coords),
                triple(s.unlit),
                triple(s.lit_gain),
                triple(r.base),
                triple(r.emission),
                r.roughness,
                r.metal,
                r.transmission,
                m.pixel_stage_count,
                m.textures.len(),
                if glow[3] > 0.0 {
                    format!("{:.2} {:.2} {:.2} {:.0} cd", glow[0], glow[1], glow[2], glow[3])
                } else if glow[3] < 0.0 {
                    format!("{:.2} {:.2} {:.2} {:.0} cd/m2", glow[0], glow[1], glow[2], -glow[3])
                } else {
                    String::new()
                }
            )?;
            glowing += u32::from(glow[3] != 0.0);
            let entry = classes.entry(r.class).or_default();
            entry.0 += 1;
            materials += 1;
            let _ = &mut entry.1;
        }
        Ok(())
    })?;
    out.flush()?;
    println!("{models} models, {materials} materials, {glowing} of them glow; {unread} files not read");
    for (class, (count, _)) in &classes {
        println!("{:<12} {count:>6}  {:.1} %", class.name(), 100.0 * f64::from(*count) / f64::from(materials.max(1)));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

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

    #[test]
    fn an_unlit_opaque_material_is_painted_and_an_added_one_emissive() {
        let mut m = MaterialIr { pixel_stage_count: 1, channel_count: 1, depth_write: 1, cull: 2, alpha_comp0: 7, alpha_comp1: 7, ..Default::default() };
        let mut s = stage([15, 15, 15, 10], [7, 7, 7, 5]);
        s.channel = 0;
        m.stages.push(s);
        m.swaps = [[0, 1, 2, 3]; 4];
        m.material_color = [[255; 4]; 2];
        assert_eq!(read(&m).class, Class::Painted);
        m.blend_type = 1;
        m.blend_src = 4;
        m.blend_dst = 1;
        assert_eq!(read(&m).class, Class::Emissive);
    }
}
