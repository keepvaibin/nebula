//! A mod's screen pass (docs/modding/services/draw.md): `layers/<l>/assets/post/<name>.hlsl` with
//! `post/<name>.json` beside it becomes `Luma/<id>/post/<name>.post` in the mods' store, which `draw.post_register`
//! names and the game parses (luma_draw_post.h). The HLSL defines `float4 post(PostIn i)`; the host owns everything
//! around it (PostIn {uv, pixel, size, time}, scene, scene_texel, depth, motion, `float4 post_params[4]`, the bindings
//! and the entry point), so the text this cook writes is only each parameter as a name for its place in post_params
//! (`#define <name> (post_params[<row>].<columns>)`) followed by the mod's code.
//!
//! `post/<name>.json`: `{"params": [{"name", "type": "float".."float4", "default"}], "reads": ["depth", "motion"]}`.
//! Parameters fill the 4 rows of 4 floats in order, each in the first row with room for it whole (a parameter never
//! crosses a row); more than 16 floats, or a set that does not fit, is Cook.Post.TooManyParams.
//!
//! The file (little-endian): "NBPOST01", u32 version 1, u32 reads (1 depth, 2 motion), u32 parameter count, u32 text
//! bytes; each parameter in 60 bytes: name[32] NUL-padded, u32 floats (1-4), u32 row (0-3), u32 column, f32
//! default[4] (unused floats 0); then the text, UTF-8, not NUL-ended.
//!
//! Refused, each by name with the file (and the line where there is one), L804 `Cook.Post.<Kind>`: no HLSL beside the
//! JSON, a JSON outside the interface, a name twice, too many parameters, HLSL that binds anything of its own or
//! reaches past the interface (the material shaders' words, surface_shader.rs, and `main`), or that has no `post`.

use anyhow::Result;
use serde_json::Value;

use super::refuse::refuse;

pub const MAGIC: &[u8; 8] = b"NBPOST01";
pub const VERSION: u32 = 1;
pub const READ_DEPTH: u32 = 1;
pub const READ_MOTION: u32 = 2;
const READS: [(&str, u32); 2] = [("depth", READ_DEPTH), ("motion", READ_MOTION)];
/// post_params: rows of four floats.
pub const ROWS: usize = 4;
pub const MOST_FLOATS: usize = 16;
/// A parameter's record and its name field (the name is at most one byte shorter).
pub const PARAM_BYTES: usize = 60;
const NAME_BYTES: usize = 32;
/// The largest program source, in bytes.
pub const MOST_BYTES: usize = 64 << 10;

/// Words a post program may not write: what would bind something of its own (as a material shader's list) or define
/// the entry point the host writes.
const FORBIDDEN: [&str; 15] = [
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
    "main",
];

/// Names a parameter may not take: a `#define` of one would rewrite every use of it in the program, so neither the
/// host's names (its prelude and entry point, render/mod_post.cpp), PostIn's fields, HLSL's words nor its intrinsics.
const RESERVED: &[&str] = &[
    "post", "PostIn", "post_params", "scene", "scene_texel", "depth", "motion", "main", "uv", "pixel", "size", "time",
    "position", "corner", "id", "program_vs", "program_ps",
    "i", "if", "else", "for", "while", "do", "return", "true", "false", "struct", "static", "const", "in", "out",
    "inout", "uniform", "void", "bool", "int", "uint", "float", "half", "double", "dword", "matrix", "vector", "string",
    "discard", "switch", "case", "default", "break", "continue", "typedef", "linear", "centroid", "nointerpolation",
    "precise", "row_major", "column_major", "abs", "acos", "all", "any", "asdouble", "asfloat", "asin", "asint", "asuint",
    "atan", "atan2", "ceil", "clamp", "clip", "cos", "cosh", "countbits", "cross", "ddx", "ddx_coarse", "ddx_fine", "ddy",
    "ddy_coarse", "ddy_fine", "degrees", "determinant", "distance", "dot", "dst", "exp", "exp2", "f16tof32", "f32tof16",
    "faceforward", "firstbithigh", "firstbitlow", "floor", "fma", "fmod", "frac", "frexp", "fwidth", "isfinite", "isinf",
    "isnan", "ldexp", "length", "lerp", "lit", "log", "log10", "log2", "mad", "max", "min", "modf", "mul", "noise",
    "normalize", "pow", "radians", "rcp", "reflect", "refract", "reversebits", "round", "rsqrt", "saturate", "sign",
    "sin", "sincos", "sinh", "smoothstep", "sqrt", "step", "tan", "tanh", "transpose", "trunc",
];
const RESERVED_PREFIXES: [&str; 15] = ["post_", "scene_", "nebula_", "NEBULA_", "program_", "SV_", "g_", "float", "int", "uint", "half", "bool", "double", "min1", "__"];

/// A parameter as the file holds it.
#[derive(Clone, Debug, PartialEq)]
pub struct Param {
    pub name: String,
    pub floats: u32,
    pub row: u32,
    pub column: u32,
    pub default: [f32; 4],
}

/// A screen pass's source JSON in the mods' store: `Luma/<id>/post/<name>.json`.
pub fn is_post_source(path: &str) -> bool {
    in_post(path).is_some_and(|rest| rest.ends_with(".json"))
}

/// A screen pass's HLSL in the mods' store: what its JSON reads, not a file of its own.
pub fn is_asset_source(path: &str) -> bool {
    in_post(path).is_some_and(|rest| rest.ends_with(".hlsl"))
}

/// The lowercase path past `Luma/<id>/post/`.
fn in_post(path: &str) -> Option<String> {
    let lower = path.trim_start_matches('/').to_ascii_lowercase();
    let mut parts = lower.splitn(4, '/');
    (parts.next() == Some("luma") && parts.next().is_some_and(|id| !id.is_empty()) && parts.next() == Some("post")).then_some(())?;
    parts.next().filter(|rest| !rest.is_empty()).map(str::to_string)
}

fn identifier_ok(name: &str) -> bool {
    let mut bytes = name.bytes();
    bytes.next().is_some_and(|b| b.is_ascii_alphabetic() || b == b'_')
        && bytes.all(|b| b.is_ascii_alphanumeric() || b == b'_')
        && name.len() < NAME_BYTES
        && !RESERVED.contains(&name)
        && !RESERVED_PREFIXES.iter().any(|p| name.starts_with(p))
}

/// A default of `floats` numbers: one number for a float, a list of that many otherwise.
fn numbers(v: &Value, floats: usize) -> Option<[f32; 4]> {
    let mut out = [0.0f32; 4];
    let finite = |x: &Value| x.as_f64().filter(|x| x.is_finite() && x.abs() <= f64::from(f32::MAX)).map(|x| x as f32);
    if floats == 1 {
        out[0] = finite(v)?;
        return Some(out);
    }
    let a = v.as_array().filter(|a| a.len() == floats)?;
    for (k, x) in a.iter().enumerate() {
        out[k] = finite(x)?;
    }
    Some(out)
}

/// The JSON's parameters, each in its place, and the reads word.
pub fn read_json(json_file: &str, json: &[u8]) -> Result<(Vec<Param>, u32)> {
    let bad = |key: &str| refuse("Cook.Post.Json", &[("file", json_file.to_string()), ("key", key.to_string())]);
    let root: Value = serde_json::from_slice(json).map_err(|_| bad("/"))?;
    let object = root.as_object().ok_or_else(|| bad("/"))?;
    if let Some(key) = object.keys().find(|k| !matches!(k.as_str(), "params" | "reads")) {
        return Err(bad(&format!("/{key}")));
    }
    if !root["params"].is_null() && !root["params"].is_array() {
        return Err(bad("/params"));
    }
    if !root["reads"].is_null() && !root["reads"].is_array() {
        return Err(bad("/reads"));
    }
    let given = root["params"].as_array().map(Vec::as_slice).unwrap_or_default();
    let mut params: Vec<Param> = Vec::new();
    for (k, p) in given.iter().enumerate() {
        let at = format!("/params/{k}");
        let fields = p.as_object().ok_or_else(|| bad(&at))?;
        if let Some(key) = fields.keys().find(|key| !matches!(key.as_str(), "name" | "type" | "default")) {
            return Err(bad(&format!("{at}/{key}")));
        }
        let name = p["name"].as_str().filter(|n| identifier_ok(n)).ok_or_else(|| bad(&format!("{at}/name")))?;
        let floats = match p["type"].as_str() {
            Some("float") => 1,
            Some("float2") => 2,
            Some("float3") => 3,
            Some("float4") => 4,
            _ => return Err(bad(&format!("{at}/type"))),
        };
        let default = numbers(&p["default"], floats).ok_or_else(|| bad(&format!("{at}/default")))?;
        if params.iter().any(|q| q.name == name) {
            return Err(refuse("Cook.Post.Twice", &[("file", json_file.to_string()), ("name", name.to_string())]));
        }
        params.push(Param { name: name.to_string(), floats: floats as u32, row: 0, column: 0, default });
    }
    // Each in the first row with room for it whole.
    let floats: usize = params.iter().map(|p| p.floats as usize).sum();
    let too_many = || refuse("Cook.Post.TooManyParams", &[("file", json_file.to_string()), ("count", floats.to_string()), ("max", MOST_FLOATS.to_string())]);
    if floats > MOST_FLOATS {
        return Err(too_many());
    }
    let mut fill = [0u32; ROWS];
    for p in &mut params {
        let row = (0..ROWS).find(|r| fill[*r] + p.floats <= 4).ok_or_else(too_many)?;
        p.row = row as u32;
        p.column = fill[row];
        fill[row] += p.floats;
    }
    let mut reads = 0u32;
    for (k, r) in root["reads"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
        reads |= r.as_str().and_then(|r| READS.iter().find(|(n, _)| *n == r)).map(|(_, b)| *b).ok_or_else(|| bad(&format!("/reads/{k}")))?;
    }
    Ok((params, reads))
}

/// The program text the file carries: each parameter's name for its place, then (from line 1 again) the mod's code.
pub fn text(params: &[Param], code: &str) -> String {
    let mut out = String::new();
    for p in params {
        let columns = &"xyzw"[p.column as usize..(p.column + p.floats) as usize];
        out.push_str(&format!("#define {} (post_params[{}].{columns})\n", p.name, p.row));
    }
    // The compiler's lines are the mod's file's.
    if !params.is_empty() {
        out.push_str("#line 1\n");
    }
    out.push_str(code);
    out
}

/// `post/<name>.json` and its HLSL (None: the mod has none beside it) as the game's `.post` file.
pub fn cook(json_file: &str, json: &[u8], hlsl_file: &str, hlsl: Option<&[u8]>) -> Result<Vec<u8>> {
    let (params, reads) = read_json(json_file, json)?;
    let hlsl = hlsl.ok_or_else(|| refuse("Cook.Post.Missing", &[("file", json_file.to_string()), ("other", hlsl_file.to_string())]))?;
    if hlsl.len() > MOST_BYTES {
        return Err(refuse("Cook.Post.TooLarge", &[("file", hlsl_file.to_string()), ("value", hlsl.len().to_string()), ("max", MOST_BYTES.to_string())]));
    }
    let code = std::str::from_utf8(hlsl).map_err(|_| refuse("Cook.Post.Text", &[("file", hlsl_file.to_string())]))?.replace("\r\n", "\n");
    check_code(hlsl_file, &code)?;
    let text = text(&params, &code);
    let mut out = Vec::with_capacity(24 + PARAM_BYTES * params.len() + text.len());
    out.extend_from_slice(MAGIC);
    for word in [VERSION, reads, params.len() as u32, u32::try_from(text.len())?] {
        out.extend_from_slice(&word.to_le_bytes());
    }
    for p in &params {
        let mut name = [0u8; NAME_BYTES];
        name[..p.name.len()].copy_from_slice(p.name.as_bytes());
        out.extend_from_slice(&name);
        for word in [p.floats, p.row, p.column] {
            out.extend_from_slice(&word.to_le_bytes());
        }
        for f in p.default {
            out.extend_from_slice(&f.to_le_bytes());
        }
    }
    out.extend_from_slice(text.as_bytes());
    Ok(out)
}

/// The HLSL's words outside comments and strings, each with its line: what it may not write, and its `post`.
fn check_code(file: &str, code: &str) -> Result<()> {
    let forbidden = |line: usize, word: &str| refuse("Cook.Post.Forbidden", &[("file", file.to_string()), ("line", line.to_string()), ("word", word.to_string())]);
    let bytes = code.as_bytes();
    let (mut at, mut line, mut line_start) = (0usize, 1usize, true);
    let mut has_post = false;
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
            if word == "post" && code[at..].trim_start().starts_with('(') {
                has_post = true;
            }
            line_start = false;
            continue;
        }
        if !b.is_ascii_whitespace() {
            line_start = false;
        }
        at += 1;
    }
    if !has_post {
        return Err(refuse("Cook.Post.NoPost", &[("file", file.to_string())]));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn why(e: anyhow::Error) -> &'static str {
        match crate::cook::refuse::refusal_of(&e).expect("a refusal").args[0].1 {
            luma_validate::Arg::Phrase(p) => p,
            _ => panic!("no phrase"),
        }
    }

    const CODE: &[u8] = b"float4 post(PostIn i)\n{\n    return scene(i.uv) * tint * strength;\n}\n";

    fn u32_at(b: &[u8], at: usize) -> u32 {
        u32::from_le_bytes(b[at..at + 4].try_into().unwrap())
    }

    #[test]
    fn a_pass_is_the_games_file_with_its_params_in_rows() {
        let json = br#"{"params": [{"name": "strength", "type": "float", "default": 0.5},
                                    {"name": "tint", "type": "float4", "default": [1, 0.5, 0.25, 1]},
                                    {"name": "offset", "type": "float3", "default": [0, 0, 0]}], "reads": ["depth"]}"#;
        let file = cook("assets/post/Warm.json", json, "assets/post/Warm.hlsl", Some(CODE)).unwrap();
        assert_eq!(&file[..8], MAGIC);
        assert_eq!((u32_at(&file, 8), u32_at(&file, 12), u32_at(&file, 16)), (1, READ_DEPTH, 3));
        // strength row 0 x; tint whole row 1; offset back in row 0 after strength.
        let param = |k: usize| {
            let at = 24 + PARAM_BYTES * k;
            let name = String::from_utf8(file[at..at + 32].iter().copied().take_while(|b| *b != 0).collect()).unwrap();
            (name, u32_at(&file, at + 32), u32_at(&file, at + 36), u32_at(&file, at + 40))
        };
        assert_eq!(param(0), ("strength".into(), 1, 0, 0));
        assert_eq!(param(1), ("tint".into(), 4, 1, 0));
        assert_eq!(param(2), ("offset".into(), 3, 0, 1));
        let at = 24 + PARAM_BYTES + 44;
        assert_eq!(f32::from_le_bytes(file[at + 4..at + 8].try_into().unwrap()), 0.5);
        let text = std::str::from_utf8(&file[24 + 3 * PARAM_BYTES..]).unwrap();
        assert_eq!(text.len() as u32, u32_at(&file, 20));
        assert!(text.starts_with("#define strength (post_params[0].x)\n#define tint (post_params[1].xyzw)\n#define offset (post_params[0].yzw)\n#line 1\nfloat4 post("));
    }

    #[test]
    fn what_leaves_the_interface_is_refused_by_name() {
        let refusal = |json: &[u8], code: Option<&[u8]>| why(cook("assets/post/P.json", json, "assets/post/P.hlsl", code).err().unwrap());
        assert_eq!(refusal(b"{}", None), "Cook.Post.Missing");
        assert_eq!(refusal(br#"{"stage": "post"}"#, Some(CODE)), "Cook.Post.Json");
        assert_eq!(refusal(br#"{"params": [{"name": "lerp", "type": "float", "default": 0}]}"#, Some(CODE)), "Cook.Post.Json");
        assert_eq!(refusal(br#"{"params": [{"name": "a", "type": "float2", "default": 1}]}"#, Some(CODE)), "Cook.Post.Json");
        assert_eq!(refusal(br#"{"reads": ["normal"]}"#, Some(CODE)), "Cook.Post.Json");
        let twice = br#"{"params": [{"name": "a", "type": "float", "default": 0}, {"name": "a", "type": "float", "default": 0}]}"#;
        assert_eq!(refusal(twice, Some(CODE)), "Cook.Post.Twice");
        // 5 float3s are 15 floats, but no two share a row.
        let three = |n: usize| (0..n).map(|k| format!(r#"{{"name": "p{k}", "type": "float3", "default": [0, 0, 0]}}"#)).collect::<Vec<_>>().join(",");
        assert_eq!(refusal(format!(r#"{{"params": [{}]}}"#, three(5)).as_bytes(), Some(CODE)), "Cook.Post.TooManyParams");
        assert_eq!(refusal(b"{}", Some(b"Texture2D t;\nfloat4 post(PostIn i) { return 0; }")), "Cook.Post.Forbidden");
        assert_eq!(refusal(b"{}", Some(b"float4 main(PostIn i) : SV_Target { return 0; }")), "Cook.Post.Forbidden");
        assert_eq!(refusal(b"{}", Some(b"#include \"x.hlsli\"\nfloat4 post(PostIn i) { return 0; }")), "Cook.Post.Forbidden");
        assert_eq!(refusal(b"{}", Some(b"float4 other(PostIn i) { return 0; } // post(")), "Cook.Post.NoPost");
        assert_eq!(refusal(b"{}", Some(b"\xff")), "Cook.Post.Text");
    }

    #[test]
    fn only_a_mods_post_folder_holds_passes() {
        assert!(is_post_source("Luma/my.mod/post/Warm.json"));
        assert!(is_asset_source("Luma/my.mod/post/Warm.hlsl"));
        assert!(!is_post_source("Luma/my.mod/fonts/Warm.json"));
        assert!(!is_post_source("post/Warm.json"));
        assert!(!is_post_source("Luma/my.mod/post/"));
    }
}
