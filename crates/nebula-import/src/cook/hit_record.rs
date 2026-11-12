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
    /// A water surface (data/rt_water.tsv).
    Water = 9,
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
            Class::Water => "water",
        }
    }

    pub fn by_name(name: &str) -> Option<Class> {
        [
            Class::Dielectric,
            Class::Gloss,
            Class::Metal,
            Class::Glass,
            Class::Ramp,
            Class::Emissive,
            Class::Painted,
            Class::Translucent,
            Class::Water,
        ]
        .into_iter()
        .find(|c| c.name() == name)
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
    /// nebula_model.h NebulaModelHit.glow_source (L-1).
    pub glow_source: u8,
    /// Units light crosses inside it (13.2): the model's meshes give it (model_write::Measure); 0 until then.
    pub thickness: f32,
    /// Its painted reflection's colour, largest channel 1 (the map's mean joins at the cook).
    pub env_tint: [f32; 3],
    /// The map read through the normal, 255 none.
    pub env_map: u8,
    /// The review's numbers (rt_materials.tsv); below 0: the class's own.
    pub body: f32,
    pub shine: f32,
    pub tint_keep: f32,
}

/// The models that glow (data/rt_glow.tsv): a model's file stem to its light's colour (None: the material's own) and
/// strength.
const GLOW_TABLE: &str = include_str!("../../data/rt_glow.tsv");

/// Mod packs' own rows for the tables here (their `merge/NebulaData/RtMaterials.tsv`, `RtGlow.tsv`, `RtWater.tsv`:
/// cook/overlay.rs), in the mods' order, read after the game's: a pack's row decides for its models. Empty but while
/// an overlay cooks.
#[derive(Default)]
pub struct PackTables {
    pub materials: Vec<String>,
    pub glow: Vec<String>,
    pub water: Vec<String>,
}

static PACK_TABLES: std::sync::RwLock<PackTables> =
    std::sync::RwLock::new(PackTables { materials: Vec::new(), glow: Vec::new(), water: Vec::new() });

/// The packs' tables while an overlay cooks; the game's alone again once it is dropped.
pub struct PackTablesInUse;

impl PackTablesInUse {
    pub fn set(tables: PackTables) -> Self {
        *PACK_TABLES.write().unwrap_or_else(|e| e.into_inner()) = tables;
        PackTablesInUse
    }
}

impl Drop for PackTablesInUse {
    fn drop(&mut self) {
        *PACK_TABLES.write().unwrap_or_else(|e| e.into_inner()) = PackTables::default();
    }
}

/// Why a pack's table (`kind` 0 RtMaterials, 1 RtGlow, 2 RtWater) cannot be used; None: it can.
pub fn pack_table_problem(kind: usize, text: &str) -> Option<String> {
    match kind {
        0 => {
            let (head, _) = rows_of(text);
            let (game, _) = rows_of(MATERIALS_TABLE);
            if !head.iter().any(|h| h == "model") || !head.iter().any(|h| h == "material") {
                return Some("its header row names no model or material column".to_string());
            }
            head.iter().find(|h| !game.contains(*h)).map(|h| format!("the column {h:?} is not one of the game's ({})", game.join(", ")))
        }
        2 => {
            let (head, _) = rows_of(text);
            (head.len() != 2 || head[0] != "model" || head[1] != "material").then(|| "its header row is not: model, material".to_string())
        }
        _ => text.lines().map(|l| l.trim_end_matches('\r')).filter(|l| !l.trim().is_empty() && !l.starts_with('#')).find_map(|line| {
            let fields: Vec<&str> = line.split('\t').collect();
            (fields.len() < 3 || fields[2].trim().parse::<f32>().is_err()).then(|| format!("the row {line:?} is not: model, colour or own, candela"))
        }),
    }
}

/// A glow table's row for `wanted` (a model's stem, or `model/material`): its colour and strength fields.
fn glow_row<'a>(table: &'a str, wanted: &str) -> Option<(&'a str, &'a str)> {
    table.lines().filter(|line| !line.starts_with('#')).find_map(|line| {
        let mut fields = line.split('\t');
        let (Some(name), Some(colour), Some(strength)) = (fields.next(), fields.next(), fields.next()) else { return None };
        (name.trim() == wanted).then_some((colour, strength))
    })
}

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

/// Whether a material is lava or a sun's surface by its name (G8): lava (Lava_v, LavaMat_v, Lava_m_v, Lava2_v, not
/// LavaRock_v), magma, yougan ("lava"), sunmat, sun_mat; not on a bloom copy, an Observatory miniature or a sky.
/// `stem` and `material` lower case.
fn lava_or_sun(stem: &str, material: &str) -> bool {
    if stem.ends_with("bloom") || stem.starts_with("mini") || stem.starts_with("vr") {
        return false;
    }
    let lava = material.strip_prefix("lava").is_some_and(|rest| {
        rest.is_empty() || rest.starts_with('_') || rest.starts_with("mat") || rest.starts_with(|c: char| c.is_ascii_digit())
    });
    // prominencemat: the prominence stages' lava falls (c57).
    lava || ["magma", "yougan", "sunmat", "sun_mat", "prominencemat"].iter().any(|p| material.starts_with(p))
}

/// What the sheen's name and model say its body is (c49).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum SheenKind {
    Crystal,
    Ice,
    Glass,
    Unknown,
}

pub fn sheen_kind(stem: &str, material: &str) -> SheenKind {
    let stem = stem.split('.').next().unwrap_or(stem).to_ascii_lowercase();
    let name = material.to_ascii_lowercase();
    let has = |w: &str| name.contains(w) || stem.contains(w);
    if has("ice") {
        SheenKind::Ice
    } else if has("glass") {
        SheenKind::Glass
    } else if has("crystal") || has("jewel") || name.starts_with("cry") || name.contains("hilight") {
        SheenKind::Crystal
    } else {
        SheenKind::Unknown
    }
}

/// A painted sheen drawn as an added layer (c49; the owner's "baked reflections"): added to what is behind it (blend
/// 2), half or more of its look a painted environment map, no glow by the tables nor by its name. While tracing it is
/// not drawn: the body it covers mirrors for real (sheen_roles).
pub fn sheen_layer(stem: &str, material: &str, r: &Reading) -> bool {
    let s = &r.signals;
    if r.class != Class::Emissive || s.blend != 2 || !s.environment || s.environment_share < 0.5 {
        return false;
    }
    let stem = stem.split('.').next().unwrap_or(stem).to_ascii_lowercase();
    let name = material.to_ascii_lowercase();
    if ["glow", "bloom", "starlight", "beam", "fire", "flash"].iter().any(|w| name.contains(w)) {
        return false;
    }
    let one = format!("{stem}/{name}");
    let packs = PACK_TABLES.read().unwrap_or_else(|e| e.into_inner());
    let listed = |table: &str| glow_row(table, &one).or_else(|| glow_row(table, &stem)).is_some();
    !(packs.glow.iter().any(|table| listed(table)) || listed(GLOW_TABLE))
}

/// What a material is to its model's painted sheens (c49).
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum SheenRole {
    None,
    /// A sheen layer and the body it covers (none found: it stays drawn).
    Sheen { body: Option<usize> },
    /// The body under the sheen `sheen`.
    Body { sheen: usize, kind: SheenKind },
}

/// Every material's role (c49): the sheens by data, each one's body the other material whose corners' places (a
/// quarter unit) hold 80 % or more of the sheen's, the most shared.
pub fn sheen_roles(ir: &ModelIr, stem: &str) -> Vec<SheenRole> {
    let n = ir.materials.len();
    let mut roles = vec![SheenRole::None; n];
    let sheens: Vec<usize> = (0..n)
        .filter(|&i| {
            let m = &ir.materials[i];
            let mut r = read(m);
            finish(stem, &m.name, 0.0, &mut r);
            sheen_layer(stem, &m.name, &r)
        })
        .collect();
    if sheens.is_empty() {
        return roles;
    }
    let key = |p: [f32; 3]| [(p[0] * 4.0).round() as i32, (p[1] * 4.0).round() as i32, (p[2] * 4.0).round() as i32];
    let mut places: Vec<std::collections::HashSet<[i32; 3]>> = vec![std::collections::HashSet::new(); n];
    // c59: each material's corners once, for a sheen drawn as a shell off its body.
    let mut points: Vec<Vec<[f32; 3]>> = vec![Vec::new(); n];
    for shape in &ir.shapes {
        let Some(set) = places.get_mut(shape.material as usize) else { continue };
        for group in &shape.groups {
            for corner in &group.corners {
                if let Some(&p) = ir.arrays.positions.get(corner.position as usize) {
                    if set.insert(key(p)) {
                        points[shape.material as usize].push(p);
                    }
                }
            }
        }
    }
    for &s in &sheens {
        let own = &places[s];
        let mut best: Option<(usize, f32)> = None;
        if !own.is_empty() {
            for b in (0..n).filter(|b| !sheens.contains(b)) {
                let shared = own.iter().filter(|p| places[b].contains(*p)).count() as f32 / own.len() as f32;
                if shared >= 0.8 && best.map_or(true, |(_, most)| shared > most) {
                    best = Some((b, shared));
                }
            }
        }
        // c59: no body with its corners: a shell a little off its body. The share of the sheen's corners within 5 % of
        // its own size (its box's diagonal, at least 1 unit) of a body's corner, through a grid of cells that size.
        if best.is_none() && !points[s].is_empty() {
            let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
            for p in &points[s] {
                for k in 0..3 {
                    lo[k] = lo[k].min(p[k]);
                    hi[k] = hi[k].max(p[k]);
                }
            }
            let diagonal = ((hi[0] - lo[0]).powi(2) + (hi[1] - lo[1]).powi(2) + (hi[2] - lo[2]).powi(2)).sqrt();
            let reach = (0.05 * diagonal).max(1.0);
            let cell = |p: [f32; 3]| [(p[0] / reach).floor() as i32, (p[1] / reach).floor() as i32, (p[2] / reach).floor() as i32];
            // Of the bodies near enough, the one nearest the sheen's size (a shell is its body's size; a base it sits
            // on is larger): the least |ln(its diagonal / the sheen's)|.
            let mut misfit = f32::MAX;
            for b in (0..n).filter(|b| !sheens.contains(b) && !points[*b].is_empty()) {
                let (mut blo, mut bhi) = ([f32::MAX; 3], [f32::MIN; 3]);
                for q in &points[b] {
                    for k in 0..3 {
                        blo[k] = blo[k].min(q[k]);
                        bhi[k] = bhi[k].max(q[k]);
                    }
                }
                let body_diagonal = ((bhi[0] - blo[0]).powi(2) + (bhi[1] - blo[1]).powi(2) + (bhi[2] - blo[2]).powi(2)).sqrt();
                let off = (body_diagonal.max(1e-3) / diagonal.max(1e-3)).ln().abs();
                let mut grid: std::collections::HashMap<[i32; 3], Vec<[f32; 3]>> = std::collections::HashMap::new();
                for &q in &points[b] {
                    grid.entry(cell(q)).or_default().push(q);
                }
                let near = points[s]
                    .iter()
                    .filter(|p| {
                        let c = cell(**p);
                        (-1..=1).any(|x| {
                            (-1..=1).any(|y| {
                                (-1..=1).any(|z| {
                                    grid.get(&[c[0] + x, c[1] + y, c[2] + z]).is_some_and(|qs| {
                                        qs.iter().any(|q| (q[0] - p[0]).powi(2) + (q[1] - p[1]).powi(2) + (q[2] - p[2]).powi(2) <= reach * reach)
                                    })
                                })
                            })
                        })
                    })
                    .count() as f32
                    / points[s].len() as f32;
                if near >= 0.8 && off < misfit {
                    misfit = off;
                    best = Some((b, near));
                }
            }
        }
        roles[s] = SheenRole::Sheen { body: best.map(|(b, _)| b) };
        if let Some((b, _)) = best {
            if roles[b] == SheenRole::None {
                roles[b] = SheenRole::Body { sheen: s, kind: sheen_kind(stem, &ir.materials[s].name) };
            }
        }
    }
    roles
}

thread_local! {
    // The last model's roles (finish_reading asks once a material): its address, sizes and stem.
    static SHEEN_ROLES: std::cell::RefCell<((usize, usize, usize, usize), String, Vec<SheenRole>)> = Default::default();
}

/// sheen_roles of `ir`, kept for the model's materials' turns.
pub fn sheen_roles_kept(ir: &ModelIr, stem: &str) -> Vec<SheenRole> {
    let id = (ir as *const ModelIr as usize, ir.materials.len(), ir.shapes.len(), ir.arrays.positions.len());
    SHEEN_ROLES.with(|cell| {
        let mut kept = cell.borrow_mut();
        if kept.0 != id || kept.1 != stem {
            *kept = (id, stem.to_string(), sheen_roles(ir, stem));
        }
        kept.2.clone()
    })
}

/// The body under a painted sheen mirrors for real (c49): a mirroring class unless it has one.
pub fn promote_sheen_body(kind: SheenKind, r: &mut Reading) {
    if matches!(r.class, Class::Glass | Class::Metal | Class::Gloss | Class::Water) {
        return;
    }
    let (class, ior, roughness) = match kind {
        SheenKind::Crystal => (Class::Glass, 1.8, 0.02),
        SheenKind::Ice => (Class::Glass, 1.31, 0.08),
        SheenKind::Glass => (Class::Glass, 1.5, 0.02),
        SheenKind::Unknown => (Class::Gloss, 1.5, 0.1),
    };
    r.class = class;
    r.ior = ior;
    r.roughness = roughness;
    r.metal = 0.0;
    if class == Class::Glass {
        r.transmission = 1.0;
        r.tint_keep = 0.0;
    }
    r.rule = "the body under a painted sheen (c49)";
}

pub fn glow_of(stem: &str, material: &str, r: &Reading) -> [f32; 4] {
    // Water is never a light, whatever its brightness.
    if r.class == Class::Water {
        return [0.0; 4];
    }
    let stem = stem.to_ascii_lowercase();
    // A row may name one material of the model (`model/material`): it alone glows, and it comes before a row of the
    // whole model.
    let one = format!("{stem}/{}", material.to_ascii_lowercase());
    let own = || {
        // The material's bright colour, its strongest channel at 1.
        let c = if luminance(r.emission) > 0.05 { r.emission } else { r.base };
        let top = c[0].max(c[1]).max(c[2]).max(1e-3);
        [c[0] / top, c[1] / top, c[2] / top]
    };
    // A pack's row first (the last pack's before the others'), then the game's.
    let packs = PACK_TABLES.read().unwrap_or_else(|e| e.into_inner());
    let found = packs
        .glow
        .iter()
        .rev()
        .find_map(|table| glow_row(table, &one).or_else(|| glow_row(table, &stem)))
        .or_else(|| glow_row(GLOW_TABLE, &one).or_else(|| glow_row(GLOW_TABLE, &stem)));
    if let Some((colour, strength)) = found {
        let strength: f32 = strength.trim().parse().unwrap_or(0.0);
        let parts: Vec<f32> = colour.split_whitespace().filter_map(|v| v.parse().ok()).collect();
        let c = if parts.len() == 3 { [parts[0], parts[1], parts[2]] } else { own() };
        // A model's dark or see-through parts (eyes, shadows' blobs) are not what glows.
        if r.class == Class::Translucent {
            return [0.0; 4];
        }
        return [c[0], c[1], c[2], strength];
    }
    // Lava and suns by name (G8): their own colour at the blend-2 rule's luminance; the renderer makes a small one a
    // lamp and a large one an emissive area.
    if lava_or_sun(&stem, &material.to_ascii_lowercase()) && !matches!(r.class, Class::Translucent | Class::Glass) {
        let c = own();
        return [c[0], c[1], c[2], -120.0];
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

/// The maps a material reads through a coordinate made from the normal that are its painted reflections: not those
/// that hold a colour ramp (c56).
fn environment_maps(m: &MaterialIr) -> Vec<u8> {
    normal_maps(m).into_iter().filter(|map| !m.ramp_maps.contains(map) && !m.highlight_maps.contains(map)).collect()
}

/// c56 (the director 11-11, Surfing's rainbow arches): what a map read through a normal-made coordinate holds. Each
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
    /// c58: the share of the texels at half the brightest one's luminance or more (the spot), and that luminance.
    pub spot: f64,
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
    let spot = if peak > 0.0 { texels[..w * h].iter().filter(|&&t| lum(t) >= 0.5 * peak).count() as f64 / n } else { 0.0 };
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
        spot,
        peak,
    })
}

/// c58's rule: a painted highlight sprite, a light spot on a dark ground (at least 0.6 of it dark), not a ramp.
pub fn is_highlight(s: &MapShape) -> bool {
    const SPRITE_DARK: f64 = 0.6;
    s.dark >= SPRITE_DARK && !is_ramp(s) && s.peak > 0.05
}

/// c58: the roughness of the highlight a sprite paints. The map stands for the normal's x and y over -1..1 (its
/// square 4, the disc pi): a spot of share A of the texels is a disc of radius sqrt(4A / pi) in n.xy, the angle
/// asin of that between the normal at its edge and the centre's: the GGX lobe's half width at half its peak, where
/// tan = 0.6436 alpha; roughness = sqrt(alpha) (enh_highlight: a2 = rough^4). 0.08 to 0.8.
pub fn sprite_roughness(s: &MapShape) -> f32 {
    let radius = (4.0 * s.spot / std::f64::consts::PI).sqrt().min(0.999);
    let alpha = radius.asin().tan() / 0.6436;
    (alpha.sqrt() as f32).clamp(0.08, 0.8)
}

/// c58: whether a map is ADDED to the stages' colour (a highlight) and not multiplied in (a mask, which hiding would
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

/// c58: each material's normal-indexed maps that are painted highlight sprites (MaterialIr highlight_maps), and the
/// first one's roughness and peak.
pub fn mark_highlights(ir: &mut ModelIr) {
    let found: Vec<Vec<(u8, MapShape)>> = ir
        .materials
        .iter()
        .map(|m| {
            normal_maps(m)
                .into_iter()
                .filter(|map| !m.ramp_maps.contains(map))
                .filter(|&map| sprite_adds(m, map))
                .filter_map(|map| map_texels(ir, m, map).and_then(|(w, h, t)| map_shape(w, h, &t)).filter(is_highlight).map(|s| (map, s)))
                .collect()
        })
        .collect();
    for (m, sprites) in ir.materials.iter_mut().zip(found) {
        if let Some((_, s)) = sprites.first() {
            m.highlight_rough = sprite_roughness(s);
            m.highlight_gain = (s.peak as f32).clamp(0.25, 1.0);
        }
        m.highlight_maps = sprites.into_iter().map(|(map, _)| map).collect();
    }
}

/// c56's rule: a colour ramp, not a picture. One axis or the radius explains kRampExplained of its colour, its hues
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

/// c56: each material's normal-indexed maps that hold a colour ramp (MaterialIr ramp_maps).
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

fn ramp_maps(m: &MaterialIr) -> bool {
    let count = (m.pixel_stage_count as usize).min(m.stages.len());
    !m.ramp_maps.is_empty() || m.stages[..count].iter().any(|s: &StageIr| s.map != 0xFF && m.coords.get(s.coord as usize).is_some_and(|c| c.kind == 10 || c.source == 32 || c.source == 33))
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
/// Whether a material's name says it is falling water: "fallmat" or "fallback" (FallMat, DxFallMat, ExFallMatSplash,
/// FallBackFaceMat ...: every Fall material of the census) or "taki" (waterfall) at its start; not OtaKing, Rainfall.
/// A clear material's index and roughness by what the census names it (A4-W6): crystal 1.8 (as the art asks), ice
/// 1.31 and a little rough, other glass 1.5. `stem` and `name` lower case.
fn clear_kind(stem: &str, name: &str) -> (f32, Option<f32>) {
    if name.starts_with("cry") || stem.contains("crystal") {
        (1.8, None)
    } else if name.starts_with("ice") || stem.starts_with("ice") || stem.ends_with("ice") {
        (1.31, Some(0.08))
    } else {
        (1.5, None)
    }
}

fn falling_water(name: &str) -> bool {
    let name = name.to_ascii_lowercase();
    name.contains("fallmat") || name.contains("fallback") || name.starts_with("taki")
}

pub fn read(m: &MaterialIr) -> Reading {
    let s = signals(m);
    let full = [s.unlit[0] + s.lit_gain[0], s.unlit[1] + s.lit_gain[1], s.unlit[2] + s.lit_gain[2]];
    // Maps were mid grey: twice the result is the colour under a white map, the nearest thing to a base colour the
    // material alone gives (the textures' own means refine it at the cook).
    let base = full.map(|v| (v * 2.0).clamp(0.0, 1.0));
    let dark_base = luminance(full) < 0.2;
    let env_map = environment_maps(m).first().copied().unwrap_or(0xFF);
    let mut r = Reading {
        class: Class::Dielectric,
        signals: s,
        base,
        emission: [0.0; 3],
        roughness: 0.45,
        metal: 0.0,
        ior: 1.5,
        transmission: 0.0,
        rule: "",
        glow_source: 0,
        thickness: 0.0,
        env_tint: unit_colour(s.environment_tint),
        env_map,
        body: -1.0,
        shine: -1.0,
        tint_keep: -1.0,
    };
    let metal_hue = s.environment_saturation < 0.15 || (8.0..=60.0).contains(&s.environment_hue);
    if s.blend == 2 {
        // Only what is added to the picture behind it is a glow by the data alone: a blended, bright, unlit surface
        // may as well be water or cracked crystal (the first audit's mistakes). The game's draw category settles
        // the rest at run time (bloom and glare buffers).
        r.class = Class::Emissive;
        r.emission = s.unlit.map(|v| (v * 2.0).min(4.0));
        r.rule = "added to what is behind it";
    } else if s.blend != 0 && (s.environment || s.warp) && falling_water(&m.name) {
        // A waterfall's sheet is drawn as glass is (warped or reflecting, blended), but its colour is the white water
        // (RT-MASTER-PLAN 13.3: translucent, lit; not glass, whose colour gives way to what is behind it).
        r.class = Class::Translucent;
        r.transmission = 0.5;
        r.rule = "falling water (named so), blended";
    } else if s.blend != 0 && (s.environment || s.warp) {
        r.class = Class::Glass;
        r.roughness = 0.02;
        r.transmission = 1.0;
        r.rule = "blended with a painted reflection or a warp";
    } else if s.blend != 0 {
        r.class = Class::Translucent;
        r.transmission = 0.5;
        r.rule = "blended, no reflection";
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
    // c58: a lit opaque material whose shine is a painted highlight sprite (no painted reflection of its own): a gloss
    // with the sprite's roughness and no painted bit, so the tracer gives it a mirror by Fresnel, not by a share.
    if !m.highlight_maps.is_empty() && s.lit && s.blend == 0 && r.env_map == 0xFF && matches!(r.class, Class::Dielectric | Class::Gloss) {
        r.class = Class::Gloss;
        r.roughness = m.highlight_rough;
        r.rule = "a painted highlight sprite: the traced sun's highlight and a mirror by Fresnel (c58)";
    }
    // Holes are not a look (14.2 change 1): an alpha-tested material keeps the class its look gives; a two-sided one
    // is a thin sheet that lets some light through.
    if s.alpha_test && s.blend == 0 && s.two_sided {
        r.transmission = r.transmission.max(0.25);
    }
    r.glow_source = glow_source(m);
    r
}

/// A colour with its largest channel at 1 (black stays black).
fn unit_colour(c: [f32; 3]) -> [f32; 3] {
    let top = c[0].max(c[1]).max(c[2]);
    if top <= 1e-4 {
        return [0.0; 3];
    }
    c.map(|v| (v / top).clamp(0.0, 1.0))
}

/// Which of the material's colours carries what it shows unlit (RT-MASTER-PLAN 8.1 L-1), as NebulaModelHit.glow_source:
/// 1 + the NEBULA_ROW_* code (registers 1 to 3: 0 to 2, constants 0 to 3: 3 to 6, material colours: 7, 8) of the one
/// whose loss darkens the unlit result most, when that is a quarter of it or more; 16 the vertex colour; 0 none (a
/// glow then keeps the colour it was cooked with).
pub fn glow_source(m: &MaterialIr) -> u8 {
    let shown = |m: &MaterialIr| luminance(signals(m).unlit);
    let whole = shown(m);
    if whole < 0.02 {
        return 0;
    }
    let mut best = (0.0f32, 0u8);
    let mut consider = |drop: f32, code: u8| {
        if drop > best.0 {
            best = (drop, code);
        }
    };
    for r in 0..3 {
        let mut t = m.clone();
        t.registers[r][0] = 0;
        t.registers[r][1] = 0;
        t.registers[r][2] = 0;
        consider(whole - shown(&t), 1 + r as u8);
    }
    for k in 0..4 {
        let mut t = m.clone();
        t.constants[k][0] = 0;
        t.constants[k][1] = 0;
        t.constants[k][2] = 0;
        consider(whole - shown(&t), 4 + k as u8);
    }
    for n in 0..2 {
        let mut t = m.clone();
        t.material_color[n][0] = 0;
        t.material_color[n][1] = 0;
        t.material_color[n][2] = 0;
        consider(whole - shown(&t), 8 + n as u8);
    }
    if m.channel_count != 0 && m.channels[0].material_source != 0 {
        // The vertex colour: the reading takes it as mid grey; with the register as black instead, what it carried.
        let mut t = m.clone();
        t.channels[0].material_source = 0;
        t.material_color[0] = [0, 0, 0, t.material_color[0][3]];
        consider(whole - shown(&t), 16);
    }
    if best.0 >= 0.25 * whole {
        best.1
    } else {
        0
    }
}

/// Water surfaces (data/rt_water.tsv, RT-MASTER-PLAN 13.3): a model's file stem and a material name (a trailing `*`:
/// names that begin so), both in any case.
const WATER_TABLE: &str = include_str!("../../data/rt_water.tsv");

/// The review's overrides (data/rt_materials.tsv, RT-MASTER-PLAN 14.2 change 8 and 14.4).
const MATERIALS_TABLE: &str = include_str!("../../data/rt_materials.tsv");

fn name_matches(pattern: &str, name: &str) -> bool {
    let (pattern, name) = (pattern.trim().to_ascii_lowercase(), name.to_ascii_lowercase());
    match pattern.strip_suffix('*') {
        Some(head) => name.starts_with(head),
        None => pattern == name,
    }
}

fn rows_of(table: &str) -> (Vec<String>, Vec<Vec<&str>>) {
    let mut lines = table.lines().map(|l| l.trim_end_matches('\r')).filter(|l| !l.is_empty() && !l.starts_with('#'));
    let head = lines.next().map(|h| h.split('\t').map(|c| c.trim().to_ascii_lowercase().replace('_', " ")).collect()).unwrap_or_default();
    (head, lines.map(|l| l.split('\t').collect()).collect())
}

/// What the tables say of the material `name` of the model `stem` (its file stem): water first, then the review's
/// overrides, last. `measured`: the thickness its meshes give.
pub fn finish(stem: &str, name: &str, measured: f32, r: &mut Reading) {
    let stem = stem.split('.').next().unwrap_or(stem).to_ascii_lowercase();
    r.thickness = measured;
    let packs = PACK_TABLES.read().unwrap_or_else(|e| e.into_inner());
    let is_water = |table: &str| {
        rows_of(table).1.iter().any(|row| row.len() >= 2 && row[0].trim().eq_ignore_ascii_case(&stem) && name_matches(row[1], name))
    };
    if is_water(WATER_TABLE) || packs.water.iter().any(|table| is_water(table.as_str())) {
        r.class = Class::Water;
        r.ior = 1.33;
        r.roughness = 0.03;
        r.metal = 0.0;
        r.transmission = 1.0;
        r.rule = "water (rt_water.tsv)";
    }
    // Lava and suns (G8): they shine of themselves. The emissive class shows them as they are on screen (draw flag
    // 64: no share, no shadow, no added light) and to every ray; glow_of makes them lights. The review's rows after.
    if lava_or_sun(&stem, &name.to_ascii_lowercase()) && !matches!(r.class, Class::Water | Class::Glass | Class::Translucent) {
        r.class = Class::Emissive;
        r.rule = "lava or a sun, by its name (G8)";
    }
    // The clear classes (A4-W6; LIGHT-MODEL.md 6, 7): the surface untinted, the colour absorption; glass's index by
    // its name. The review's rows come after and may say otherwise.
    if r.class == Class::Glass || r.class == Class::Water {
        r.tint_keep = 0.0;
    }
    if r.class == Class::Glass {
        let (ior, roughness) = clear_kind(&stem, &name.to_ascii_lowercase());
        r.ior = ior;
        if let Some(roughness) = roughness {
            r.roughness = roughness;
        }
        // c62 (A4-K5): the game's other glass is a shell (capsules, bubbles, barriers, tanks, windows, transparent
        // planets): a closed mesh's 3V/A made each a solid ball to rt_transmit. Crystal and ice keep the body.
        if roughness.is_none() && ior < 1.7 {
            r.thickness = 0.0;
        }
    }
    review(MATERIALS_TABLE, &stem, name, r);
    for table in &packs.materials {
        review(table, &stem, name, r);
    }
}

/// A review table's rows (the game's data/rt_materials.tsv, or a pack's) for the material `name` of the model `stem`
/// (lower case): each matching row's fields over what `r` holds.
fn review(table: &str, stem: &str, name: &str, r: &mut Reading) {
    let (head, rows) = rows_of(table);
    let column = |what: &str| head.iter().position(|h| h == what);
    let (model, material) = (column("model"), column("material"));
    let (Some(model), Some(material)) = (model, material) else { return };
    for row in rows {
        let field = |what: &str| column(what).and_then(|c| row.get(c)).map(|v| v.trim()).filter(|v| !v.is_empty());
        let number = |what: &str| field(what).and_then(|v| v.parse::<f32>().ok()).filter(|v| v.is_finite());
        let numbers = |what: &str| field(what).map(|v| v.split_whitespace().filter_map(|x| x.parse::<f32>().ok()).collect::<Vec<f32>>());
        if !row.get(model).is_some_and(|m| m.trim().eq_ignore_ascii_case(&stem)) || !row.get(material).is_some_and(|m| name_matches(m, name)) {
            continue;
        }
        if let Some(class) = field("class").and_then(|c| Class::by_name(&c.to_ascii_lowercase())) {
            r.class = class;
            r.rule = "the review (rt_materials.tsv)";
        }
        if let Some(v) = number("roughness") {
            r.roughness = v.clamp(0.0, 1.0);
        }
        if let Some(v) = number("metal") {
            r.metal = v.clamp(0.0, 1.0);
        }
        if let Some(v) = number("ior") {
            r.ior = v.clamp(1.0, 3.0);
        }
        if let Some(v) = number("thickness") {
            r.thickness = v.max(0.0);
        }
        if let Some(v) = number("tint keep") {
            r.tint_keep = v.clamp(0.0, 1.0);
        }
        if let Some(v) = number("body") {
            r.body = v.clamp(0.0, 1.0);
        }
        if let Some(v) = number("shine") {
            r.shine = v.clamp(0.0, 16.0);
        }
        if let Some(v) = numbers("emission").filter(|v| v.len() == 3) {
            r.emission = [v[0], v[1], v[2]].map(|x| x.clamp(0.0, 16.0));
        }
    }
}

/// Roughness from the art (14.2 change 4): a painted environment map's sharpness, the mean size of its luminance's
/// gradient over level 0's texels (0 to 1 a texel), as roughness: crisp chrome 0.03, a soft gradient 0.35.
pub fn roughness_of_map(width: u32, height: u32, texels: &[[u8; 4]]) -> Option<f32> {
    let (w, h) = (width as usize, height as usize);
    if w < 2 || h < 2 || texels.len() < w * h {
        return None;
    }
    let l = |x: usize, y: usize| {
        let t = texels[y * w + x];
        (0.2126 * f32::from(t[0]) + 0.7152 * f32::from(t[1]) + 0.0722 * f32::from(t[2])) / 255.0
    };
    let mut sum = 0.0f64;
    for y in 0..h - 1 {
        for x in 0..w - 1 {
            let (dx, dy) = (l(x + 1, y) - l(x, y), l(x, y + 1) - l(x, y));
            sum += f64::from((dx * dx + dy * dy).sqrt());
        }
    }
    let s = (sum / ((w - 1) * (h - 1)) as f64) as f32;
    Some((0.35 - 3.5 * s).clamp(0.03, 0.35))
}

/// The mean colour of level 0's texels, largest channel 1.
pub fn mean_colour(texels: &[[u8; 4]]) -> Option<[f32; 3]> {
    if texels.is_empty() {
        return None;
    }
    let mut sum = [0.0f64; 3];
    for t in texels {
        for c in 0..3 {
            sum[c] += f64::from(t[c]);
        }
    }
    let n = texels.len() as f64 * 255.0;
    Some(unit_colour([(sum[0] / n) as f32, (sum[1] / n) as f32, (sum[2] / n) as f32]))
}

/// Every material of every model under CONTENT, one row each, and the classes' counts.
pub fn audit(input: &Path, output: &Path) -> Result<()> {
    let mut out = std::io::BufWriter::new(std::fs::File::create(output)?);
    // c49: every painted sheen drawn as an added layer, the body found under it and the body's class before and after.
    let mut sheens = std::io::BufWriter::new(std::fs::File::create(output.with_file_name("sheen_layers.tsv"))?);
    writeln!(
        sheens,
        "# Written by nebula-import rt-materials (c49). A sheen is not drawn while tracing; its body mirrors. none: no body\n\
         # found, the sheen stays drawn. unknown kind: the body is a gloss coat; give it a row.\n\
         model\tsheen\tkind\tenvironment share\tbody\tbody class before\tbody class after"
    )?;
    // G8: what the lava-and-sun rule took, and what it did not take that looks like it by its data (unlit, opaque, and
    // bright and warm, or a moving texture): each of the second read once and settled by a row.
    let mut by_rule = std::io::BufWriter::new(std::fs::File::create(output.with_file_name("glow_by_rule.tsv"))?);
    writeln!(
        by_rule,
        "# Written by nebula-import rt-materials (c46, G8). taken: the name rule (hit_record.rs lava_or_sun). looks like it: unlit, opaque,\n\
         # base x map mean bright (luminance over 0.5) and warm (red over twice blue), or its texture moves; settle each by a row in\n\
         # rt_glow.tsv (strength 0: not a light) or say why not. size: the material's shapes' box in metres at scale 1; area: over 6 m\n\
         # in two axes (rt_walk's L-3), else a lamp.\n\
         why\tmodel\tmaterial\tclass\tlit\tblend\tmoving texture\tmean colour\tluminance\tsize\tlamp or area\tglow"
    )?;
    writeln!(
        out,
        "model\tmaterial\tname\tclass\trule\tlit\tspecular\tvertex colour\tenvironment\tenvironment share\tenvironment hue\tenvironment saturation\t\
         ramp\tbump\twarp\talpha test\tblend\ttwo-sided\tdepth write\tanimated coordinates\tunlit\tlit gain\tbase\temission\troughness\tmetal\ttransmission\tstages\tmaps\tglow\t\
         ior\tthickness\tglow source\tenvironment map\tenvironment tint\tbody\tshine\ttint keep"
    )?;
    // c56: every normal-indexed map, its numbers and whether it is a colour ramp or a painted reflection.
    let mut ramps = std::io::BufWriter::new(std::fs::File::create(output.with_file_name("env_or_ramp.tsv"))?);
    writeln!(
        ramps,
        "# Written by nebula-import rt-materials (c56). rows/columns/rings: the share of the map's colour variance its rows', columns',\n\
         # rings' means explain; hue span in degrees among saturated texels; dark: share under luminance 0.1; spread: colour sd.\n\
         verdict\tmodel\tmaterial\tmap\ttexture\tsize\trows\tcolumns\trings\tsaturation\thue span\tdark\tspread"
    )?;
    let (mut ramp_count, mut picture_count, mut sprite_count) = (0u32, 0u32, 0u32);
    // c55 (d07's census): each model's de-light with c37 alone and with the first map's light (M5).
    let mut debake = std::io::BufWriter::new(std::fs::File::create(output.with_file_name("debake_maps.tsv"))?);
    writeln!(debake, "model\tde-lit meshes\twith a map ratio\tvertices\tmean gain c37\tmean gain d07")?;
    let (mut debaked_meshes, mut mapped_meshes, mut debaked_models, mut mapped_models) = (0usize, 0usize, 0usize, 0usize);
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
        let stem = name.rsplit('/').next().unwrap_or(name);
        let measured = super::model_write::material_measures(&ir, stem);
        let roles = sheen_roles(&ir, stem);
        for m in &ir.materials {
            for map in normal_maps(m) {
                let Some((w, h, texels)) = map_texels(&ir, m, map) else { continue };
                let Some(s) = map_shape(w, h, &texels) else { continue };
                let ramp = m.ramp_maps.contains(&map);
                let sprite = m.highlight_maps.contains(&map);
                ramp_count += u32::from(ramp);
                sprite_count += u32::from(sprite);
                picture_count += u32::from(!ramp && !sprite);
                let texture = m.textures.get(usize::from(map)).and_then(|&t| ir.textures.get(t as usize)).map_or("", |t| t.name.as_str());
                writeln!(
                    ramps,
                    "{}\t{name}\t{}\t{map}\t{texture}\t{w} x {h}\t{:.3}\t{:.3}\t{:.3}\t{:.2}\t{:.0}\t{:.2}\t{:.3}",
                    if ramp {
                        "ramp".to_string()
                    } else if sprite {
                        format!("highlight (rough {:.2}, peak {:.2})", sprite_roughness(&s), s.peak)
                    } else {
                        "reflection".to_string()
                    },
                    m.name,
                    s.rows,
                    s.columns,
                    s.rings,
                    s.saturation,
                    s.hue_span,
                    s.dark,
                    s.spread
                )?;
            }
        }
        if let Some(d) = super::model_write::debake_report(&ir, stem) {
            writeln!(debake, "{name}\t{}\t{}\t{}\t{:.3}\t{:.3}", d.debaked, d.mapped, d.vertices, d.gain_c37, d.gain_d07)?;
            debaked_meshes += d.debaked;
            mapped_meshes += d.mapped;
            debaked_models += 1;
            mapped_models += usize::from(d.mapped > 0);
        }
        for (i, m) in ir.materials.iter().enumerate() {
            let mut r = read(m);
            super::model_write::finish_reading(&ir, stem, i, measured.get(i).copied().unwrap_or(0.0), &mut r);
            let s = r.signals;
            let glow = glow_of(stem.split('.').next().unwrap_or(stem), &m.name, &r);
            let triple = |v: [f32; 3]| format!("{:.2} {:.2} {:.2}", v[0], v[1], v[2]);
            let flag = |b: bool| u8::from(b);
            writeln!(
                out,
                "{name}\t{i}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{:.2}\t{:.0}\t{:.2}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{:.2}\t{:.0}\t{:.2}\t{}\t{}\t{}\t{:.2}\t{:.0}\t{}\t{}\t{}\t{:.2}\t{:.2}\t{:.2}",
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
                },
                r.ior,
                r.thickness,
                r.glow_source,
                if r.env_map == 0xFF { String::new() } else { r.env_map.to_string() },
                if r.env_map == 0xFF { String::new() } else { triple(r.env_tint) },
                r.body,
                r.shine,
                r.tint_keep
            )?;
            glowing += u32::from(glow[3] != 0.0);
            if let Some(SheenRole::Sheen { body }) = roles.get(i).copied() {
                let class_of = |j: usize, promoted: bool| {
                    let mut b = read(&ir.materials[j]);
                    finish(stem, &ir.materials[j].name, 0.0, &mut b);
                    if promoted {
                        promote_sheen_body(sheen_kind(stem, &m.name), &mut b);
                    }
                    b.class.name()
                };
                writeln!(
                    sheens,
                    "{name}\t{}\t{:?}\t{:.2}\t{}\t{}\t{}",
                    m.name,
                    sheen_kind(stem, &m.name),
                    s.environment_share,
                    body.map_or_else(|| "none".to_string(), |j| ir.materials[j].name.clone()),
                    body.map_or("", |j| class_of(j, false)),
                    body.map_or("", |j| class_of(j, true))
                )?;
            }
            let short = stem.split('.').next().unwrap_or(stem).to_ascii_lowercase();
            let taken = lava_or_sun(&short, &m.name.to_ascii_lowercase());
            let map = m
                .textures
                .first()
                .and_then(|&t| ir.textures.get(t as usize))
                .and_then(|t| ir.records.get(t.record))
                .and_then(|bytes| super::texture::record_level0(bytes))
                .filter(|(_, _, texels)| !texels.is_empty())
                .map(|(_, _, texels)| {
                    let mut sum = [0.0f64; 3];
                    for t in &texels {
                        for c in 0..3 {
                            sum[c] += f64::from(t[c]);
                        }
                    }
                    let n = texels.len() as f64 * 255.0;
                    [(sum[0] / n) as f32, (sum[1] / n) as f32, (sum[2] / n) as f32]
                })
                .unwrap_or([1.0; 3]);
            let mean = [(r.base[0] * map[0]).min(1.0), (r.base[1] * map[1]).min(1.0), (r.base[2] * map[2]).min(1.0)];
            let warm = luminance(mean) > 0.5 && mean[0] > 2.0 * mean[2];
            let clear = matches!(r.class, Class::Water | Class::Glass | Class::Translucent);
            let looks = !taken && !s.lit && s.blend == 0 && !clear && (warm || s.animated_coords);
            if taken || looks {
                let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
                for shape in ir.shapes.iter().filter(|shape| shape.material as usize == i) {
                    for k in 0..3 {
                        lo[k] = lo[k].min(shape.min[k]);
                        hi[k] = hi[k].max(shape.max[k]);
                    }
                }
                let size = if lo[0] <= hi[0] { [hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]] } else { [0.0; 3] };
                let wide = size.iter().filter(|&&v| v > 600.0).count() >= 2;
                let why = if taken { "taken" } else if warm { "looks like it: bright, warm" } else { "looks like it: moving texture" };
                writeln!(
                    by_rule,
                    "{why}\t{name}\t{}\t{}\t{}\t{}\t{}\t{}\t{:.2}\t{:.1} x {:.1} x {:.1} m\t{}\t{}",
                    m.name,
                    r.class.name(),
                    flag(s.lit),
                    s.blend,
                    flag(s.animated_coords),
                    triple(mean),
                    luminance(mean),
                    size[0] / 100.0,
                    size[1] / 100.0,
                    size[2] / 100.0,
                    if wide { "area" } else { "lamp" },
                    if glow[3] > 0.0 {
                        format!("{:.0} cd", glow[3])
                    } else if glow[3] < 0.0 {
                        format!("{:.0} cd/m2", -glow[3])
                    } else {
                        "none".to_string()
                    }
                )?;
            }
            let entry = classes.entry(r.class).or_default();
            entry.0 += 1;
            materials += 1;
            let _ = &mut entry.1;
        }
        Ok(())
    })?;
    out.flush()?;
    sheens.flush()?;
    by_rule.flush()?;
    debake.flush()?;
    ramps.flush()?;
    println!("normal-indexed maps (c56, c58): {ramp_count} colour ramps, {sprite_count} highlight sprites, {picture_count} painted reflections");
    println!("de-light (c55): {debaked_meshes} meshes in {debaked_models} models; {mapped_meshes} meshes in {mapped_models} models with a map ratio (d07)");
    println!("{models} models, {materials} materials, {glowing} of them glow; {unread} files not read");
    for (class, (count, _)) in &classes {
        println!("{:<12} {count:>6}  {:.1} %", class.name(), 100.0 * f64::from(*count) / f64::from(materials.max(1)));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clear_kinds_by_name() {
        assert_eq!(clear_kind("assemblyblockpartsa", "crystal_v"), (1.8, None));
        assert_eq!(clear_kind("stardustartplanet", "crya_v"), (1.8, None));
        assert_eq!(clear_kind("smallmazeplanet", "icemat"), (1.31, Some(0.08)));
        assert_eq!(clear_kind("icecube", "lambert2(2)"), (1.31, Some(0.08)));
        assert_eq!(clear_kind("karikariice", "omotemat_v"), (1.31, Some(0.08)));
        assert_eq!(clear_kind("bathtubplanet", "glass_v"), (1.5, None));
    }

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

    fn painted() -> MaterialIr {
        let mut m = MaterialIr { pixel_stage_count: 1, channel_count: 1, depth_write: 1, cull: 2, alpha_comp0: 7, alpha_comp1: 7, ..Default::default() };
        let mut s = stage([15, 15, 15, 10], [7, 7, 7, 5]);
        s.channel = 0;
        m.stages.push(s);
        m.swaps = [[0, 1, 2, 3]; 4];
        m.material_color = [[255; 4]; 2];
        m
    }

    #[test]
    fn an_alpha_tested_material_keeps_its_look() {
        // Holes are not a look: an unlit alpha-tested sheet is painted and lets some light through, two-sided.
        let mut m = painted();
        m.alpha_comp0 = 4;
        m.alpha_ref0 = 128;
        m.alpha_comp1 = 0;
        m.alpha_op = 0;
        m.cull = 0;
        let r = read(&m);
        assert_eq!(r.class, Class::Painted);
        assert!(r.signals.alpha_test && r.transmission >= 0.25);
    }

    #[test]
    fn the_colour_a_glow_shows_is_found() {
        // The stage passes the raster colour, the register's material colour: losing it loses everything.
        let m = painted();
        assert_eq!(glow_source(&m), 8, "material colour 0: NEBULA_ROW_MATERIAL_COLOR + 1");
        let mut dark = painted();
        dark.material_color = [[0, 0, 0, 255]; 2];
        assert_eq!(glow_source(&dark), 0, "nothing shown: no source");
    }

    #[test]
    fn lava_and_suns_are_named_so() {
        for (stem, name) in [("lavahomeplanet", "lavamat_v"), ("lavameteoplanet", "lava_m_v"), ("lavajshapeplanet", "lava2_v"),
                             ("floaterotakingplanet", "lava00_v"), ("iceringfireplanet", "lava_01_v"), ("hellballrotatepartsa", "lava_v"),
                             ("koopavs3cylinderinsideplanetlow", "magma_lod_v"), ("bossplanetr50", "yougan"),
                             ("koopabattlesunplanet", "sunmat"), ("lavajambosunplanet", "sunmat_v"), ("lavahomevolcanoflow", "lavamat"),
                             ("lavaprominenceenvironment", "prominencemat_v_x"), ("lavaprominencetriple", "prominencemat_v")] {
            assert!(lava_or_sun(stem, name), "{stem}/{name}");
        }
        for (stem, name) in [("lavahomeplanet", "lavarock_v"), ("lavahomeplanet", "lavaundergorund_v"),
                             ("lavahomevolcanoinnerflow", "lavaedgemat_v"), ("koopabattlemapplanetbloom", "lavamat_v"),
                             ("minihellprominencegalaxy", "sunmat_v"), ("vrdarkspace", "sunmat"), ("koopavs3sandplanet", "suna_v"),
                             ("lavahomeplanet", "meltmountain_v"), ("lavaprominencetriplebloom", "prominencemat_v")] {
            assert!(!lava_or_sun(stem, name), "{stem}/{name}");
        }
    }

    #[test]
    fn sheens_say_their_body() {
        assert_eq!(sheen_kind("crystalcages.bdl", "HiLightMat"), SheenKind::Crystal);
        assert_eq!(sheen_kind("capsulecage", "CrystalFaceMat(2)"), SheenKind::Crystal);
        assert_eq!(sheen_kind("bosskameck", "StickJewelHiLightMat_v"), SheenKind::Crystal);
        assert_eq!(sheen_kind("koopavs3dryiceplanet", "DIceFace01_v"), SheenKind::Ice);
        assert_eq!(sheen_kind("mariofreezeice", "Break_HiLightMat(3)"), SheenKind::Ice);
        assert_eq!(sheen_kind("someplanet", "glass_omote_v"), SheenKind::Glass);
        assert_eq!(sheen_kind("starpiececluster", "StarPieceClusterMat_v"), SheenKind::Unknown);
    }

    #[test]
    fn falling_water_is_named_so() {
        for name in ["FallMat_v_x", "A_FallMat_v_x", "zz_FallMat_v_x", "DxFallMat_v_x", "HxCupFallMat_v_x", "ExFallMatSplash_v_x", "taki_v_x",
                     "FallBackFaceMat_v", "AxCupFallMatBackFace_v_x"] {
            assert!(falling_water(name), "{name}");
        }
        for name in ["glass_v_x", "OtaKingLow_a_v", "crystal_v", "WaterMat_v_x", "Rainfall_v", "FallenLeaf_v"] {
            assert!(!falling_water(name), "{name}");
        }
    }

    #[test]
    fn water_and_overrides_finish_a_reading() {
        let m = painted();
        let mut r = read(&m);
        finish("heavenlybeachplanetwater.bdl", "CxSeaMat", 40.0, &mut r);
        assert_eq!(r.class, Class::Water);
        assert!((r.ior - 1.33).abs() < 1e-6 && (r.thickness - 40.0).abs() < 1e-6);
        let mut other = read(&m);
        finish("somethingelse", "CxSeaMat", 3.0, &mut other);
        assert_eq!(other.class, Class::Painted);
        assert!(name_matches("Water*", "watermat_v_x") && !name_matches("Water", "watermat"));
    }

    #[test]
    fn a_crisp_map_is_smooth_and_a_soft_one_rough() {
        let flat = vec![[128u8, 128, 128, 255]; 16];
        assert!((roughness_of_map(4, 4, &flat).unwrap() - 0.35).abs() < 1e-6, "no detail: the softest");
        let mut checker = vec![[0u8, 0, 0, 255]; 16];
        for (i, t) in checker.iter_mut().enumerate() {
            if (i % 4 + i / 4) % 2 == 0 {
                *t = [255, 255, 255, 255];
            }
        }
        assert!((roughness_of_map(4, 4, &checker).unwrap() - 0.03).abs() < 1e-6, "every texel an edge: chrome");
        assert_eq!(mean_colour(&[[255, 128, 0, 255]]).unwrap()[0], 1.0);
    }
}
