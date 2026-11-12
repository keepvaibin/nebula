//! A material's programs: one vertex and one pixel shader written for exactly what it does, numbered by first use so that
//! materials differing only in which constants, textures or coordinates they name share a program. The compiled set and
//! its table come from tools/dev/build_programs.py over these texts.

use super::clip::{self, ClipIr, Track, KIND_REGISTERS};
use super::model_ir::*;
use super::{archive, pak_files, pak_system_area, Stats};
use anyhow::{bail, Context, Result};
use std::collections::{BTreeSet, HashMap, HashSet};
use std::fmt::Write as _;
use std::fs::File;
use std::io::{BufWriter, Read, Seek, SeekFrom, Write as _};
use std::path::Path;

// What a constant row holds. The game fills a material's rows from these numbers (nebula_model.h NEBULA_ROW_*).
pub const ROW_REGISTER: u32 = 0; // registers 1 to 3
pub const ROW_CONSTANT: u32 = 3; // constant colours 0 to 3
pub const ROW_MATERIAL_COLOR: u32 = 7;
pub const ROW_AMBIENT_COLOR: u32 = 9;
pub const ROW_FOG: u32 = 11; // start, end, near, far
pub const ROW_FOG_COLOR: u32 = 12;
pub const ROW_ALPHA_REF: u32 = 13;
pub const ROW_OVERRIDE: u32 = 14; // the replacing forms' values
// The counted kinds: kind << 16 | index.
pub const ROW_SCALE: u32 = 1 << 16; // + coordinate: xy its scale, zw its map's size
pub const ROW_WARP: u32 = 2 << 16; // + two per warp matrix
pub const ROW_COORD: u32 = 3 << 16; // + three per coordinate

const CC: [&str; 16] = [
    "prev.rgb", "prev.aaa", "r0.rgb", "r0.aaa", "r1.rgb", "r1.aaa", "r2.rgb", "r2.aaa", "t.rgb", "t.aaa", "ras.rgb", "ras.aaa",
    "float3(1.0, 1.0, 1.0)", "float3(128.0, 128.0, 128.0) / 255.0", "k.rgb", "float3(0.0, 0.0, 0.0)",
];
const CA: [&str; 8] = ["prev.a", "r0.a", "r1.a", "r2.a", "t.a", "ras.a", "ka", "0.0"];
/// The fixed constant selections, as the combiner's 8-bit values on the 0-1 scale.
const FRACTION: [&str; 8] = ["1.0", "223.0 / 255.0", "191.0 / 255.0", "159.0 / 255.0", "128.0 / 255.0", "96.0 / 255.0", "64.0 / 255.0", "32.0 / 255.0"];
const FRACTION_VALUE: [f32; 8] = [1.0, 223.0 / 255.0, 191.0 / 255.0, 159.0 / 255.0, 128.0 / 255.0, 96.0 / 255.0, 64.0 / 255.0, 32.0 / 255.0];
const BIAS: [&str; 3] = ["", " + 128.0 / 255.0", " - 128.0 / 255.0"];
const SCALE: [&str; 4] = ["", " * 2.0", " * 4.0", " * 0.5"];
const DEST: [&str; 4] = ["prev", "r0", "r1", "r2"];
const COMPARE: [&str; 8] = ["false", "a8 < r8", "a8 == r8", "a8 <= r8", "a8 > r8", "a8 != r8", "a8 >= r8", "true"];
const RGBA: [char; 4] = ['r', 'g', 'b', 'a'];

/// Which program of a material. Everything else about it comes from the material.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct Form {
    /// 0 the material's stages. Otherwise a pass that replaces them: 1 texture 0 at alpha 1/4, 2 the channel colour at alpha
    /// A0, 3 the channel colour at alpha RASA - K(1 - A0), 4 the channel colour at alpha (A1 + (A0 - A1) RASA) / 2, 5 texture
    /// 0 halved with stage 0's own alpha.
    pub replace: u8,
    /// Coordinate 0 is the vertex's normal through the identity, normalized.
    pub normal_coord: bool,
    /// Lit at the pixel, plainly or enhanced by a view flag.
    pub lit: bool,
    /// NEBULA_FORM_PASS_*: an edit of the material made before its programs are written.
    pub pass: u8,
    /// Linear fog over a material cooked without any: the player's light list.
    pub fog_linear: bool,
}

pub const PASS_PLAYER_LIGHT: u8 = 1;
pub const PASS_REFLECTION_ALPHA: u8 = 2;
pub const PASS_PLAYER_OPACITY: u8 = 16;
pub const PASS_FLEXIBLE_COORDINATES: u8 = 32;

/// The material as a pass draws it. Light: the second colour channel unlit. Reflection: alpha from register 2 at stage 0.
/// Opacity: one stage, texture 0's colour, alpha from register 2, no channels, no alpha test. Flexible: the first three
/// coordinates are the first UV through their own matrices.
pub fn with_pass(source: &MaterialIr, pass: u8) -> MaterialIr {
    let mut m = source.clone();
    let off = ChannelIr { enable: 0, material_source: 0, ambient_source: 0, light_mask: 0, diffuse: 2, attenuation: 2 };
    let alpha_register2 = |s: &mut StageIr| {
        s.alpha_in = [3, 7, 7, 7];
        (s.alpha_op, s.alpha_bias, s.alpha_scale, s.alpha_clamp, s.alpha_out) = (0, 0, 0, 1, 0);
        (s.raster_swap, s.texture_swap) = (0, 0);
    };
    if pass & PASS_PLAYER_LIGHT != 0 {
        m.channels[2] = off;
    }
    if pass & PASS_PLAYER_OPACITY != 0 {
        m.pixel_stage_count = 1;
        m.channel_count = 0;
        m.coords.truncate(1);
        (m.alpha_comp0, m.alpha_op, m.alpha_comp1) = (7, 0, 7);
        m.channels = [off; 4];
        m.warp_orders.clear();
        if let Some(s) = m.stages.first_mut() {
            alpha_register2(s);
            s.color_in = [8, 15, 15, 15];
            (s.color_op, s.color_bias, s.color_scale, s.color_clamp, s.color_out) = (0, 0, 0, 1, 0);
            (s.coord, s.map, s.channel) = (0, 0, 255);
            s.warp = [0; 9];
        }
    }
    if pass & PASS_REFLECTION_ALPHA != 0 {
        if let Some(s) = m.stages.first_mut() {
            alpha_register2(s);
        }
    }
    if pass & PASS_FLEXIBLE_COORDINATES != 0 {
        for c in m.coords.iter_mut().take(3) {
            *c = CoordIr { kind: 1, source: 4, matrix: 0 };
        }
    }
    m
}

/// The range a register can hold, in the 0-1 scale of the stages: colour and alpha, from the material's value and every
/// animation track that can write it.
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct Range(pub f32, pub f32);

impl Range {
    const UNIT: Range = Range(0.0, 1.0);
    const UNKNOWN: Range = Range(-4.0, 4.0);
    fn one(v: f32) -> Range {
        Range(v, v)
    }
    fn add(self, o: Range) -> Range {
        Range(self.0 + o.0, self.1 + o.1)
    }
    fn sub(self, o: Range) -> Range {
        Range(self.0 - o.1, self.1 - o.0)
    }
    fn mul(self, o: Range) -> Range {
        let v = [self.0 * o.0, self.0 * o.1, self.1 * o.0, self.1 * o.1];
        Range(v.iter().copied().fold(f32::MAX, f32::min), v.iter().copied().fold(f32::MIN, f32::max))
    }
    fn hull(self, o: Range) -> Range {
        Range(self.0.min(o.0), self.1.max(o.1))
    }
    fn saturate(self) -> Range {
        Range(self.0.clamp(0.0, 1.0), self.1.clamp(0.0, 1.0))
    }
    /// Its low 8 bits are itself: floor(x * 255 / 256) is 0 over the whole range.
    fn stays(self) -> bool {
        self.0 >= 0.0 && self.1 < 256.0 / 255.0
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Registers {
    pub color: [Range; 3],
    pub alpha: [Range; 3],
}

impl Registers {
    pub fn of(m: &MaterialIr) -> Registers {
        let value = |r: usize, c: usize| Range::one(m.registers[r][c] as f32 / 255.0);
        let mut out = Registers { color: [Range::UNIT; 3], alpha: [Range::UNIT; 3] };
        for r in 0..3 {
            out.color[r] = value(r, 0).hull(value(r, 1)).hull(value(r, 2));
            out.alpha[r] = value(r, 3);
        }
        out
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug, PartialOrd, Ord)]
pub enum Input {
    Normal,
    Color0,
    Color1,
    /// A coordinate of the mesh, by its number.
    Coord(u8),
    Tangent,
    Binormal,
    /// The painted light's de-light gain (nebula_model.h NEBULA_INPUT_GAIN; model_write.rs debake_gains).
    Gain,
}

#[derive(Default, Clone)]
pub struct Text {
    pub vertex: String,
    pub pixel: String,
    /// What each constant row of the stage holds.
    pub vertex_rows: Vec<u32>,
    pub pixel_rows: Vec<u32>,
    /// The material's texture numbers, in the order the pixel stage reads them (TEX(n), SAMP(n): program.hlsli).
    pub textures: Vec<u8>,
    /// What each vertex attribute after position and matrix reads, in order: COLOR, NORMAL by name, UVn for coordinates.
    pub inputs: Vec<Input>,
    pub alpha_test: bool,
}

/// Rows of one stage, numbered by first use.
#[derive(Default)]
struct Rows(Vec<u32>);

impl Rows {
    fn at(&mut self, what: u32) -> String {
        let n = match self.0.iter().position(|&r| r == what) {
            Some(n) => n,
            None => {
                self.0.push(what);
                self.0.len() - 1
            }
        };
        format!("m[{n}]")
    }
    fn declare(&self, register: u32, name: &str) -> String {
        if self.0.is_empty() {
            return String::new();
        }
        format!("cbuffer {name} : register(b{register}) {{\n    float4 m[{}];\n}};\n", self.0.len())
    }
}

/// Numbers in first-use order.
#[derive(Default)]
struct Numbers(Vec<u8>);

impl Numbers {
    fn at(&mut self, what: u8) -> usize {
        match self.0.iter().position(|&r| r == what) {
            Some(n) => n,
            None => {
                self.0.push(what);
                self.0.len() - 1
            }
        }
    }
}

fn konst_color(rows: &mut Rows, sel: u8) -> Result<String> {
    if sel < 8 {
        let f = FRACTION[sel as usize];
        return Ok(format!("float3({f}, {f}, {f})"));
    }
    if (0x0C..=0x0F).contains(&sel) {
        return Ok(format!("{}.rgb", rows.at(ROW_CONSTANT + (sel - 0x0C) as u32)));
    }
    if (0x10..=0x1F).contains(&sel) {
        let comp = RGBA[((sel - 0x10) / 4) as usize];
        return Ok(format!("{}.{comp}{comp}{comp}", rows.at(ROW_CONSTANT + ((sel - 0x10) % 4) as u32)));
    }
    bail!("constant colour selection {sel}")
}

fn konst_alpha(rows: &mut Rows, sel: u8) -> Result<String> {
    if sel < 8 {
        return Ok(FRACTION[sel as usize].to_string());
    }
    if (0x10..=0x1F).contains(&sel) {
        return Ok(format!("{}.{}", rows.at(ROW_CONSTANT + ((sel - 0x10) % 4) as u32), RGBA[((sel - 0x10) / 4) as usize]));
    }
    bail!("constant alpha selection {sel}")
}

fn swizzle(table: &[u8; 4]) -> String {
    table.iter().map(|&c| RGBA[(c & 3) as usize]).collect()
}

fn register_of(name: &str) -> Option<usize> {
    DEST.iter().position(|r| *r == name.split('.').next().unwrap_or(name))
}

/// Where a coordinate's vector comes from.
#[derive(Clone, Copy, PartialEq, Eq)]
enum From {
    ObjectPosition,
    ObjectNormal,
    EyePosition,
    EyeNormal,
    WorldPosition,
    WorldNormal,
    Tangent,
    Binormal,
    Uv(u8),
    Color,
}

/// Modes whose matrix today carries the inverse view read world space instead; the game leaves that factor out.
pub fn world_mode(mode: u8) -> bool {
    matches!(mode, 2 | 5 | 8 | 10 | 11)
}

fn coord_source(m: &MaterialIr, c: &CoordIr) -> Result<From> {
    if (2..=9).contains(&c.kind) {
        bail!("coordinate kind {} (emboss)", c.kind);
    }
    if c.kind == 10 {
        return Ok(From::Color);
    }
    let mode = if c.matrix != NONE {
        let slot = c.matrix;
        m.matrices.iter().find(|x| u32::from(x.slot) == slot).map_or(0, |x| x.info & 0x3f)
    } else {
        0
    };
    Ok(match c.source {
        0 if mode == 0 => From::ObjectPosition,
        1 if mode == 0 => From::ObjectNormal,
        0 if world_mode(mode) || m.on_billboard => From::WorldPosition,
        1 if world_mode(mode) || m.on_billboard => From::WorldNormal,
        0 => From::EyePosition,
        1 => From::EyeNormal,
        2 => From::Tangent,
        3 => From::Binormal,
        4..=19 => From::Uv(c.source - 4),
        32 | 33 => From::Color,
        n => bail!("coordinate source {n}"),
    })
}

fn is_projective(c: &CoordIr) -> bool {
    c.kind == 0
}

struct Pixel<'a> {
    m: &'a MaterialIr,
    form: Form,
    rows: Rows,
    maps: Numbers,
    coords: Numbers,
    lines: Vec<String>,
    /// For each coordinate: read as texels by a warp; the maps it is sampled from.
    texels: BTreeSet<u8>,
    sampled: Vec<(u8, u8)>,
    color_read: [bool; 2],
    env_coords: BTreeSet<u8>,
    /// c58: the coordinates whose every map is a painted highlight sprite (HIGHLIGHT_FETCH in a lit form).
    sprite_coords: BTreeSet<u8>,
}

impl Pixel<'_> {
    fn uses_tex(s: &StageIr) -> bool {
        s.map != 0xFF && (s.color_in.contains(&8) || s.color_in.contains(&9) || s.alpha_in.contains(&4))
    }
    fn uses_warp(s: &StageIr) -> bool {
        s.warp.iter().any(|&b| b != 0)
    }
    fn stages(&self) -> &[StageIr] {
        &self.m.stages[..self.m.pixel_stage_count as usize]
    }
    fn warp_used(&self) -> bool {
        !self.m.warp_orders.is_empty() && self.stages().iter().any(Self::uses_warp)
    }
    /// Which coordinates are sampled from which maps and read as texels: a coordinate's scale over the size of the one map
    /// it samples cancels.
    fn survey(&mut self) {
        let warp = self.warp_used();
        let (replace, stages) = (self.form.replace, self.stages().to_vec());
        if replace == 1 || replace == 5 {
            self.sampled.push((0, 0));
            return;
        }
        if replace != 0 {
            return;
        }
        let mut warp_stages = BTreeSet::new();
        for s in &stages {
            if Self::uses_tex(s) {
                if warp && Self::uses_warp(s) {
                    self.texels.insert(s.coord);
                    warp_stages.insert(s.warp[0]);
                } else {
                    self.sampled.push((s.coord, s.map));
                }
            }
        }
        for n in warp_stages {
            if let Some(order) = self.m.warp_orders.get(n as usize) {
                self.sampled.push((order[0], order[1]));
            }
        }
    }
    fn plain_coord(&self, c: u8) -> bool {
        if self.texels.contains(&c) {
            return false;
        }
        let maps: BTreeSet<u8> = self.sampled.iter().filter(|x| x.0 == c).map(|x| x.1).collect();
        maps.len() == 1
    }
    fn projective(&self, c: u8) -> bool {
        if self.form.normal_coord && c == 0 {
            return false;
        }
        self.m.coords.get(c as usize).is_some_and(is_projective)
    }
    fn coord_value(&mut self, c: u8) -> String {
        let n = self.coords.at(c);
        if self.projective(c) {
            format!("(i.uv{n}.z != 0.0 ? i.uv{n}.xy / i.uv{n}.z : i.uv{n}.xy)")
        } else {
            format!("i.uv{n}")
        }
    }
    /// The coordinate with which a texture is sampled, without a warp.
    fn sample_coord(&mut self, c: u8, map: u8, divide: Option<(u32, u32)>) -> String {
        let value = self.coord_value(c);
        let part = divide.map_or(String::new(), |(s, t)| format!(" / float2({s}.0, {t}.0)"));
        if self.plain_coord(c) {
            return format!("{value}{part}");
        }
        let scale = self.rows.at(ROW_SCALE + c as u32);
        let size = self.rows.at(ROW_SCALE + map as u32);
        format!("({value} * {scale}.xy){part} / {size}.zw")
    }
    fn sample(&mut self, map: u8, coord: &str) -> String {
        let n = self.maps.at(map);
        format!("TEX({n}).Sample(SAMP({n}), {coord})")
    }
    fn raster(&mut self, channel: u8) -> Result<&'static str> {
        Ok(match channel {
            4 => {
                self.color_read[0] = true;
                "i.color"
            }
            5 => {
                self.color_read[1] = true;
                "i.color1"
            }
            7 | 8 => bail!("bump alpha raster"),
            _ => "float4(0.0, 0.0, 0.0, 0.0)",
        })
    }
}

/// A stage's input: a register through its low 8 bits unless its range keeps them; what it can hold after.
fn operand(name: &str, state: &[[Range; 2]; 4], k_range: Range) -> (String, Range) {
    let base = name.split('.').next().unwrap_or(name);
    let field = name.split('.').nth(1).unwrap_or("");
    if let Some(reg) = register_of(name) {
        let range = if field.starts_with('a') { state[reg][1] } else { state[reg][0] };
        if range.stays() {
            return (name.to_string(), range);
        }
        return (format!("low8({name})"), Range(0.0, 256.0 / 255.0));
    }
    let range = match base {
        "t" | "ras" => Range::UNIT,
        "k" | "ka" => k_range,
        _ if name.starts_with("float3(1.0") || name == "1.0" => Range::one(1.0),
        _ if name.starts_with("float3(128.0") => Range::one(128.0 / 255.0),
        _ => Range::one(0.0),
    };
    (name.to_string(), range)
}

fn direct(name: &str, state: &[[Range; 2]; 4], k_range: Range) -> Range {
    if let Some(reg) = register_of(name) {
        let field = name.split('.').nth(1).unwrap_or("");
        return if field.starts_with('a') { state[reg][1] } else { state[reg][0] };
    }
    operand(name, state, k_range).1
}

fn combine_range(d: Range, sign: i32, a: Range, b: Range, c: Range, bias: u8, scale: u8) -> Range {
    let mix = a.mul(Range::one(1.0).sub(c)).add(b.mul(c));
    let mut v = if sign < 0 { d.sub(mix) } else { d.add(mix) };
    v = match bias % 3 {
        1 => v.add(Range::one(128.0 / 255.0)),
        2 => v.sub(Range::one(128.0 / 255.0)),
        _ => v,
    };
    match scale & 3 {
        1 => v.mul(Range::one(2.0)),
        2 => v.mul(Range::one(4.0)),
        3 => v.mul(Range::one(0.5)),
        _ => v,
    }
}

fn alpha_stage(s: &StageIr, state: &[[Range; 2]; 4], k_range: Range) -> Result<(String, Range)> {
    let names = [CA[s.alpha_in[0] as usize & 7], CA[s.alpha_in[1] as usize & 7], CA[s.alpha_in[2] as usize & 7], CA[s.alpha_in[3] as usize & 7]];
    let (a, ra) = operand(names[0], state, k_range);
    let (b, rb) = operand(names[1], state, k_range);
    let (c, rc) = operand(names[2], state, k_range);
    let d = names[3];
    let rd = direct(d, state, k_range);
    let op = s.alpha_op;
    let (expr, range) = if op <= 1 {
        let sign = if op == 1 { "-" } else { "+" };
        (
            format!("({d} {sign} lerp({a}, {b}, {c}){}){}", BIAS[s.alpha_bias as usize % 3], SCALE[s.alpha_scale as usize & 3]),
            combine_range(rd, if op == 1 { -1 } else { 1 }, ra, rb, rc, s.alpha_bias, s.alpha_scale),
        )
    } else if op == 14 || op == 15 {
        let cmp = if op == 14 { ">" } else { "==" };
        (format!("{d} + (round({a} * 255.0) {cmp} round({b} * 255.0) ? {c} : 0.0)"), Range::UNKNOWN)
    } else {
        bail!("alpha compare op {op} on colour channels");
    };
    Ok(if s.alpha_clamp != 0 { (format!("saturate({expr})"), range.saturate()) } else { (expr, range) })
}

fn color_stage(s: &StageIr, state: &[[Range; 2]; 4], k_range: Range) -> Result<(String, Range)> {
    let names = [CC[s.color_in[0] as usize & 15], CC[s.color_in[1] as usize & 15], CC[s.color_in[2] as usize & 15], CC[s.color_in[3] as usize & 15]];
    let (a, ra) = operand(names[0], state, k_range);
    let (b, rb) = operand(names[1], state, k_range);
    let (c, rc) = operand(names[2], state, k_range);
    let d = names[3];
    let rd = direct(d, state, k_range);
    let op = s.color_op;
    let (expr, range) = if op <= 1 {
        let sign = if op == 1 { "-" } else { "+" };
        (
            format!("({d} {sign} lerp({a}, {b}, {c}){}){}", BIAS[s.color_bias as usize % 3], SCALE[s.color_scale as usize & 3]),
            combine_range(rd, if op == 1 { -1 } else { 1 }, ra, rb, rc, s.color_bias, s.color_scale),
        )
    } else if op == 14 || op == 15 {
        let cmp = if op == 14 { ">" } else { "==" };
        (format!("{d} + float3(round({a} * 255.0) {cmp} round({b} * 255.0)) * {c}"), Range::UNKNOWN)
    } else {
        let pack = match op & !1 {
            8 => "round(x.r * 255.0)",
            10 => "round(x.g * 255.0) * 256.0 + round(x.r * 255.0)",
            12 => "dot(round(x * 255.0), float3(1.0, 256.0, 65536.0))",
            _ => bail!("colour op {op}"),
        };
        let cmp = if op % 2 == 0 { ">" } else { "==" };
        let (pa, pb) = (pack.replace('x', &format!("({a})")), pack.replace('x', &format!("({b})")));
        (format!("{d} + (({pa}) {cmp} ({pb}) ? {c} : float3(0.0, 0.0, 0.0))"), Range::UNKNOWN)
    };
    Ok(if s.color_clamp != 0 { (format!("saturate({expr})"), range.saturate()) } else { (expr, range) })
}

fn constant_range(sel: u8) -> Range {
    if (sel as usize) < FRACTION_VALUE.len() {
        Range::one(FRACTION_VALUE[sel as usize])
    } else {
        Range::UNIT
    }
}

fn fog_lines(fog_type: u8, rows: &mut Rows) -> Vec<String> {
    if fog_type == 0 {
        return Vec::new();
    }
    let fog = rows.at(ROW_FOG);
    let color = rows.at(ROW_FOG_COLOR);
    let ze = if fog_type & 8 != 0 { format!("lerp({fog}.z, {fog}.w, i.pos.z)") } else { "i.pos.w".to_string() };
    let amount = format!("fog_amount({ze}, {fog}.x, {fog}.y)");
    let curved = match fog_type & 7 {
        4 => format!("fog_exp({amount})"),
        5 => format!("fog_exp2({amount})"),
        6 => format!("fog_rev_exp({amount})"),
        7 => format!("fog_rev_exp2({amount})"),
        _ => amount,
    };
    // program.hlsli program_fog_curve: fogs at once, or in a traced form after the traced light, the fog lit by the
    // traced share along its curve (RT-MASTER-PLAN 15.3; FB b57): start, end, the pixel's depth, the type.
    vec![format!("    prev.rgb = program_fog_curve(prev.rgb, {color}.rgb, {curved}, float4({fog}.x, {fog}.y, {ze}, {fog_type}.0));")]
}

fn replace_lines(p: &mut Pixel, fog_type: u8) -> Result<()> {
    let stages = p.stages().to_vec();
    let mode = p.form.replace;
    let first = stages.first().copied();
    let k0 = match &first {
        Some(s) => konst_alpha(&mut p.rows, s.alpha_constant)?,
        None => "1.0".to_string(),
    };
    let ovr = p.rows.at(ROW_OVERRIDE);
    let texture0 = if p.m.coords.is_empty() {
        "float4(0.0, 0.0, 0.0, 0.0)".to_string()
    } else {
        let coord = p.sample_coord(0, 0, None);
        p.sample(0, &coord)
    };
    p.lines.push("    float4 prev;".into());
    let mut tested = "prev.a".to_string();
    match mode {
        1 => {
            p.lines.push(format!("    const float4 t = {texture0};"));
            p.lines.push("    prev = float4(t.rgb, 0.25);".into());
        }
        2 | 3 | 4 => {
            p.color_read[0] = true;
            p.lines.push("    const float4 ras = i.color;".into());
            p.lines.push(match mode {
                2 => format!("    prev = float4(ras.rgb, {ovr}.y);"),
                3 => format!("    prev = float4(ras.rgb, saturate(ras.a - {k0} * (1.0 - {ovr}.y)));"),
                _ => format!("    prev = float4(ras.rgb, saturate(lerp({ovr}.z, {ovr}.y, ras.a) * 0.5));"),
            });
        }
        5 => {
            // Stage 0's alpha with no channel: its operands as the material names them.
            p.lines.push(format!("    const float4 t = {texture0};"));
            p.lines.push("    const float4 ras = float4(0.0, 0.0, 0.0, 0.0);".into());
            p.lines.push(format!("    const float ka = {k0};"));
            let a0 = match &first {
                Some(s) => {
                    let mut state = [[Range::UNKNOWN; 2]; 4];
                    state[0] = [Range::one(1.0); 2];
                    for reg in 1..4 {
                        let name = DEST[reg];
                        let row = p.rows.at(ROW_REGISTER + reg as u32 - 1);
                        if s.alpha_in.iter().any(|&x| x as usize == reg) {
                            p.lines.push(format!("    const float4 {name} = {row};"));
                        }
                    }
                    if s.alpha_in.contains(&0) {
                        p.lines.push("    const float4 prev_in = float4(1.0, 1.0, 1.0, 1.0);".into());
                    }
                    alpha_stage(s, &state, Range::UNIT)?.0.replace("prev.a", "prev_in.a")
                }
                None => "1.0".to_string(),
            };
            p.lines.push(format!("    const float a0 = {a0};"));
            p.lines.push(format!("    prev = float4(saturate(t.rgb * 0.5), {ovr}.y > 0.0 ? {ovr}.y : a0);"));
            tested = "(a0 >= 0.0 ? a0 : prev.a)".to_string();
        }
        _ => bail!("replacing form {mode}"),
    }
    p.lines.push(format!("    if (round({tested} * 255.0) <= {ovr}.w) {{"));
    p.lines.push("        discard;".into());
    p.lines.push("    }".into());
    let fog = fog_lines(fog_type, &mut p.rows);
    p.lines.extend(fog);
    Ok(())
}

fn stage_lines(p: &mut Pixel, registers: &Registers) -> Result<bool> {
    let stages = p.stages().to_vec();
    let warp_used = p.warp_used();
    let reads_before_write = |reg: usize| -> bool {
        let (mut color_written, mut alpha_written) = (false, false);
        for s in &stages {
            let reads_color = s.color_in.iter().any(|&x| (x as usize) / 2 == reg && x < 8 && x & 1 == 0);
            let reads_alpha_in_color = s.color_in.iter().any(|&x| (x as usize) / 2 == reg && x < 8 && x & 1 == 1);
            let reads_alpha = s.alpha_in.iter().any(|&x| x as usize == reg);
            if (reads_color && !color_written) || ((reads_alpha_in_color || reads_alpha) && !alpha_written) {
                return true;
            }
            color_written |= s.color_out as usize & 3 == reg;
            alpha_written |= s.alpha_out as usize & 3 == reg;
        }
        false
    };
    let last = stages.last().copied();
    let mentioned = |reg: usize| -> bool {
        stages.iter().any(|s| {
            s.color_in.iter().any(|&x| (x as usize) / 2 == reg && x < 8)
                || s.alpha_in.iter().any(|&x| x as usize == reg)
                || s.color_out as usize & 3 == reg
                || s.alpha_out as usize & 3 == reg
        })
    };
    let mut state = [[Range::UNKNOWN; 2]; 4];
    state[0] = [Range::one(1.0); 2];
    p.lines.push("    float4 prev = float4(1.0, 1.0, 1.0, 1.0);".into());
    for reg in 1..4 {
        state[reg] = [registers.color[reg - 1], registers.alpha[reg - 1]];
        if mentioned(reg) {
            if reads_before_write(reg) {
                let row = p.rows.at(ROW_REGISTER + reg as u32 - 1);
                p.lines.push(format!("    float4 {} = {row};", DEST[reg]));
            } else {
                p.lines.push(format!("    float4 {} = float4(0.0, 0.0, 0.0, 0.0);", DEST[reg]));
            }
        }
    }
    let mut warp_stages = BTreeSet::new();
    if warp_used {
        for s in &stages {
            if Pixel::uses_tex(s) && Pixel::uses_warp(s) {
                warp_stages.insert(s.warp[0]);
            }
        }
        p.lines.push("    float2 tevcoord = float2(0.0, 0.0);".into());
        for &n in &warp_stages {
            let Some(order) = p.m.warp_orders.get(n as usize).copied() else {
                bail!("a stage warps from order {n}, past the material's");
            };
            let (coord, imap, scale_s, scale_t) = (order[0], order[1], order[2], order[3]);
            let c = p.sample_coord(coord, imap, Some((1u32 << scale_s, 1u32 << scale_t)));
            let sampled = p.sample(imap, &c);
            p.lines.push(format!("    const float3 ind{n} = round({sampled}.abg * 255.0);"));
        }
    }
    for (number, s) in stages.iter().enumerate() {
        p.lines.push("    {".into());
        let reads_t = s.color_in.contains(&8) || s.color_in.contains(&9) || s.alpha_in.contains(&4);
        if Pixel::uses_tex(s) && reads_t {
            let (c, map) = (s.coord, s.map);
            let mut coord = String::new();
            if warp_used && Pixel::uses_warp(s) {
                let w = &s.warp;
                let (ind_stage, fmt, bias, matrix, wrap_s, wrap_t, add_prev, ind_alpha) = (w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[8]);
                if ind_alpha != 0 {
                    bail!("bump alpha");
                }
                let value = p.coord_value(c);
                let scale = p.rows.at(ROW_SCALE + c as u32);
                p.lines.push(format!("        float2 texel = {value} * {scale}.xy;"));
                for (axis, wrap) in [("x", wrap_s), ("y", wrap_t)] {
                    if wrap == 6 {
                        p.lines.push(format!("        texel.{axis} = 0.0;"));
                    } else if wrap != 0 {
                        let size = 256u32 >> (wrap - 1);
                        p.lines.push(format!("        texel.{axis} = texel.{axis} - {size}.0 * floor(texel.{axis} / {size}.0);"));
                    }
                }
                let shift = [0u32, 3, 4, 5][fmt as usize & 3];
                let add = if fmt == 0 { -128 } else { 1 };
                p.lines.push(format!("        float3 sv = floor(ind{ind_stage} / {}.0);", 1u32 << shift));
                for (b, comp) in ["x", "y", "z"].iter().enumerate() {
                    if bias & (1 << b) != 0 {
                        p.lines.push(format!("        sv.{comp} += {add}.0;"));
                    }
                }
                match matrix {
                    1..=3 => {
                        let m = matrix as u32 - 1;
                        let r0 = p.rows.at(ROW_WARP + 2 * m);
                        let r1 = p.rows.at(ROW_WARP + 2 * m + 1);
                        p.lines.push(format!("        const float2 off = float2(dot({r0}.xyz, sv), dot({r1}.xyz, sv));"));
                    }
                    5..=7 => p.lines.push("        const float2 off = texel * sv.x / 256.0;".into()),
                    9..=11 => p.lines.push("        const float2 off = texel * sv.y / 256.0;".into()),
                    _ => p.lines.push("        const float2 off = float2(0.0, 0.0);".into()),
                }
                let previous = if add_prev != 0 { "tevcoord + " } else { "" };
                p.lines.push(format!("        tevcoord = {previous}texel + off;"));
                let size = p.rows.at(ROW_SCALE + map as u32);
                coord = format!("tevcoord / {size}.zw");
            }
            if coord.is_empty() {
                coord = p.sample_coord(c, map, None);
            }
            let sampled = p.sample(map, &coord);
            let sampled = format!("{sampled}.{}", swizzle(&p.m.swaps[s.texture_swap as usize & 3]));
            let wrapped = if p.form.lit && p.env_coords.contains(&c) { format!("enh_env({sampled})") } else { sampled };
            // A painted environment: the traced form's reflection takes its place (program.hlsli ENV_FETCH).
            let wrapped = if p.env_coords.contains(&c) { format!("ENV_FETCH({wrapped})") } else { wrapped };
            // A painted highlight sprite (c58): not drawn under the relight in a lit form; the sun's highlight stands for it.
            let wrapped = if p.form.lit && p.sprite_coords.contains(&c) { format!("HIGHLIGHT_FETCH({wrapped})") } else { wrapped };
            p.lines.push(format!("        const float4 t = {wrapped};"));
        } else if reads_t {
            p.lines.push("        const float4 t = float4(0.0, 0.0, 0.0, 0.0);".into());
        }
        if s.color_in.contains(&10) || s.color_in.contains(&11) || s.alpha_in.contains(&5) {
            let raster = p.raster(s.channel)?;
            let swap = swizzle(&p.m.swaps[s.raster_swap as usize & 3]);
            p.lines.push(format!("        const float4 ras = {raster}.{swap};"));
        }
        let mut k_color = Range::UNIT;
        if s.color_in.contains(&14) {
            let value = konst_color(&mut p.rows, s.color_constant)?;
            p.lines.push(format!("        const float3 k = {value};"));
            k_color = constant_range(s.color_constant);
        }
        let mut k_alpha = Range::UNIT;
        if s.alpha_in.contains(&6) {
            let value = konst_alpha(&mut p.rows, s.alpha_constant)?;
            p.lines.push(format!("        const float ka = {value};"));
            k_alpha = constant_range(s.alpha_constant);
        }
        let (color, color_range) = color_stage(s, &state, k_color)?;
        let (alpha, alpha_range) = alpha_stage(s, &state, k_alpha)?;
        p.lines.push(format!("        const float3 rc = {color};"));
        p.lines.push(format!("        const float ra = {alpha};"));
        p.lines.push(format!("        {}.rgb = rc;", DEST[s.color_out as usize & 3]));
        p.lines.push(format!("        {}.a = ra;", DEST[s.alpha_out as usize & 3]));
        p.lines.push(format!("        NATIVE_STAGE({number}, float4(rc, ra))"));
        p.lines.push("    }".into());
        state[s.color_out as usize & 3][0] = color_range;
        state[s.alpha_out as usize & 3][1] = alpha_range;
    }
    match last {
        Some(s) => {
            let (c, a) = (s.color_out as usize & 3, s.alpha_out as usize & 3);
            let value = format!("float4({}.rgb, {}.a)", DEST[c], DEST[a]);
            if state[c][0].stays() && state[a][1].stays() {
                p.lines.push(format!("    prev = {value};"));
            } else {
                p.lines.push(format!("    prev = low8({value});"));
            }
        }
        None => {}
    }
    let m = p.m;
    let (comp0, op, comp1) = (m.alpha_comp0, m.alpha_op, m.alpha_comp1);
    let mut tested = false;
    if !(comp0 == 7 && comp1 == 7) && !(op == 1 && (comp0 == 7 || comp1 == 7)) {
        tested = true;
        let logic = ["&&", "||", "!=", "=="][op as usize & 3];
        let refs = p.rows.at(ROW_ALPHA_REF);
        let c0 = COMPARE[comp0 as usize & 7].replace("r8", &format!("round({refs}.x * 255.0)"));
        let c1 = COMPARE[comp1 as usize & 7].replace("r8", &format!("round({refs}.y * 255.0)"));
        p.lines.push("    const float a8 = round(prev.a * 255.0);".into());
        p.lines.push(format!("    if (!(({c0}) {logic} ({c1}))) {{"));
        p.lines.push("        discard;".into());
        p.lines.push("    }".into());
    }
    Ok(tested)
}

/// One channel's lines in the vertex stage, or its body in a lit pixel stage: `target.part = ...` with what it reads.
struct Channel {
    target: &'static str,
    vertex: &'static str,
}

/// `(lit terms)`: each light with its attenuation and diffuse.
fn light_list(c: &ChannelIr) -> Vec<(u32, u32, u32)> {
    let atten = match c.attenuation {
        0 => 1,
        1 => 3,
        _ => 2,
    };
    (0..8).filter(|n| (c.light_mask >> n) & 1 != 0).map(|n| (n, atten, c.diffuse as u32)).collect()
}

fn channel_is_lit(c: &ChannelIr) -> bool {
    c.enable != 0
}

fn channel_lines(m: &MaterialIr, ch: usize, names: &Channel, eye: &str, nrm: &str, base: &str, rows: &mut Rows, out: &mut Vec<String>) {
    let target = names.target;
    let vtx = names.vertex;
    if ch >= m.channel_count as usize {
        out.push(format!("    {target} = {vtx};"));
        return;
    }
    for (part, c) in [("rgb", &m.channels[2 * ch]), ("a", &m.channels[2 * ch + 1])] {
        let mat = if c.material_source != 0 {
            format!("{vtx}.{part}")
        } else {
            format!("{}.{part}", rows.at(ROW_MATERIAL_COLOR + ch as u32))
        };
        if c.enable == 0 {
            out.push(format!("    {target}.{part} = {mat};"));
            continue;
        }
        let amb = if c.ambient_source != 0 {
            format!("{vtx}.{part}")
        } else {
            format!("{}.{part}", rows.at(ROW_AMBIENT_COLOR + ch as u32))
        };
        let terms: Vec<String> =
            light_list(c).iter().map(|(n, atten, diffuse)| format!("light_term({base}, {n}u, {atten}u, {diffuse}u, {eye}, {nrm}).{part}")).collect();
        let terms = if terms.is_empty() { "0.0".to_string() } else { terms.join(" + ") };
        // Channel 0's light level, which the traced form keeps (program.hlsli TRACED_LEVEL: the added light's base).
        let level = format!("saturate((AMBIENT_TERM({amb}) * 255.0 + {terms}) / 255.0)");
        let level = if ch == 0 && part == "rgb" { format!("TRACED_LEVEL({level})") } else { level };
        out.push(format!("    {target}.{part} = {mat} * {level};"));
    }
}

/// The enhanced lit channel: lights in linear light (program_lit.hlsli).
fn enhanced_channel_lines(m: &MaterialIr, ch: usize, names: &Channel, eye: &str, nrm: &str, base: &str, metal: bool, rows: &mut Rows, out: &mut Vec<String>) {
    let target = names.target;
    let vtx = names.vertex;
    if ch >= m.channel_count as usize {
        out.push(format!("    {target} = {vtx};"));
        return;
    }
    let c = &m.channels[2 * ch];
    if c.enable != 0 {
        let mat = if c.material_source != 0 { format!("{vtx}.rgb") } else { format!("{}.rgb", rows.at(ROW_MATERIAL_COLOR + ch as u32)) };
        let amb = if c.ambient_source != 0 { format!("{vtx}.rgb") } else { format!("{}.rgb", rows.at(ROW_AMBIENT_COLOR + ch as u32)) };
        out.push("    {".into());
        out.push(format!("        const float3 enh_mat = {mat};"));
        out.push(format!("        const float3 enh_amb = {amb};"));
        out.push("        const float3 enh_amb_lin = enh_lin(enh_amb);".into());
        out.push("        float3 enh_acc = AMBIENT_TERM(enh_amb_lin * enh_sky(g_enh));".into());
        for (i, (n, atten, diffuse)) in light_list(c).iter().enumerate() {
            out.push(format!(
                "        enh_add_light({base}, {n}u, {}, {diffuse}u, {}, {metal}, {eye}, enh_amb, enh_amb_lin, enh_acc);",
                if *atten == 3 { "true" } else { "false" },
                if i == 0 { "true" } else { "false" },
            ));
        }
        out.push("        enh_acc *= enh_sky_reflection(g_enh, enh_amb_lin);".into());
        let level = "enh_srgb(enh_shoulder(max(enh_acc, 0.0)))";
        let level = if ch == 0 { format!("TRACED_LEVEL({level})") } else { level.to_string() };
        out.push(format!("        {target}.rgb = enh_mat * {level};"));
        out.push("    }".into());
    } else {
        let mat = if c.material_source != 0 { format!("{vtx}.rgb") } else { format!("{}.rgb", rows.at(ROW_MATERIAL_COLOR + ch as u32)) };
        out.push(format!("    {target}.rgb = {mat};"));
    }
    let a = &m.channels[2 * ch + 1];
    let mat = if a.material_source != 0 { format!("{vtx}.a") } else { format!("{}.a", rows.at(ROW_MATERIAL_COLOR + ch as u32)) };
    if a.enable == 0 {
        out.push(format!("    {target}.a = {mat};"));
    } else {
        let amb = if a.ambient_source != 0 { format!("{vtx}.a") } else { format!("{}.a", rows.at(ROW_AMBIENT_COLOR + ch as u32)) };
        let terms: Vec<String> =
            light_list(a).iter().map(|(n, atten, diffuse)| format!("light_term({base}, {n}u, {atten}u, {diffuse}u, {eye}, {nrm}).a")).collect();
        let terms = if terms.is_empty() { "0.0".to_string() } else { terms.join(" + ") };
        out.push(format!("    {target}.a = {mat} * saturate(({amb} * 255.0 + {terms}) / 255.0);"));
    }
}

fn light_terms_any(m: &MaterialIr) -> bool {
    (0..2 * (m.channel_count as usize).min(2)).any(|k| channel_is_lit(&m.channels[k]) && m.channels[k].light_mask != 0)
}

/// Whether the material has a lit form at all: lights, or a coordinate generated from the normal.
pub fn has_lit_form(m: &MaterialIr) -> Result<bool> {
    if light_terms_any(m) {
        return Ok(true);
    }
    for c in &m.coords {
        if matches!(coord_source(m, c)?, From::EyeNormal | From::WorldNormal | From::ObjectNormal) {
            return Ok(true);
        }
    }
    Ok(false)
}

/// The program's varyings. Its own coordinates take TEXCOORD0 up; the lit varyings (three) and the motion varyings (two)
/// follow them in that order, so their places move with the coordinate count.
fn varyings(uv_kinds: &[bool], color: [bool; 2], lit: bool) -> String {
    let lit_first = uv_kinds.len();
    let motion_first = lit_first + if lit { 3 } else { 0 };
    let mut s = format!("#define NEBULA_MOTION_CURRENT_INDEX {motion_first}\n#define NEBULA_MOTION_PREVIOUS_INDEX {}\n", motion_first + 1);
    s.push_str("struct Varyings {\n");
    if lit {
        let _ = writeln!(s, "    PER_SAMPLE float3 lit_eye : TEXCOORD{lit_first};\n    PER_SAMPLE float3 lit_nrm : TEXCOORD{};\n    nointerpolation uint lit_light : TEXCOORD{};", lit_first + 1, lit_first + 2);
    }
    s.push_str("    float4 pos : SV_Position;\n");
    if color[0] {
        s.push_str("    PER_SAMPLE float4 color : COLOR0;\n");
    }
    if color[1] {
        s.push_str("    PER_SAMPLE float4 color1 : COLOR1;\n");
    }
    for (k, three) in uv_kinds.iter().enumerate() {
        let _ = writeln!(s, "    PER_SAMPLE {} uv{k} : TEXCOORD{k};", if *three { "float3" } else { "float2" });
    }
    s.push_str("    VARYINGS_MOTION\n};\n");
    s
}

/// A material's two programs in one form. `fog_type` is the material's own; `skinned`: its mesh has a matrix per vertex.
pub fn generate(source: &MaterialIr, form: Form, skinned: bool, registers: &Registers) -> Result<Text> {
    let edited;
    let m = if form.pass != 0 {
        edited = with_pass(source, form.pass);
        &edited
    } else {
        source
    };
    let fog_type = if form.fog_linear { 2 } else { m.fog.kind };
    let mut env_coords = BTreeSet::new();
    for (i, c) in m.coords.iter().enumerate() {
        // A coordinate whose every map is a colour ramp (c56, MaterialIr ramp_maps) keeps its own fetch.
        let mut maps = m.stages.iter().filter(|s| s.map != 0xFF && usize::from(s.coord) == i).map(|s| s.map).peekable();
        let ramp_only = maps.peek().is_some() && maps.all(|map| m.ramp_maps.contains(&map));
        if !ramp_only && matches!(coord_source(m, c)?, From::EyeNormal | From::WorldNormal | From::ObjectNormal) {
            env_coords.insert(i as u8);
        }
    }
    // c58: a coordinate whose every map is a painted highlight sprite is no environment's (MaterialIr highlight_maps).
    let mut sprite_coords = BTreeSet::new();
    for i in 0..m.coords.len() {
        let mut maps = m.stages.iter().filter(|s| s.map != 0xFF && usize::from(s.coord) == i).map(|s| s.map).peekable();
        if maps.peek().is_some() && maps.all(|map| m.highlight_maps.contains(&map)) {
            sprite_coords.insert(i as u8);
        }
    }
    env_coords.retain(|c| !sprite_coords.contains(c));
    let lit_channels = form.lit && light_terms_any(m);
    let metal = !env_coords.is_empty();
    let mut p = Pixel {
        m,
        form,
        rows: Rows::default(),
        maps: Numbers::default(),
        coords: Numbers::default(),
        lines: Vec::new(),
        texels: BTreeSet::new(),
        sampled: Vec::new(),
        color_read: [false; 2],
        env_coords: env_coords.clone(),
        sprite_coords,
    };
    p.survey();
    let mut alpha_test = false;
    if form.replace != 0 {
        replace_lines(&mut p, fog_type)?;
        alpha_test = true;
    } else {
        alpha_test |= stage_lines(&mut p, registers)?;
        if form.lit {
            p.lines.push("    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);".into());
        }
        let fog = fog_lines(fog_type, &mut p.rows);
        p.lines.extend(fog);
    }
    // The lit form's channels run in the pixel stage and read both vertex colours.
    let mut lit_body: Vec<String> = Vec::new();
    let mut lit_rows_used = false;
    if lit_channels {
        lit_rows_used = true;
        let names = [Channel { target: "lit0", vertex: "i.color" }, Channel { target: "lit1", vertex: "i.color1" }];
        let (mut plain, mut enhanced) = (Vec::new(), Vec::new());
        for ch in 0..2 {
            if ch == 0 && !p.color_read[0] && !p.color_read[1] {
                continue;
            }
            if !p.color_read[ch] {
                continue;
            }
            channel_lines(m, ch, &names[ch], "i.lit_eye", "g_enh.N", "i.lit_light", &mut p.rows, &mut plain);
            enhanced_channel_lines(m, ch, &names[ch], "i.lit_eye", "g_enh.N", "i.lit_light", metal, &mut p.rows, &mut enhanced);
        }
        lit_body.push("    float4 lit0 = float4(0.0, 0.0, 0.0, 0.0);".into());
        lit_body.push("    float4 lit1 = float4(0.0, 0.0, 0.0, 0.0);".into());
        lit_body.push("    if (enh_on()) {".into());
        lit_body.extend(enhanced.iter().map(|l| format!("    {l}")));
        lit_body.push("    } else {".into());
        lit_body.extend(plain.iter().map(|l| format!("    {l}")));
        lit_body.push("    }".into());
    }

    // Whether the lit form's colour sums the key (light 0 in a lit colour channel it reads, as channel_lines writes
    // it): only then does its traced form take V and O in its sums (program_lit.hlsli TRACED_UNKEYED; D1).
    let keyed = lit_channels
        && (0..2usize).any(|ch| {
            p.color_read[ch] && ch < m.channel_count as usize && channel_is_lit(&m.channels[2 * ch]) && m.channels[2 * ch].light_mask & 1 != 0
        });

    // Vertex stage: writes exactly what the pixel stage reads, numbered as it reads them.
    let coords_read: Vec<u8> = p.coords.0.clone();
    let mut v_rows = Rows::default();
    let mut inputs: Vec<Input> = Vec::new();
    let mut body: Vec<String> = Vec::new();
    let color_written = if lit_channels { [p.color_read[0], p.color_read[1]] } else { p.color_read };
    let needs_color0_for_coords = coords_read.iter().any(|&c| {
        !(form.normal_coord && c == 0) && m.coords.get(c as usize).is_some_and(|x| matches!(coord_source(m, x), Ok(From::Color)))
    });
    let needs_channel0 = color_written[0] || (needs_color0_for_coords && !lit_channels);
    if form.lit {
        body.push("    o.lit_eye = p.eye;".into());
        body.push("    o.lit_nrm = nrm;".into());
        body.push("    o.lit_light = p.light;".into());
    }
    let mut reads_color = [false; 2];
    let names = [Channel { target: "c0", vertex: "v.color0" }, Channel { target: "c1", vertex: "v.color1" }];
    if lit_channels {
        for ch in 0..2 {
            if color_written[ch] {
                body.push(format!("    o.color{} = v.color{ch};", if ch == 0 { "" } else { "1" }));
                reads_color[ch] = true;
            }
        }
    } else {
        for ch in 0..2 {
            let wanted = if ch == 0 { needs_channel0 } else { color_written[1] };
            if !wanted {
                continue;
            }
            body.push(format!("    float4 c{ch} = float4(0.0, 0.0, 0.0, 0.0);"));
            let mut lines = Vec::new();
            channel_lines(m, ch, &names[ch], "p.eye", "nrm", "p.light", &mut v_rows, &mut lines);
            if lines.iter().any(|l| l.contains(names[ch].vertex)) {
                reads_color[ch] = true;
            }
            body.extend(lines);
            if color_written[ch] {
                body.push(format!("    o.color{} = c{ch};", if ch == 0 { "" } else { "1" }));
            }
        }
    }
    let mut uv_kinds = Vec::new();
    for (k, &c) in coords_read.iter().enumerate() {
        if form.normal_coord && c == 0 {
            body.push("    {".into());
            body.push("        const float3 n = dot(v.nrm, v.nrm) > 0.0 ? normalize(v.nrm) : v.nrm;".into());
            body.push(format!("        o.uv{k} = n.xy;"));
            body.push("    }".into());
            uv_kinds.push(false);
            continue;
        }
        let Some(coord) = m.coords.get(c as usize) else {
            bail!("a stage reads coordinate {c}, past the material's {}", m.coords.len());
        };
        let source = coord_source(m, coord)?;
        if source == From::Color {
            let from = if lit_channels { "v.color0" } else { "c0" };
            if lit_channels {
                reads_color[0] = true;
            }
            body.push(format!("    o.uv{k} = {from}.rg;"));
            uv_kinds.push(false);
            continue;
        }
        let vector = match source {
            From::ObjectPosition => "float4(v.pos, 1.0)".to_string(),
            From::ObjectNormal => "float4(v.nrm, 1.0)".to_string(),
            From::EyePosition => "float4(p.eye, 1.0)".to_string(),
            From::EyeNormal => format!("float4({}(p, v.nrm), 1.0)", if skinned { "eye_normal_skinned" } else { "eye_normal_rigid" }),
            From::WorldPosition => "float4(world_position(p, v.pos), 1.0)".to_string(),
            From::WorldNormal => format!("float4({}(p, v.nrm), 1.0)", if skinned { "world_normal_skinned" } else { "world_normal_rigid" }),
            From::Tangent => {
                if !inputs.contains(&Input::Tangent) {
                    inputs.push(Input::Tangent);
                }
                "float4(v.tangent, 1.0)".to_string()
            }
            From::Binormal => {
                if !inputs.contains(&Input::Binormal) {
                    inputs.push(Input::Binormal);
                }
                "float4(v.binormal, 1.0)".to_string()
            }
            From::Uv(n) => {
                let at = match inputs.iter().filter(|i| matches!(i, Input::Coord(_))).position(|i| *i == Input::Coord(n)) {
                    Some(at) => at,
                    None => {
                        inputs.push(Input::Coord(n));
                        inputs.iter().filter(|i| matches!(i, Input::Coord(_))).count() - 1
                    }
                };
                format!("float4(v.uv{at}, 1.0, 1.0)")
            }
            From::Color => unreachable!(),
        };
        let (a, b, d) = (ROW_COORD + 3 * c as u32, ROW_COORD + 3 * c as u32 + 1, ROW_COORD + 3 * c as u32 + 2);
        let projective = is_projective(coord);
        body.push("    {".into());
        body.push(format!("        const float4 c = {vector};"));
        if projective {
            let (ra, rb, rd) = (v_rows.at(a), v_rows.at(b), v_rows.at(d));
            body.push(format!("        float3 res = float3(dot(c, {ra}), dot(c, {rb}), dot(c, {rd}));"));
            body.push("        if (res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);".into());
            body.push(format!("        o.uv{k} = res;"));
        } else {
            let (ra, rb) = (v_rows.at(a), v_rows.at(b));
            body.push(format!("        o.uv{k} = float2(dot(c, {ra}), dot(c, {rb}));"));
        }
        body.push("    }".into());
        uv_kinds.push(projective);
    }

    // An unlit channel's vertex colour de-lit by the cooker's gain, by kDebake (program.hlsli traced_vertex_gain; D2):
    // in the traced form alone (compile_programs.py gives such a stage its traced forms), so a draw without ray tracing
    // runs what it ran before.
    // Every form that reads colour 0: the gain word is the cooker's say (0, a gain of 1, where its rgb is lit).
    let gained = reads_color[0];
    if gained {
        for (k, line) in ["#if NATIVE_TRACED", "    v.color0.rgb = traced_vertex_gain(v.color0.rgb, v.gain);", "#endif"].into_iter().enumerate() {
            body.insert(k, line.into());
        }
    }
    // Lit lines name the turned normal; only a stage that reads it pays for it.
    if body.iter().any(|l| l.contains(", nrm)") || l.contains("= nrm;")) {
        body.insert(0, "    const float3 nrm = lit_normal(p, v.nrm);".into());
    }
    let read_normal = body.iter().any(|l| l.contains("v.nrm"));
    let vary = varyings(&uv_kinds, color_written, form.lit);
    let mut vertex_in = String::from("struct VertexIn {\n    float3 pos : POSITION;\n    uint draw_matrix : BLENDINDICES;\n");
    let mut final_inputs = Vec::new();
    if read_normal {
        vertex_in.push_str("    float3 nrm : NORMAL;\n");
        final_inputs.push(Input::Normal);
    }
    for ch in 0..2 {
        if reads_color[ch] {
            let _ = writeln!(vertex_in, "    float4 color{ch} : COLOR{ch};");
            final_inputs.push(if ch == 0 { Input::Color0 } else { Input::Color1 });
        }
    }
    if gained {
        vertex_in.push_str("    uint gain : BLENDWEIGHT;\n");
        final_inputs.push(Input::Gain);
    }
    let mut uv = 0;
    for i in &inputs {
        match i {
            Input::Coord(_) => {
                let _ = writeln!(vertex_in, "    float2 uv{uv} : TEXCOORD{uv};");
                uv += 1;
            }
            Input::Tangent => vertex_in.push_str("    float3 tangent : TANGENT;\n"),
            Input::Binormal => vertex_in.push_str("    float3 binormal : BINORMAL;\n"),
            _ => {}
        }
        final_inputs.push(*i);
    }
    vertex_in.push_str("};\n");

    // A vertex stage that sums lights takes the game's ambient: the traced light reaches only a pixel stage
    // (program_lit.hlsli's AMBIENT_TERM).
    let mut vertex = String::from("#include \"program.hlsli\"\n#define AMBIENT_TERM(x) (x)\n");
    vertex.push_str(&v_rows.declare(1, "MaterialVertex"));
    vertex.push_str(&vertex_in);
    vertex.push_str(&vary);
    vertex.push_str("Varyings program_vs(VertexIn v, uint instance : SV_InstanceID) {\n    Varyings o = (Varyings)0;\n    const Place p = native_place(v.pos, v.draw_matrix, instance);\n    PLACE_OUT(o, p);\n");
    for l in &body {
        vertex.push_str(l);
        vertex.push('\n');
    }
    vertex.push_str("    return o;\n}\n");

    let mut pixel = String::from("#include \"program.hlsli\"\n");
    if form.lit {
        pixel.push_str("#include \"program_lit.hlsli\"\n");
    }
    pixel.push_str(&p.rows.declare(2, "MaterialPixel"));
    pixel.push_str(&vary);
    pixel.push_str("NativeOut program_ps(Varyings i) {\n");
    if form.lit {
        if !keyed {
            pixel.push_str("    TRACED_UNKEYED;\n");
        }
        pixel.push_str("    g_enh = enh_begin(i.lit_eye, i.lit_nrm);\n");
        if !m.highlight_maps.is_empty() {
            // c58: the enhanced highlight as the sprite painted it (its roughness and peak), under the relight.
            pixel.push_str(&format!("    enh_sprite({:.3}, {:.3});\n", m.highlight_rough, m.highlight_gain));
        }
    }
    if lit_rows_used {
        for l in &lit_body {
            pixel.push_str(l);
            pixel.push('\n');
        }
        if p.color_read[0] {
            pixel.push_str("    i.color = lit0;\n");
        }
        if p.color_read[1] {
            pixel.push_str("    i.color1 = lit1;\n");
        }
    } else if form.lit && form.replace == 0 {
        pixel.push_str("    if (enh_on()) enh_key_highlight(i.lit_light, g_enh, i.lit_eye);\n");
    }
    for l in &p.lines {
        pixel.push_str(l);
        pixel.push('\n');
    }
    if form.lit {
        pixel.push_str("    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));\n}\n");
    } else {
        pixel.push_str("    PROGRAM_RETURN(saturate(prev));\n}\n");
    }

    let textures = p.maps.0.clone();
    Ok(Text { vertex, pixel, vertex_rows: v_rows.0, pixel_rows: p.rows.0, textures, inputs: final_inputs, alpha_test })
}

/// The headers a mod's programs include, for building them on the player's machine.
pub const HEADERS: [(&str, &str); 3] = [
    ("nebula_limits.hlsli", include_str!("../../../../runtime/src/render/materials/nebula_limits.hlsli")),
    ("program.hlsli", include_str!("../../../../runtime/src/render/materials/program.hlsli")),
    ("program_lit.hlsli", include_str!("../../../../runtime/src/render/materials/program_lit.hlsli")),
];

/// How a form names a vertex attribute (nebula_model.h NEBULA_INPUT_*).
pub fn input_code(input: &Input) -> u8 {
    match input {
        Input::Normal => 0,
        Input::Color0 => 1,
        Input::Color1 => 2,
        Input::Tangent => 3,
        Input::Binormal => 4,
        Input::Gain => 7,
        Input::Coord(k) => 16 + *k,
    }
}

/// What the game's own animations write, once the store's cook has walked them. Unset (a mod's conversion): anything.
static WRITTEN: std::sync::Mutex<Option<Written>> = std::sync::Mutex::new(None);

pub fn written_or_unknown() -> Written {
    WRITTEN.lock().unwrap_or_else(|e| e.into_inner()).clone().unwrap_or_else(Written::unknown)
}

/// Every animation that can write a register, for the store's whole cook.
pub fn prepare_written_files(files: &[super::PakFile], read: &mut dyn FnMut(&super::PakFile) -> Result<Vec<u8>>) -> Result<()> {
    let mut written = Written::default();
    walk_partition_files(files, read, &mut |name, bytes| {
        if name.to_ascii_lowercase().ends_with(".brk") {
            written.add(&clip::read(&bytes).with_context(|| name.to_string())?);
        }
        Ok(())
    })?;
    *WRITTEN.lock().unwrap_or_else(|e| e.into_inner()) = Some(written);
    Ok(())
}

/// A mod's conversion: the texts its models name, by file name (`v<hash>.hlsl`, `p<hash>.hlsl`), which the host builds when
/// the mod didn't bring them; and pixel programs the mod wrote, by material name.
pub static MOD_TEXTS: std::sync::Mutex<Option<std::collections::BTreeMap<String, String>>> = std::sync::Mutex::new(None);
pub static MOD_WRITTEN: std::sync::Mutex<Option<std::collections::BTreeMap<String, String>>> = std::sync::Mutex::new(None);

/// A form's texts as the file will name them: a mod's own pixel program in place of the plain form's, each text noted for a
/// mod's conversion.
pub fn finish_text(material: &str, form: Form, t: &mut Text) {
    if form == Form::default() {
        if let Some(words) = MOD_WRITTEN.lock().unwrap_or_else(|e| e.into_inner()).as_ref().and_then(|w| w.get(material)) {
            t.pixel = words.clone();
        }
    }
    if let Some(texts) = MOD_TEXTS.lock().unwrap_or_else(|e| e.into_inner()).as_mut() {
        texts.insert(format!("v{:016x}.hlsl", text_hash(&t.vertex)), t.vertex.clone());
        texts.insert(format!("p{:016x}.hlsl", text_hash(&t.pixel)), t.pixel.clone());
    }
}

/// A layout material's program (material_layout::programs) as a material. Registers can hold anything a layout animation
/// writes, so they stay unknown to the range analysis.
pub fn material_from_program(p: &super::material::LayoutProgram) -> MaterialIr {
    let mut m = MaterialIr { name: String::new(), has_stage_colors: true, ..MaterialIr::default() };
    m.pixel_stage_count = p.stages.len() as u32;
    m.channel_count = u32::from(p.channel_count);
    for s in &p.stages {
        let mut warp = [0u8; 9];
        warp.copy_from_slice(&s[25..34]);
        m.stages.push(StageIr {
            coord: s[0],
            map: s[1],
            channel: s[2],
            color_in: [s[3], s[4], s[5], s[6]],
            color_op: s[7],
            color_bias: s[8],
            color_scale: s[9],
            color_clamp: s[10],
            color_out: s[11],
            alpha_in: [s[12], s[13], s[14], s[15]],
            alpha_op: s[16],
            alpha_bias: s[17],
            alpha_scale: s[18],
            alpha_clamp: s[19],
            alpha_out: s[20],
            raster_swap: s[21],
            texture_swap: s[22],
            color_constant: s[23],
            alpha_constant: s[24],
            warp,
        });
    }
    for g in &p.generators {
        // Normalized coding: mode, projective, position or normal source; row; the matrix place.
        let row = g[1] & 15;
        let projective = g[0] & 4 != 0;
        let source = match row {
            13 => 0,
            14 => 1,
            5..=12 => row - 1,
            2 => 19,
            n => n,
        };
        m.coords.push(CoordIr { kind: u8::from(!projective), source: if source >= 19 { source + 13 } else { source }, matrix: if g[2] == 60 { NONE } else { u32::from(g[2].saturating_sub(30) / 3) } });
    }
    // The four channels are enabled by nothing here; the first two read the material sources the layout names.
    for c in 0..4 {
        m.channels[c] = ChannelIr {
            enable: 0,
            material_source: if c < 2 { p.channel_sources[c] } else { 0 },
            ambient_source: 0,
            light_mask: 0,
            diffuse: 0,
            attenuation: 1,
        };
    }
    m.swaps = p.swaps;
    m.warp_orders = p.orders.clone();
    (m.alpha_comp0, m.alpha_op, m.alpha_comp1) = (p.alpha[0], p.alpha[1], p.alpha[2]);
    m
}

/// Registers a layout's materials and animations can write: unknown.
pub fn layout_registers() -> Registers {
    Registers { color: [Range::UNKNOWN; 3], alpha: [Range::UNKNOWN; 3] }
}

/// The first 64 bits of a text's SHA-1: how the store names a program.
pub fn text_hash(text: &str) -> u64 {
    use sha1::{Digest, Sha1};
    let mut hash = Sha1::new();
    hash.update(text.replace('\r', "").as_bytes());
    let d = hash.finalize();
    u64::from_le_bytes(d[..8].try_into().unwrap())
}

/// The game's player models and the replacing passes it can draw over each of their materials.
const REPLACED_MODELS: [&str; 29] = [
    "Mario", "Luigi", "MarioHandL", "MarioHandR", "MarioFace", "LuigiFace", "MarioShadow", "LuigiShadow", "IceMario", "IceLuigi",
    "IceMarioHandL", "IceMarioHandR", "InvincibleMarioHandL", "InvincibleMarioHandR", "InvincibleMario", "InvincibleLuigi",
    "BeeMario", "BeeLuigi", "HopperMario", "HopperLuigi", "BoneMario", "BoneLuigi", "SearchLightCone", "FlexibleSphere",
    "FlexibleSandPlanetParts", "ScaleDownRelayPlanet", "GhostMario", "GhostLuigi", "Koura",
];

/// The models the flexible sphere's shape callback draws.
const FLEXIBLE_MODELS: [&str; 3] = ["FlexibleSphere", "FlexibleSandPlanetParts", "ScaleDownRelayPlanet"];

/// Every form a material of a model cooks: its own, lit if it has lights or reads a normal, and the replacing passes of a
/// player model. `name` is the model's file name without its extension.
pub fn forms_of(name: &str, m: &MaterialIr, index: usize) -> Result<Vec<Form>> {
    let lit = has_lit_form(m)?;
    let with_lit = |out: &mut Vec<Form>, f: Form, needs: bool| {
        out.push(f);
        if needs {
            out.push(Form { lit: true, ..f });
        }
    };
    let mut out = Vec::new();
    with_lit(&mut out, Form::default(), lit);
    if FLEXIBLE_MODELS.iter().any(|n| n.eq_ignore_ascii_case(name)) {
        with_lit(&mut out, Form { pass: PASS_FLEXIBLE_COORDINATES, ..Form::default() }, has_lit_form(&with_pass(m, PASS_FLEXIBLE_COORDINATES))?);
    }
    if REPLACED_MODELS.iter().any(|n| n.eq_ignore_ascii_case(name)) {
        // The alpha lists can stand over a replacing pass (PlayerModel flags 2 and 11), the light list over anything drawn plainly.
        for pass in [0, PASS_PLAYER_LIGHT, PASS_REFLECTION_ALPHA, PASS_PLAYER_OPACITY] {
            // Opacity reads texture 0: a material with none (the shadow models) never draws under it.
            if pass == PASS_PLAYER_OPACITY && (m.coords.is_empty() || m.textures.is_empty()) {
                continue;
            }
            let lit = has_lit_form(&with_pass(m, pass))?;
            if pass != 0 {
                with_lit(&mut out, Form { pass, ..Form::default() }, lit);
            }
            with_lit(&mut out, Form { replace: 1, pass, ..Form::default() }, false);
            with_lit(&mut out, Form { replace: 5, pass, ..Form::default() }, false);
            with_lit(&mut out, Form { normal_coord: true, pass, ..Form::default() }, lit);
            if pass == PASS_PLAYER_LIGHT {
                with_lit(&mut out, Form { pass, fog_linear: true, ..Form::default() }, lit);
                with_lit(&mut out, Form { normal_coord: true, pass, fog_linear: true, ..Form::default() }, lit);
            }
        }
        if name.eq_ignore_ascii_case("SearchLightCone") && index == 0 {
            for replace in 2..=4 {
                with_lit(&mut out, Form { replace, ..Form::default() }, light_terms_any(m));
            }
        }
    }
    Ok(out)
}

/// What a track can hold: an empty one writes 0, one entry its value.
fn track_range(c: &ClipIr, t: &Track) -> Range {
    let (at, n) = (t.at as usize, t.count as usize);
    let value = |i: usize| c.values[at + i];
    if n < 2 {
        return Range::one(if n == 0 { 0.0 } else { value(0) });
    }
    let stride = t.stride as usize;
    if stride < 3 {
        return (0..n).map(|i| Range::one(value(i))).reduce(Range::hull).unwrap_or(Range::one(0.0));
    }
    let mut r = Range::one(value(1));
    for k in 0..n {
        r = r.hull(Range::one(value(k * stride + 1)));
        if k + 1 < n {
            // Hermite between two keys: fine samples, and a margin of one step for what falls between them.
            let (t0, v0, m0) = (value(k * stride), value(k * stride + 1), value(k * stride + stride - 1));
            let (t1, v1, m1) = (value((k + 1) * stride), value((k + 1) * stride + 1), value((k + 1) * stride + 2));
            let d = t1 - t0;
            for i in 1..32 {
                let x = i as f32 / 32.0;
                let (x2, x3) = (x * x, x * x * x);
                let v = v0 * (2.0 * x3 - 3.0 * x2 + 1.0) + m0 * d * (x3 - 2.0 * x2 + x) + v1 * (-2.0 * x3 + 3.0 * x2) + m1 * d * (x3 - x2);
                r = r.hull(Range::one(v));
            }
        }
    }
    Range(r.0 - 1.0, r.1 + 1.0)
}

/// What register animations can write, by material name: every animation of the game, because an actor can play another
/// archive's. Colour and alpha of registers 0 to 2.
#[derive(Default, Clone)]
pub struct Written(HashMap<Vec<u8>, [[Option<Range>; 2]; 3]>, bool);

impl Written {
    /// For materials cooked apart from the game's whole set (a mod's): any animation may write any register.
    pub fn unknown() -> Written {
        Written(HashMap::new(), true)
    }
    pub fn add(&mut self, c: &ClipIr) {
        if c.kind != KIND_REGISTERS {
            return;
        }
        for e in 0..c.first_count as usize {
            let target = &c.targets[e];
            let slot = target.slot as usize;
            if slot >= 3 {
                continue;
            }
            let row = self.0.entry(target.name.clone()).or_default();
            for k in 0..4 {
                let r = track_range(c, &c.tracks[e * 4 + k]);
                let r = Range(r.0 / 255.0, r.1 / 255.0);
                let at = &mut row[slot][usize::from(k == 3)];
                *at = Some(at.map_or(r, |o| o.hull(r)));
            }
        }
    }
    pub fn registers(&self, m: &MaterialIr) -> Registers {
        let mut out = Registers::of(m);
        if self.1 {
            return Registers { color: [Range::UNKNOWN; 3], alpha: [Range::UNKNOWN; 3] };
        }
        if let Some(row) = self.0.get(m.name.as_bytes()) {
            for r in 0..3 {
                if let Some(v) = row[r][0] {
                    out.color[r] = out.color[r].hull(v);
                }
                if let Some(v) = row[r][1] {
                    out.alpha[r] = out.alpha[r].hull(v);
                }
            }
        }
        out
    }
}

fn walk_partition_files(files: &[super::PakFile], read: &mut dyn FnMut(&super::PakFile) -> Result<Vec<u8>>,
    visit: &mut dyn FnMut(&str, Vec<u8>) -> Result<()>) -> Result<()> {
    fn walk(name: &str, bytes: Vec<u8>, visit: &mut dyn FnMut(&str, Vec<u8>) -> Result<()>) -> Result<()> {
        let mut bytes = bytes;
        if archive::is_compressed(&bytes) {
            bytes = archive::expand(&bytes)?;
        }
        if archive::is_archive(&bytes) {
            for member in archive::members(&bytes)? {
                let file = member.path.rsplit('/').next().unwrap_or(&member.path);
                walk(&format!("{name}/{file}"), member.bytes, visit)?;
            }
            return Ok(());
        }
        visit(name, bytes)
    }
    let mut order: Vec<_> = files.iter().collect();
    order.sort_by_key(|f| f.offset);
    for file in order {
        let lower = file.path.to_ascii_lowercase();
        if !(lower.ends_with(".arc") || lower.ends_with(".szs") || lower.ends_with(".bdl") || lower.ends_with(".bmd") || lower.ends_with(".brk")) {
            continue;
        }
        walk(&format!("/{}", file.path), read(file)?, visit)?;
    }
    Ok(())
}

pub(super) fn walk_files(input: &Path, visit: &mut dyn FnMut(&str, Vec<u8>) -> Result<()>) -> Result<()> {
    let source = input.join("game.pak");
    let mut pak = File::open(&source).with_context(|| format!("{} cannot be opened", source.display()))?;
    let system = pak_system_area(&mut pak)?;
    walk_partition_files(&pak_files(system.fst())?, &mut |file| {
        let mut bytes = vec![0u8; file.length as usize];
        pak.seek(SeekFrom::Start(file.offset))?;
        pak.read_exact(&mut bytes)?;
        Ok(bytes)
    }, visit)
}

/// Whether a mesh names its matrix per vertex.
pub fn mesh_skinned(ir: &ModelIr, m: &MaterialIr) -> bool {
    ir.shapes.get(m.mesh as usize).is_some_and(|s| s.groups.iter().any(|g| g.attributes & ATTR_MATRIX != 0))
}

/// `nebula-import programs CONTENT OUT`: every text of the game in OUT/v<hash>.hlsl and p<hash>.hlsl, and OUT/forms.tsv naming
/// each model's material forms. tools/dev/build_programs.py compiles them.
pub fn write_set(input: &Path, output: &Path) -> Result<()> {
    std::fs::create_dir_all(output)?;
    let mut written = Written::default();
    walk_files(input, &mut |name, bytes| {
        if name.to_ascii_lowercase().ends_with(".brk") {
            written.add(&clip::read(&bytes).with_context(|| name.to_string())?);
        }
        Ok(())
    })?;
    let mut table = BufWriter::new(File::create(output.join("forms.tsv"))?);
    let mut seen = HashSet::new();
    let mut stats = Stats::default();
    let emit = |text: &str, prefix: char, seen: &mut HashSet<(char, u64)>| -> Result<u64> {
        let hash = text_hash(text);
        if seen.insert((prefix, hash)) {
            std::fs::write(output.join(format!("{prefix}{hash:016x}.hlsl")), text)?;
        }
        Ok(hash)
    };
    walk_files(input, &mut |name, bytes| {
        if bytes.starts_with(b"RLYT") && bytes.get(4..6) != Some(&[255, 254][..]) {
            for (i, program) in super::material::layout::file_programs(&bytes).with_context(|| name.to_string())?.iter().enumerate() {
                let m = material_from_program(program);
                let t = generate(&m, Form::default(), false, &layout_registers()).with_context(|| format!("{name} layout program {i}"))?;
                let (v, p) = (emit(&t.vertex, 'v', &mut seen)?, emit(&t.pixel, 'p', &mut seen)?);
                writeln!(table, "{name}\t{i}\t0\t0\t0\t{v:016x}\t{p:016x}\t{}\t{}\t0\t0", t.vertex_rows.len(), t.pixel_rows.len())?;
            }
            return Ok(());
        }
        let source = Source::of(&bytes);
        if source == Source::Table {
            return Ok(());
        }
        let ir = read_file(&bytes, source, &mut stats).with_context(|| name.to_string())?;
        let stem = name.rsplit('/').next().unwrap_or(name);
        let stem = stem.split('.').next().unwrap_or(stem);
        for (i, m) in ir.materials.iter().enumerate() {
            let registers = written.registers(m);
            for form in forms_of(stem, m, i)? {
                let t = generate(m, form, mesh_skinned(&ir, m), &registers).with_context(|| format!("{name} material {} form {form:?}", m.name))?;
                let (v, p) = (emit(&t.vertex, 'v', &mut seen)?, emit(&t.pixel, 'p', &mut seen)?);
                writeln!(table, "{name}\t{i}\t{}\t{}\t{}\t{v:016x}\t{p:016x}\t{}\t{}\t{}\t{}", form.replace, u8::from(form.normal_coord), u8::from(form.lit), t.textures.len(), u8::from(t.alpha_test), form.pass, u8::from(form.fog_linear))?;
            }
        }
        Ok(())
    })?;
    table.flush()?;
    println!("{} texts", seen.len());
    Ok(())
}
