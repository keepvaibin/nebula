use super::*;
use crate::cook::refuse::said;

const STRIPES: &str = "// Animated stripes (plan 3.4's test shader).\nvoid surface(in SurfaceIn i, inout Surface s) {\n    const float band = frac(i.uv0.x * 8.0 + i.time * speed);\n    s.base = band < 0.5 ? tint.rgb : s.base * paint(i.uv0).rgb;\n    s.roughness = 0.3;\n    s.emission = glow * s.base;\n}\n";

fn stripes_json() -> Value {
    serde_json::json!({
        "stage": "material",
        "params": [
            {"name": "speed", "type": "float", "default": 0.5},
            {"name": "tint", "type": "float4", "default": [1.0, 0.2, 0.1, 1.0]},
            {"name": "glow", "type": "float", "default": 0.0}
        ],
        "textures": [{"slot": 0, "name": "paint"}],
        "reads": ["time"]
    })
}

fn read_json(json: &Value, code: &str) -> Result<Source> {
    read("shaders/Stripes", "assets/shaders/Stripes.hlsl", json.to_string().as_bytes(), code.as_bytes())
}

fn refusal(r: Result<impl std::fmt::Debug>) -> (String, Vec<(&'static str, String)>) {
    let error = r.unwrap_err();
    let (why, args) = said(&error);
    (why.to_string(), args)
}

#[test]
fn a_shader_is_its_json_and_its_surface() {
    let s = read_json(&stripes_json(), STRIPES).unwrap();
    assert_eq!(s.params.iter().map(|p| (p.name.as_str(), p.four)).collect::<Vec<_>>(), [("speed", false), ("tint", true), ("glow", false)]);
    assert_eq!(s.params[1].default, [1.0, 0.2, 0.1, 1.0]);
    assert_eq!(s.textures, [(0, "paint".to_string())]);
    assert_eq!(s.reads, READ_TIME);
    assert_eq!(s.file, "assets/shaders/Stripes.hlsl");
}

#[test]
fn a_shader_outside_the_interface_is_refused_by_name_and_line() {
    let forbidden = |code: &str| refusal(read_json(&stripes_json(), code));
    for (code, word, line) in [
        ("void surface(in SurfaceIn i, inout Surface s) {}\nTexture2D extra : register(t9);\n", "Texture2D", "2"),
        ("void surface(in SurfaceIn i, inout Surface s) {}\n\n\ncbuffer X { float4 y; };\n", "cbuffer", "4"),
        ("#include \"other.hlsl\"\nvoid surface(in SurfaceIn i, inout Surface s) {}\n", "#include", "1"),
        ("  #  pragma once\nvoid surface(in SurfaceIn i, inout Surface s) {}\n", "#pragma", "1"),
        ("void surface(in SurfaceIn i, inout Surface s) {}\nRWStructuredBuffer<float> out;\n", "RWStructuredBuffer", "2"),
        ("void surface(in SurfaceIn i, inout Surface s) {}\nSamplerState mine;\n", "SamplerState", "2"),
        ("[[vk::binding(0)]] float x;\nvoid surface(in SurfaceIn i, inout Surface s) {}\n", "[[", "1"),
        ("/* a\nb */ float y : register(c0);\nvoid surface(in SurfaceIn i, inout Surface s) {}\n", "register", "2"),
    ] {
        let (why, args) = forbidden(code);
        assert_eq!(why, "Cook.Shader.Forbidden", "{code}");
        assert!(args.contains(&("word", word.to_string())) && args.contains(&("line", line.to_string())), "{code}: {args:?}");
    }
    // Words in comments and strings are not code; a shader with no surface says so.
    let (why, _) = forbidden("// register cbuffer Texture2D\nvoid surface_x() {}\n");
    assert_eq!(why, "Cook.Shader.NoSurface");
    assert!(read_json(&stripes_json(), &format!("/* register */\n{STRIPES}")).is_ok());
    let (why, args) = refusal(read_json(&stripes_json(), &" ".repeat(MOST_BYTES + 1)));
    assert_eq!(why, "Cook.Shader.TooLarge");
    assert!(args.contains(&("max", MOST_BYTES.to_string())));
}

#[test]
fn a_shaders_json_is_its_interface_and_nothing_else() {
    let json_fault = |edit: &dyn Fn(&mut Value)| {
        let mut j = stripes_json();
        edit(&mut j);
        refusal(read_json(&j, STRIPES))
    };
    let cases: [(&dyn Fn(&mut Value), &str, &str); 8] = [
        (&|j| j["stage"] = "post".into(), "Cook.Shader.Json", "/stage"),
        (&|j| j["extra"] = 1.into(), "Cook.Shader.Json", "/extra"),
        (&|j| j["params"][0]["type"] = "int".into(), "Cook.Shader.Json", "/params/0/type"),
        (&|j| j["params"][1]["default"] = serde_json::json!([1.0, 2.0]), "Cook.Shader.Json", "/params/1/default"),
        (&|j| j["params"][0]["name"] = "prev".into(), "Cook.Shader.Json", "/params/0/name"),
        (&|j| j["params"][0]["name"] = "pbr_x".into(), "Cook.Shader.Json", "/params/0/name"),
        (&|j| j["textures"][0]["slot"] = 8.into(), "Cook.Shader.Json", "/textures/0/slot"),
        (&|j| j["reads"] = serde_json::json!(["normal", "colour"]), "Cook.Shader.Json", "/reads/1"),
    ];
    for (edit, why, key) in cases {
        let (got, args) = json_fault(edit);
        assert_eq!((got.as_str(), args.iter().find(|a| a.0 == "key").map(|a| a.1.as_str())), (why, Some(key)));
    }
    let (why, args) = json_fault(&|j| j["textures"][0]["name"] = "speed".into());
    assert_eq!(why, "Cook.Shader.Twice");
    assert!(args.contains(&("name", "speed".to_string())));
    let (why, _) = json_fault(&|j| j["params"][2]["name"] = "speed".into());
    assert_eq!(why, "Cook.Shader.Twice");
}

#[test]
fn a_material_gives_its_values_and_every_texture() {
    let s = read_json(&stripes_json(), STRIPES).unwrap();
    let extras = serde_json::json!({"shader": "shaders/Stripes", "params": {"speed": 2.0}, "textures": {"paint": 3}});
    let u = use_of(&s, "Cube", &extras).unwrap();
    assert_eq!(u.values, [[2.0, 0.0, 0.0, 0.0], [1.0, 0.2, 0.1, 1.0], [0.0; 4]]);
    assert_eq!(u.textures, [3]);
    let fault = |extras: Value| refusal(use_of(&s, "Cube", &extras));
    let (why, args) = fault(serde_json::json!({"params": {"sped": 2.0}, "textures": {"paint": 3}}));
    assert_eq!(why, "Cook.Shader.UnknownParam");
    assert!(args.contains(&("param", "sped".to_string())) && args.contains(&("shader", "shaders/Stripes".to_string())));
    let (why, args) = fault(serde_json::json!({"params": {"tint": 1.0}, "textures": {"paint": 3}}));
    assert_eq!(why, "Cook.Shader.ParamType");
    assert!(args.contains(&("expected", "float4".to_string())));
    let (why, args) = fault(serde_json::json!({}));
    assert_eq!(why, "Cook.Shader.NoTexture");
    assert!(args.contains(&("texture", "paint".to_string())) && args.contains(&("material", "Cube".to_string())));
    let (why, args) = fault(serde_json::json!({"textures": {"paint": 3, "other": 1}}));
    assert_eq!(why, "Cook.Shader.UnknownTexture");
    assert!(args.contains(&("texture", "other".to_string())));
}

#[test]
fn parameters_take_the_free_places_in_order_and_their_range() {
    let s = read_json(&stripes_json(), STRIPES).unwrap();
    let values = [[0.5, 0.0, 0.0, 0.0], [1.0, 0.2, 0.1, 1.0], [0.0; 4]];
    // Register 1 and constant colour 0 are the material's own: speed takes register 2, tint register 3, glow constant 1.
    let mut used = [false; PLACES];
    used[0] = true;
    used[3] = true;
    assert_eq!(places(&s, "Cube", &values, used).unwrap(), [1, 2, 4]);
    // A register holds -4 to 4, a constant colour 0 to 1.
    assert_eq!(places(&s, "Cube", &[[-3.5, 0.0, 0.0, 0.0], values[1], values[2]], [false; PLACES]).unwrap(), [0, 1, 2]);
    // Registers taken, tint lands on a constant colour, which holds 0 to 1.
    let (why, args) = refusal(places(&s, "Cube", &[values[0], [2.0, 0.0, 0.0, 1.0], values[2]], [true, true, true, false, false, false, false]));
    assert_eq!(why, "Cook.Shader.ParamRange");
    assert!(args.contains(&("param", "tint".to_string())) && args.contains(&("max", "1".to_string())), "{args:?}");
    let (why, args) = refusal(places(&s, "Cube", &values, [true, true, true, true, true, true, false]));
    assert_eq!(why, "Cook.Shader.TooManyParams");
    assert!(args.contains(&("count", "3".to_string())) && args.contains(&("max", "1".to_string())));
    let (why, args) = refusal(places(&s, "Cube", &[[5.0, 0.0, 0.0, 0.0], values[1], values[2]], [false; PLACES]));
    assert_eq!(why, "Cook.Shader.ParamRange");
    assert!(args.contains(&("param", "speed".to_string())) && args.contains(&("max", "4".to_string())));
}

// ---------------------------------------------------------------- a mod's cube with the shader, through the cook

fn encode(data: &[u8]) -> String {
    const T: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::new();
    for chunk in data.chunks(3) {
        let b = [chunk[0], *chunk.get(1).unwrap_or(&0), *chunk.get(2).unwrap_or(&0)];
        let n = (u32::from(b[0]) << 16) | (u32::from(b[1]) << 8) | u32::from(b[2]);
        for k in 0..4 {
            if k <= chunk.len() {
                out.push(T[((n >> (18 - 6 * k)) & 63) as usize] as char);
            } else {
                out.push('=');
            }
        }
    }
    out
}

/// A one-triangle glTF with normals and TEXCOORD_0 whose material uses the stripes shader, and a 1 x 1 image.
pub(crate) fn shader_document(extras: Value) -> Value {
    let floats: Vec<f32> = [
        [0.0f32, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0],
        [0.0, 0.0, 1.0],
        [0.0, 0.0, 1.0],
    ]
    .concat()
    .into_iter()
    .chain([0.0f32, 0.0, 1.0, 0.0, 0.0, 1.0])
    .collect();
    let mut bytes: Vec<u8> = floats.iter().flat_map(|f| f.to_le_bytes()).collect();
    bytes.extend([0u8, 0, 1, 0, 2, 0]);
    // A 1 x 1 RGBA PNG.
    let png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
    serde_json::json!({
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": "body", "mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "materials": [{"name": "Cube", "pbrMetallicRoughness": {"baseColorFactor": [1.0, 1.0, 1.0, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.5},
                       "extras": {"nebula": extras}}],
        "textures": [{"source": 0}],
        "images": [{"uri": format!("data:image/png;base64,{png}")}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
            {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
        ],
        "bufferViews": [
            {"buffer": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": 36},
            {"buffer": 0, "byteOffset": 72, "byteLength": 24}, {"buffer": 0, "byteOffset": 96, "byteLength": 6}
        ],
        "buffers": [{"byteLength": bytes.len(), "uri": format!("data:application/octet-stream;base64,{}", encode(&bytes))}]
    })
}

/// A mod folder holding the stripes shader; the glTF's folder inside it.
pub(crate) fn shader_mod(json: &Value, code: &str) -> (tempfile::TempDir, std::path::PathBuf) {
    let dir = tempfile::tempdir().unwrap();
    let root = dir.path().join("stripes-mod");
    let shaders = root.join("layers/base/assets/shaders");
    std::fs::create_dir_all(&shaders).unwrap();
    std::fs::write(root.join("luma.json"), br#"{"id": "stripes.mod"}"#).unwrap();
    std::fs::write(shaders.join("Stripes.shader.json"), json.to_string()).unwrap();
    std::fs::write(shaders.join("Stripes.hlsl"), code).unwrap();
    let models = root.join("layers/base/assets/models");
    std::fs::create_dir_all(&models).unwrap();
    (dir, models)
}

#[test]
fn a_material_naming_a_shader_is_lit_through_its_surface() {
    use crate::cook::programs::{forms_of, generate, Registers};
    let (_dir, folder) = shader_mod(&stripes_json(), STRIPES);
    let doc = shader_document(serde_json::json!({"shader": "shaders/Stripes", "params": {"speed": 2.0}, "textures": {"paint": 0}}));
    let bytes = serde_json::to_vec(&doc).unwrap();
    let (ir, _, _) = crate::cook::gltf::to_ir_for(&bytes, Some(&folder), None).unwrap();
    let m = &ir.materials[0];
    let sf = m.surface.as_ref().expect("the material's surface");
    assert_eq!(sf.file, "assets/shaders/Stripes.hlsl");
    assert_eq!(sf.uv, [true, false]);
    // The stages read none of the places: speed register 1, tint register 2, glow register 3 (its values in 255ths).
    assert_eq!(sf.params, [("speed".into(), false, 0), ("tint".into(), true, 1), ("glow".into(), false, 2)]);
    assert_eq!(m.registers[1][0], 510);
    assert_eq!(m.registers[2], [255, 51, 26, 255]);
    assert_eq!(sf.textures.len(), 1);
    // `<model>.params` beside the model, as content.model set_param reads it.
    let (_, files, _) = crate::cook::gltf::to_files_for(&bytes, Some(&folder), "Cube", None).unwrap();
    let (name, table) = files.iter().find(|(n, _)| n.ends_with(".params")).expect("Cube.params");
    assert_eq!(name, "Cube.params");
    let t = crate::cook::table::TableIr::from_ntbl(table).unwrap();
    assert_eq!(t.rows.len(), 3);
    // Every form: the plain one calls surface under the shader's own lines; a replacing pass keeps the stages.
    let mut called = 0;
    for form in forms_of("Cube", m, 0).unwrap() {
        let t = generate(m, form, false, &Registers::of(m)).unwrap();
        if form.replace != 0 {
            assert!(!t.pixel.contains("surface(si, s);"));
            continue;
        }
        called += 1;
        assert!(t.pixel.contains("surface(si, s);"), "{}", t.pixel);
        assert!(t.pixel.contains("#line 1 \"assets/shaders/Stripes.hlsl\""));
        let surface_in = &t.pixel[t.pixel.find("struct SurfaceIn").unwrap()..];
        let surface_in = &surface_in[..surface_in.find("};").unwrap()];
        assert!(surface_in.contains("float time;") && !surface_in.contains("normal"), "only what it reads: {surface_in}");
        assert!(t.pixel.contains("float4 paint(float2 uv)"));
        assert!(t.pixel.contains("    speed = m[") && t.pixel.contains("    tint = m["));
        assert!(t.vertex.contains("o.surf_uv0 = v.uv"));
        assert!(t.pixel.find("surface(si, s);").unwrap() < t.pixel.find("g_enh = enh_begin").unwrap(), "the normal is turned before the light");
    }
    assert!(called > 0);
}

#[test]
fn a_material_and_its_shader_must_agree() {
    let doc = |extras: Value| serde_json::to_vec(&shader_document(extras)).unwrap();
    let (_dir, folder) = shader_mod(&stripes_json(), STRIPES);
    let cook = |bytes: &[u8], folder: Option<&std::path::Path>| refusal(crate::cook::gltf::to_ir_for(bytes, folder, None).map(|_| ()));
    let (why, args) = cook(&doc(serde_json::json!({"shader": "shaders/Other", "textures": {"paint": 0}})), Some(&folder));
    assert_eq!(why, "Cook.Shader.Missing");
    assert!(args.contains(&("file", "assets/shaders/Other.shader.json".into())));
    let (why, _) = cook(&doc(serde_json::json!({"shader": "../x", "textures": {"paint": 0}})), Some(&folder));
    assert_eq!(why, "Cook.Shader.Name");
    // A glTF read from no folder has no mod to hold the shader.
    let (why, _) = cook(&doc(serde_json::json!({"shader": "shaders/Stripes", "textures": {"paint": 0}})), None);
    assert_eq!(why, "Cook.Shader.Missing");
    let (why, args) = cook(&doc(serde_json::json!({"shader": "shaders/Stripes"})), Some(&folder));
    assert_eq!((why.as_str(), args.contains(&("texture", "paint".into()))), ("Cook.Shader.NoTexture", true));
    // An unlit material has no light to give a surface.
    let mut unlit = shader_document(serde_json::json!({"shader": "shaders/Stripes", "textures": {"paint": 0}}));
    unlit["materials"][0]["extensions"] = serde_json::json!({"KHR_materials_unlit": {}});
    let (why, _) = cook(&serde_json::to_vec(&unlit).unwrap(), Some(&folder));
    assert_eq!(why, "Cook.Shader.Unlit");
}

/// LumaLink's check with no compiler says, by material, that its shader was not compiled; the game's own cook says
/// nothing (it builds the programs itself).
#[test]
fn a_check_without_a_compiler_says_the_shader_was_not_compiled() {
    let (_dir, folder) = shader_mod(&stripes_json(), STRIPES);
    let bytes = serde_json::to_vec(&shader_document(serde_json::json!({"shader": "shaders/Stripes", "textures": {"paint": 0}}))).unwrap();
    let mut ir = crate::cook::gltf::to_ir_for(&bytes, Some(&folder), None).unwrap().0;
    // Two meshes of one material: said once.
    ir.materials.push(ir.materials[0].clone());
    crate::cook::refuse::take_notes();
    check_compile(&ir).unwrap();
    assert!(crate::cook::refuse::said_notes().is_empty());
    let was = CHECK_COMPILER.lock().unwrap().replace(None);
    let got = check_compile(&ir);
    *CHECK_COMPILER.lock().unwrap() = was;
    got.unwrap();
    let notes = crate::cook::refuse::said_notes();
    assert_eq!(notes.len(), 1, "{notes:?}");
    assert_eq!(notes[0].0, "Cook.Shader.NotCompiled");
    assert!(notes[0].1.contains(&("file", "assets/shaders/Stripes.hlsl".to_string())));
}

#[test]
fn a_compilers_first_error_names_the_shaders_line() {
    let said = "C:/t/surface.ps.hlsl:40:5: warning: x\nassets/shaders/Stripes.hlsl:3:22: error: use of undeclared identifier 'spee'\n";
    assert_eq!(first_error(said, "assets/shaders/Stripes.hlsl"), (Some(3), "use of undeclared identifier 'spee'".to_string()));
    let generated = "program:12:1: error: redefinition of 'm'\n";
    assert_eq!(first_error(generated, "assets/shaders/Stripes.hlsl"), (None, "redefinition of 'm'".to_string()));
}

/// The stripes shader compiled by dxc, and one with a mistake on its third line refused at that line:
/// NEBULA_DXC=<dxc.exe> cargo test -p nebula-import --lib -- --ignored a_shader_compiles_or_names_its_line
#[test]
#[ignore]
fn a_shader_compiles_or_names_its_line() {
    let Some(dxc) = std::env::var_os("NEBULA_DXC").map(std::path::PathBuf::from) else { return };
    let cook = |code: &str| {
        let (_dir, folder) = shader_mod(&stripes_json(), code);
        let bytes = serde_json::to_vec(&shader_document(serde_json::json!({"shader": "shaders/Stripes", "textures": {"paint": 0}}))).unwrap();
        let was = CHECK_COMPILER.lock().unwrap().replace(Some(dxc.clone()));
        let got = crate::cook::gltf::to_files_for(&bytes, Some(&folder), "Cube", None).map(|_| ());
        *CHECK_COMPILER.lock().unwrap() = was;
        got
    };
    cook(STRIPES).unwrap();
    let broken = STRIPES.replace("i.time * speed", "i.time * sped");
    let (why, args) = refusal(cook(&broken));
    assert_eq!(why, "Cook.Shader.Compile", "{args:?}");
    assert!(args.contains(&("line", "3".to_string())) && args.contains(&("file", "assets/shaders/Stripes.hlsl".to_string())), "{args:?}");
    // An input it did not declare in `reads` is not there.
    let undeclared = STRIPES.replace("s.roughness = 0.3;", "s.roughness = i.normal.z;");
    let (why, args) = refusal(cook(&undeclared));
    assert_eq!(why, "Cook.Shader.Compile", "{args:?}");
    assert!(args.contains(&("line", "5".to_string())), "{args:?}");
}

/// Every form's texts of the stripes cube, for a compile on both back ends (agents/luma/g/tools/surface-dxc.sh):
/// NEBULA_SURFACE_DUMP=<folder> cargo test -p nebula-import --lib -- --ignored dump_the_surface_shader
#[test]
#[ignore]
fn dump_the_surface_shader() {
    use crate::cook::programs::{finish_text, forms_of, generate, Registers};
    let Ok(out) = std::env::var("NEBULA_SURFACE_DUMP") else { return };
    let out = std::path::PathBuf::from(out);
    std::fs::create_dir_all(&out).unwrap();
    let mut all_reads = stripes_json();
    all_reads["reads"] = serde_json::json!(["normal", "world_position", "view", "time"]);
    let turning = format!("{STRIPES}\n// reads the rest\nvoid unused_reads(SurfaceIn i) {{ float3 x = i.normal + i.world_position + i.view; }}\n");
    for (name, json, code) in [("stripes", stripes_json(), STRIPES.to_string()), ("all", all_reads, turning)] {
        let (_dir, folder) = shader_mod(&json, &code);
        let doc = shader_document(serde_json::json!({"shader": "shaders/Stripes", "textures": {"paint": 0}}));
        let (ir, _, _) = crate::cook::gltf::to_ir_for(&serde_json::to_vec(&doc).unwrap(), Some(&folder), None).unwrap();
        let m = &ir.materials[0];
        for (k, form) in forms_of("Cube", m, 0).unwrap().into_iter().enumerate() {
            for skinned in [false, true] {
                let mut t = generate(m, form, skinned, &Registers::of(m)).unwrap();
                finish_text(&m.name, form, &mut t);
                let stem = format!("{name}-{k}{}", if skinned { "s" } else { "" });
                std::fs::write(out.join(format!("{stem}.vs.hlsl")), &t.vertex).unwrap();
                std::fs::write(out.join(format!("{stem}.ps.hlsl")), &t.pixel).unwrap();
            }
        }
    }
}
