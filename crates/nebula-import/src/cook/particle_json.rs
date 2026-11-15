//! Particles written as JSON, cooked to the same records as the game's own (particles.rs, ParticleData.hpp), and the
//! game's particles written back as that JSON (`lumalink extract`), so a mod can start from one of them. A mod's
//! particles are in its effects.json (effects.rs); one particle:
//!
//! ```json
//! { "like": "Glow", "textures": { "sprite": "GoldSpark" }, "emitter": { "rate": 4 }, "shape": { "primary": "ffd040ff" } }
//! ```
//!
//! A particle starts from the game's particle `like` names, else from a plain glowing dot (or, replacing a game
//! particle, from that particle), and each value given replaces that one: every value of every record has a name
//! ([`DYNAMICS`], [`SHAPE`], [`EXTRA`], [`CHILD`], [`EXTEX`], [`FIELD`]). The values the game derives (the fade and
//! scale rates, the colour tables, the counts) are derived here. `null` for `extra`, `child` or `second_texture` takes
//! that record away. Textures are named by role ([`ROLES`]) and `texture_frames`, never by a place in a list: the cook
//! makes the particle's texture list and every index into it.
//!
//! Every refusal is L804 with a `Cook.Effect.<Kind>` phrase whose `where` is the value's JSON pointer in the file.

use super::particles::{ParticleIr, Resource, Shape, Texture};
use super::refuse::{note, refuse, refuse_with};
use anyhow::{Context, Result};
use luma_validate::Arg;
use serde_json::{json, Map, Value};
use std::collections::HashMap;

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

/// `at` one key further in, as a JSON pointer (`/` is the file itself).
pub fn at_key(at: &str, key: &str) -> String {
    let key = key.replace('~', "~0").replace('/', "~1");
    if at == "/" { format!("/{key}") } else { format!("{at}/{key}") }
}

pub(crate) fn at_index(at: &str, n: usize) -> String {
    at_key(at, &n.to_string())
}

/// The value at `at` is not of the kind it takes; `is` is that kind's phrase (`Cook.Effect.Is.Number`).
pub(crate) fn not_kind(at: &str, is: &'static str) -> anyhow::Error {
    refuse_with("Cook.Effect.Kind", vec![("where", Arg::Text(at.to_string())), ("expected", Arg::Phrase(is))])
}

pub(crate) fn range(at: &str, value: impl ToString, min: impl ToString, max: impl ToString) -> anyhow::Error {
    refuse("Cook.Effect.Range", &[("where", at.to_string()), ("value", value.to_string()), ("min", min.to_string()), ("max", max.to_string())])
}

/// `key` is wanted in the object at `at` and it has none.
pub fn missing(at: &str, key: &str) -> anyhow::Error {
    refuse("Cook.Effect.Missing", &[("where", at.to_string()), ("key", key.to_string())])
}

/// A value as the file wrote it: text without its quotes.
pub(crate) fn shown(v: &Value) -> String {
    v.as_str().map_or_else(|| v.to_string(), str::to_string)
}

pub fn text<'a>(v: &'a Value, at: &str) -> Result<&'a str> {
    v.as_str().ok_or_else(|| not_kind(at, "Cook.Effect.Is.Text"))
}

pub fn object<'a>(v: &'a Value, at: &str) -> Result<&'a Map<String, Value>> {
    v.as_object().ok_or_else(|| not_kind(at, "Cook.Effect.Is.Object"))
}

pub fn list<'a>(v: &'a Value, at: &str) -> Result<&'a Vec<Value>> {
    v.as_array().ok_or_else(|| not_kind(at, "Cook.Effect.Is.List"))
}

/// Every key of the object at `at` one of `parts`.
pub fn only(map: &Map<String, Value>, at: &str, parts: &[&str]) -> Result<()> {
    match map.keys().find(|key| !parts.contains(&key.as_str())) {
        Some(key) => Err(refuse("Cook.Effect.UnknownPart", &[("where", at.to_string()), ("key", key.clone()), ("expected", parts.join(", "))])),
        None => Ok(()),
    }
}

pub(crate) fn as_f32(v: &Value, at: &str) -> Result<f32> {
    let x = v.as_f64().ok_or_else(|| not_kind(at, "Cook.Effect.Is.Number"))?;
    if !(x as f32).is_finite() {
        return Err(range(at, x, "-3.4e38", "3.4e38"));
    }
    Ok(x as f32)
}

pub(crate) fn as_int(v: &Value, at: &str, min: i64, max: i64) -> Result<i64> {
    let n = match (v.as_i64(), v.as_u64(), v.as_f64()) {
        (Some(n), _, _) => i128::from(n),
        (None, Some(n), _) => i128::from(n),
        // 3.0 is 3; 2.5 is no whole number.
        (None, None, Some(x)) if x.fract() == 0.0 && x.abs() < 1e30 => x as i128,
        _ => return Err(not_kind(at, "Cook.Effect.Is.Whole")),
    };
    if n < i128::from(min) || n > i128::from(max) {
        return Err(range(at, n, min, max));
    }
    Ok(n as i64)
}

/// One of `names` by name, or its number up to `max`.
pub(crate) fn choice(names: &[&str], v: &Value, at: &str, max: i64) -> Result<u32> {
    if v.is_number() {
        return as_int(v, at, 0, max).map(|n| n as u32);
    }
    v.as_str()
        .and_then(|t| names.iter().position(|n| !n.is_empty() && *n == t))
        .map(|p| p as u32)
        .ok_or_else(|| {
            let expected = names.iter().filter(|n| !n.is_empty()).cloned().collect::<Vec<_>>().join(", ");
            refuse("Cook.Effect.Choice", &[("where", at.to_string()), ("value", shown(v)), ("expected", expected)])
        })
}

/// "rrggbbaa" (or "#rrggbbaa", or "rrggbb", opaque).
pub fn parse_rgba(v: &Value, at: &str) -> Result<[u8; 4]> {
    let bad = || refuse("Cook.Effect.Color", &[("where", at.to_string()), ("value", shown(v))]);
    let text = v.as_str().ok_or_else(bad)?.trim_start_matches('#');
    let digits = match text.len() {
        6 => format!("{text}ff"),
        8 => text.to_string(),
        _ => return Err(bad()),
    };
    if !digits.bytes().all(|b| b.is_ascii_hexdigit()) {
        return Err(bad());
    }
    Ok(u32::from_str_radix(&digits, 16).map_err(|_| bad())?.to_be_bytes())
}

pub(crate) fn length(at: &str, count: usize, found: usize) -> anyhow::Error {
    refuse("Cook.Effect.Length", &[("where", at.to_string()), ("count", count.to_string()), ("found", found.to_string())])
}

pub(crate) fn floats_of(v: &Value, n: usize, at: &str) -> Result<Vec<f32>> {
    let items = list(v, at)?;
    if items.len() != n {
        return Err(length(at, n, items.len()));
    }
    items.iter().enumerate().map(|(k, x)| as_f32(x, &at_index(at, k))).collect()
}

/// `values` (the object at `at`) over record `r`: each name must be one of `fields`.
pub fn encode(fields: &[Field], at: &str, values: &Map<String, Value>, r: &mut [u8]) -> Result<()> {
    for (name, v) in values {
        let fd = fields.iter().find(|fd| fd.name == name).ok_or_else(|| {
            let expected = fields.iter().map(|fd| fd.name).collect::<Vec<_>>().join(", ");
            refuse("Cook.Effect.UnknownValue", &[("where", at.to_string()), ("key", name.clone()), ("expected", expected)])
        })?;
        let here = at_key(at, name);
        let what = here.as_str();
        let o = fd.at;
        match fd.kind {
            Kind::U8 => r[o] = as_int(v, what, 0, 255)? as u8,
            Kind::Bool => r[o] = u8::from(v.as_bool().ok_or_else(|| not_kind(what, "Cook.Effect.Is.Bool"))?),
            Kind::I8 => r[o] = as_int(v, what, -128, 127)? as i8 as u8,
            Kind::I16 => r[o..o + 2].copy_from_slice(&(as_int(v, what, -32768, 32767)? as i16).to_le_bytes()),
            Kind::U16 => r[o..o + 2].copy_from_slice(&(as_int(v, what, 0, 65535)? as u16).to_le_bytes()),
            Kind::U32 => r[o..o + 4].copy_from_slice(&(as_int(v, what, 0, i64::from(u32::MAX))? as u32).to_le_bytes()),
            Kind::F32 => r[o..o + 4].copy_from_slice(&as_f32(v, what)?.to_le_bytes()),
            Kind::Vec3 | Kind::Floats(_) => {
                let n = if let Kind::Floats(n) = fd.kind { n } else { 3 };
                for (k, x) in floats_of(v, n, what)?.into_iter().enumerate() {
                    r[o + 4 * k..o + 4 * k + 4].copy_from_slice(&x.to_le_bytes());
                }
            }
            Kind::I16Vec3 => {
                let items = list(v, what)?;
                if items.len() != 3 {
                    return Err(length(what, 3, items.len()));
                }
                for (k, x) in items.iter().enumerate() {
                    r[o + 2 * k..o + 2 * k + 2].copy_from_slice(&(as_int(x, &at_index(what, k), -32768, 32767)? as i16).to_le_bytes());
                }
            }
            Kind::Rgba => r[o..o + 4].copy_from_slice(&parse_rgba(v, what)?),
            Kind::Bit(mask) => {
                let mut word = u32::from_le_bytes(r[o..o + 4].try_into().unwrap());
                if v.as_bool().ok_or_else(|| not_kind(what, "Cook.Effect.Is.Bool"))? { word |= mask } else { word &= !mask }
                r[o..o + 4].copy_from_slice(&word.to_le_bytes());
            }
            Kind::Choice(names) => r[o] = choice(names, v, what, 255)? as u8,
            Kind::ChoiceU32(names) => r[o..o + 4].copy_from_slice(&choice(names, v, what, i64::from(u32::MAX))?.to_le_bytes()),
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

fn bad_keys(at: &str) -> anyhow::Error {
    refuse("Cook.Effect.Keys", &[("where", at.to_string())])
}

fn color_keys(v: &Value, at: &str) -> Result<Vec<(i16, [u8; 4])>> {
    let items = v.as_array().ok_or_else(|| bad_keys(at))?;
    let mut keys = Vec::with_capacity(items.len());
    for (k, key) in items.iter().enumerate() {
        let pair = key.as_array().filter(|p| p.len() == 2).ok_or_else(|| bad_keys(at))?;
        let here = at_index(at, k);
        keys.push((as_int(&pair[0], &at_index(&here, 0), 0, 32767)? as i16, parse_rgba(&pair[1], &at_index(&here, 1))?));
    }
    if keys.is_empty() || keys.windows(2).any(|w| w[1].0 <= w[0].0) {
        return Err(bad_keys(at));
    }
    Ok(keys)
}

/// The colour of frame `frame` between keys `a` and `b`, as `color_table` makes it.
fn between(a: (i32, [u8; 4]), b: (i32, [u8; 4]), frame: i32) -> [u8; 4] {
    let t = (frame - a.0) as f32 / (b.0 - a.0) as f32;
    std::array::from_fn(|k| (f32::from(a.1[k]) + t * (f32::from(b.1[k]) - f32::from(a.1[k]))) as i32 as u8)
}

/// The keys of a colour table, from which `color_table` makes the same table again: from each key, the next is the
/// farthest frame whose line from it gives every frame between as the table has it.
fn table_keys(table: &[u8]) -> Value {
    let colors: Vec<[u8; 4]> = table.chunks_exact(4).map(|c| c.try_into().unwrap()).collect();
    let mut keys = vec![json!([0, rgba_text(&colors[0])])];
    let mut from = 0usize;
    while from + 1 < colors.len() {
        let fits = |to: usize| (from + 1..to).all(|f| between((from as i32, colors[from]), (to as i32, colors[to]), f as i32) == colors[f]);
        let mut to = from + 1;
        while to + 1 < colors.len() && fits(to + 1) {
            to += 1;
        }
        keys.push(json!([to, rgba_text(&colors[to])]));
        from = to;
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

/// A record that may be taken away (`null`), made from nothing (zeros, then its values), or changed. `raw` are its
/// texture indices, each refused with the role to write instead.
fn optional_record(current: &mut Option<Vec<u8>>, v: Option<&Value>, fields: &[Field], at: &str, size: usize, raw: &[(&str, &str)]) -> Result<()> {
    match v {
        None => {}
        Some(Value::Null) => *current = None,
        Some(v) => {
            let values = object(v, at)?;
            no_raw_index(values, at, raw)?;
            let mut r = current.take().unwrap_or_else(|| vec![0u8; size]);
            encode(fields, at, values, &mut r)?;
            *current = Some(r);
        }
    }
    Ok(())
}

/// A texture index written by number: the particle names its textures by role.
fn no_raw_index(values: &Map<String, Value>, at: &str, raw: &[(&str, &str)]) -> Result<()> {
    match raw.iter().find(|(key, _)| values.contains_key(*key)) {
        Some((key, role)) => Err(refuse("Cook.Effect.RawIndex", &[("where", at_key(at, key)), ("key", key.to_string()), ("role", role.to_string())])),
        None => Ok(()),
    }
}

const SHAPE_TEX_INDEX: usize = 0x50;
const CHILD_TEX_INDEX: usize = 0x45;
const EXTEX_INDIRECT: usize = 0x19;
const EXTEX_SECOND: usize = 0x1A;
const EXTEX_INDIRECT_ON: usize = 0x1B;
const EXTEX_SECOND_ON: usize = 0x1C;

/// The places a particle's records point into its texture list (effects.md section 7).
pub const ROLES: [&str; 4] = ["sprite", "child", "second", "indirect"];

/// The textures a particle draws, by role, and its sprite animation: from these the cook makes its texture list.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Roles {
    pub sprite: Option<String>,
    pub child: Option<String>,
    pub second: Option<String>,
    pub indirect: Option<String>,
    /// A texture a frame.
    pub frames: Vec<String>,
}

impl Roles {
    fn slot(&mut self, role: &str) -> &mut Option<String> {
        match role {
            "sprite" => &mut self.sprite,
            "child" => &mut self.child,
            "second" => &mut self.second,
            _ => &mut self.indirect,
        }
    }

    /// A particle's roles as its records and list give them. A role its record does not draw (no child record, the
    /// second or indirect texture turned off) is none.
    pub fn of(ir: &ParticleIr, res: &Resource) -> Roles {
        let name = |place: u8| {
            let t = *res.texture_indices.get(usize::from(place))?;
            ir.textures.get(t as usize).map(|t| String::from_utf8_lossy(&t.name).into_owned())
        };
        let frames = res.shape.texture_table.as_ref().and_then(|t| t.iter().map(|&p| name(p)).collect::<Option<Vec<_>>>());
        Roles {
            sprite: name(res.shape.record[SHAPE_TEX_INDEX]),
            child: res.child.as_ref().and_then(|c| name(c[CHILD_TEX_INDEX])),
            second: res.extex.as_ref().filter(|e| e[EXTEX_SECOND_ON] != 0).and_then(|e| name(e[EXTEX_SECOND])),
            indirect: res.extex.as_ref().filter(|e| e[EXTEX_INDIRECT_ON] != 0).and_then(|e| name(e[EXTEX_INDIRECT])),
            frames: frames.unwrap_or_default(),
        }
    }
}

/// The game's particles a mod's reach by name (`like`, `replace`, texture names).
pub struct Game<'a> {
    pub particles: &'a ParticleIr,
    /// ParticleNames: name to number.
    pub names: &'a HashMap<String, u32>,
    /// Checking with no game at hand (LumaLink): any name stands for one of the game's.
    pub any_name: bool,
}

/// What a particle starts from: its records, its roles and its texture list by name.
pub struct Start {
    pub res: Resource,
    pub roles: Roles,
    pub list: Vec<String>,
}

impl Game<'_> {
    pub fn particle(&self, name: &str) -> Option<Start> {
        let found = self.names.get(name).and_then(|&n| self.particles.resources.iter().find(|r| r.user_index == n));
        match found {
            Some(res) => Some(Start {
                res: res.clone(),
                roles: Roles::of(self.particles, res),
                list: res
                    .texture_indices
                    .iter()
                    .filter_map(|&t| self.particles.textures.get(t as usize))
                    .map(|t| String::from_utf8_lossy(&t.name).into_owned())
                    .collect(),
            }),
            None => self.any_name.then(|| Start { res: plain(), roles: Roles::default(), list: Vec::new() }),
        }
    }
}

/// The textures of one effects file: its own (cooked from their pictures) and the game's particle textures its
/// particles name, each once. Every resource's list holds indices into `out`.
pub struct Textures<'a> {
    game: &'a Game<'a>,
    pub out: Vec<Texture>,
    by_name: HashMap<String, u32>,
}

impl<'a> Textures<'a> {
    pub fn new(game: &'a Game<'a>) -> Self {
        Textures { game, out: Vec::new(), by_name: HashMap::new() }
    }

    /// One of the file's own textures.
    pub fn add(&mut self, name: &str, texture: Texture) {
        self.out.push(texture);
        self.by_name.insert(name.to_string(), (self.out.len() - 1) as u32);
    }

    fn index(&mut self, name: &str, at: &str) -> Result<u32> {
        if let Some(&i) = self.by_name.get(name) {
            return Ok(i);
        }
        let game = match self.game.particles.textures.iter().find(|t| t.name == name.as_bytes()) {
            Some(game) => game.clone(),
            None if self.game.any_name => Texture { name: name.as_bytes().to_vec(), info: vec![0; 64], record: Vec::new(), palette: 0 },
            None => return Err(refuse("Cook.Effect.NoTexture", &[("where", at.to_string()), ("name", name.to_string())])),
        };
        self.out.push(game);
        let i = (self.out.len() - 1) as u32;
        self.by_name.insert(name.to_string(), i);
        Ok(i)
    }
}

/// The particle's texture list and indices from its roles. The list starts as the particle it starts from had it (so a
/// particle changed in nothing else is the same bytes); a texture a role names that is not in it is added. A role not
/// given leaves its index as it was.
fn place_roles(res: &mut Resource, start: &[String], roles: &Roles, at: &str, textures: &mut Textures) -> Result<()> {
    let here = at_key(at, "textures");
    let mut names: Vec<String> = start.to_vec();
    let mut list: Vec<u32> = start.iter().map(|n| textures.index(n, &here)).collect::<Result<_>>()?;
    // Its place in the list: where it was (`was`, when that is this texture), else its first, else a new one.
    let mut place = |name: &str, was: u8, at: &str, list: &mut Vec<u32>, names: &mut Vec<String>| -> Result<u8> {
        let index = textures.index(name, at)?;
        let p = if names.get(usize::from(was)).is_some_and(|n| n == name) {
            usize::from(was)
        } else if let Some(p) = names.iter().position(|n| n == name) {
            p
        } else {
            list.push(index);
            names.push(name.to_string());
            list.len() - 1
        };
        u8::try_from(p).map_err(|_| refuse("Cook.Effect.Frames", &[("where", here.clone()), ("count", (p + 1).to_string())]))
    };
    let old_frames = res.shape.texture_table.clone().unwrap_or_default();
    let mut frames = Vec::with_capacity(roles.frames.len());
    for (k, name) in roles.frames.iter().enumerate() {
        let was = old_frames.get(k).copied().unwrap_or(u8::MAX);
        frames.push(place(name, was, &at_index(&at_key(at, "texture_frames"), k), &mut list, &mut names)?);
    }
    if let Some(name) = roles.sprite.as_deref().or(roles.frames.first().map(String::as_str)) {
        res.shape.record[SHAPE_TEX_INDEX] = place(name, res.shape.record[SHAPE_TEX_INDEX], &here, &mut list, &mut names)?;
    }
    let off = |role: &str| note("Cook.Effect.RoleOff", &[("where", at_key(&here, role)), ("role", role.to_string())]);
    if let Some(name) = &roles.child {
        let was = res.child.as_ref().map_or(u8::MAX, |c| c[CHILD_TEX_INDEX]);
        let p = place(name, was, &here, &mut list, &mut names)?;
        match res.child.as_mut() {
            Some(c) => c[CHILD_TEX_INDEX] = p,
            None => off("child"),
        }
    }
    for (role, given, index_at, on_at) in [("second", &roles.second, EXTEX_SECOND, EXTEX_SECOND_ON), ("indirect", &roles.indirect, EXTEX_INDIRECT, EXTEX_INDIRECT_ON)] {
        if let Some(name) = given {
            let was = res.extex.as_ref().map_or(u8::MAX, |e| e[index_at]);
            let p = place(name, was, &here, &mut list, &mut names)?;
            match res.extex.as_mut() {
                Some(e) => {
                    e[index_at] = p;
                    if e[on_at] == 0 {
                        off(role);
                    }
                }
                None => off(role),
            }
        }
    }
    if !frames.is_empty() {
        res.shape.record[SHAPE_TEX_ANM] = 1;
        res.shape.record[SHAPE_TEX_KEYS] = frames.len() as u8;
        res.shape.texture_table = Some(frames);
    } else if res.shape.texture_table.is_some() {
        res.shape.record[SHAPE_TEX_ANM] = 0;
        res.shape.record[SHAPE_TEX_KEYS] = 0;
        res.shape.texture_table = None;
    }
    res.texture_indices = list;
    Ok(())
}

/// The parts of a particle.
pub const PARTS: [&str; 12] = [
    "like", "textures", "texture_frames", "emitter", "shape", "extra", "child", "second_texture", "fields", "keys", "primary_colors",
    "environment_colors",
];

/// One particle from its JSON at `at` (a JSON pointer in the file), over `start` (a replaced game particle's own) or
/// the game particle it names `like`, else the plain one.
pub fn particle(v: &Value, at: &str, start: Option<Start>, game: &Game, textures: &mut Textures) -> Result<Resource> {
    let p = object(v, at)?;
    only(p, at, &PARTS)?;
    let Start { mut res, mut roles, list: start_list } = match p.get("like") {
        Some(like) => {
            let here = at_key(at, "like");
            let name = text(like, &here)?;
            game.particle(name).ok_or_else(|| refuse("Cook.Effect.LikeUnknown", &[("where", here.clone()), ("name", name.to_string())]))?
        }
        None => start.unwrap_or_else(|| Start { res: plain(), roles: Roles::default(), list: Vec::new() }),
    };
    if let Some(t) = p.get("textures") {
        let here = at_key(at, "textures");
        if t.is_array() {
            return Err(refuse("Cook.Effect.Positional", &[("where", here)]));
        }
        let t = object(t, &here)?;
        only(t, &here, &ROLES)?;
        for (role, name) in t {
            let there = at_key(&here, role);
            // A role other than the sprite may be taken away.
            *roles.slot(role) = match name {
                Value::Null if role != "sprite" => None,
                _ => Some(text(name, &there)?.to_string()),
            };
        }
    }
    if let Some(f) = p.get("texture_frames") {
        let here = at_key(at, "texture_frames");
        let items = list(f, &here)?;
        if items.len() > 255 {
            return Err(refuse("Cook.Effect.Frames", &[("where", here), ("count", items.len().to_string())]));
        }
        roles.frames = items.iter().enumerate().map(|(k, x)| text(x, &at_index(&here, k)).map(str::to_string)).collect::<Result<_>>()?;
    }
    // Colour over the particle's life: keys, from which the tables are built; the last colour frame is the latest key.
    // Before the shape's values, so that a `primary` or `environment` written there is the one kept.
    let prm = p.get("primary_colors").map(|v| color_keys(v, &at_key(at, "primary_colors"))).transpose()?;
    let env = p.get("environment_colors").map(|v| color_keys(v, &at_key(at, "environment_colors"))).transpose()?;
    if prm.is_some() || env.is_some() {
        let last = prm.iter().chain(env.iter()).filter_map(|k| k.last().map(|k| k.0)).max().unwrap_or(0);
        let shape = &mut res.shape;
        shape.record[SHAPE_LAST_COLOR_FRAME..SHAPE_LAST_COLOR_FRAME + 2].copy_from_slice(&last.to_le_bytes());
        let entries = (usize::from(last as u16) + 1) * 4;
        for (given, table, color_at, anm_at) in [(prm, &mut shape.prm_table, 0x58, SHAPE_PRM_ANM), (env, &mut shape.env_table, 0x5C, SHAPE_ENV_ANM)] {
            let keys = match given {
                Some(keys) => keys,
                // The other table, if the particle has one and it is the wrong length now: the colour it starts from
                // stands for all of it.
                None => match table.as_ref() {
                    Some(t) if t.len() != entries => vec![(0, t[0..4].try_into().unwrap())],
                    _ => continue,
                },
            };
            shape.record[color_at..color_at + 4].copy_from_slice(&keys[0].1);
            shape.record[anm_at] = 1;
            *table = Some(color_table(&keys, last));
        }
    }
    if let Some(e) = p.get("emitter") {
        let here = at_key(at, "emitter");
        encode(DYNAMICS, &here, object(e, &here)?, &mut res.dynamics)?;
    }
    if let Some(s) = p.get("shape") {
        let here = at_key(at, "shape");
        let values = object(s, &here)?;
        no_raw_index(values, &here, &[("texture_index", "sprite")])?;
        encode(SHAPE, &here, values, &mut res.shape.record)?;
    }
    optional_record(&mut res.extra, p.get("extra"), EXTRA, &at_key(at, "extra"), EXTRA_SIZE, &[])?;
    if let Some(r) = res.extra.as_mut() {
        derive_extra(r);
    }
    optional_record(&mut res.child, p.get("child"), CHILD, &at_key(at, "child"), CHILD_SIZE, &[("texture_index", "child")])?;
    let raw = [("second_texture", "second"), ("indirect_texture", "indirect")];
    optional_record(&mut res.extex, p.get("second_texture"), EXTEX, &at_key(at, "second_texture"), EXTEX_SIZE, &raw)?;
    if let Some(v) = p.get("fields") {
        let here = at_key(at, "fields");
        let items = list(v, &here)?;
        res.fields = Vec::with_capacity(items.len());
        for (k, v) in items.iter().enumerate() {
            let there = at_index(&here, k);
            let mut r = vec![0u8; FIELD_SIZE];
            encode(FIELD, &there, object(v, &there)?, &mut r)?;
            derive_field(&mut r);
            res.fields.push(r);
        }
    }
    if let Some(v) = p.get("keys") {
        let here = at_key(at, "keys");
        let items = list(v, &here)?;
        res.keys = Vec::with_capacity(items.len());
        for (k, v) in items.iter().enumerate() {
            let track = at_index(&here, k);
            let o = object(v, &track)?;
            only(o, &track, &["value", "keys", "loop"])?;
            let target = choice(KEY_TARGETS, o.get("value").ok_or_else(|| missing(&track, "value"))?, &at_key(&track, "value"), i64::from(u32::MAX))?;
            let keys_at = at_key(&track, "keys");
            let keys = list(o.get("keys").ok_or_else(|| missing(&track, "keys"))?, &keys_at)?;
            let looped = match o.get("loop") {
                None => false,
                Some(b) => b.as_bool().ok_or_else(|| not_kind(&at_key(&track, "loop"), "Cook.Effect.Is.Bool"))?,
            };
            let mut r = Vec::with_capacity(16 + 16 * keys.len());
            r.extend_from_slice(&target.to_le_bytes());
            r.extend_from_slice(&(keys.len() as u32).to_le_bytes());
            r.extend_from_slice(&u32::from(looped).to_le_bytes());
            r.extend_from_slice(&0u32.to_le_bytes());
            let mut frames = Vec::with_capacity(keys.len());
            for (j, key) in keys.iter().enumerate() {
                let values = floats_of(key, 4, &at_index(&keys_at, j))?;
                frames.push(values[0]);
                for x in values {
                    r.extend_from_slice(&x.to_le_bytes());
                }
            }
            if frames.is_empty() || frames.windows(2).any(|w| w[1] <= w[0]) {
                return Err(bad_keys(&keys_at));
            }
            res.keys.push(r);
        }
    }
    place_roles(&mut res, &start_list, &roles, at, textures)?;
    Ok(res)
}

/// A particle texture from a picture: its record (named `<name>` for texture packs) and its NebulaTextureInfo.
pub fn texture_from_rgba(name: &str, width: u32, height: u32, rgba: &[u8]) -> Result<Texture> {
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

/// One of the game's particles as a mod writes it: every value it has, its textures by role (so that `replace` with it
/// unchanged gives the same records).
pub fn write(ir: &ParticleIr, res: &Resource) -> Value {
    let mut p = Map::new();
    let roles = Roles::of(ir, res);
    let mut by_role = Map::new();
    for (role, name) in [("sprite", &roles.sprite), ("child", &roles.child), ("second", &roles.second), ("indirect", &roles.indirect)] {
        if let Some(name) = name {
            by_role.insert(role.into(), Value::from(name.clone()));
        }
    }
    p.insert("textures".into(), Value::Object(by_role));
    if !roles.frames.is_empty() {
        p.insert("texture_frames".into(), Value::Array(roles.frames.iter().map(|n| Value::from(n.clone())).collect()));
    }
    p.insert("emitter".into(), Value::Object(decode(DYNAMICS, &res.dynamics)));
    if let Some(t) = &res.shape.prm_table {
        p.insert("primary_colors".into(), table_keys(t));
    }
    if let Some(t) = &res.shape.env_table {
        p.insert("environment_colors".into(), table_keys(t));
    }
    let mut shape = decode(SHAPE, &res.shape.record);
    shape.remove("texture_index");
    shape.retain(|_, v| !v.is_null());
    p.insert("shape".into(), Value::Object(shape));
    let optional = |r: &Option<Vec<u8>>, fields: &[Field], raw: &[&str]| {
        r.as_ref().map_or(Value::Null, |r| {
            let mut values = decode(fields, r);
            for key in raw {
                values.remove(*key);
            }
            Value::Object(values)
        })
    };
    p.insert("extra".into(), optional(&res.extra, EXTRA, &[]));
    p.insert("child".into(), optional(&res.child, CHILD, &["texture_index"]));
    p.insert("second_texture".into(), optional(&res.extex, EXTEX, &["second_texture", "indirect_texture"]));
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
pub(crate) mod tests {
    use super::super::refuse::{said, said_notes};
    use super::*;

    pub(crate) fn tex(name: &str) -> Texture {
        let mut record = super::super::texture::cook_rgba(2, 2, &[255; 16]).unwrap();
        super::super::texture::name_record(&mut record, name, 0);
        Texture { name: name.as_bytes().to_vec(), info: vec![0; 64], record, palette: 0 }
    }

    fn own(pairs: &[(&'static str, &str)]) -> Vec<(&'static str, String)> {
        pairs.iter().map(|(n, v)| (*n, v.to_string())).collect()
    }

    /// A game with one particle, "Sparkle" (number 7), drawing "Ring" of its textures "Other" and "Ring".
    fn sparkle_game() -> (ParticleIr, HashMap<String, u32>) {
        let mut game = plain();
        game.user_index = 7;
        game.texture_indices = vec![1];
        (ParticleIr { resources: vec![game], textures: vec![tex("Other"), tex("Ring")] }, HashMap::from([("Sparkle".to_string(), 7u32)]))
    }

    fn cook_one(v: &Value, game: &Game) -> Result<(Resource, Vec<Texture>)> {
        let mut textures = Textures::new(game);
        let res = particle(v, "/effects/aura/particles/0", None, game, &mut textures)?;
        Ok((res, textures.out))
    }

    #[test]
    fn every_record_reads_back_as_written() {
        let res = plain();
        for (fields, record) in [(DYNAMICS, &res.dynamics), (SHAPE, &res.shape.record)] {
            let values = decode(fields, record);
            let mut again = vec![0u8; record.len()];
            encode(fields, "/test", &values, &mut again).unwrap();
            // The bytes no value names (counts, table places) are left at zero by both.
            assert_eq!(&again, record);
        }
        let mut extra = vec![0u8; EXTRA_SIZE];
        encode(EXTRA, "/extra", json!({ "alpha_in_until": 0.2, "alpha_in": 0, "alpha": 1, "rotate": true }).as_object().unwrap(), &mut extra).unwrap();
        derive_extra(&mut extra);
        assert_eq!(read_f32(&extra, 0x60), 5.0); // (1 - 0) / 0.2
        let values = decode(EXTRA, &extra);
        let mut again = vec![0u8; EXTRA_SIZE];
        encode(EXTRA, "/extra", &values, &mut again).unwrap();
        derive_extra(&mut again);
        assert_eq!(again, extra);
    }

    #[test]
    fn colour_keys_make_the_same_table_again() {
        let table = color_table(&[(0, [0, 0, 0, 0]), (4, [40, 80, 120, 160])], 6);
        assert_eq!(table.len(), 7 * 4);
        assert_eq!(&table[8..12], &[20, 40, 60, 80]); // frame 2, half way
        assert_eq!(&table[24..28], &[40, 80, 120, 160]); // after the last key, its colour
        assert_eq!(table_keys(&table), json!([[0, "00000000"], [4, "285078a0"], [6, "285078a0"]]));
        // Any table: its keys give it back byte for byte (truncation and all).
        let uneven = color_table(&[(0, [0, 0, 0, 0]), (3, [255, 10, 0, 7]), (7, [1, 200, 30, 255]), (8, [9, 9, 9, 9])], 11);
        let mut odd = uneven.clone();
        odd[21] ^= 0x55;
        for table in [uneven, odd] {
            let keys = color_keys(&table_keys(&table), "/k").unwrap();
            assert_eq!(color_table(&keys, (table.len() / 4 - 1) as i16), table);
        }
    }

    #[test]
    fn a_wrong_name_or_value_says_what_is_right() {
        let (ir, names) = sparkle_game();
        let game = Game { particles: &ir, names: &names, any_name: false };
        let at = "/effects/aura/particles/0";
        let refused = |v: Value| said(&cook_one(&v, &game).err().unwrap());
        assert_eq!(refused(json!({ "emitter": { "rat": 2 } })).0, "Cook.Effect.UnknownValue");
        assert_eq!(refused(json!({ "emitter": { "rat": 2 } })).1[..2], own(&[("where", &format!("{at}/emitter")), ("key", "rat")]));
        assert_eq!(refused(json!({ "emitter": { "volume": "blob" } })).0, "Cook.Effect.Choice");
        assert_eq!(refused(json!({ "emitter": { "rate": "many" } })), ("Cook.Effect.Kind", own(&[("where", &format!("{at}/emitter/rate")), ("expected", "P:Cook.Effect.Is.Number")])));
        assert_eq!(refused(json!({ "shape": { "primary": "red" } })), ("Cook.Effect.Color", own(&[("where", &format!("{at}/shape/primary")), ("value", "red")])));
        assert_eq!(refused(json!({ "shape": { "primary": "+fffffff" } })).0, "Cook.Effect.Color");
        assert_eq!(refused(json!({ "shape": { "depth_test": 1 } })).1[1], ("expected", "P:Cook.Effect.Is.Bool".to_string()));
        assert_eq!(refused(json!({ "extra": { "rotate": true, "alpha": 1, "pivot_nope": 300 } })).0, "Cook.Effect.UnknownValue");
        assert_eq!(refused(json!({ "emitter": { "life": 2.5 } })).1[1], ("expected", "P:Cook.Effect.Is.Whole".to_string()));
        assert_eq!(refused(json!({ "emitter": { "scale": [1, 1] } })), ("Cook.Effect.Length", own(&[("where", &format!("{at}/emitter/scale")), ("count", "3"), ("found", "2")])));
        assert_eq!(refused(json!({ "child": { "life": 40000 } })), ("Cook.Effect.Range", own(&[("where", &format!("{at}/child/life")), ("value", "40000"), ("min", "-32768"), ("max", "32767")])));
        assert_eq!(refused(json!({ "primary_colors": [[5, "ffffffff"], [5, "00000000"]] })), ("Cook.Effect.Keys", own(&[("where", &format!("{at}/primary_colors"))])));
        assert_eq!(refused(json!({ "keys": [{ "keys": [[0, 1, 0, 0]] }] })), ("Cook.Effect.Missing", own(&[("where", &format!("{at}/keys/0")), ("key", "value")])));
        assert_eq!(refused(json!({ "keys": [{ "value": "rate", "keys": [[3, 1, 0, 0], [1, 0, 0, 0]] }] })).0, "Cook.Effect.Keys");
        assert_eq!(refused(json!({ "fields": [{ "type": "glue" }] })).0, "Cook.Effect.Choice");
        assert_eq!(refused(json!({ "colour": 1 })).0, "Cook.Effect.UnknownPart");
        assert_eq!(refused(json!({ "like": "Nope" })), ("Cook.Effect.LikeUnknown", own(&[("where", &format!("{at}/like")), ("name", "Nope")])));
        assert_eq!(refused(json!({ "textures": { "sprite": "Nope" } })), ("Cook.Effect.NoTexture", own(&[("where", &format!("{at}/textures")), ("name", "Nope")])));
        assert_eq!(refused(json!({ "textures": { "glow": "Ring" } })).0, "Cook.Effect.UnknownPart");
        assert_eq!(refused(json!({ "textures": ["Ring"] })), ("Cook.Effect.Positional", own(&[("where", &format!("{at}/textures"))])));
        assert_eq!(refused(json!({ "texture_frames": [0, 1] })).1[1], ("expected", "P:Cook.Effect.Is.Text".to_string()));
        let many: Vec<Value> = (0..256).map(|_| Value::from("Ring")).collect();
        assert_eq!(refused(json!({ "texture_frames": many })), ("Cook.Effect.Frames", own(&[("where", &format!("{at}/texture_frames")), ("count", "256")])));
    }

    #[test]
    fn a_texture_index_by_number_is_refused_naming_its_role() {
        let (ir, names) = sparkle_game();
        let game = Game { particles: &ir, names: &names, any_name: false };
        let at = "/effects/aura/particles/0";
        for (v, key, role, record) in [
            (json!({ "shape": { "texture_index": 1 } }), "texture_index", "sprite", "shape"),
            (json!({ "child": { "texture_index": 0 } }), "texture_index", "child", "child"),
            (json!({ "second_texture": { "second_texture": 0 } }), "second_texture", "second", "second_texture"),
            (json!({ "second_texture": { "indirect_texture": 0 } }), "indirect_texture", "indirect", "second_texture"),
        ] {
            assert_eq!(
                said(&cook_one(&v, &game).err().unwrap()),
                ("Cook.Effect.RawIndex", own(&[("where", &format!("{at}/{record}/{key}")), ("key", key), ("role", role)]))
            );
        }
    }

    #[test]
    fn roles_make_the_texture_list() {
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
        let (ir, names) = sparkle_game();
        let game = Game { particles: &ir, names: &names, any_name: false };
        // Over `like`, a role changes that role only: Sparkle's list keeps its Ring, Other is added for the sprite.
        let (res, out) = cook_one(&json!({ "like": "Sparkle", "textures": { "sprite": "Other" }, "emitter": { "rate": 3 } }), &game).unwrap();
        let cooked = ParticleIr { resources: vec![], textures: out };
        assert_eq!(cooked.textures.iter().map(|t| t.name.as_slice()).collect::<Vec<_>>(), [b"Ring".as_slice(), b"Other"]);
        assert_eq!(res.texture_indices, [0, 1]);
        assert_eq!(Roles::of(&cooked, &res), Roles { sprite: Some("Other".into()), ..Roles::default() });
        assert_eq!(read_f32(&res.dynamics, 0x44), 3.0);
        assert_eq!(res.user_index, 7, "the start's number; the effect cook numbers it");
        // Frames: each texture once in the list, the sprite the first frame's.
        let (res, out) = cook_one(&json!({ "texture_frames": ["Ring", "Other", "Ring"] }), &game).unwrap();
        assert_eq!(res.texture_indices, [0, 1]);
        assert_eq!(res.shape.texture_table, Some(vec![0, 1, 0]));
        assert_eq!((res.shape.record[SHAPE_TEX_ANM], res.shape.record[SHAPE_TEX_KEYS], res.shape.record[SHAPE_TEX_INDEX]), (1, 3, 0));
        let roles = Roles::of(&ParticleIr { resources: vec![], textures: out }, &res);
        assert_eq!(roles.frames, ["Ring", "Other", "Ring"]);
        // A role its record does not draw is said, and kept.
        said_notes();
        let (res, _) = cook_one(&json!({ "textures": { "sprite": "Ring", "second": "Other", "child": "Other" } }), &game).unwrap();
        assert_eq!(res.texture_indices, [0, 1]);
        assert_eq!(
            said_notes(),
            [
                ("Cook.Effect.RoleOff", own(&[("where", "/effects/aura/particles/0/textures/child"), ("role", "child")])),
                ("Cook.Effect.RoleOff", own(&[("where", "/effects/aura/particles/0/textures/second"), ("role", "second")])),
            ]
        );
        let (res, _) = cook_one(&json!({ "textures": { "sprite": "Ring", "second": "Other" }, "second_texture": { "second_on": true } }), &game).unwrap();
        assert_eq!(res.extex.as_ref().unwrap()[EXTEX_SECOND], 1);
        assert!(said_notes().is_empty());
        // The sprite may not be taken away; the others may.
        assert_eq!(said(&cook_one(&json!({ "textures": { "sprite": null } }), &game).err().unwrap()).0, "Cook.Effect.Kind");
    }

    /// A particle with every record and table, cooked in a game of its own.
    fn rich_game() -> (ParticleIr, HashMap<String, u32>) {
        let empty = ParticleIr::default();
        let no_names = HashMap::new();
        let game = Game { particles: &empty, names: &no_names, any_name: false };
        let mut textures = Textures::new(&game);
        for name in ["A", "B", "C", "D"] {
            textures.add(name, tex(name));
        }
        let v = json!({
            "textures": { "sprite": "B", "child": "C", "second": "D", "indirect": "A" },
            "texture_frames": ["B", "A", "B"],
            "emitter": { "rate": 2.5, "life": 44, "volume": "sphere" },
            "shape": { "type": "line", "primary": "11223344" },
            "primary_colors": [[0, "ff000000"], [3, "00ff0080"], [9, "0000ffff"]],
            "environment_colors": [[0, "ffffffff"], [9, "00000000"]],
            "extra": { "alpha_in_until": 0.25, "alpha": 0.5, "rotate": true },
            "child": { "life": 12, "rate": 3, "speed": 1.5 },
            "second_texture": { "second_on": true, "indirect_on": true, "indirect_scale": -3, "indirect_matrix": [1, 0, 0, 0, 1, 0] },
            "fields": [{ "type": "vortex", "strength": 0.75, "fade_in_until": 2, "on_from": 1, "position": [1, 2, 3] }],
            "keys": [{ "value": "rate", "loop": true, "keys": [[0, 1, 0, 0], [30, 0.5, 0.1, 0.2]] }]
        });
        let mut res = particle(&v, "/x", None, &game, &mut textures).unwrap();
        res.user_index = 3;
        (ParticleIr { resources: vec![res], textures: textures.out }, HashMap::from([("Rich".to_string(), 3u32)]))
    }

    #[test]
    fn a_game_particle_written_out_and_replaced_by_itself_is_the_same_particle() {
        let (ir, names) = rich_game();
        let game = Game { particles: &ir, names: &names, any_name: false };
        let original = &ir.resources[0];
        // Through the file's text, as a modder's copy would be.
        let written = serde_json::to_string_pretty(&write(&ir, original)).unwrap();
        assert!(!written.contains("texture_index") && !written.contains("\"second_texture\": 0"), "{written}");
        let v: Value = serde_json::from_str(&written).unwrap();
        let mut textures = Textures::new(&game);
        let again = particle(&v, "/replace/Rich", game.particle("Rich"), &game, &mut textures).unwrap();
        assert_eq!(again.dynamics, original.dynamics);
        assert_eq!(again.shape.record, original.shape.record);
        assert_eq!(
            (&again.shape.prm_table, &again.shape.env_table, &again.shape.texture_table),
            (&original.shape.prm_table, &original.shape.env_table, &original.shape.texture_table)
        );
        assert_eq!((&again.extra, &again.child, &again.extex), (&original.extra, &original.child, &original.extex));
        assert_eq!((&again.fields, &again.keys, again.user_index), (&original.fields, &original.keys, 3));
        let names_of = |ir_textures: &[Texture], list: &[u32]| list.iter().map(|&t| ir_textures[t as usize].name.clone()).collect::<Vec<_>>();
        assert_eq!(names_of(&textures.out, &again.texture_indices), names_of(&ir.textures, &original.texture_indices));
        // And the game's own file holds it as written.
        let cooked = super::super::particles::write(&ParticleIr { resources: vec![again], textures: textures.out });
        let back = super::super::particles::read_nptc(&cooked).unwrap();
        assert_eq!(Roles::of(&back, &back.resources[0]), Roles::of(&ir, original));
    }
}
