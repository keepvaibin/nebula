//! Particles written as JSON (`ParticleData/Effect.arc/Particles.jpc.json` in a mod's merge folder), cooked to the same
//! records as the game's own (particles.rs, ParticleData.hpp), and the game's particles written back as that JSON
//! (`lumalink extract`), so a mod can start from one of them.
//!
//! The file:
//!
//! ```json
//! { "textures": { "GoldGlow": "gold_glow.png" },
//!   "particles": [ { "like": "Glow", "emitter": { "rate": 4 }, "shape": { "texture": "GoldGlow", "primary": "ffd040ff" } } ] }
//! ```
//!
//! Each particle's number is its place in the list, as each one's name is its place in ParticleNames.bcsv.json beside it.
//! A particle starts from the game's particle `like` names, else from a plain glowing dot, and each value given replaces
//! that one: every value of every record has a name ([`DYNAMICS`], [`SHAPE`], [`EXTRA`], [`CHILD`], [`EXTEX`],
//! [`FIELD`]). The values the game derives (the fade and scale rates, the colour tables, the counts) are derived here.
//! `null` for `extra`, `child` or `second_texture` takes that record away.

use super::particles::{ParticleIr, Resource, Shape, Texture};
use anyhow::{bail, Context, Result};
use serde_json::{json, Map, Value};
use std::collections::HashMap;
use std::path::Path;

#[derive(Clone, Copy)]
enum Kind {
    U8,
    Bool,
    I8,
    I16,
    U16,
    U32,
    F32,
    Vec3,
    I16Vec3,
    Floats(usize),
    Rgba,
    /// A bit of the u32 at the field's place.
    Bit(u32),
    /// A u8 named from a list (or its number).
    Choice(&'static [&'static str]),
    ChoiceU32(&'static [&'static str]),
}

#[derive(Clone, Copy)]
pub struct Field {
    pub name: &'static str,
    at: usize,
    kind: Kind,
}

const fn f(name: &'static str, at: usize, kind: Kind) -> Field {
    Field { name, at, kind }
}

const VOLUMES: &[&str] = &["cube", "sphere", "cylinder", "torus", "point", "circle", "line"];
const SHAPE_TYPES: &[&str] = &[
    "point", "line", "billboard", "direction", "direction_cross", "stripe", "stripe_cross", "rotation", "rotation_cross",
    "direction_billboard", "y_billboard",
];
const DIRECTIONS: &[&str] = &["velocity", "position", "position_inverse", "emitter_direction", "previous_particle"];
const ROTATIONS: &[&str] = &["y", "x", "z", "xyz", "y_jiggle"];
const PLANES: &[&str] = &["xy", "xz", "x"];
const COLOR_MODES: &[&str] = &[
    "texture", "texture_times_color", "color_to_white", "environment_to_color", "environment_plus_color_times_texture", "color",
];
const ALPHA_MODES: &[&str] = &["texture_times_color", "color"];
const BLEND_OPS: &[&str] = &["replace", "add", "subtract"];
const FACTORS: &[&str] = &[
    "zero", "one", "source_color", "inverse_source_color", "source_alpha", "inverse_source_alpha", "destination_alpha",
    "inverse_destination_alpha", "destination_color", "inverse_destination_color",
];
const COMPARES: &[&str] = &["never", "less", "equal", "less_or_equal", "greater", "not_equal", "greater_or_equal", "always"];
const ALPHA_OPS: &[&str] = &["and", "or", "exclusive_or", "equal"];
const ANIMATION_TYPES: &[&str] = &["once", "repeat", "reverse", "merge", "random"];
const FIELD_TYPES: &[&str] = &["gravity", "air", "magnet", "newton", "vortex", "random", "drag", "convection", "spin"];
const FIELD_ADDS: &[&str] = &["base_velocity", "own_velocity", "field_velocity"];
/// ParticleKey::Target_*: the emitter value a key track drives.
const KEY_TARGETS: &[&str] = &[
    "rate", "volume_size", "", "volume_min_radius", "life", "", "speed_from_center", "speed_from_axis", "speed_along_direction",
    "spread", "scale",
];

/// EmitterDynamicsData.
pub const DYNAMICS: &[Field] = &[
    f("fixed_density", 0, Kind::Bit(0x01)),
    f("fixed_interval", 0, Kind::Bit(0x02)),
    f("inherit_scale", 0, Kind::Bit(0x04)),
    f("follow_emitter", 0, Kind::Bit(0x08)),
    f("follow_emitter_children", 0, Kind::Bit(0x10)),
    f("volume", 4, Kind::ChoiceU32(VOLUMES)),
    f("scale", 0x08, Kind::Vec3),
    f("translation", 0x14, Kind::Vec3),
    f("direction", 0x20, Kind::Vec3),
    f("speed_from_center", 0x2C, Kind::F32),
    f("speed_from_axis", 0x30, Kind::F32),
    f("speed_random", 0x34, Kind::F32),
    f("speed_along_direction", 0x38, Kind::F32),
    f("spread", 0x3C, Kind::F32),
    f("speed_ratio", 0x40, Kind::F32),
    f("rate", 0x44, Kind::F32),
    f("rate_random", 0x48, Kind::F32),
    f("life_random", 0x4C, Kind::F32),
    f("volume_sweep", 0x50, Kind::F32),
    f("volume_min_radius", 0x54, Kind::F32),
    f("air_resistance", 0x58, Kind::F32),
    f("moment", 0x5C, Kind::F32),
    f("rotation", 0x60, Kind::I16Vec3),
    f("last_frame", 0x66, Kind::I16),
    f("first_frame", 0x68, Kind::I16),
    f("life", 0x6A, Kind::I16),
    f("volume_size", 0x6C, Kind::U16),
    f("divisions", 0x6E, Kind::U16),
    f("rate_step", 0x70, Kind::U8),
];

/// ParticleShapeData, but for what the colours and texture frames decide (shape_from_json).
pub const SHAPE: &[Field] = &[
    f("size", 0, Kind::Floats(2)),
    f("texture_scroll", 8, Kind::Floats(10)),
    f("type", 0x30, Kind::Choice(SHAPE_TYPES)),
    f("facing", 0x31, Kind::Choice(DIRECTIONS)),
    f("rotation_axis", 0x32, Kind::Choice(ROTATIONS)),
    f("plane", 0x33, Kind::Choice(PLANES)),
    f("tile_s", 0x34, Kind::Bool),
    f("tile_t", 0x35, Kind::Bool),
    f("color_mode", 0x36, Kind::Choice(COLOR_MODES)),
    f("alpha_mode", 0x37, Kind::Choice(ALPHA_MODES)),
    f("blend", 0x38, Kind::Choice(BLEND_OPS)),
    f("blend_source", 0x39, Kind::Choice(FACTORS)),
    f("blend_destination", 0x3A, Kind::Choice(FACTORS)),
    f("depth_test", 0x3B, Kind::Bool),
    f("depth_compare", 0x3C, Kind::Choice(COMPARES)),
    f("depth_write", 0x3D, Kind::Bool),
    f("alpha_compare_0", 0x3E, Kind::Choice(COMPARES)),
    f("alpha_compare_1", 0x3F, Kind::Choice(COMPARES)),
    f("alpha_compare_op", 0x40, Kind::Choice(ALPHA_OPS)),
    f("alpha_reference_0", 0x41, Kind::U8),
    f("alpha_reference_1", 0x42, Kind::U8),
    f("colors_shared", 0x43, Kind::Bool),
    f("texture_frames_shared", 0x44, Kind::Bool),
    f("projected_texture", 0x45, Kind::Bool),
    f("draw_newest_first", 0x46, Kind::Bool),
    f("draw_parents_first", 0x47, Kind::Bool),
    f("texture_scroll_on", 0x48, Kind::Bool),
    f("hide_parents", 0x49, Kind::Bool),
    f("hide_children", 0x4A, Kind::Bool),
    f("color_animation", 0x4D, Kind::Choice(ANIMATION_TYPES)),
    f("texture_animation", 0x4F, Kind::Choice(ANIMATION_TYPES)),
    f("texture_index", 0x50, Kind::U8),
    f("color_loop_mask", 0x52, Kind::U8),
    f("texture_loop_mask", 0x53, Kind::U8),
    f("loop_value", 0x54, Kind::U8),
    f("primary", 0x58, Kind::Rgba),
    f("environment", 0x5C, Kind::Rgba),
];
const SHAPE_PRM_ANM: usize = 0x4B;
const SHAPE_ENV_ANM: usize = 0x4C;
const SHAPE_TEX_ANM: usize = 0x4E;
const SHAPE_TEX_KEYS: usize = 0x51;
const SHAPE_LAST_COLOR_FRAME: usize = 0x56;

/// ExtraShapeData: scale, alpha and rotation over a particle's life.
pub const EXTRA: &[Field] = &[
    f("scale_animation", 0, Kind::Bool),
    f("scale_xy_apart", 1, Kind::Bool),
    f("scale_animation_x", 2, Kind::Choice(ANIMATION_TYPES)),
    f("scale_animation_y", 3, Kind::Choice(ANIMATION_TYPES)),
    f("scale_center_x", 4, Kind::U8),
    f("scale_center_y", 5, Kind::U8),
    f("alpha_animation", 6, Kind::Bool),
    f("alpha_flicker", 7, Kind::Bool),
    f("rotate", 8, Kind::Bool),
    f("scale_cycle_x", 0xA, Kind::I16),
    f("scale_cycle_y", 0xC, Kind::I16),
    f("scale_in_until", 0x10, Kind::F32),
    f("scale_out_from", 0x14, Kind::F32),
    f("scale_in_x", 0x18, Kind::F32),
    f("scale_out_x", 0x1C, Kind::F32),
    f("scale_in_y", 0x20, Kind::F32),
    f("scale_out_y", 0x24, Kind::F32),
    f("scale_out_random", 0x28, Kind::F32),
    f("alpha_in_until", 0x2C, Kind::F32),
    f("alpha_out_from", 0x30, Kind::F32),
    f("alpha_in", 0x34, Kind::F32),
    f("alpha", 0x38, Kind::F32),
    f("alpha_out", 0x3C, Kind::F32),
    f("alpha_wave_frequency", 0x40, Kind::F32),
    f("alpha_wave_random", 0x44, Kind::F32),
    f("alpha_wave_amplitude", 0x48, Kind::F32),
    f("rotate_angle", 0x4C, Kind::F32),
    f("rotate_angle_random", 0x50, Kind::F32),
    f("rotate_speed", 0x54, Kind::F32),
    f("rotate_speed_random", 0x58, Kind::F32),
    f("rotate_direction", 0x5C, Kind::F32),
];

/// ChildShapeData: the particles each particle leaves behind.
pub const CHILD: &[Field] = &[
    f("type", 0, Kind::Choice(SHAPE_TYPES)),
    f("facing", 1, Kind::Choice(DIRECTIONS)),
    f("rotation_axis", 2, Kind::Choice(ROTATIONS)),
    f("plane", 3, Kind::Choice(PLANES)),
    f("inherit_scale_on", 4, Kind::Bool),
    f("inherit_alpha_on", 5, Kind::Bool),
    f("inherit_color_on", 6, Kind::Bool),
    f("fields_move_them", 7, Kind::Bool),
    f("scale_out", 8, Kind::Bool),
    f("alpha_out", 9, Kind::Bool),
    f("rotate", 0xA, Kind::Bool),
    f("position_random", 0x0C, Kind::F32),
    f("speed", 0x10, Kind::F32),
    f("speed_random", 0x14, Kind::F32),
    f("parent_speed", 0x18, Kind::F32),
    f("gravity", 0x1C, Kind::F32),
    f("scale_x", 0x20, Kind::F32),
    f("scale_y", 0x24, Kind::F32),
    f("inherit_scale", 0x28, Kind::F32),
    f("inherit_alpha", 0x2C, Kind::F32),
    f("inherit_color", 0x30, Kind::F32),
    f("primary", 0x34, Kind::Rgba),
    f("environment", 0x38, Kind::Rgba),
    f("from", 0x3C, Kind::F32),
    f("life", 0x40, Kind::I16),
    f("rate", 0x42, Kind::I16),
    f("step", 0x44, Kind::U8),
    f("texture_index", 0x45, Kind::U8),
    f("rotate_speed", 0x46, Kind::I16),
];

/// ExTexShapeData: a second texture and an indirect one.
pub const EXTEX: &[Field] = &[
    f("indirect_matrix", 0, Kind::Floats(6)),
    f("indirect_scale", 0x18, Kind::I8),
    f("indirect_texture", 0x19, Kind::U8),
    f("second_texture", 0x1A, Kind::U8),
    f("indirect_on", 0x1B, Kind::Bool),
    f("second_on", 0x1C, Kind::Bool),
];

/// ParticleFieldData: a force on the particles.
pub const FIELD: &[Field] = &[
    f("type", 0, Kind::ChoiceU32(FIELD_TYPES)),
    f("adds_to", 4, Kind::ChoiceU32(FIELD_ADDS)),
    f("status", 8, Kind::U32),
    f("position", 0xC, Kind::Vec3),
    f("direction", 0x18, Kind::Vec3),
    f("strength", 0x24, Kind::F32),
    f("strength_random", 0x28, Kind::F32),
    f("value", 0x2C, Kind::F32),
    f("fade_in_until", 0x30, Kind::F32),
    f("fade_out_from", 0x34, Kind::F32),
    f("on_from", 0x38, Kind::F32),
    f("off_from", 0x3C, Kind::F32),
    f("cycle", 0x40, Kind::U32),
];

fn read_f32(r: &[u8], at: usize) -> f32 {
    f32::from_le_bytes(r[at..at + 4].try_into().unwrap())
}

fn number(v: f32) -> Value {
    // Shortest text that reads back as the same f32.
    serde_json::Number::from_f64(format!("{v}").parse::<f64>().unwrap_or(0.0)).map_or(Value::Null, Value::Number)
}

fn named(list: &[&str], n: u32) -> Value {
    match list.get(n as usize) {
        Some(name) if !name.is_empty() => Value::from(*name),
        _ => Value::from(n),
    }
}

fn rgba_text(c: &[u8]) -> Value {
    Value::from(format!("{:02x}{:02x}{:02x}{:02x}", c[0], c[1], c[2], c[3]))
}

/// A record's values by name.
pub fn decode(fields: &[Field], r: &[u8]) -> Map<String, Value> {
    let mut out = Map::new();
    for fd in fields {
        let at = fd.at;
        let v = match fd.kind {
            Kind::U8 => Value::from(r[at]),
            Kind::Bool => Value::from(r[at] != 0),
            Kind::I8 => Value::from(r[at] as i8),
            Kind::I16 => Value::from(i16::from_le_bytes([r[at], r[at + 1]])),
            Kind::U16 => Value::from(u16::from_le_bytes([r[at], r[at + 1]])),
            Kind::U32 => Value::from(u32::from_le_bytes(r[at..at + 4].try_into().unwrap())),
            Kind::F32 => number(read_f32(r, at)),
            Kind::Vec3 => Value::Array((0..3).map(|k| number(read_f32(r, at + 4 * k))).collect()),
            Kind::I16Vec3 => Value::Array((0..3).map(|k| Value::from(i16::from_le_bytes([r[at + 2 * k], r[at + 2 * k + 1]]))).collect()),
            Kind::Floats(n) => Value::Array((0..n).map(|k| number(read_f32(r, at + 4 * k))).collect()),
            Kind::Rgba => rgba_text(&r[at..at + 4]),
            Kind::Bit(mask) => Value::from(u32::from_le_bytes(r[at..at + 4].try_into().unwrap()) & mask != 0),
            Kind::Choice(list) => named(list, u32::from(r[at])),
            Kind::ChoiceU32(list) => named(list, u32::from_le_bytes(r[at..at + 4].try_into().unwrap())),
        };
        out.insert(fd.name.to_string(), v);
    }
    out
}

fn as_f32(v: &Value, what: &str) -> Result<f32> {
    v.as_f64().map(|x| x as f32).with_context(|| format!("{what}: a number"))
}

fn as_int(v: &Value, what: &str, min: i64, max: i64) -> Result<i64> {
    let n = v.as_i64().with_context(|| format!("{what}: a whole number"))?;
    if n < min || n > max {
        bail!("{what}: {n} is outside {min} to {max}");
    }
    Ok(n)
}

fn choice(list: &[&str], v: &Value, what: &str) -> Result<u32> {
    if let Some(n) = v.as_u64() {
        return u32::try_from(n).with_context(|| format!("{what}: {n}"));
    }
    let text = v.as_str().with_context(|| format!("{what}: one of {}", list.iter().filter(|n| !n.is_empty()).cloned().collect::<Vec<_>>().join(", ")))?;
    list.iter().position(|n| *n == text).map(|p| p as u32).with_context(|| {
        format!("{what}: \"{text}\" is not one of {}", list.iter().filter(|n| !n.is_empty()).cloned().collect::<Vec<_>>().join(", "))
    })
}

/// "rrggbbaa" (or "#rrggbbaa", or "rrggbb", opaque).
pub fn parse_rgba(v: &Value, what: &str) -> Result<[u8; 4]> {
    let text = v.as_str().with_context(|| format!("{what}: a colour as \"rrggbbaa\""))?.trim_start_matches('#');
    let digits = match text.len() {
        6 => format!("{text}ff"),
        8 => text.to_string(),
        _ => bail!("{what}: \"{text}\" is not \"rrggbbaa\""),
    };
    let word = u32::from_str_radix(&digits, 16).with_context(|| format!("{what}: \"{text}\" is not \"rrggbbaa\""))?;
    Ok(word.to_be_bytes())
}

fn floats_of(v: &Value, n: usize, what: &str) -> Result<Vec<f32>> {
    let list = v.as_array().with_context(|| format!("{what}: a list of {n} numbers"))?;
    if list.len() != n {
        bail!("{what}: {} numbers, not {n}", list.len());
    }
    list.iter().map(|x| as_f32(x, what)).collect()
}

/// `values` over record `r`: each name must be one of `fields`.
pub fn encode(fields: &[Field], record: &str, values: &Map<String, Value>, r: &mut [u8]) -> Result<()> {
    for (name, v) in values {
        let fd = fields.iter().find(|fd| fd.name == name).with_context(|| {
            format!("{record} has no value \"{name}\" (it has {})", fields.iter().map(|fd| fd.name).collect::<Vec<_>>().join(", "))
        })?;
        let what = format!("{record}.{name}");
        let at = fd.at;
        match fd.kind {
            Kind::U8 => r[at] = as_int(v, &what, 0, 255)? as u8,
            Kind::Bool => r[at] = u8::from(v.as_bool().with_context(|| format!("{what}: true or false"))?),
            Kind::I8 => r[at] = as_int(v, &what, -128, 127)? as i8 as u8,
            Kind::I16 => r[at..at + 2].copy_from_slice(&(as_int(v, &what, -32768, 32767)? as i16).to_le_bytes()),
            Kind::U16 => r[at..at + 2].copy_from_slice(&(as_int(v, &what, 0, 65535)? as u16).to_le_bytes()),
            Kind::U32 => r[at..at + 4].copy_from_slice(&(as_int(v, &what, 0, u32::MAX as i64)? as u32).to_le_bytes()),
            Kind::F32 => r[at..at + 4].copy_from_slice(&as_f32(v, &what)?.to_le_bytes()),
            Kind::Vec3 | Kind::Floats(_) => {
                let n = if let Kind::Floats(n) = fd.kind { n } else { 3 };
                for (k, x) in floats_of(v, n, &what)?.into_iter().enumerate() {
                    r[at + 4 * k..at + 4 * k + 4].copy_from_slice(&x.to_le_bytes());
                }
            }
            Kind::I16Vec3 => {
                let list = v.as_array().filter(|l| l.len() == 3).with_context(|| format!("{what}: three whole numbers"))?;
                for (k, x) in list.iter().enumerate() {
                    r[at + 2 * k..at + 2 * k + 2].copy_from_slice(&(as_int(x, &what, -32768, 32767)? as i16).to_le_bytes());
                }
            }
            Kind::Rgba => r[at..at + 4].copy_from_slice(&parse_rgba(v, &what)?),
            Kind::Bit(mask) => {
                let mut word = u32::from_le_bytes(r[at..at + 4].try_into().unwrap());
                if v.as_bool().with_context(|| format!("{what}: true or false"))? { word |= mask } else { word &= !mask }
                r[at..at + 4].copy_from_slice(&word.to_le_bytes());
            }
            Kind::Choice(list) => r[at] = u8::try_from(choice(list, v, &what)?).with_context(|| what.clone())?,
            Kind::ChoiceU32(list) => r[at..at + 4].copy_from_slice(&choice(list, v, &what)?.to_le_bytes()),
        }
    }
    Ok(())
}

/// A colour table from keys (frame, colour), as the game's loader builds one: each frame between two keys a step of the
/// way, the last key's colour after it.
fn color_table(keys: &[(i16, [u8; 4])], last_frame: i16) -> Vec<u8> {
    let entries = (i32::from(last_frame) + 1).max(0) as usize;
    let mut table = vec![[0u8; 4]; entries];
    for (i, slot) in table.iter_mut().enumerate() {
        let frame = i as i32;
        let next = keys.iter().position(|(k, _)| i32::from(*k) > frame);
        *slot = match next {
            None => keys.last().map_or([0; 4], |k| k.1),
            Some(0) => keys[0].1,
            Some(n) => {
                let (a, b) = (keys[n - 1], keys[n]);
                let t = (frame - i32::from(a.0)) as f32 / (i32::from(b.0) - i32::from(a.0)) as f32;
                std::array::from_fn(|k| (f32::from(a.1[k]) + t * (f32::from(b.1[k]) - f32::from(a.1[k]))) as i32 as u8)
            }
        };
    }
    table.concat()
}

fn color_keys(v: &Value, what: &str) -> Result<Vec<(i16, [u8; 4])>> {
    let list = v.as_array().with_context(|| format!("{what}: a list of [frame, \"rrggbbaa\"]"))?;
    let mut keys = Vec::with_capacity(list.len());
    for key in list {
        let pair = key.as_array().filter(|p| p.len() == 2).with_context(|| format!("{what}: each key is [frame, \"rrggbbaa\"]"))?;
        keys.push((as_int(&pair[0], what, 0, 32767)? as i16, parse_rgba(&pair[1], what)?));
    }
    if keys.is_empty() || keys.windows(2).any(|w| w[1].0 <= w[0].0) {
        bail!("{what}: at least one key, frames rising");
    }
    Ok(keys)
}

/// The keys of a colour table: where it changes direction (a key at each frame whose colour isn't on the line between its
/// neighbours').
fn table_keys(table: &[u8]) -> Value {
    let colors: Vec<&[u8]> = table.chunks_exact(4).collect();
    let mut keys = Vec::new();
    for (i, c) in colors.iter().enumerate() {
        let keep = i == 0 || i + 1 == colors.len() || (0..4).any(|k| {
            let (a, b, x) = (i32::from(colors[i - 1][k]), i32::from(colors[i + 1][k]), i32::from(c[k]));
            ((a + b) - 2 * x).abs() > 1
        });
        if keep {
            keys.push(json!([i, rgba_text(c)]));
        }
    }
    Value::Array(keys)
}

/// The plain particle a JSON one starts from without `like`: a soft dot, emitted once a frame, living 30 frames.
fn plain() -> Resource {
    let mut dynamics = vec![0u8; DYNAMICS_SIZE];
    let mut shape = vec![0u8; SHAPE_SIZE];
    let base = json!({ "volume": "point", "scale": [1, 1, 1], "direction": [0, 1, 0], "rate": 1, "life": 30, "air_resistance": 1,
                       "volume_sweep": 1, "speed_from_center": 1 });
    encode(DYNAMICS, "emitter", base.as_object().unwrap(), &mut dynamics).expect("the plain emitter");
    let base = json!({ "size": [10, 10], "type": "billboard", "color_mode": "texture_times_color", "alpha_mode": "texture_times_color",
                       "blend": "add", "blend_source": "source_alpha", "blend_destination": "one", "depth_test": true,
                       "depth_compare": "less_or_equal", "alpha_compare_0": "always", "alpha_compare_1": "always",
                       "primary": "ffffffff", "environment": "ffffffff" });
    encode(SHAPE, "shape", base.as_object().unwrap(), &mut shape).expect("the plain shape");
    Resource {
        user_index: 0,
        dynamics,
        shape: Shape { record: shape, prm_table: None, env_table: None, texture_table: None },
        extra: None,
        child: None,
        extex: None,
        fields: Vec::new(),
        keys: Vec::new(),
        texture_indices: Vec::new(),
    }
}

const DYNAMICS_SIZE: usize = 0x74;
const SHAPE_SIZE: usize = 0x6C;
const EXTRA_SIZE: usize = 0x78;
const CHILD_SIZE: usize = 0x48;
const EXTEX_SIZE: usize = 0x20;
const FIELD_SIZE: usize = 0x4C;

/// The rates ExtraShape::init derives, at the end of the record (particles.rs `extra` does the same from a disc file).
fn derive_extra(r: &mut [u8]) {
    let g = |o: usize| read_f32(r, o);
    let (scale_in, scale_out, in_x, out_x, in_y, out_y) = (g(0x10), g(0x14), g(0x18), g(0x1C), g(0x20), g(0x24));
    let (alpha_in, alpha_out, alpha_in_value, alpha_base, alpha_out_value) = (g(0x2C), g(0x30), g(0x34), g(0x38), g(0x3C));
    let alpha_inc = if alpha_in != 0.0 { (alpha_base - alpha_in_value) / alpha_in } else { 1.0 };
    let alpha_dec = if alpha_out != 1.0 { (alpha_out_value - alpha_base) / (1.0 - alpha_out) } else { 1.0 };
    let (inc_x, inc_y) = if scale_in != 0.0 { ((1.0 - in_x) / scale_in, (1.0 - in_y) / scale_in) } else { (1.0, 1.0) };
    let (dec_x, dec_y) = if scale_out != 1.0 { ((out_x - 1.0) / (1.0 - scale_out), (out_y - 1.0) / (1.0 - scale_out)) } else { (1.0, 1.0) };
    for (k, v) in [alpha_inc, alpha_dec, inc_x, inc_y, dec_x, dec_y].into_iter().enumerate() {
        r[0x60 + 4 * k..0x64 + 4 * k].copy_from_slice(&v.to_le_bytes());
    }
}

/// The fade rates ParticleField::init derives.
fn derive_field(r: &mut [u8]) {
    let (fade_in, fade_out, enable, disable) = (read_f32(r, 0x30), read_f32(r, 0x34), read_f32(r, 0x38), read_f32(r, 0x3C));
    let in_rate = if fade_in - enable == 0.0 { 1.0 } else { 1.0 / (fade_in - enable) };
    let out_rate = if disable - fade_out == 0.0 { 1.0 } else { 1.0 / (disable - fade_out) };
    r[0x44..0x48].copy_from_slice(&in_rate.to_le_bytes());
    r[0x48..0x4C].copy_from_slice(&out_rate.to_le_bytes());
}

fn object<'a>(v: &'a Value, what: &str) -> Result<&'a Map<String, Value>> {
    v.as_object().with_context(|| format!("{what}: an object of values by name"))
}

/// A record that may be taken away (`null`), made from nothing (zeros, then its values), or changed.
fn optional_record(current: &mut Option<Vec<u8>>, v: Option<&Value>, fields: &[Field], what: &str, size: usize) -> Result<()> {
    match v {
        None => {}
        Some(Value::Null) => *current = None,
        Some(v) => {
            let mut r = current.take().unwrap_or_else(|| vec![0u8; size]);
            encode(fields, what, object(v, what)?, &mut r)?;
            *current = Some(r);
        }
    }
    Ok(())
}

/// The game's textures, the file's own (by name) and the textures one particle names: each resource's list holds the
/// file's indices, and a game texture a particle names is copied in (two of one image share its record when written).
struct Textures<'a> {
    base: &'a ParticleIr,
    out: Vec<Texture>,
    by_name: HashMap<String, u32>,
    /// Checking without the game: a name that isn't the file's stands for one of the game's.
    any_game_name: bool,
}

impl Textures<'_> {
    fn index(&mut self, name: &str) -> Result<u32> {
        if let Some(&i) = self.by_name.get(name) {
            return Ok(i);
        }
        let game = match self.base.textures.iter().find(|t| t.name == name.as_bytes()) {
            Some(game) => game.clone(),
            None if self.any_game_name => Texture { name: name.as_bytes().to_vec(), info: vec![0; 64], record: Vec::new(), palette: 0 },
            None => bail!("no texture \"{name}\": the file's \"textures\" name none, and the game's particles have none"),
        };
        self.out.push(game);
        let i = (self.out.len() - 1) as u32;
        self.by_name.insert(name.to_string(), i);
        Ok(i)
    }
}

/// One particle from its JSON over its start (`like`'s, or the plain one).
fn particle(v: &Value, n: usize, base: &ParticleIr, base_index: &HashMap<String, u32>, textures: &mut Textures) -> Result<Resource> {
    let what = format!("particle {n}");
    let p = object(v, &what)?;
    for key in p.keys() {
        if !["like", "emitter", "shape", "extra", "child", "second_texture", "fields", "keys", "textures", "texture_frames",
            "primary_colors", "environment_colors", "name"].contains(&key.as_str()) {
            bail!("{what}: \"{key}\" is not a part of a particle (like, emitter, shape, extra, child, second_texture, fields, keys, textures, texture_frames, primary_colors, environment_colors)");
        }
    }
    let mut res = match p.get("like") {
        Some(like) => {
            let name = like.as_str().with_context(|| format!("{what}.like: a particle's name"))?;
            let user = *base_index.get(name).with_context(|| format!("{what}.like: the game has no particle \"{name}\""))?;
            let found = base.resources.iter().find(|r| r.user_index == user).with_context(|| format!("{what}.like: \"{name}\" has no record"))?;
            let mut res = found.clone();
            // Its textures by name, as this file holds them.
            let names: Vec<String> = res.texture_indices.iter().map(|&t| String::from_utf8_lossy(&base.textures[t as usize].name).into_owned()).collect();
            res.texture_indices = names.iter().map(|n| textures.index(n)).collect::<Result<_>>()?;
            res
        }
        None => plain(),
    };
    res.user_index = n as u32;
    if let Some(list) = p.get("textures") {
        let list = list.as_array().with_context(|| format!("{what}.textures: a list of texture names"))?;
        res.texture_indices = list
            .iter()
            .map(|t| textures.index(t.as_str().with_context(|| format!("{what}.textures: names"))?))
            .collect::<Result<_>>()?;
    }
    if let Some(e) = p.get("emitter") {
        encode(DYNAMICS, &format!("{what}.emitter"), object(e, "emitter")?, &mut res.dynamics)?;
    }
    if let Some(s) = p.get("shape") {
        let mut values = object(s, "shape")?.clone();
        // A shape's one texture by name: the particle's list is then that texture alone.
        if let Some(t) = values.remove("texture") {
            let name = t.as_str().with_context(|| format!("{what}.shape.texture: a texture's name"))?;
            res.texture_indices = vec![textures.index(name)?];
            res.shape.record[0x50] = 0;
        }
        encode(SHAPE, &format!("{what}.shape"), &values, &mut res.shape.record)?;
    }
    // Colour over the particle's life: keys, from which the tables are built; the last colour frame is the latest key.
    let prm = p.get("primary_colors").map(|v| color_keys(v, &format!("{what}.primary_colors"))).transpose()?;
    let env = p.get("environment_colors").map(|v| color_keys(v, &format!("{what}.environment_colors"))).transpose()?;
    if prm.is_some() || env.is_some() {
        let last = prm.iter().chain(env.iter()).filter_map(|k| k.last().map(|k| k.0)).max().unwrap_or(0);
        let rec = &mut res.shape.record;
        rec[SHAPE_LAST_COLOR_FRAME..SHAPE_LAST_COLOR_FRAME + 2].copy_from_slice(&last.to_le_bytes());
        // A table left as it was would be the wrong length now: the colour it starts from stands for all of it.
        let refit = |table: &Option<Vec<u8>>, color_at: usize, rec: &[u8]| -> Vec<(i16, [u8; 4])> {
            let first = table.as_ref().map_or_else(|| rec[color_at..color_at + 4].try_into().unwrap(), |t| t[0..4].try_into().unwrap());
            vec![(0, first)]
        };
        let prm_keys = prm.unwrap_or_else(|| refit(&res.shape.prm_table, 0x58, rec));
        let env_keys = env.unwrap_or_else(|| refit(&res.shape.env_table, 0x5C, rec));
        rec[0x58..0x5C].copy_from_slice(&prm_keys[0].1);
        rec[0x5C..0x60].copy_from_slice(&env_keys[0].1);
        rec[SHAPE_PRM_ANM] = 1;
        rec[SHAPE_ENV_ANM] = 1;
        res.shape.prm_table = Some(color_table(&prm_keys, last));
        res.shape.env_table = Some(color_table(&env_keys, last));
    }
    if let Some(frames) = p.get("texture_frames") {
        let list = frames.as_array().with_context(|| format!("{what}.texture_frames: a list of places in the particle's textures"))?;
        let table: Vec<u8> = list.iter().map(|x| as_int(x, &format!("{what}.texture_frames"), 0, 255).map(|x| x as u8)).collect::<Result<_>>()?;
        if table.iter().any(|&t| t as usize >= res.texture_indices.len()) {
            bail!("{what}.texture_frames: a place past the particle's {} textures", res.texture_indices.len());
        }
        res.shape.record[SHAPE_TEX_ANM] = u8::from(!table.is_empty());
        res.shape.record[SHAPE_TEX_KEYS] = u8::try_from(table.len()).context("at most 255 texture frames")?;
        res.shape.texture_table = (!table.is_empty()).then_some(table);
    }
    optional_record(&mut res.extra, p.get("extra"), EXTRA, &format!("{what}.extra"), EXTRA_SIZE)?;
    if let Some(r) = res.extra.as_mut() {
        derive_extra(r);
    }
    optional_record(&mut res.child, p.get("child"), CHILD, &format!("{what}.child"), CHILD_SIZE)?;
    optional_record(&mut res.extex, p.get("second_texture"), EXTEX, &format!("{what}.second_texture"), EXTEX_SIZE)?;
    if let Some(list) = p.get("fields") {
        let list = list.as_array().with_context(|| format!("{what}.fields: a list of forces"))?;
        res.fields = Vec::with_capacity(list.len());
        for (k, v) in list.iter().enumerate() {
            let mut r = vec![0u8; FIELD_SIZE];
            encode(FIELD, &format!("{what}.fields[{k}]"), object(v, "field")?, &mut r)?;
            derive_field(&mut r);
            res.fields.push(r);
        }
    }
    if let Some(list) = p.get("keys") {
        let list = list.as_array().with_context(|| format!("{what}.keys: a list of key tracks"))?;
        res.keys = Vec::with_capacity(list.len());
        for (k, v) in list.iter().enumerate() {
            let track = format!("{what}.keys[{k}]");
            let o = object(v, &track)?;
            let target = choice(KEY_TARGETS, o.get("value").with_context(|| format!("{track}: \"value\", the emitter value it drives"))?, &track)?;
            let keys = o.get("keys").and_then(Value::as_array).with_context(|| format!("{track}: \"keys\", each [frame, value, slope in, slope out]"))?;
            let mut r = Vec::with_capacity(16 + 16 * keys.len());
            r.extend_from_slice(&target.to_le_bytes());
            r.extend_from_slice(&(keys.len() as u32).to_le_bytes());
            r.extend_from_slice(&u32::from(o.get("loop").and_then(Value::as_bool).unwrap_or(false)).to_le_bytes());
            r.extend_from_slice(&0u32.to_le_bytes());
            for key in keys {
                for x in floats_of(key, 4, &track)? {
                    r.extend_from_slice(&x.to_le_bytes());
                }
            }
            res.keys.push(r);
        }
    }
    Ok(res)
}

/// The particles of a mod's `Particles.jpc.json`, their textures read from `folder`. `base` is the game's cooked
/// particle file and `base_index` its names (ParticleNames: name to number), which `like` and texture names reach.
pub fn read(json: &[u8], folder: &Path, base: &ParticleIr, base_index: &HashMap<String, u32>) -> Result<ParticleIr> {
    read_with(json, folder, base, base_index, false)
}

/// Everything `read` checks but the game's names, which any name passes for (LumaLink's check, with no game at hand):
/// the number of particles.
pub fn check(json: &[u8], folder: &Path) -> Result<usize> {
    let root: Value = serde_json::from_slice(json).context("not JSON")?;
    let likes: Vec<String> = root["particles"]
        .as_array()
        .map(Vec::as_slice)
        .unwrap_or_default()
        .iter()
        .filter_map(|p| p["like"].as_str().map(str::to_string))
        .collect();
    let mut base = ParticleIr::default();
    let mut index = HashMap::new();
    for (n, name) in likes.iter().enumerate() {
        let mut stand_in = plain();
        stand_in.user_index = n as u32;
        base.resources.push(stand_in);
        index.insert(name.clone(), n as u32);
    }
    Ok(read_with(json, folder, &base, &index, true)?.resources.len())
}

fn read_with(json: &[u8], folder: &Path, base: &ParticleIr, base_index: &HashMap<String, u32>, any_game_name: bool) -> Result<ParticleIr> {
    let root: Value = serde_json::from_slice(json).context("not JSON")?;
    let root = object(&root, "the file")?;
    let mut textures = Textures { base, out: Vec::new(), by_name: HashMap::new(), any_game_name };
    if let Some(list) = root.get("textures") {
        for (name, file) in object(list, "textures")? {
            let file = file.as_str().with_context(|| format!("textures.{name}: a PNG file's name"))?;
            let bytes = std::fs::read(folder.join(file)).with_context(|| format!("textures.{name}: {file} cannot be read"))?;
            let (width, height, rgba) = super::gltf::decode_image(&bytes, file).with_context(|| format!("textures.{name}: {file}"))?;
            textures.out.push(texture_from_rgba(name, width, height, &rgba)?);
            textures.by_name.insert(name.clone(), (textures.out.len() - 1) as u32);
        }
    }
    let list = root.get("particles").and_then(Value::as_array).context("\"particles\": the list of particles, in ParticleNames.bcsv.json's order")?;
    let mut ir = ParticleIr::default();
    for (n, v) in list.iter().enumerate() {
        ir.resources.push(particle(v, n, base, base_index, &mut textures)?);
    }
    ir.textures = textures.out;
    Ok(ir)
}

/// A particle texture from a picture: its record (named `<name>` for texture packs) and its NebulaTextureInfo.
fn texture_from_rgba(name: &str, width: u32, height: u32, rgba: &[u8]) -> Result<Texture> {
    let mut record = super::texture::cook_rgba(width, height, rgba)?;
    super::texture::name_record(&mut record, name, 0);
    let levels = u32::from_le_bytes(record[24..28].try_into().unwrap()).max(1);
    let mut info = Vec::with_capacity(64);
    for w in [0x4954_424Eu32, 1, width, height, levels, 0] {
        info.extend_from_slice(&w.to_le_bytes());
    }
    for x in [0.0f32, (levels - 1) as f32, 0.0] {
        info.extend_from_slice(&x.to_le_bytes());
    }
    let transparent = rgba.chunks_exact(4).any(|p| p[3] != 255);
    // RGBA8, clamped at the edges (a particle's quad shows its picture once), linear between texels and levels.
    info.extend_from_slice(&[6, u8::from(transparent), 0, 0, u8::from(levels > 1), 1, if levels > 1 { 5 } else { 1 }, 1]);
    info.resize(64, 0);
    Ok(Texture { name: name.as_bytes().to_vec(), info, record, palette: 0 })
}

/// One of the game's particles as the JSON a mod writes (`like` aside: every value it has), its textures by name.
pub fn write(ir: &ParticleIr, res: &Resource) -> Value {
    let mut p = Map::new();
    p.insert("emitter".into(), Value::Object(decode(DYNAMICS, &res.dynamics)));
    let mut shape = decode(SHAPE, &res.shape.record);
    let texture_names: Vec<Value> =
        res.texture_indices.iter().map(|&t| Value::from(String::from_utf8_lossy(&ir.textures[t as usize].name).into_owned())).collect();
    p.insert("textures".into(), Value::Array(texture_names));
    if let Some(t) = &res.shape.prm_table {
        p.insert("primary_colors".into(), table_keys(t));
    }
    if let Some(t) = &res.shape.env_table {
        p.insert("environment_colors".into(), table_keys(t));
    }
    if let Some(t) = &res.shape.texture_table {
        p.insert("texture_frames".into(), Value::Array(t.iter().map(|&x| Value::from(x)).collect()));
    }
    shape.retain(|_, v| !v.is_null());
    p.insert("shape".into(), Value::Object(shape));
    let optional = |r: &Option<Vec<u8>>, fields: &[Field]| r.as_ref().map_or(Value::Null, |r| Value::Object(decode(fields, r)));
    p.insert("extra".into(), optional(&res.extra, EXTRA));
    p.insert("child".into(), optional(&res.child, CHILD));
    p.insert("second_texture".into(), optional(&res.extex, EXTEX));
    p.insert("fields".into(), Value::Array(res.fields.iter().map(|r| Value::Object(decode(FIELD, r))).collect()));
    let keys = res
        .keys
        .iter()
        .map(|r| {
            let target = u32::from_le_bytes(r[0..4].try_into().unwrap());
            let count = u32::from_le_bytes(r[4..8].try_into().unwrap()) as usize;
            let looped = u32::from_le_bytes(r[8..12].try_into().unwrap()) != 0;
            let keys: Vec<Value> = (0..count).map(|k| Value::Array((0..4).map(|j| number(read_f32(r, 16 + 16 * k + 4 * j))).collect())).collect();
            json!({ "value": named(KEY_TARGETS, target), "loop": looped, "keys": keys })
        })
        .collect();
    p.insert("keys".into(), Value::Array(keys));
    Value::Object(p)
}

/// The game's particle names (ParticleNames.bcsv as cooked: its "name" and "id" columns) to their numbers.
pub fn names_index(table: &[u8]) -> Result<HashMap<String, u32>> {
    use super::table::{column_hash, Cell, TableIr};
    let t = TableIr::from_ntbl(table)?;
    let column = |name: &str| t.columns.iter().position(|c| c.hash == column_hash(name)).with_context(|| format!("ParticleNames has no \"{name}\" column"));
    let (name, id) = (column("name")?, column("id")?);
    let mut out = HashMap::new();
    for row in &t.rows {
        if let (Cell::Text(text), Cell::Bits(n)) = (&row[name], &row[id]) {
            out.insert(String::from_utf8_lossy(text).into_owned(), *n);
        }
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::super::particles;
    use super::*;

    #[test]
    fn every_record_reads_back_as_written() {
        let res = plain();
        for (fields, record) in [(DYNAMICS, &res.dynamics), (SHAPE, &res.shape.record)] {
            let values = decode(fields, record);
            let mut again = vec![0u8; record.len()];
            encode(fields, "test", &values, &mut again).unwrap();
            // The bytes no value names (counts, table places) are left at zero by both.
            assert_eq!(&again, record);
        }
        let mut extra = vec![0u8; EXTRA_SIZE];
        encode(EXTRA, "extra", json!({ "alpha_in_until": 0.2, "alpha_in": 0, "alpha": 1, "rotate": true }).as_object().unwrap(), &mut extra).unwrap();
        derive_extra(&mut extra);
        assert_eq!(read_f32(&extra, 0x60), 5.0); // (1 - 0) / 0.2
        let values = decode(EXTRA, &extra);
        let mut again = vec![0u8; EXTRA_SIZE];
        encode(EXTRA, "extra", &values, &mut again).unwrap();
        derive_extra(&mut again);
        assert_eq!(again, extra);
    }

    #[test]
    fn a_wrong_name_or_value_says_what_is_right() {
        let mut r = vec![0u8; DYNAMICS_SIZE];
        let error = encode(DYNAMICS, "emitter", json!({ "rat": 2 }).as_object().unwrap(), &mut r).unwrap_err().to_string();
        assert!(error.contains("no value \"rat\"") && error.contains("rate"), "{error}");
        let error = encode(DYNAMICS, "emitter", json!({ "volume": "blob" }).as_object().unwrap(), &mut r).unwrap_err().to_string();
        assert!(error.contains("sphere"), "{error}");
        let error = encode(SHAPE, "shape", json!({ "primary": "red" }).as_object().unwrap(), &mut [0u8; SHAPE_SIZE]).unwrap_err().to_string();
        assert!(error.contains("rrggbbaa"), "{error}");
    }

    #[test]
    fn colour_keys_become_the_table_the_game_reads() {
        let table = color_table(&[(0, [0, 0, 0, 0]), (4, [40, 80, 120, 160])], 6);
        assert_eq!(table.len(), 7 * 4);
        assert_eq!(&table[8..12], &[20, 40, 60, 80]); // frame 2, half way
        assert_eq!(&table[24..28], &[40, 80, 120, 160]); // after the last key, its colour
        assert_eq!(table_keys(&table), json!([[0, "00000000"], [4, "285078a0"], [6, "285078a0"]]));
    }

    #[test]
    fn a_particle_like_the_games_takes_its_records_and_its_textures_by_name() {
        let mut game = plain();
        game.user_index = 7;
        game.texture_indices = vec![1];
        let mut record = super::super::texture::cook_rgba(2, 2, &[255; 16]).unwrap();
        super::super::texture::name_record(&mut record, "game", 0);
        let tex = |name: &str| Texture { name: name.as_bytes().to_vec(), info: vec![0; 64], record: record.clone(), palette: 0 };
        let base = ParticleIr { resources: vec![game], textures: vec![tex("Other"), tex("Ring")] };
        let index = HashMap::from([("Sparkle".to_string(), 7u32)]);
        let file = json!({ "particles": [
            { "like": "Sparkle", "emitter": { "rate": 3 }, "primary_colors": [[0, "ff000000"], [10, "ffff00ff"]] },
            { "shape": { "type": "line" }, "fields": [{ "type": "gravity", "strength": 0.5, "fade_in_until": 1, "on_from": 0 }],
              "keys": [{ "value": "rate", "keys": [[0, 1, 0, 0], [30, 0, 0, 0]] }] }
        ]});
        let ir = read(file.to_string().as_bytes(), Path::new("."), &base, &index).unwrap();
        assert_eq!(ir.resources.len(), 2);
        assert_eq!(ir.resources[0].user_index, 0);
        assert_eq!(read_f32(&ir.resources[0].dynamics, 0x44), 3.0);
        assert_eq!(ir.textures.len(), 1);
        assert_eq!(ir.textures[0].name, b"Ring");
        assert_eq!(ir.resources[0].texture_indices, vec![0]);
        assert_eq!(ir.resources[0].shape.prm_table.as_ref().unwrap().len(), 11 * 4);
        assert_eq!(ir.resources[1].shape.record[0x30], 1);
        assert_eq!(read_f32(&ir.resources[1].fields[0], 0x44), 1.0);
        assert_eq!(ir.resources[1].keys[0].len(), 16 + 32);
        // As the game's file holds them, and back to JSON.
        let cooked = particles::write(&ir);
        let back = particles::read_nptc(&cooked).unwrap();
        let again = write(&back, &back.resources[0]);
        assert_eq!(again["emitter"]["rate"], json!(3.0));
        assert_eq!(again["textures"], json!(["Ring"]));
        let error = read(json!({ "particles": [{ "like": "Nope" }] }).to_string().as_bytes(), Path::new("."), &base, &index).err().unwrap();
        assert!(error.to_string().contains("no particle \"Nope\""));
    }
}
