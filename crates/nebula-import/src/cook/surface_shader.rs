//! A mod's hand-written material shader (plan 3.4 layer 1; contract materials 3.4): `layers/<l>/assets/<name>.hlsl`
//! with `<name>.shader.json` beside it, picked by a glTF material's `extras.nebula.shader`. The HLSL defines
//! `void surface(in SurfaceIn i, inout Surface s)`; the generator (programs.rs) calls it where a glTF material's stages
//! would make its base colour and lights the record it fills with the material template (program_pbr.hlsli). This file
//! reads and checks the source and lays the parameters on the material's colour places; it writes no program text.
//!
//! The interface, the only one a shader has:
//! - parameters: `float` or `float4` globals of the shader's names, read from the material's places (registers 1 to 3
//!   hold -4 to 4, constant colours 0 to 3 hold 0 to 1, in 255ths); `content.model.set_param` writes them.
//! - textures: a function per name, `float4 <name>(float2 uv)` (and `<name>_level(uv, lod)`), sampling the glTF
//!   texture the material names for it.
//! - SurfaceIn: uv0, uv1 (the mesh's TEXCOORD_0 and _1, 0 where it has none), color (COLOR_0), and what `reads` lists:
//!   normal (eye space, unit), world_position (game units), view (eye space, unit, toward the eye), time (seconds of
//!   the frame's particle clock, view_scale.y).
//! - Surface: base (display-encoded colour, 0 to 1), alpha, normal (eye space), roughness, metal, emission
//!   (display-encoded, added after the light; above 1 feeds bloom).
//!
//! - ray (layer 3): shader.json `ray` {class, base, roughness, metal, emission, through}, each optional: what a ray
//!   reads of the material in place of its factors (the hit record). The ray-hit form (programs::ray_hit_form) runs the
//!   same `surface` at a ray's hit: SurfaceIn from the hit (normal and view in world space there, eye space in the
//!   raster: only their relations are the shader's), a texture sampled at the ray cone's level, `ddx`/`ddy`/`fwidth`
//!   0, and `discard`/`clip` mark the hit as not there (alpha 0) instead of ending it.
//!
//! Refused, each by name with the file (and the line where there is one), L804 `Cook.Shader.<Kind>`: no source, a
//! JSON outside the interface, HLSL that binds anything of its own (`register`, a texture, sampler or buffer type,
//! `cbuffer`, `#include`, `#pragma`, `groupshared`, a Vulkan attribute) or has no `surface`, a parameter or texture the
//! material gives that the shader lacks, one it lacks that the shader has, more parameters than places, a default or
//! value outside its place's range.

use std::path::Path;

use anyhow::Result;
use serde_json::Value;

use super::refuse::refuse;

/// The largest shader source, in bytes.
pub const MOST_BYTES: usize = 64 << 10;
/// Texture slots a shader may declare.
pub const TEXTURE_SLOTS: u64 = 8;
/// The material's places a parameter can take: registers 1 to 3 (rows 0 to 2), constant colours 0 to 3 (3 to 6).
pub const PLACES: usize = 7;
/// What SurfaceIn carries beyond uv0, uv1 and color (shader.json `reads`).
pub const READ_NORMAL: u8 = 1;
pub const READ_WORLD: u8 = 2;
pub const READ_VIEW: u8 = 4;
pub const READ_TIME: u8 = 8;
const READS: [(&str, u8); 4] = [("normal", READ_NORMAL), ("world_position", READ_WORLD), ("view", READ_VIEW), ("time", READ_TIME)];

/// Words a shader may not write: what would bind something of its own or reach past the interface.
const FORBIDDEN: [&str; 14] = [
    "register",
    "cbuffer",
    "tbuffer",
    "ConstantBuffer",
    "SamplerState",
    "SamplerComparisonState",
    "sampler",
    "sampler1D",
    "sampler2D",
    "sampler3D",
    "samplerCUBE",
    "groupshared",
    "globallycoherent",
    "AppendStructuredBuffer",
];
/// Names the generated program uses around the shader's code: a parameter or texture may not take them.
const RESERVED: [&str; 42] = [
    "m", "i", "o", "s", "si", "p", "h", "prev", "tex", "samp", "surface", "Surface", "SurfaceIn", "Varyings", "VertexIn", "Place",
    "NativeOut", "program_ps", "program_vs", "if", "else", "for", "while", "do", "return", "true", "false", "struct",
    "static", "const", "in", "out", "inout", "void", "bool", "int", "uint", "float", "half", "double", "discard", "switch",
];
const RESERVED_PREFIXES: [&str; 15] =
    ["g_", "pbr_", "enh_", "traced_", "native_", "program_", "lit", "nm_", "NEBULA_", "NATIVE_", "float", "int", "rt_", "Rt", "RT_"];
/// The classes a shader's `ray` may name: what its rows alone make (ramp needs a ramp map, water the pack's row).
pub const RAY_CLASSES: [&str; 8] = ["dielectric", "gloss", "metal", "glass", "cutout", "translucent", "emissive", "painted"];

/// A shader as its files say, checked.
#[derive(Clone, Debug, PartialEq)]
pub struct Source {
    /// The assets path without its ending (`shaders/Stripes`), as the material names it.
    pub name: String,
    /// The HLSL file as the mod has it (`assets/shaders/Stripes.hlsl`), for the compiler's lines.
    pub file: String,
    pub code: String,
    pub params: Vec<Param>,
    /// Declared textures: (slot 0 to 7, name).
    pub textures: Vec<(u8, String)>,
    pub reads: u8,
    /// shader.json `ray`: the rows a ray reads in place of the material's factors.
    pub ray: super::model_ir::RayRows,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Param {
    pub name: String,
    pub four: bool,
    pub default: [f32; 4],
}

/// One material's use of a shader: each parameter's value and place, each texture's glTF texture index.
#[derive(Clone, Debug, PartialEq)]
pub struct Use {
    pub values: Vec<[f32; 4]>,
    pub textures: Vec<u64>,
}

/// Whether `name` (`shaders/Stripes`) is a path a material may name: parts of letters, digits, `_` and `-`.
fn name_ok(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= 160
        && name.split('/').all(|part| !part.is_empty() && part.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-'))
}

/// The shader's files, from the mod the glTF file is in (`folder` and up to the folder holding `luma.json`): the first
/// layer, in name order, whose assets hold `<name>.shader.json`. Refused when the material names no such shader.
pub fn find(folder: Option<&Path>, material: &str, name: &str) -> Result<(String, Vec<u8>, Vec<u8>)> {
    if !name_ok(name) {
        return Err(refuse("Cook.Shader.Name", &[("material", material.to_string()), ("name", name.to_string())]));
    }
    let wanted = format!("assets/{name}.shader.json");
    let missing = || refuse("Cook.Shader.Missing", &[("material", material.to_string()), ("name", name.to_string()), ("file", wanted.clone())]);
    let root = folder.and_then(|f| f.ancestors().find(|a| a.join("luma.json").is_file())).ok_or_else(missing)?;
    let mut layers: Vec<_> = std::fs::read_dir(root.join("layers")).map_err(|_| missing())?.flatten().map(|e| e.path()).collect();
    layers.sort();
    for layer in layers {
        let json = layer.join("assets").join(format!("{name}.shader.json"));
        if !json.is_file() {
            continue;
        }
        let hlsl_file = format!("assets/{name}.hlsl");
        let hlsl = std::fs::read(layer.join("assets").join(format!("{name}.hlsl")))
            .map_err(|_| refuse("Cook.Shader.Missing", &[("material", material.to_string()), ("name", name.to_string()), ("file", hlsl_file.clone())]))?;
        let json = std::fs::read(&json).map_err(|_| missing())?;
        return Ok((hlsl_file, json, hlsl));
    }
    Err(missing())
}

fn identifier_ok(name: &str) -> bool {
    let mut bytes = name.bytes();
    bytes.next().is_some_and(|b| b.is_ascii_alphabetic() || b == b'_')
        && bytes.all(|b| b.is_ascii_alphanumeric() || b == b'_')
        && name.len() <= 32
        && !RESERVED.contains(&name)
        && !RESERVED_PREFIXES.iter().any(|p| name.starts_with(p))
}

fn numbers(v: &Value, four: bool) -> Option<[f32; 4]> {
    if four {
        let a = v.as_array().filter(|a| a.len() == 4)?;
        let mut out = [0.0f32; 4];
        for (k, x) in a.iter().enumerate() {
            out[k] = x.as_f64().filter(|x| x.is_finite())? as f32;
        }
        Some(out)
    } else {
        let x = v.as_f64().filter(|x| x.is_finite())? as f32;
        Some([x, 0.0, 0.0, 0.0])
    }
}

/// `<name>.shader.json` and the HLSL, checked against the interface.
pub fn read(name: &str, file: &str, json: &[u8], hlsl: &[u8]) -> Result<Source> {
    let json_file = format!("{}.shader.json", file.strip_suffix(".hlsl").unwrap_or(file));
    let bad = |key: &str| refuse("Cook.Shader.Json", &[("file", json_file.clone()), ("key", key.to_string())]);
    let root: Value = serde_json::from_slice(json).map_err(|_| bad("/"))?;
    let object = root.as_object().ok_or_else(|| bad("/"))?;
    if let Some(key) = object.keys().find(|k| !matches!(k.as_str(), "stage" | "params" | "textures" | "reads" | "ray")) {
        return Err(bad(&format!("/{key}")));
    }
    if root["stage"].as_str() != Some("material") {
        return Err(bad("/stage"));
    }
    let mut params: Vec<Param> = Vec::new();
    for (k, p) in root["params"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
        let at = format!("/params/{k}");
        if p.as_object().is_some_and(|o| o.keys().any(|key| !matches!(key.as_str(), "name" | "type" | "default"))) {
            return Err(bad(&at));
        }
        let pname = p["name"].as_str().filter(|n| identifier_ok(n)).ok_or_else(|| bad(&format!("{at}/name")))?;
        let four = match p["type"].as_str() {
            Some("float") => false,
            Some("float4") => true,
            _ => return Err(bad(&format!("{at}/type"))),
        };
        let default = numbers(&p["default"], four).ok_or_else(|| bad(&format!("{at}/default")))?;
        if params.iter().any(|q| q.name == pname) {
            return Err(refuse("Cook.Shader.Twice", &[("file", json_file.clone()), ("name", pname.to_string())]));
        }
        params.push(Param { name: pname.to_string(), four, default });
    }
    if !root["params"].is_null() && !root["params"].is_array() {
        return Err(bad("/params"));
    }
    let mut textures: Vec<(u8, String)> = Vec::new();
    for (k, t) in root["textures"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
        let at = format!("/textures/{k}");
        if t.as_object().is_some_and(|o| o.keys().any(|key| !matches!(key.as_str(), "slot" | "name"))) {
            return Err(bad(&at));
        }
        let slot = t["slot"].as_u64().filter(|s| *s < TEXTURE_SLOTS).ok_or_else(|| bad(&format!("{at}/slot")))? as u8;
        let tname = t["name"].as_str().filter(|n| identifier_ok(n)).ok_or_else(|| bad(&format!("{at}/name")))?;
        if textures.iter().any(|(s, n)| *s == slot || n == tname) || params.iter().any(|p| p.name == tname) {
            return Err(refuse("Cook.Shader.Twice", &[("file", json_file.clone()), ("name", tname.to_string())]));
        }
        textures.push((slot, tname.to_string()));
    }
    if !root["textures"].is_null() && !root["textures"].is_array() {
        return Err(bad("/textures"));
    }
    let mut reads = 0u8;
    for (k, r) in root["reads"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
        let bit = r.as_str().and_then(|r| READS.iter().find(|(n, _)| *n == r)).map(|(_, b)| *b).ok_or_else(|| bad(&format!("/reads/{k}")))?;
        reads |= bit;
    }
    if !root["reads"].is_null() && !root["reads"].is_array() {
        return Err(bad("/reads"));
    }
    let ray = ray_rows(&root["ray"]).map_err(|key| bad(&format!("/ray{key}")))?;
    if hlsl.len() > MOST_BYTES {
        return Err(refuse("Cook.Shader.TooLarge", &[("file", file.to_string()), ("value", hlsl.len().to_string()), ("max", MOST_BYTES.to_string())]));
    }
    let code = std::str::from_utf8(hlsl).map_err(|_| refuse("Cook.Shader.Text", &[("file", file.to_string())]))?.replace("\r\n", "\n");
    check_code(file, &code)?;
    Ok(Source { name: name.to_string(), file: file.to_string(), code, params, textures, reads, ray })
}

/// shader.json `ray`, or the key (under `/ray`) that is outside it: a class of RAY_CLASSES, colours of three numbers 0
/// to 1 (emission 0 or more), roughness, metal and through 0 to 1.
fn ray_rows(v: &Value) -> std::result::Result<super::model_ir::RayRows, String> {
    let mut rows = super::model_ir::RayRows::default();
    if v.is_null() {
        return Ok(rows);
    }
    let object = v.as_object().ok_or_else(String::new)?;
    if let Some(key) = object.keys().find(|k| !matches!(k.as_str(), "class" | "base" | "roughness" | "metal" | "emission" | "through")) {
        return Err(format!("/{key}"));
    }
    let share = |key: &str| -> std::result::Result<Option<f32>, String> {
        match &v[key] {
            Value::Null => Ok(None),
            x => x.as_f64().filter(|x| (0.0..=1.0).contains(x)).map(|x| Some(x as f32)).ok_or_else(|| format!("/{key}")),
        }
    };
    let colour = |key: &str, top: f64| -> std::result::Result<Option<[f32; 3]>, String> {
        match &v[key] {
            Value::Null => Ok(None),
            x => {
                let a = x.as_array().filter(|a| a.len() == 3).ok_or_else(|| format!("/{key}"))?;
                let mut out = [0.0f32; 3];
                for (k, c) in a.iter().enumerate() {
                    out[k] = c.as_f64().filter(|c| c.is_finite() && *c >= 0.0 && *c <= top).ok_or_else(|| format!("/{key}/{k}"))? as f32;
                }
                Ok(Some(out))
            }
        }
    };
    rows.class = match &v["class"] {
        Value::Null => None,
        c => Some(c.as_str().filter(|c| RAY_CLASSES.contains(c)).ok_or_else(|| "/class".to_string())?.to_string()),
    };
    rows.base = colour("base", 1.0)?;
    rows.emission = colour("emission", f64::from(f32::MAX))?;
    rows.roughness = share("roughness")?;
    rows.metal = share("metal")?;
    rows.through = share("through")?;
    Ok(rows)
}

/// The HLSL's words outside comments and strings, each with its line: what it may not write, and its `surface`.
fn check_code(file: &str, code: &str) -> Result<()> {
    let forbidden = |line: usize, word: &str| refuse("Cook.Shader.Forbidden", &[("file", file.to_string()), ("line", line.to_string()), ("word", word.to_string())]);
    let bytes = code.as_bytes();
    let (mut at, mut line, mut line_start) = (0usize, 1usize, true);
    let mut has_surface = false;
    while at < bytes.len() {
        let b = bytes[at];
        if b == b'\n' {
            line += 1;
            line_start = true;
            at += 1;
            continue;
        }
        if b == b'/' && bytes.get(at + 1) == Some(&b'/') {
            while at < bytes.len() && bytes[at] != b'\n' {
                at += 1;
            }
            continue;
        }
        if b == b'/' && bytes.get(at + 1) == Some(&b'*') {
            at += 2;
            while at < bytes.len() && !(bytes[at] == b'*' && bytes.get(at + 1) == Some(&b'/')) {
                if bytes[at] == b'\n' {
                    line += 1;
                }
                at += 1;
            }
            at += 2;
            continue;
        }
        if b == b'"' {
            at += 1;
            while at < bytes.len() && bytes[at] != b'"' && bytes[at] != b'\n' {
                at += 1;
            }
            at += 1;
            continue;
        }
        if b == b'#' && line_start {
            let rest = code[at + 1..].trim_start_matches([' ', '\t']);
            let word: String = rest.chars().take_while(|c| c.is_ascii_alphabetic()).collect();
            if matches!(word.as_str(), "include" | "pragma" | "line") {
                return Err(forbidden(line, &format!("#{word}")));
            }
        }
        if b == b'[' && bytes.get(at + 1) == Some(&b'[') {
            return Err(forbidden(line, "[["));
        }
        if b.is_ascii_alphabetic() || b == b'_' {
            let start = at;
            while at < bytes.len() && (bytes[at].is_ascii_alphanumeric() || bytes[at] == b'_') {
                at += 1;
            }
            let word = &code[start..at];
            let typed = ["Texture", "RWTexture", "FeedbackTexture", "RasterizerOrdered", "RaytracingAccelerationStructure"].iter().any(|p| word.starts_with(p))
                || word.ends_with("Buffer");
            if FORBIDDEN.contains(&word) || typed {
                return Err(forbidden(line, word));
            }
            if word == "surface" && code[at..].trim_start().starts_with('(') {
                has_surface = true;
            }
            line_start = false;
            continue;
        }
        if !b.is_ascii_whitespace() {
            line_start = false;
        }
        at += 1;
    }
    if !has_surface {
        return Err(refuse("Cook.Shader.NoSurface", &[("file", file.to_string())]));
    }
    Ok(())
}

/// A material's `extras.nebula` against its shader: each parameter's value (the material's, else the default), each
/// declared texture's glTF texture index.
pub fn use_of(source: &Source, material: &str, extras: &Value) -> Result<Use> {
    let params = &extras["params"];
    if !params.is_null() && !params.is_object() {
        return Err(refuse("Cook.Shader.ParamType", &[("material", material.to_string()), ("param", "params".into()), ("expected", "object".into())]));
    }
    if let Some(name) = params.as_object().and_then(|o| o.keys().find(|k| !source.params.iter().any(|p| &p.name == *k))) {
        return Err(refuse("Cook.Shader.UnknownParam", &[("material", material.to_string()), ("param", name.clone()), ("shader", source.name.clone())]));
    }
    let mut values = Vec::new();
    for p in &source.params {
        let value = match params.get(&p.name) {
            Some(v) => numbers(v, p.four).ok_or_else(|| {
                refuse("Cook.Shader.ParamType", &[("material", material.to_string()), ("param", p.name.clone()), ("expected", (if p.four { "float4" } else { "float" }).into())])
            })?,
            None => p.default,
        };
        values.push(value);
    }
    let given = &extras["textures"];
    if !given.is_null() && !given.is_object() {
        return Err(refuse("Cook.Shader.NoTexture", &[("material", material.to_string()), ("texture", "textures".into()), ("shader", source.name.clone())]));
    }
    if let Some(name) = given.as_object().and_then(|o| o.keys().find(|k| !source.textures.iter().any(|(_, n)| n == *k))) {
        return Err(refuse("Cook.Shader.UnknownTexture", &[("material", material.to_string()), ("texture", name.clone()), ("shader", source.name.clone())]));
    }
    let mut textures = Vec::new();
    for (_, name) in &source.textures {
        let index = given[name.as_str()]
            .as_u64()
            .ok_or_else(|| refuse("Cook.Shader.NoTexture", &[("material", material.to_string()), ("texture", name.clone()), ("shader", source.name.clone())]))?;
        textures.push(index);
    }
    Ok(Use { values, textures })
}

/// The range a place holds: registers -4 to 4, constant colours 0 to 1.
pub fn place_range(place: u8) -> (f32, f32) {
    if place < 3 {
        (-4.0, 4.0)
    } else {
        (0.0, 1.0)
    }
}

/// Each parameter's place, in params order, skipping places in `used` (the material's own colours): refused when
/// there are more parameters than free places, or a value lies outside its place's range.
pub fn places(source: &Source, material: &str, values: &[[f32; 4]], used: [bool; PLACES]) -> Result<Vec<u8>> {
    let free: Vec<u8> = (0..PLACES as u8).filter(|p| !used[*p as usize]).collect();
    if source.params.len() > free.len() {
        return Err(refuse(
            "Cook.Shader.TooManyParams",
            &[("material", material.to_string()), ("count", source.params.len().to_string()), ("max", free.len().to_string())],
        ));
    }
    let mut out = Vec::new();
    for ((p, value), &place) in source.params.iter().zip(values).zip(&free) {
        let (low, high) = place_range(place);
        let width = if p.four { 4 } else { 1 };
        if value[..width].iter().any(|v| *v < low || *v > high) {
            return Err(refuse(
                "Cook.Shader.ParamRange",
                &[("material", material.to_string()), ("param", p.name.clone()), ("min", format!("{low}")), ("max", format!("{high}"))],
            ));
        }
        out.push(place);
    }
    Ok(out)
}

/// LumaLink's check (contract materials 3.4: a shader's compile errors are L804 with its file and line): None, a cook
/// that does not compile (the game's own, which builds the programs itself); Some(None), a check with no compiler
/// found (a note says the shaders were not compiled); Some(Some(dxc)), a check that compiles each surface material's
/// plain form with it.
pub static CHECK_COMPILER: std::sync::Mutex<Option<Option<std::path::PathBuf>>> = std::sync::Mutex::new(None);

/// The compiler a check uses: `NEBULA_DXC`, else `dxc/dxc.exe` (`dxc/dxc` elsewhere) beside each of `beside`, the
/// first that is a file.
pub fn find_compiler(beside: &[&Path]) -> Option<std::path::PathBuf> {
    if let Some(path) = std::env::var_os("NEBULA_DXC").map(std::path::PathBuf::from).filter(|p| p.is_file()) {
        return Some(path);
    }
    let name = if cfg!(windows) { "dxc.exe" } else { "dxc" };
    beside.iter().map(|b| b.join("dxc").join(name)).find(|p| p.is_file())
}

/// Each surface material's plain form compiled as LumaLink's check asks (CHECK_COMPILER): the first error is refused
/// with the shader's file and line (Cook.Shader.Compile), or the material when the error is in the generated part.
pub fn check_compile(ir: &super::model_ir::ModelIr) -> Result<()> {
    let wanted = CHECK_COMPILER.lock().unwrap_or_else(|e| e.into_inner()).clone();
    let Some(compiler) = wanted else { return Ok(()) };
    let mut seen = std::collections::BTreeSet::new();
    for m in &ir.materials {
        let Some(sf) = &m.surface else { continue };
        if !seen.insert(m.name.clone()) {
            continue;
        }
        let Some(dxc) = &compiler else {
            super::refuse::note("Cook.Shader.NotCompiled", &[("material", m.name.clone()), ("file", sf.file.clone())]);
            continue;
        };
        let form = super::programs::Form { lit: true, ..super::programs::Form::default() };
        let text = super::programs::generate(m, form, false, &super::programs::Registers::of(m))?;
        let dir = Scratch::new()?;
        for (name, header) in super::programs::HEADERS {
            std::fs::write(dir.0.join(name), header)?;
        }
        let source = dir.0.join("surface.ps.hlsl");
        std::fs::write(&source, &text.pixel)?;
        let output = std::process::Command::new(dxc)
            .args(["-nologo", "-T", "ps_6_0", "-E", "program_ps", "-O3", "-I"])
            .arg(&dir.0)
            .arg("-Fo")
            .arg(dir.0.join("surface.bin"))
            .arg(&source)
            .output()
            .map_err(|e| refuse("Cook.Shader.Compiler", &[("file", dxc.display().to_string()), ("detail", e.to_string())]))?;
        if output.status.success() {
            continue;
        }
        let said = String::from_utf8_lossy(&output.stderr).to_string() + &String::from_utf8_lossy(&output.stdout);
        return Err(compile_refusal(&said, m, sf));
    }
    // The ray-hit form (layer 3) over a stand-in of the tracer's side, as a compute program: what cannot run at a
    // ray's hit is refused at its line too.
    seen.clear();
    for m in &ir.materials {
        let (Some(sf), Some(dxc)) = (&m.surface, &compiler) else { continue };
        if !seen.insert(m.name.clone()) {
            continue;
        }
        let dir = Scratch::new()?;
        let source = dir.0.join("surface.ray.hlsl");
        std::fs::write(&source, format!("{RAY_HIT_STAND_IN}{}{RAY_HIT_ENTRY}", super::programs::ray_hit_form(sf)))?;
        let output = std::process::Command::new(dxc)
            .args(["-nologo", "-T", "cs_6_0", "-E", "ray_hit_check", "-O3", "-Fo"])
            .arg(dir.0.join("surface.ray.bin"))
            .arg(&source)
            .output()
            .map_err(|e| refuse("Cook.Shader.Compiler", &[("file", dxc.display().to_string()), ("detail", e.to_string())]))?;
        if !output.status.success() {
            let said = String::from_utf8_lossy(&output.stderr).to_string() + &String::from_utf8_lossy(&output.stdout);
            return Err(compile_refusal(&said, m, sf));
        }
    }
    Ok(())
}

/// The compiler's first error as the shader's (its file and line) or the generated part's (the material).
fn compile_refusal(said: &str, m: &super::model_ir::MaterialIr, sf: &super::model_ir::SurfaceIr) -> anyhow::Error {
    let (line, detail) = first_error(said, &sf.file);
    match line {
        Some(line) => refuse("Cook.Shader.Compile", &[("file", sf.file.clone()), ("line", line.to_string()), ("detail", detail)]),
        None => refuse("Cook.Shader.CompileProgram", &[("material", m.name.clone()), ("file", sf.file.clone()), ("detail", detail)]),
    }
}

/// What the tracer gives a ray-hit form (programs::ray_hit_form), as the check stands it in: the hit, the record it
/// fills, a place's value and a map's texel. The tracer's own is the same three names with its tables behind them.
pub const RAY_HIT_STAND_IN: &str = "\
struct RtModHit {
    float2 uv;
    float4 color;
    float3 normal;
    float3 world_position;
    float3 view;
    float time;
    float lod;
};
struct RtModSurface {
    float3 base;
    float alpha;
    float3 normal;
    float roughness;
    float metal;
    float3 emission;
};
RWStructuredBuffer<float4> rt_check_out : register(u0);
float4 rt_mod_place(uint place) { return float4(0.5, 0.5, 0.5, 0.5) * float(place + 1u); }
float4 rt_mod_map(uint slot, float2 uv, float lod) { return float4(uv, lod, float(slot)); }
#line 1 \"program\"
";

/// The check's entry: one hit through the form, its outputs kept so nothing is optimized away.
const RAY_HIT_ENTRY: &str = "\
[numthreads(1, 1, 1)]
void ray_hit_check(uint3 id : SV_DispatchThreadID) {
    RtModHit h = (RtModHit)0;
    h.uv = float2(id.xy);
    h.normal = float3(0.0, 0.0, 1.0);
    h.view = float3(0.0, 0.0, 1.0);
    h.color = float4(1.0, 1.0, 1.0, 1.0);
    RtModSurface o = (RtModSurface)0;
    RT_MOD_SURFACE_NS::surface_hit(h, o);
    rt_check_out[0] = float4(o.base, o.alpha) + float4(o.normal, o.roughness) + float4(o.emission, o.metal);
}
";

/// A folder of its own under the system's temporary folder for one compile, removed when dropped.
struct Scratch(std::path::PathBuf);

impl Scratch {
    fn new() -> Result<Scratch> {
        static NEXT: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
        let n = NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        let dir = std::env::temp_dir().join(format!("nebula-surface-{}-{n}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir)?;
        Ok(Scratch(dir))
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

/// The compiler's first error (`<file>:<line>:<column>: error: <words>`): its line when it is in `file`, and its words.
fn first_error(said: &str, file: &str) -> (Option<u32>, String) {
    for l in said.lines() {
        let Some(at) = l.find(": error: ") else { continue };
        let words = l[at + ": error: ".len()..].trim().chars().take(300).collect::<String>();
        let place = &l[..at];
        let mut parts = place.rsplitn(3, ':');
        let (_column, line, path) = (parts.next(), parts.next(), parts.next());
        let line = line.and_then(|n| n.trim().parse::<u32>().ok());
        let ours = path.is_some_and(|p| p.replace('\\', "/").ends_with(file));
        return (line.filter(|_| ours), words);
    }
    (None, said.lines().find(|l| !l.trim().is_empty()).unwrap_or("").chars().take(300).collect())
}

#[cfg(test)]
#[path = "surface_shader_tests.rs"]
mod tests;
