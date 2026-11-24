use super::*;

/// A triangle, as a `.gltf` with its data in place. `primitives`: how many draw it.
fn triangle(primitives: usize) -> Vec<u8> {
    let mut data = Vec::new();
    for v in [0.0f32, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0] {
        data.extend_from_slice(&v.to_le_bytes());
    }
    for v in [0u16, 1, 2] {
        data.extend_from_slice(&v.to_le_bytes());
    }
    let encoded = encode(&data);
    let drawn = vec![r#"{"attributes":{"POSITION":0},"indices":1,"material":0}"#; primitives].join(",");
    format!(
        r#"{{"asset":{{"version":"2.0"}},"scene":0,"scenes":[{{"nodes":[0]}}],"nodes":[{{"mesh":0}}],
            "materials":[{{"name":"material"}}],
            "meshes":[{{"primitives":[{drawn}]}}],
            "accessors":[{{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}},{{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}}],
            "bufferViews":[{{"buffer":0,"byteLength":36}},{{"buffer":0,"byteOffset":36,"byteLength":6}}],
            "buffers":[{{"byteLength":42,"uri":"data:application/octet-stream;base64,{encoded}"}}]}}"#
    )
    .into_bytes()
}

/// `triangles` separate triangles, every vertex its own.
fn many_triangles(triangles: usize) -> Vec<u8> {
    let count = triangles * 3;
    let mut data = Vec::with_capacity(count * 16);
    for i in 0..count {
        for v in [i as f32 * 0.01, (i % 3) as f32, 0.0] {
            data.extend_from_slice(&v.to_le_bytes());
        }
    }
    let indices_at = data.len();
    for i in 0..count as u32 {
        data.extend_from_slice(&i.to_le_bytes());
    }
    let encoded = encode(&data);
    let total = data.len();
    let indices = total - indices_at;
    format!(
        r#"{{"asset":{{"version":"2.0"}},"scene":0,"scenes":[{{"nodes":[0]}}],"nodes":[{{"mesh":0}}],
            "meshes":[{{"primitives":[{{"attributes":{{"POSITION":0}},"indices":1}}]}}],
            "accessors":[{{"bufferView":0,"componentType":5126,"count":{count},"type":"VEC3"}},{{"bufferView":1,"componentType":5125,"count":{count},"type":"SCALAR"}}],
            "bufferViews":[{{"buffer":0,"byteLength":{indices_at}}},{{"buffer":0,"byteOffset":{indices_at},"byteLength":{indices}}}],
            "buffers":[{{"byteLength":{total},"uri":"data:application/octet-stream;base64,{encoded}"}}]}}"#
    )
    .into_bytes()
}

fn encode(data: &[u8]) -> String {
    const T: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut s = String::new();
    for c in data.chunks(3) {
        let v = (u32::from(c[0]) << 16) | (u32::from(*c.get(1).unwrap_or(&0)) << 8) | u32::from(*c.get(2).unwrap_or(&0));
        s.push(T[(v >> 18) as usize & 63] as char);
        s.push(T[(v >> 12) as usize & 63] as char);
        s.push(if c.len() > 1 { T[(v >> 6) as usize & 63] as char } else { '=' });
        s.push(if c.len() > 2 { T[v as usize & 63] as char } else { '=' });
    }
    s
}

#[test]
fn base64_reads_what_it_is_given() {
    assert_eq!(base64("aGVsbG8=").unwrap(), b"hello");
    assert_eq!(base64("aGVsbG8h").unwrap(), b"hello!");
}

#[test]
fn a_triangle_is_a_model() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let (model, clips) = to_files(&triangle(1), None, "Triangle").unwrap();
    assert_eq!(u32::from_le_bytes(model[0..4].try_into().unwrap()), super::super::model_write::MAGIC);
    assert!(clips.is_empty());
    let (ir, _) = to_ir(&triangle(1), None).unwrap();
    assert_eq!(ir.materials.len(), 1);
    assert_eq!(ir.materials[0].name, "material");
    assert_eq!(ir.joints.len(), 1);
    // Its names read back from the cooked model, as `lumalink list models` reads the game's.
    let names = super::super::model_write::names(&model).unwrap();
    assert_eq!(names.joints, ir.joints.iter().map(|j| j.name.clone()).collect::<Vec<_>>());
    assert_eq!(names.materials, ["material"]);
    assert_eq!(names.textures, ir.textures.iter().map(|t| t.name.clone()).collect::<Vec<_>>());
    assert!(super::super::model_write::names(&model[..8]).is_err());
}

#[test]
fn two_meshes_of_one_material_both_draw() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let (ir, _) = to_ir(&triangle(2), None).unwrap();
    assert_eq!(ir.shapes.len(), 2);
    assert_eq!(ir.materials.len(), 2);
    assert_eq!((ir.materials[0].mesh, ir.materials[1].mesh), (0, 1));
    assert_eq!((ir.shapes[0].material, ir.shapes[1].material), (0, 1));
    assert_eq!(ir.materials[1].same_as, 0);
    assert_eq!(ir.joints[0].meshes.len(), 2);
}

#[test]
fn more_vertices_than_sixteen_bits_count() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let bytes = many_triangles(24_000);
    let (ir, _) = to_ir(&bytes, None).unwrap();
    assert_eq!(ir.arrays.positions.len() > 0x1_0000, true);
    assert_eq!(ir.shapes[0].groups[0].corners.len(), 72_000);
    to_files(&bytes, None, "Many").unwrap();
}

#[test]
fn an_animation_plays_as_its_extras_say() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let mut keys = Vec::new();
    for v in [0.0f32, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0] {
        keys.extend_from_slice(&v.to_le_bytes());
    }
    let mut document: serde_json::Value = serde_json::from_slice(&triangle(1)).unwrap();
    document["buffers"].as_array_mut().unwrap().push(serde_json::json!({
        "byteLength": 32, "uri": format!("data:application/octet-stream;base64,{}", encode(&keys)) }));
    document["bufferViews"].as_array_mut().unwrap().extend([
        serde_json::json!({ "buffer": 1, "byteLength": 8 }),
        serde_json::json!({ "buffer": 1, "byteOffset": 8, "byteLength": 24 }),
    ]);
    document["accessors"].as_array_mut().unwrap().extend([
        serde_json::json!({ "bufferView": 2, "componentType": 5126, "count": 2, "type": "SCALAR" }),
        serde_json::json!({ "bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC3" }),
    ]);
    let animation = |name: &str, play: Option<&str>| {
        let mut a = serde_json::json!({ "name": name, "samplers": [ { "input": 2, "output": 3 } ],
            "channels": [ { "sampler": 0, "target": { "node": 0, "path": "translation" } } ] });
        if let Some(play) = play {
            a["extras"] = serde_json::json!({ "nebula": { "play": play } });
        }
        a
    };
    document["animations"] = serde_json::json!([animation("Plain", None), animation("Once", Some("once")),
        animation("Back", Some("once_then_first")), animation("Mirror", Some("loop_there_and_back"))]);
    let (_, clips) = to_ir(&serde_json::to_vec(&document).unwrap(), None).unwrap();
    let modes: Vec<(String, u8)> = clips.iter().map(|(name, clip)| (name.clone(), clip.loop_mode)).collect();
    assert_eq!(modes, [("Plain".to_string(), 2), ("Once".to_string(), 0), ("Back".to_string(), 1), ("Mirror".to_string(), 4)]);
    document["animations"] = serde_json::json!([animation("Odd", Some("sideways"))]);
    let Err(error) = to_ir(&serde_json::to_vec(&document).unwrap(), None) else { panic!("an unknown play mode cooked") };
    assert!(format!("{error:#}").contains("sideways"), "{error:#}");
}

#[test]
fn triangles_wind_as_the_games_do() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let (ir, _) = to_ir(&triangle(1), None).unwrap();
    assert_eq!(ir.shapes[0].groups[0].indices, vec![0, 2, 1]);
}

#[test]
fn a_rotation_becomes_the_games_angles() {
    // A quarter turn about Y.
    let half = std::f32::consts::FRAC_1_SQRT_2;
    let quarter = angles([0.0, half, 0.0, half]);
    assert!(quarter[0].abs() < 1e-6);
    assert!((quarter[1] - std::f32::consts::FRAC_PI_2).abs() < 0.001, "{quarter:?}");
    assert_eq!(angles([0.0, 0.0, 0.0, 1.0]), [0.0, 0.0, 0.0]);
}

#[test]
fn an_image_is_tiled_as_the_console_has_it() {
    let rgba: Vec<u8> = (0..16).flat_map(|i| [i as u8, 0x20, 0x30, 0xFF]).collect();
    let tiled = rgba8_tiled(4, 4, &rgba);
    assert_eq!(tiled.len(), 64);
    assert_eq!(&tiled[0..2], &[0xFF, 0]);
    assert_eq!(&tiled[2..4], &[0xFF, 1]);
    assert_eq!(&tiled[32..34], &[0x20, 0x30]);
}

#[test]
fn twelve_texture_layers_cook_without_losing_slots_or_coordinates() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let mut document: Value = serde_json::from_slice(&triangle(1)).unwrap();
    document["images"] = serde_json::json!((0..12).map(|_| serde_json::json!({"uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR4nGP4DwQACfsD/fteaysAAAAASUVORK5CYII="})).collect::<Vec<_>>());
    document["textures"] = serde_json::json!((0..12).map(|i| serde_json::json!({"source": i})).collect::<Vec<_>>());
    document["materials"] = serde_json::json!([{"extras": {"nebula": {"textures":
        (0..12).map(|i| serde_json::json!({"index": i, "texCoord": i})).collect::<Vec<_>>()}}}]);
    let (ir, _) = to_ir(&serde_json::to_vec(&document).unwrap(), None).unwrap();
    let material = &ir.materials[0];
    assert_eq!(material.stages.len(), 12);
    assert_eq!(material.coords.len(), 12);
    assert_eq!(material.textures[11], 11);
    let forms = super::super::programs::forms_of("fixture", material, 0).unwrap();
    let text = super::super::programs::generate(material, forms[0], false, &super::super::programs::Registers::of(material)).unwrap();
    assert_eq!(text.textures.len(), 12);
    assert!(text.inputs.contains(&super::super::programs::Input::Coord(11)));
    let block = super::super::model_write::write(&ir, "fixture", &super::super::programs::Written::unknown(), &mut Default::default()).unwrap();
    let word = |at| u32::from_le_bytes(block[at..at + 4].try_into().unwrap());
    assert_eq!(word(4), super::super::model_write::VERSION);
    let table = super::super::model_write::sections(&block).unwrap();
    let at = super::super::model_write::section(&table, b"MATL").unwrap().at as usize;
    assert_eq!(word(at + 40), 12); // MATL slot_count
    // MATL's first_slot (word 9) in SLOT.
    let slots = super::super::model_write::section(&table, b"SLOT").unwrap().at as usize
        + word(at + 36) as usize * super::super::model_write::SLOT_BYTES;
    for n in 0..12 {
        let slot = slots + n * super::super::model_write::SLOT_BYTES;
        assert_eq!(word(slot), n as u32);
        assert_eq!(block[slot + 6], super::super::model_write::SLOT_USES_MAP | super::super::model_write::SLOT_USES_COORD);
    }
}

#[test]
fn a_gltf_camera_is_a_camera_animation() {
    // A camera 5 m behind the origin turning from looking at it to looking left over one second, rolled 90 degrees at
    // the end; 40 degrees of view.
    let half = std::f32::consts::FRAC_PI_4.sin();
    let mut document = serde_json::json!({
        "asset": { "version": "2.0" }, "buffers": [], "bufferViews": [], "accessors": [],
        "nodes": [{ "camera": 0, "translation": [0, 0, 5] }],
        "cameras": [{ "type": "perspective", "perspective": { "yfov": 40f32.to_radians(), "znear": 0.1 } }],
        "animations": [{ "channels": [{ "sampler": 0, "target": { "node": 0, "path": "rotation" } }],
                         "samplers": [{ "input": 0, "output": 1 }] }]
    });
    // From no turn to a quarter turn about Y then a quarter turn about Z (its own view axis).
    let q_y = [0.0, half, 0.0, half];
    let q_z = [0.0, 0.0, half, half];
    let product = |a: [f32; 4], b: [f32; 4]| [
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
    ];
    let last = product(q_y, q_z);
    let views = add_floats(&mut document, &[(2, "SCALAR", vec![0.0, 1.0]), (2, "VEC4", [[0.0, 0.0, 0.0, 1.0], last].concat())]);
    document["animations"][0]["samplers"][0] = serde_json::json!({ "input": views[0], "output": views[1] });
    let ncam = super::super::camera::Ncam::from_ncam(&to_camera(document.to_string().as_bytes(), None).unwrap()).unwrap();
    assert_eq!(ncam.frames, 61);
    let read = |track: usize, frame: f32| super::super::camera::read(&ncam, track, frame).unwrap();
    // The eye stands still: one value each, in the game's units.
    assert_eq!(ncam.tracks[2].count, 1);
    assert!((read(2, 0.0) - 500.0).abs() < 1e-3);
    // At first it watches 10 m ahead, the origin's way, untwisted; at the end to its left (-X), twisted a quarter.
    assert!((read(5, 0.0) - (500.0 - 1000.0)).abs() < 1e-2, "{}", read(5, 0.0));
    assert!(read(6, 0.0).abs() < 1e-3);
    assert!((read(3, 60.0) - -1000.0).abs() < 1e-1, "{}", read(3, 60.0));
    assert!((read(6, 60.0).abs() - 90.0).abs() < 1e-2, "{}", read(6, 60.0));
    assert!((read(7, 30.0) - 40.0).abs() < 1e-3);
    // Each cause a modder can fix is named.
    let why = |doc: &serde_json::Value| refused(&to_camera(doc.to_string().as_bytes(), None).unwrap_err()).0;
    assert_eq!(why(&serde_json::json!({ "asset": { "version": "2.0" }, "nodes": [{}] })), "Cook.Camera.None");
    let mut other = document.clone();
    other["cameras"][0] = serde_json::json!({ "type": "orthographic", "orthographic": {} });
    assert_eq!(why(&other), "Cook.Camera.NotPerspective");
    let mut other = document.clone();
    other["cameras"][0]["perspective"] = serde_json::json!({ "znear": 0.1 });
    assert_eq!(why(&other), "Cook.Camera.NoFov");
    let mut other = document.clone();
    other["animations"] = serde_json::json!([]);
    assert_eq!(why(&other), "Cook.Camera.NoAnimation");
    let mut other = document.clone();
    other["nodes"] = serde_json::json!([{ "camera": 0, "translation": [0, 0, 5] }, { "children": [0] }]);
    other["animations"][0]["channels"].as_array_mut().unwrap().push(serde_json::json!({ "sampler": 0, "target": { "node": 1, "path": "rotation" } }));
    assert_eq!(why(&other), "Cook.Camera.ParentMoves");
    // A camera node that is its own parent (no glTF tree): refused, never a walk without end.
    let mut other = document.clone();
    other["nodes"][0]["children"] = serde_json::json!([0]);
    assert_eq!(refused(&to_camera(other.to_string().as_bytes(), None).unwrap_err()), ("Cook.Model.Invalid", vec![own(("where", "/nodes/0/children"))]));
    // Keys whose times fall, or with fewer values than keys: refused before a curve reads past its values.
    let mut other = document.clone();
    let views = add_floats(&mut other, &[(2, "SCALAR", vec![1.0, 0.0]), (1, "VEC4", vec![0.0, 0.0, 0.0, 1.0])]);
    other["animations"][0]["samplers"][0] = serde_json::json!({ "input": views[0], "output": 1 });
    assert_eq!(why(&other), "Cook.Model.Invalid");
    other["animations"][0]["samplers"][0] = serde_json::json!({ "input": 0, "output": views[1] });
    assert_eq!(why(&other), "Cook.Model.Invalid");
    // A key far past the longest clip.
    let mut other = document.clone();
    let views = add_floats(&mut other, &[(2, "SCALAR", vec![0.0, 1.0e30])]);
    other["animations"][0]["samplers"][0]["input"] = views[0].into();
    assert_eq!(why(&other), "Cook.Anim.TooLong");
}

#[test]
fn a_damaged_model_is_refused_by_name_never_a_panic() {
    let collision = |doc: &[u8]| refused(&to_collision(doc, None).unwrap_err()).0;
    // Solid ground whose triangles name vertices past the model's (found by fuzzing the cook).
    for case in [
        &include_bytes!("../../data/solid-vertex-past.kcl.glb")[..],
        &include_bytes!("../../data/solid-vertex-past-2.kcl.glb")[..],
        &include_bytes!("../../data/solid-vertex-past-3.kcl.glb")[..],
        &include_bytes!("../../data/solid-vertex-past-4.kcl.glb")[..],
    ] {
        assert_eq!(collision(case), "Cook.Model.VertexPast");
    }
    let base: Value = serde_json::from_slice(&triangle(1)).unwrap();
    let with = |edit: &dyn Fn(&mut Value)| {
        let mut doc = base.clone();
        edit(&mut doc);
        serde_json::to_vec(&doc).unwrap()
    };
    let max = serde_json::json!(u64::MAX);
    // Offsets, strides and counts at their largest, in the accessor, the view and the sparse values.
    let cases: [(&str, Box<dyn Fn(&mut Value)>, &str); 7] = [
        ("vertex past", Box::new(|d| d["accessors"][0]["count"] = 2.into()), "Cook.Model.VertexPast"),
        ("accessor offset", Box::new(|d| d["accessors"][0]["byteOffset"] = max.clone()), "Cook.Model.AccessorPast"),
        ("view offset", Box::new(|d| d["bufferViews"][0]["byteOffset"] = max.clone()), "Cook.Model.ViewPast"),
        ("stride", Box::new(|d| d["bufferViews"][0]["byteStride"] = max.clone()), "Cook.Model.AccessorPast"),
        ("index offset", Box::new(|d| d["accessors"][1]["byteOffset"] = max.clone()), "Cook.Model.AccessorPast"),
        ("sparse count", Box::new(|d| d["accessors"][0]["sparse"] = serde_json::json!({ "count": u64::MAX, "indices": { "bufferView": 1, "componentType": 5123 }, "values": { "bufferView": 0 } })), "Cook.Model.AccessorPast"),
        ("node cycle", Box::new(|d| d["nodes"][0]["children"] = serde_json::json!([0])), "Cook.Model.Invalid"),
    ];
    for (what, edit, phrase) in cases {
        let doc = with(&*edit);
        assert_eq!(collision(&doc), phrase, "{what}");
        assert_eq!(refused(&to_ir(&doc, None).err().unwrap()).0, phrase, "{what}: the model");
    }
    // A GLB whose chunk lengths run past the file.
    let mut glb = b"glTF".to_vec();
    glb.extend_from_slice(&2u32.to_le_bytes());
    glb.extend_from_slice(&64u32.to_le_bytes());
    glb.extend_from_slice(&u32::MAX.to_le_bytes());
    glb.extend_from_slice(b"JSON");
    glb.extend_from_slice(b"{}");
    assert_eq!(collision(&glb), "Cook.Model.Cut");
}

#[test]
fn a_bad_file_says_what_is_wrong() {
    assert_eq!(refused(&to_ir(b"nope", None).err().unwrap()).0, "Cook.Model.Json");
    let old = br#"{"asset":{"version":"1.0"}}"#;
    assert_eq!(refused(&to_ir(old, None).err().unwrap()), ("Cook.Model.Version", vec![own(("found", "1.0"))]));
    // A binary file whose header says it is longer than it is.
    let mut cut = b"glTF".to_vec();
    for word in [2u32, 1000, 100, 0x4E4F_534A] {
        cut.extend_from_slice(&word.to_le_bytes());
    }
    assert_eq!(refused(&to_ir(&cut, None).err().unwrap()).0, "Cook.Model.Cut");
}

/// One more buffer of little-endian floats, its views and accessors (`(count, type, floats)`), numbered after the
/// document's; the accessors' numbers.
fn add_floats(document: &mut Value, accessors: &[(usize, &str, Vec<f32>)]) -> Vec<usize> {
    let buffer = document["buffers"].as_array().unwrap().len();
    let mut data = Vec::new();
    let mut numbers = Vec::new();
    for (count, kind, floats) in accessors {
        let view = document["bufferViews"].as_array().unwrap().len();
        document["bufferViews"].as_array_mut().unwrap().push(serde_json::json!({ "buffer": buffer, "byteOffset": data.len(), "byteLength": floats.len() * 4 }));
        numbers.push(document["accessors"].as_array().unwrap().len());
        document["accessors"].as_array_mut().unwrap().push(serde_json::json!({ "bufferView": view, "componentType": 5126, "count": count, "type": kind }));
        for v in floats {
            data.extend_from_slice(&v.to_le_bytes());
        }
    }
    document["buffers"].as_array_mut().unwrap().push(serde_json::json!({
        "byteLength": data.len(), "uri": format!("data:application/octet-stream;base64,{}", encode(&data)) }));
    numbers
}

const PIXEL: &str = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR4nGP4DwQACfsD/fteaysAAAAASUVORK5CYII=";

/// The triangle with a texture of two images to choose from, and a texture transform.
fn textured(transform: Value) -> Value {
    let mut document: Value = serde_json::from_slice(&triangle(1)).unwrap();
    let a = add_floats(&mut document, &[(3, "VEC2", vec![0.0, 0.0, 1.0, 0.0, 0.0, 1.0])]);
    document["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"] = a[0].into();
    document["images"] = serde_json::json!([{ "uri": PIXEL }, { "uri": PIXEL }]);
    document["textures"] = serde_json::json!([{ "source": 0 }, { "source": 1 }]);
    document["materials"] = serde_json::json!([{ "name": "Face", "pbrMetallicRoughness": {
        "baseColorTexture": { "index": 0, "extensions": { "KHR_texture_transform": transform } } } }]);
    document
}

#[test]
fn a_material_s_colour_scroll_and_swaps_cook_as_the_games() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let mut document = textured(serde_json::json!({ "scale": [2.0, 2.0], "rotation": 0.5, "offset": [0.25, 0.0] }));
    let a = add_floats(&mut document, &[
        (2, "SCALAR", vec![0.0, 1.0]),
        (2, "VEC4", vec![1.0, 1.0, 1.0, 1.0, 1.0, 0.5, 0.0, 1.0]),
        (2, "VEC2", vec![0.0, 0.0, 1.0, 0.0]),
    ]);
    let pointer = |sampler: usize, to: &str| serde_json::json!({ "sampler": sampler,
        "target": { "path": "pointer", "extensions": { "KHR_animation_pointer": { "pointer": to } } } });
    document["animations"] = serde_json::json!([
        { "name": "Glow", "samplers": [{ "input": a[0], "output": a[1] }],
          "channels": [pointer(0, "/materials/0/pbrMetallicRoughness/baseColorFactor")] },
        { "name": "Scroll", "extras": { "nebula": { "play": "once" } }, "samplers": [{ "input": a[0], "output": a[2] }],
          "channels": [pointer(0, "/materials/0/pbrMetallicRoughness/baseColorTexture/extensions/KHR_texture_transform/offset")] },
        { "name": "Blink", "extras": { "nebula": { "texture_swaps": [{ "material": 0, "keys": [[0.0, 0], [0.5, 1]] }] } } },
    ]);
    let bytes = serde_json::to_vec(&document).unwrap();
    let (ir, clips) = to_ir(&bytes, None).unwrap();

    // The still transform: texture matrix 0 on the base texture, turned the game's way.
    let material = &ir.materials[0];
    assert_eq!(material.coords[0].matrix, 0);
    let m = &material.matrices[0];
    assert_eq!((m.slot, m.info, m.scale, m.rotation, m.translation), (0, 0, [2.0, 2.0], -0.5, [0.25, 0.0]));
    // The swapped-in image is a texture of the model's too.
    assert_eq!(ir.textures.len(), 2);

    let kinds: Vec<(&str, u8)> = clips.iter().map(|(name, c)| (name.as_str(), c.kind)).collect();
    assert_eq!(kinds, [("Glow", KIND_COLORS), ("Scroll", KIND_TEXTURE_MATRICES), ("Blink", KIND_PATTERN)]);
    let glow = &clips[0].1;
    assert_eq!((glow.frames, glow.loop_mode, glow.tracks.len(), glow.targets[0].name.as_slice()), (60, 2, 4, b"Face".as_slice()));
    // Red stays 255; green falls from 255 to linear half by the end, sRGB-encoded as the still factor is.
    assert_eq!((glow.tracks[0].count, glow.values[glow.tracks[0].at as usize]), (1, 255.0));
    let green = &glow.tracks[1];
    let last = green.at as usize + (green.count as usize - 1) * 3;
    assert_eq!((glow.values[last], glow.values[last + 1]), (60.0, srgb(0.5) * 255.0));
    assert!((glow.values[last + 1] - 187.5).abs() < 0.1, "{}", glow.values[last + 1]);
    let scroll = &clips[1].1;
    assert_eq!((scroll.loop_mode, scroll.tracks.len()), (0, 5));
    // Scale and turn stay as the material has them; the offset moves.
    assert_eq!(scroll.values[scroll.tracks[0].at as usize], 2.0);
    assert_eq!(scroll.values[scroll.tracks[2].at as usize], -0.5);
    assert!(scroll.tracks[3].count > 1);
    let blink = &clips[2].1;
    assert_eq!((blink.frames, blink.tracks[0].stride, blink.tracks[0].count), (30, STEP, 31));
    assert_eq!((blink.indices[0], blink.indices[29], blink.indices[30]), (0, 0, 1));

    // Each a file of its kind, by the animation's name.
    let (_, files) = to_files(&bytes, None, "Swap").unwrap();
    let names: Vec<&str> = files.iter().map(|(name, _)| name.as_str()).collect();
    assert_eq!(names, ["Glow.bpk", "Scroll.btk", "Blink.btp"]);
    for (_, file) in &files {
        assert_eq!(&file[0..4], b"NCLP");
    }
}

#[test]
fn what_a_material_animation_cannot_be_says_so() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let error = |document: &Value| refused(&to_ir(&serde_json::to_vec(document).unwrap(), None).err().expect("it cooked"));
    // Turned with uneven scales: glTF and the game scale and turn in different orders.
    let uneven = textured(serde_json::json!({ "scale": [2.0, 1.0], "rotation": 0.5 }));
    assert_eq!(error(&uneven), ("Cook.Model.UnevenTransform", vec![own(("material", "Face"))]));
    // Something else of a material.
    let mut other = textured(serde_json::json!({}));
    let a = add_floats(&mut other, &[(1, "SCALAR", vec![0.0]), (1, "VEC3", vec![1.0, 0.0, 0.0])]);
    other["animations"] = serde_json::json!([{ "name": "Shine", "samplers": [{ "input": a[0], "output": a[1] }],
        "channels": [{ "sampler": 0, "target": { "path": "pointer",
            "extensions": { "KHR_animation_pointer": { "pointer": "/materials/0/emissiveFactor" } } } }] }]);
    assert_eq!(error(&other), ("Cook.Clip.Pointer", vec![own(("animation", "Shine")), own(("pointer", "/materials/0/emissiveFactor"))]));
    // A swap of a material that isn't there.
    let mut missing = textured(serde_json::json!({}));
    missing["animations"] = serde_json::json!([{ "name": "Blink", "extras": { "nebula": { "texture_swaps": [{ "material": 3, "keys": [[0.0, 1]] }] } } }]);
    assert_eq!(error(&missing), ("Cook.Model.Invalid", vec![own(("where", "/animations/0/extras/nebula/texture_swaps"))]));
    // An extension the file needs and Nebula doesn't read.
    let mut draco = textured(serde_json::json!({}));
    draco["extensionsRequired"] = serde_json::json!(["KHR_draco_mesh_compression"]);
    let (why, args) = error(&draco);
    assert_eq!((why, &args[0]), ("Cook.Model.Extension", &own(("name", "KHR_draco_mesh_compression"))));
    assert!(args[1].1.contains("KHR_texture_transform"), "{args:?}");
    draco["extensionsRequired"] = serde_json::json!(["KHR_texture_transform"]);
    to_ir(&serde_json::to_vec(&draco).unwrap(), None).unwrap();
}

#[test]
fn signed_and_sparse_numbers_read_as_gltf_says() {
    // Two signed normalized shorts, then sparse: element 1 of a three-float accessor with no view is 7.
    let mut data = Vec::new();
    for v in [32767i16, -32768] {
        data.extend_from_slice(&v.to_le_bytes());
    }
    data.extend_from_slice(&1u16.to_le_bytes());
    data.extend_from_slice(&[0, 0]);
    data.extend_from_slice(&7.0f32.to_le_bytes());
    let json = serde_json::json!({ "asset": { "version": "2.0" },
        "buffers": [{ "byteLength": data.len(), "uri": format!("data:application/octet-stream;base64,{}", encode(&data)) }],
        "bufferViews": [{ "buffer": 0, "byteLength": 4 }, { "buffer": 0, "byteOffset": 4, "byteLength": 2 }, { "buffer": 0, "byteOffset": 8, "byteLength": 4 }],
        "accessors": [
            { "bufferView": 0, "componentType": 5122, "normalized": true, "count": 2, "type": "SCALAR" },
            { "componentType": 5126, "count": 3, "type": "SCALAR", "sparse": { "count": 1,
                "indices": { "bufferView": 1, "componentType": 5123 }, "values": { "bufferView": 2 } } },
        ] });
    let document = Document::parse(&serde_json::to_vec(&json).unwrap(), None).unwrap();
    assert_eq!(document.floats(0, 1).unwrap(), [1.0, -1.0]);
    assert_eq!(document.floats(1, 1).unwrap(), [0.0, 7.0, 0.0]);
}

#[test]
fn a_damaged_count_is_refused() {
    let json = serde_json::json!({ "asset": { "version": "2.0" },
        "accessors": [ { "componentType": 5126, "count": 1u64 << 40, "type": "SCALAR" } ] });
    let document = Document::parse(&serde_json::to_vec(&json).unwrap(), None).unwrap();
    assert!(document.floats(0, 1).is_err());
}

#[test]
fn an_image_past_the_largest_side_is_refused() {
    let mut png = Vec::new();
    {
        let mut encoder = png::Encoder::new(&mut png, MAX_IMAGE_SIDE + 1, 1);
        encoder.set_color(png::ColorType::Rgba);
        encoder.write_header().unwrap().write_image_data(&vec![0u8; (MAX_IMAGE_SIDE as usize + 1) * 4]).unwrap();
    }
    let error = decode_image(&png, "big.png").unwrap_err();
    let (side, max) = ((MAX_IMAGE_SIDE + 1).to_string(), MAX_IMAGE_SIDE.to_string());
    assert_eq!(refused(&error), ("Cook.Image.Side", vec![own(("image", "big.png")), own(("width", &side)), own(("height", "1")), own(("max", &max))]));
}

#[test]
fn a_jpeg_is_an_image() {
    // A 2 by 1 baseline JPEG of orange (200, 100, 50).
    let jpeg = base64("/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAIBAQEBAQIBAQECAgICAgQDAgICAgUEBAMEBgUGBgYFBgYGBwkIBgcJBwYGCAsICQoKCgoKBggLDAsKDAkKCgr/2wBDAQICAgICAgUDAwUKBwYHCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgr/wAARCAABAAIDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDPooor+Vz+2D//2Q==").unwrap();
    let (width, height, rgba) = decode_image(&jpeg, "orange.jpg").unwrap();
    assert_eq!((width, height, rgba.len()), (2, 1, 8));
    assert!(rgba[0].abs_diff(200) < 8 && rgba[1].abs_diff(100) < 8 && rgba[2].abs_diff(50) < 8 && rgba[3] == 255, "{rgba:?}");
    assert_eq!(refused(&decode_image(b"GIF89a", "a.gif").err().unwrap()), ("Cook.Image.Kind", vec![own(("image", "a.gif"))]));
}

fn own((name, value): (&'static str, &str)) -> (&'static str, String) {
    (name, value.to_string())
}

/// A refusal's phrase and arguments; every phrase has its row and every placeholder of the row is given.
fn refused(error: &anyhow::Error) -> (&'static str, Vec<(&'static str, String)>) {
    let finding = super::super::refuse::refusal_of(error).unwrap_or_else(|| panic!("not a refusal: {error:#}"));
    assert_eq!(finding.code, super::super::refuse::COOK_FAILED);
    let mut args = finding.args.iter();
    let Some(("why", luma_validate::Arg::Phrase(why))) = args.next() else { panic!("no why: {finding:?}") };
    let words = luma_validate::reason(finding, "UsEnglish");
    assert!(!words.contains('{'), "{why}: a placeholder with no argument: {words}");
    for language in ["UsFrench", "JpJapanese", "KrKorean"] {
        assert!(!luma_validate::reason(finding, language).contains('{'), "{why} in {language}");
    }
    let rest = args
        .map(|(name, value)| match value {
            luma_validate::Arg::Text(text) => (*name, text.clone()),
            other => panic!("{why}: {name} is {other:?}"),
        })
        .collect();
    (why, rest)
}

/// The notes said so far on this thread.
fn notes() -> Vec<(&'static str, Vec<(&'static str, String)>)> {
    super::super::refuse::take_notes()
        .into_iter()
        .map(|f| {
            let mut args = f.args.into_iter();
            let Some(("why", luma_validate::Arg::Phrase(why))) = args.next() else { panic!("no why") };
            (why, args.map(|(n, v)| (n, if let luma_validate::Arg::Text(t) = v { t } else { String::new() })).collect())
        })
        .collect()
}

#[test]
fn every_cook_phrase_has_a_row_in_every_language() {
    let sources = [include_str!("gltf.rs"), include_str!("overlay.rs"), include_str!("refuse.rs")];
    let mut seen = 0;
    for source in sources {
        for piece in source.split('"').skip(1).step_by(2) {
            if piece.starts_with("Cook.") && piece.split('.').count() == 3 && piece.chars().all(|c| c.is_ascii_alphanumeric() || c == '.') {
                seen += 1;
                for language in ["UsEnglish", "UsFrench", "EuGerman", "JpJapanese", "CnSimpChinese"] {
                    let words = luma_validate::phrase(piece, language);
                    assert_ne!(words, format!("{{{piece}}}"), "no row Validator.{piece} ({language})");
                }
            }
        }
    }
    assert!(seen > 40, "{seen}");
}

fn document_of(bytes: &[u8]) -> Value {
    serde_json::from_slice(bytes).unwrap()
}

fn cook(document: &Value, game: Option<&GameModel>) -> Result<(ModelIr, Clips, ModelFacts)> {
    to_ir_for(&serde_json::to_vec(document).unwrap(), None, game)
}

#[test]
fn points_lines_and_past_vertices_are_refused_by_mesh() {
    let mut lines = document_of(&triangle(1));
    lines["meshes"][0]["name"] = "Rope".into();
    lines["meshes"][0]["primitives"][0]["mode"] = 1.into();
    assert_eq!(refused(&cook(&lines, None).err().unwrap()), ("Cook.Model.NotTriangles", vec![own(("mesh", "Rope")), own(("mode", "1"))]));
    let mut past = document_of(&triangle(1));
    past["accessors"][0]["count"] = 2.into();
    assert_eq!(refused(&cook(&past, None).err().unwrap()), ("Cook.Model.VertexPast", vec![own(("mesh", "#0")), own(("vertex", "2")), own(("count", "2"))]));
    let mut unknown = document_of(&triangle(1));
    unknown["meshes"][0]["primitives"][0]["mode"] = 9.into();
    assert_eq!(refused(&cook(&unknown, None).err().unwrap()), ("Cook.Model.Invalid", vec![own(("where", "/meshes/0/primitives/0/mode"))]));
}

#[test]
fn strips_and_fans_are_triangles_as_gltf_says() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    assert_eq!(triangles_of(5, &[0, 1, 2, 3, 4], "").unwrap(), [0, 1, 2, 1, 3, 2, 2, 3, 4]);
    assert_eq!(triangles_of(6, &[0, 1, 2, 3], "").unwrap(), [1, 2, 0, 2, 3, 0]);
    assert_eq!(triangles_of(4, &[0, 1, 2, 3], "").unwrap(), [0, 1, 2]);
    let mut strip = document_of(&triangle(1));
    strip["meshes"][0]["primitives"][0]["mode"] = 5.into();
    let (strip_ir, _, strip_facts) = cook(&strip, None).unwrap();
    let (ir, _, facts) = cook(&document_of(&triangle(1)), None).unwrap();
    assert_eq!((strip_facts.vertices, strip_facts.meshes), (facts.vertices, facts.meshes));
    assert_eq!(strip_ir.arrays.positions, ir.arrays.positions);
}

#[test]
fn a_mesh_with_no_normals_is_flat_and_says_so() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    notes();
    let (_, _, facts) = cook(&document_of(&triangle(2)), None).unwrap();
    // Once for the mesh, though both its primitives (each a mesh of the model) lack them.
    assert_eq!(notes(), [("Cook.Model.FlatNormals", vec![own(("mesh", "#0"))])]);
    assert_eq!(facts.meshes, 2);
}

#[test]
fn vertex_colours_are_srgb_bytes_and_three_sets_are_refused() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let mut document = document_of(&triangle(1));
    let a = add_floats(&mut document, &[(3, "VEC4", vec![1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0]), (3, "VEC3", vec![0.5; 9])]);
    document["meshes"][0]["primitives"][0]["attributes"]["COLOR_0"] = a[0].into();
    let (ir, _, _) = cook(&document, None).unwrap();
    assert!(ir.arrays.colors[0].contains(&0xFF00_00FF), "{:x?}", ir.arrays.colors[0]);
    assert!(ir.arrays.colors[1].is_empty());
    // Linear 0.5 is sRGB 188.
    document["meshes"][0]["primitives"][0]["attributes"]["COLOR_1"] = a[1].into();
    let (ir, _, _) = cook(&document, None).unwrap();
    assert!(ir.arrays.colors[1].contains(&0xBCBC_BCFF), "{:x?}", ir.arrays.colors[1]);
    document["meshes"][0]["primitives"][0]["attributes"]["COLOR_2"] = a[1].into();
    assert_eq!(refused(&cook(&document, None).err().unwrap()), ("Cook.Model.ColorSets", vec![own(("mesh", "#0")), own(("count", "3")), own(("max", "2"))]));
}

/// The triangle on a node `body` with `children` nodes (name, extras) under it.
fn with_nodes(children: &[(&str, Value)]) -> Value {
    let mut document = document_of(&triangle(1));
    let mut nodes = vec![serde_json::json!({ "name": "body", "mesh": 0, "children": (1..=children.len()).collect::<Vec<_>>() })];
    for (name, extras) in children {
        nodes.push(serde_json::json!({ "name": name, "translation": [0.0, 1.0, 0.0], "extras": extras }));
    }
    document["nodes"] = Value::Array(nodes);
    document
}

#[test]
fn a_gltf_metre_is_a_hundred_units() {
    // The 3208 model: one node a metre up, a metre cube hanging from it to the origin.
    let (ir, _, facts) = to_ir_for(include_bytes!("../../data/metre.gltf"), None, None).unwrap();
    assert_eq!((facts.joints, ir.joints.len(), ir.joints[0].name.as_str()), (1, 1, "metre"));
    assert_eq!(ir.joints[0].translation, [0.0, 100.0, 0.0]);
    // The cube in its joint's space hangs 100 down; at rest in the model it stands from 0 to 100.
    let low = ir.arrays.positions.iter().fold([f32::MAX; 3], |m, p| [m[0].min(p[0]), m[1].min(p[1]), m[2].min(p[2])]);
    let high = ir.arrays.positions.iter().fold([f32::MIN; 3], |m, p| [m[0].max(p[0]), m[1].max(p[1]), m[2].max(p[2])]);
    assert_eq!((low, high), ([-50.0, -100.0, -50.0], [50.0, 0.0, 50.0]));
    assert_eq!((ir.joints[0].min[1], ir.joints[0].max[1]), (0.0, 100.0));
}

#[test]
fn the_metal_smooth_sphere_is_lit_as_metal() {
    // The conformance model metal-smooth (docs/modding/how-tos/writing-material-shaders.md): a 1 m sphere, metal 1, roughness
    // 0.05.
    let source = include_bytes!("../../data/metal-smooth.gltf");
    let (ir, _, facts) = to_ir_for(source, None, None).unwrap();
    assert_eq!((facts.joints, ir.joints[0].translation), (1, [0.0, 50.0, 0.0]));
    assert_eq!((ir.pbr.len(), ir.pbr[0].metal, ir.pbr[0].roughness, ir.pbr[0].base_color), (1, 1.0, 0.05, [0.95, 0.95, 0.95, 1.0]));
    // A metre across: every vertex 50 units from the joint, and the joint's bounds from 0 to 100 up.
    assert!(ir.arrays.positions.iter().all(|p| ((p[0] * p[0] + p[1] * p[1] + p[2] * p[2]).sqrt() - 50.0).abs() < 1e-3));
    assert!((ir.joints[0].min[1] - 0.0).abs() < 1e-3 && (ir.joints[0].max[1] - 100.0).abs() < 1e-3, "{:?} {:?}", ir.joints[0].min, ir.joints[0].max);
    // Its lit form takes metal 1 and roughness 0.05 (program_pbr.hlsli).
    let lit = ir.materials[0].pbr.expect("a lit glTF material");
    assert_eq!((lit.metal, lit.roughness), (1.0, 0.05));
}

/// A glTF material's `extras.nebula.ray` (the shelved ray tracing's rows): taken whatever it holds and not read, so the
/// model is the same byte for byte, and the cook notes Cook.RayData once.
#[test]
fn a_materials_ray_values_are_not_read() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let mut document = document_of(&triangle(1));
    let a = add_floats(&mut document, &[(3, "VEC4", vec![1.0, 0.5, 0.25, 1.0, 0.5, 1.0, 0.25, 1.0, 0.25, 0.5, 1.0, 1.0])]);
    document["meshes"][0]["primitives"][0]["attributes"]["COLOR_0"] = a[0].into();
    let with = |ray: Value| {
        let mut d = document.clone();
        d["materials"][0]["extras"] = serde_json::json!({ "nebula": { "ray": ray } });
        d
    };
    let model_of = |d: &Value| to_files(&serde_json::to_vec(d).unwrap(), None, "raymat").unwrap().0;
    let notes = || crate::cook::refuse::take_notes().into_iter().filter(|f| f.args.first().is_some_and(|(_, a)| matches!(a, luma_validate::Arg::Phrase("Cook.RayData")))).count();
    crate::cook::refuse::take_notes();
    let plain = model_of(&document);
    assert_eq!(notes(), 0);
    for given in [
        serde_json::json!({}),
        serde_json::json!({"vertex_colour": "light"}),
        serde_json::json!({"ior": 1.33, "thickness": 40.0, "body": 0.5, "shine": 2.0, "tint_keep": 0.25, "traced": "hidden",
            "sun_through": 0.5, "metal_kind": "gold", "glow": {"color": [1.0, 0.5, 0.0], "candela": 100.0}}),
        // Values the checks of the shelved rows refused: a key a material did not take, out of range, no object.
        serde_json::json!({"class": "metal"}),
        serde_json::json!({"ior": 0.5, "vertex_colour": "paint"}),
        serde_json::json!({"glow": {"color": "own"}}),
        serde_json::json!(3),
    ] {
        assert!(model_of(&with(given.clone())) == plain, "{given}");
        assert_eq!(notes(), 1, "{given}");
    }
    assert!(model_of(&with(Value::Null)) == plain);
    assert_eq!(notes(), 0);
}

#[test]
fn sockets_billboards_and_joint_names() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let (ir, _, facts) = cook(&with_nodes(&[("head", Value::Null), ("socket:hand", Value::Null)]), None).unwrap();
    let names: Vec<&str> = ir.joints.iter().map(|j| j.name.as_str()).collect();
    assert_eq!(names, ["body", "head", "hand"]);
    assert_eq!((ir.joints[2].parent, facts.joints, facts.extra_joints), (0, 3, 0));
    let twice = cook(&with_nodes(&[("socket:hand", Value::Null), ("socket:hand", Value::Null)]), None).err().unwrap();
    assert_eq!(refused(&twice), ("Cook.Model.SocketTwice", vec![own(("name", "hand"))]));
    let joint = cook(&with_nodes(&[("head", Value::Null), ("socket:head", Value::Null)]), None).err().unwrap();
    assert_eq!(refused(&joint), ("Cook.Model.SocketIsJoint", vec![own(("name", "head"))]));
    let same = cook(&with_nodes(&[("body", Value::Null)]), None).err().unwrap();
    assert_eq!(refused(&same), ("Cook.Model.JointTwice", vec![own(("joint", "body"))]));
    let facing = serde_json::json!({ "nebula": { "billboard": "xy" } });
    let (ir, _, _) = cook(&with_nodes(&[("leaf", facing)]), None).unwrap();
    assert_eq!((ir.joints[0].billboard, ir.joints[1].billboard), (0, 1));
    let bad = cook(&with_nodes(&[("leaf", serde_json::json!({ "nebula": { "billboard": "z" } }))]), None).err().unwrap();
    assert_eq!(refused(&bad), ("Cook.Model.Billboard", vec![own(("node", "leaf")), own(("value", "\"z\""))]));
}

/// A joint or material name outside ASCII is refused by name: the model keeps names one byte a character, its
/// ray-hit forms in UTF-8. A mesh's name is not kept, and a texture is named by its image's number.
#[test]
fn a_name_the_model_cannot_keep_is_refused() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let joint = cook(&with_nodes(&[("tête", Value::Null)]), None).err().unwrap();
    assert_eq!(refused(&joint), ("Cook.Model.JointName", vec![own(("joint", "tête"))]));
    let mut material = document_of(&triangle(1));
    material["materials"][0]["name"] = "金".into();
    assert_eq!(refused(&cook(&material, None).err().unwrap()), ("Cook.Model.MaterialName", vec![own(("material", "金"))]));
    let mut mesh = document_of(&triangle(1));
    mesh["meshes"][0]["name"] = "金".into();
    assert!(cook(&mesh, None).is_ok());
}

fn game(joints: &[&str]) -> GameModel {
    GameModel {
        path: "ObjectData/Kuribo.arc/Kuribo.bdl".into(),
        joints: joints.iter().map(|j| j.to_string()).collect(),
        parents: vec![],
        materials: vec!["body_v".into(), "eye_v".into()],
        textures: vec!["kuribo_body".into(), "kuribo_eye".into(), "kuribo_eye.closed".into()],
    }
}

#[test]
fn joints_are_numbered_as_the_game_model_has_them() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let document = with_nodes(&[("tail", Value::Null), ("head", Value::Null)]);
    let (ir, _, facts) = cook(&document, Some(&game(&["body", "head"]))).unwrap();
    let names: Vec<&str> = ir.joints.iter().map(|j| j.name.as_str()).collect();
    assert_eq!(names, ["body", "head", "tail"]);
    assert_eq!((ir.joints[1].parent, ir.joints[2].parent), (0, 0));
    assert_eq!((facts.replaced.as_str(), facts.joints, facts.extra_joints), ("ObjectData/Kuribo.arc/Kuribo.bdl", 2, 1));
    // The game's joints in another order than the file's.
    let (ir, _, _) = cook(&document, Some(&game(&["body", "head", "tail"]))).unwrap();
    assert_eq!(ir.joints.iter().map(|j| j.name.as_str()).collect::<Vec<_>>(), ["body", "head", "tail"]);
    let missing = cook(&document, Some(&game(&["body", "neck"]))).err().unwrap();
    assert_eq!(refused(&missing), ("Cook.Model.MissingJoint", vec![own(("joint", "neck")), own(("model", "ObjectData/Kuribo.arc/Kuribo.bdl"))]));
    // A one-joint game model: the root is its joint, whatever the file calls it.
    let (ir, _, facts) = cook(&document_of(&triangle(1)), Some(&game(&["Kuribo"]))).unwrap();
    assert_eq!((ir.joints[0].name.as_str(), facts.joints, facts.extra_joints), ("Kuribo", 1, 0));
    let clash = cook(&document, Some(&game(&["head"]))).err().unwrap();
    assert_eq!(refused(&clash), ("Cook.Model.JointTwice", vec![own(("joint", "head"))]));
}

/// A three-joint arm (body, upper hanging 1 m up from it, lower 1 m up from upper) whose `Wave` turns `upper` a
/// quarter about Z in a second; `mesh`: the body draws the triangle (a model), else the file is animations only.
fn arm(mesh: bool, times: [f32; 2], last_z: f32) -> Value {
    let mut document = document_of(&triangle(1));
    document["nodes"] = serde_json::json!([
        { "name": "body", "mesh": 0, "children": [1] },
        { "name": "upper", "translation": [0.0, 1.0, 0.0], "children": [2] },
        { "name": "lower", "translation": [0.0, 1.0, 0.0] },
    ]);
    if !mesh {
        document["nodes"][0].as_object_mut().unwrap().remove("mesh");
        document.as_object_mut().unwrap().remove("meshes");
    }
    let half = std::f32::consts::FRAC_PI_4.sin();
    let views = add_floats(&mut document, &[(2, "SCALAR", times.to_vec()), (2, "VEC4", vec![0.0, 0.0, 0.0, 1.0, 0.0, 0.0, last_z, half])]);
    document["animations"] = serde_json::json!([{ "name": "Wave", "samplers": [{ "input": views[0], "output": views[1] }],
        "channels": [{ "sampler": 0, "target": { "node": 1, "path": "rotation" } }] }]);
    document
}

fn arm_model(parents: &[Option<usize>]) -> GameModel {
    GameModel { parents: parents.to_vec(), ..game(&["body", "upper", "lower"]) }
}

fn clip_files(document: &Value, game: &GameModel) -> Result<(ClipFiles, ModelFacts)> {
    to_clip_files(&serde_json::to_vec(document).unwrap(), None, "ObjectData/Kuribo.arc/Arm.anim.glb", game)
}

#[test]
fn an_animation_file_makes_the_clips_its_model_would() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let half = std::f32::consts::FRAC_PI_4.sin();
    let game = arm_model(&[None, Some(0), Some(1)]);
    // The same skeleton and animation cooked inside a model standing in for the game's: the clip is the same bytes.
    let (_, from_model, _) = to_files_for(&serde_json::to_vec(&arm(true, [0.0, 1.0], half)).unwrap(), None, "Kuribo", Some(&game)).unwrap();
    let (files, facts) = clip_files(&arm(false, [0.0, 1.0], half), &game).unwrap();
    assert_eq!(files.iter().map(|(n, _)| n.as_str()).collect::<Vec<_>>(), ["Wave.bck"]);
    assert_eq!(files, from_model);
    assert_eq!((facts.replaced.as_str(), facts.joints, facts.clips, facts.meshes), ("ObjectData/Kuribo.arc/Kuribo.bdl", 3, 1, 0));
    // Without the game's parents (an older reader) nothing is checked against them.
    assert_eq!(clip_files(&arm(false, [0.0, 1.0], half), &arm_model(&[])).unwrap().0, from_model);
}

#[test]
fn a_hidden_model_keeps_every_joint_and_draws_nothing() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    // The game's model: the arm with its triangle, as the store keeps it.
    let game = arm_model(&[None, Some(0), Some(1)]);
    let (block, _, _) = to_files_for(&serde_json::to_vec(&arm(true, [0.0, 1.0], 0.5)).unwrap(), None, "Kuribo", None).unwrap();
    let read = GameModel::read(&game.path, &block).unwrap();
    assert_eq!(read.joints, game.joints);
    // Hidden: the same joints in the same order, no meshes or materials (the section table's counts: nebula_model.h).
    let skeleton = skeleton_gltf(&read, &block).unwrap();
    let (hidden, facts) = hidden_files_for(&skeleton, "Kuribo", &read).unwrap();
    assert_eq!(GameModel::read(&game.path, &hidden).unwrap().joints, game.joints);
    let table = super::super::model_write::sections(&hidden).unwrap();
    let count = |name: &[u8; 4]| super::super::model_write::section(&table, name).unwrap().count;
    assert_eq!((count(b"JNTS"), count(b"MESH"), count(b"MATL")), (3, 0, 0));
    assert_eq!((facts.joints, facts.extra_joints, facts.meshes), (3, 0, 0));
    // An ordinary model with no triangles is still refused.
    let bare = to_files_for(&skeleton, None, "Kuribo", Some(&read)).err().unwrap();
    assert_eq!(refused(&bare).0, "Cook.Model.NoTriangles");
}

#[test]
fn an_animation_file_says_what_it_cannot_make() {
    let half = std::f32::consts::FRAC_PI_4.sin();
    let game = arm_model(&[None, Some(0), Some(1)]);
    let model = "ObjectData/Kuribo.arc/Kuribo.bdl";
    let fails = |document: &Value, game: &GameModel| refused(&clip_files(document, game).expect_err("refused"));
    let document = arm(false, [0.0, 1.0], half);
    let hand = GameModel { joints: vec!["body".into(), "upper".into(), "hand".into()], ..game.clone() };
    assert_eq!(fails(&document, &hand), ("Cook.Model.MissingJoint", vec![own(("joint", "hand")), own(("model", model))]));
    assert_eq!(fails(&document, &arm_model(&[None, Some(0), Some(0)])), ("Cook.Anim.ParentDiffers", vec![own(("joint", "lower")), own(("parent", "body")), own(("found", "upper"))]));
    assert_eq!(fails(&document, &arm_model(&[Some(1), None, Some(1)])), ("Cook.Anim.ParentMissing", vec![own(("joint", "body")), own(("parent", "upper"))]));
    assert_eq!(fails(&document, &arm_model(&[None, None, Some(1)])), ("Cook.Anim.RootDiffers", vec![own(("joint", "upper")), own(("found", "body"))]));
    let mut prop = document.clone();
    prop["nodes"].as_array_mut().unwrap().push(serde_json::json!({ "name": "prop" }));
    prop["nodes"][0]["children"] = serde_json::json!([1, 3]);
    prop["animations"][0]["channels"][0]["target"]["node"] = 3.into();
    assert_eq!(fails(&prop, &game), ("Cook.Anim.NotAJoint", vec![own(("animation", "Wave")), own(("node", "prop")), own(("model", model))]));
    let mut weights = document.clone();
    weights["animations"][0]["channels"][0]["target"]["path"] = "weights".into();
    assert_eq!(fails(&weights, &game), ("Cook.Anim.OnlyJoints", vec![own(("animation", "Wave")), own(("path", "weights"))]));
    let mut pointer = document.clone();
    pointer["animations"][0]["channels"][0]["target"] = serde_json::json!({ "path": "pointer",
        "extensions": { "KHR_animation_pointer": { "pointer": "/nodes/1/translation" } } });
    assert_eq!(fails(&pointer, &game), ("Cook.Anim.OnlyJoints", vec![own(("animation", "Wave")), own(("path", "/nodes/1/translation"))]));
    let mut unnamed = document.clone();
    unnamed["animations"][0].as_object_mut().unwrap().remove("name");
    assert_eq!(fails(&unnamed, &game), ("Cook.Anim.NoName", vec![own(("animation", "#0"))]));
    let mut twice = document.clone();
    let wave = twice["animations"][0].clone();
    twice["animations"].as_array_mut().unwrap().push(wave);
    assert_eq!(fails(&twice, &game), ("Cook.Anim.Twice", vec![own(("name", "Wave"))]));
    let mut none = document.clone();
    none.as_object_mut().unwrap().remove("animations");
    assert_eq!(fails(&none, &game), ("Cook.Anim.None", vec![own(("file", "ObjectData/Kuribo.arc/Arm.anim.glb"))]));
}

/// The arm with a material `name`, an image `image` named so, and a clip `Glow` moving that material's colour and
/// texture offset and swapping its texture (layer 4).
fn glowing_arm(name: &str, image: Value) -> Value {
    let mut document = arm(false, [0.0, 1.0], std::f32::consts::FRAC_PI_4.sin());
    document["materials"] = serde_json::json!([{ "name": "unused" }, { "name": name }]);
    document["images"] = serde_json::json!([image]);
    document["textures"] = serde_json::json!([{ "source": 0 }]);
    let views = add_floats(&mut document, &[(2, "SCALAR", vec![0.0, 0.5]), (2, "VEC4", vec![1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 1.0]), (2, "VEC2", vec![0.0, 0.0, 0.5, 0.0])]);
    let pointer = |p: &str| serde_json::json!({ "path": "pointer", "extensions": { "KHR_animation_pointer": { "pointer": p } } });
    document["animations"].as_array_mut().unwrap().push(serde_json::json!({ "name": "Glow",
        "samplers": [{ "input": views[0], "output": views[1] }, { "input": views[0], "output": views[2] }],
        "channels": [
            { "sampler": 0, "target": pointer("/materials/1/pbrMetallicRoughness/baseColorFactor") },
            { "sampler": 1, "target": pointer("/materials/1/pbrMetallicRoughness/baseColorTexture/extensions/KHR_texture_transform/offset") },
        ],
        "extras": { "nebula": { "texture_swaps": [{ "material": 1, "keys": [[0.0, 0], [0.25, 0]] }] } } }));
    document
}

/// A cooked clip's first target's material number (clip.rs: the targets' place is word 8; a target's material is
/// its third word), and for a pattern clip its first index.
fn first_target(clip: &[u8]) -> (u32, Option<u32>) {
    let word = |at: usize| u32::from_le_bytes(clip[at..at + 4].try_into().unwrap());
    let pattern = clip[12] == super::super::clip::KIND_PATTERN;
    (word(word(32) as usize + 8), pattern.then(|| word(word(36) as usize)))
}

#[test]
fn an_animation_file_moves_the_game_models_materials_by_name() {
    let game = arm_model(&[None, Some(0), Some(1)]);
    let (files, facts) = clip_files(&glowing_arm("EYE_V", serde_json::json!({ "uri": "tex/kuribo_eye.closed.png" })), &game).unwrap();
    // Wave moves joints; Glow only materials, so the game's joint clip Glow (if any) stays.
    let names: Vec<&str> = files.iter().map(|(n, _)| n.as_str()).collect();
    assert_eq!(names, ["Wave.bck", "Glow.bpk", "Glow.btk", "Glow.btp"]);
    assert_eq!((facts.materials, facts.clips), (1, 4));
    // Each material clip names the game's material eye_v (its number 1, its name as the game writes it), and the swap
    // the game's texture kuribo_eye.closed (number 2), found by the image's file name less its last ending.
    for (name, clip) in &files[1..] {
        let (material, index) = first_target(clip);
        assert_eq!(material, 1, "{name}");
        assert!(clip.windows(5).any(|w| w == b"eye_v"), "{name}");
        if name.ends_with(".btp") {
            assert_eq!(index, Some(2));
        }
    }
    // The image's own name wins over its file's.
    let named = clip_files(&glowing_arm("eye_v", serde_json::json!({ "name": "kuribo_eye", "uri": "x.png" })), &game).unwrap().0;
    assert_eq!(first_target(&named[3].1).1, Some(1));
    let fails = |document: &Value| refused(&clip_files(document, &game).expect_err("refused"));
    assert_eq!(
        fails(&glowing_arm("lid", serde_json::json!({ "name": "kuribo_eye" }))),
        ("Cook.Anim.NoGameMaterial", vec![own(("material", "lid")), own(("model", "ObjectData/Kuribo.arc/Kuribo.bdl")), own(("materials", "body_v, eye_v"))])
    );
    assert_eq!(
        fails(&glowing_arm("eye_v", serde_json::json!({ "name": "kuribo_wink" }))),
        (
            "Cook.Anim.NoGameTexture",
            vec![own(("texture", "kuribo_wink")), own(("model", "ObjectData/Kuribo.arc/Kuribo.bdl")), own(("textures", "kuribo_body, kuribo_eye, kuribo_eye.closed"))]
        )
    );
    assert_eq!(
        fails(&glowing_arm("eye_v", serde_json::json!({ "uri": "data:image/png;base64,AAAA" }))),
        ("Cook.Anim.TextureUnnamed", vec![own(("texture", "/textures/0")), own(("model", "ObjectData/Kuribo.arc/Kuribo.bdl"))])
    );
    // A pointer to a material part no clip moves is the model's own refusal.
    let mut other = glowing_arm("eye_v", serde_json::json!({ "name": "kuribo_eye" }));
    other["animations"][1]["channels"][0]["target"]["extensions"]["KHR_animation_pointer"]["pointer"] = "/materials/1/emissiveStrength".into();
    // A game model read without its names (a store from an older import).
    let old = GameModel { materials: vec![], textures: vec![], ..game.clone() };
    let old_store = refused(&clip_files(&glowing_arm("eye_v", serde_json::json!({ "name": "kuribo_eye" })), &old).expect_err("refused"));
    assert_eq!(old_store, ("Cook.Anim.ModelUnread", vec![own(("model", "ObjectData/Kuribo.arc/Kuribo.bdl"))]));
    assert_eq!(fails(&other).0, "Cook.Clip.Pointer");
    // The material an animation names must be in the file.
    let mut missing = glowing_arm("eye_v", serde_json::json!({ "name": "kuribo_eye" }));
    missing["animations"][1]["channels"][0]["target"]["extensions"]["KHR_animation_pointer"]["pointer"] = "/materials/5/pbrMetallicRoughness/baseColorFactor".into();
    assert_eq!(fails(&missing).0, "Cook.Model.Invalid");
}

#[test]
fn a_clip_that_is_no_number_or_too_long_is_refused() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let half = std::f32::consts::FRAC_PI_4.sin();
    let game = arm_model(&[None, Some(0), Some(1)]);
    // A key that is NaN, in an animation file and in a model alike.
    let nan = arm(false, [0.0, 1.0], f32::NAN);
    assert_eq!(refused(&clip_files(&nan, &game).err().unwrap()), ("Cook.Anim.NotANumber", vec![own(("animation", "Wave"))]));
    let nan_model = arm(true, [0.0, 1.0], f32::NAN);
    assert_eq!(refused(&cook(&nan_model, Some(&game)).err().unwrap()), ("Cook.Anim.NotANumber", vec![own(("animation", "Wave"))]));
    let infinite_time = arm(false, [0.0, f32::INFINITY], half);
    assert_eq!(refused(&clip_files(&infinite_time, &game).err().unwrap()), ("Cook.Anim.NotANumber", vec![own(("animation", "Wave"))]));
    // 2000 seconds: past the 65,535 frames a clip counts (refused before a sample is taken).
    let long = arm(false, [0.0, 2000.0], half);
    assert_eq!(refused(&clip_files(&long, &game).err().unwrap()), ("Cook.Anim.TooLong", vec![own(("animation", "Wave")), own(("seconds", "2000.0")), own(("max", "1092.2"))]));
    // A clip of no length is one frame.
    let (files, _) = clip_files(&arm(false, [0.0, 0.0], half), &game).unwrap();
    // The block's fifth word is its frame count (clip::write).
    assert_eq!(u32::from_le_bytes(files[0].1[16..20].try_into().unwrap()), 1);
}

#[test]
fn a_cooked_model_reads_back_as_a_game_model() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let (model, _) = to_files(&serde_json::to_vec(&with_nodes(&[("head", Value::Null)])).unwrap(), None, "Kuribo").unwrap();
    let read = GameModel::read("ObjectData/Kuribo.arc/Kuribo.bdl", &model).unwrap();
    assert_eq!(read.joints, ["body", "head"]);
    assert_eq!(read.parents, [None, Some(0)]);
    assert!(GameModel::read("x", &model[..40]).is_none());
    assert!(GameModel::read("x", b"not a model").is_none());
}

#[test]
fn skins_are_one_and_weigh_four() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let mut document = with_nodes(&[("head", Value::Null)]);
    document["skins"] = serde_json::json!([{ "joints": [0, 1] }, { "joints": [1] }]);
    document["nodes"][0]["skin"] = 0.into();
    assert_eq!(refused(&cook(&document, None).err().unwrap()), ("Cook.Model.SkinNoWeights", vec![own(("mesh", "#0"))]));
    document["meshes"] = serde_json::json!([document["meshes"][0].clone(), document["meshes"][0].clone()]);
    document["nodes"][1]["mesh"] = 1.into();
    document["nodes"][1]["skin"] = 1.into();
    assert_eq!(refused(&cook(&document, None).err().unwrap()), ("Cook.Model.TwoSkins", vec![own(("mesh", "#0")), own(("other", "#1"))]));
}

/// The triangle with a full lit material: every factor, every map (metal-rough and occlusion on set 1, the normal
/// map on set 2), glow strength 4.
fn lit_document() -> Value {
    let mut document = document_of(&triangle(1));
    let a = add_floats(&mut document, &[
        (3, "VEC2", vec![0.0, 0.0, 1.0, 0.0, 0.0, 1.0]),
        (3, "VEC2", vec![0.5; 6]),
        (3, "VEC2", vec![0.25; 6]),
    ]);
    for (set, accessor) in a.iter().enumerate() {
        document["meshes"][0]["primitives"][0]["attributes"][format!("TEXCOORD_{set}")] = (*accessor).into();
    }
    document["images"] = serde_json::json!([{ "uri": PIXEL }, { "uri": PIXEL }, { "uri": PIXEL }]);
    document["textures"] = serde_json::json!([{ "source": 0 }, { "source": 1 }, { "source": 2 }]);
    document["materials"] = serde_json::json!([{ "name": "Shell",
        "pbrMetallicRoughness": { "baseColorFactor": [1.0, 0.5, 0.25, 1.0], "baseColorTexture": { "index": 0 },
            "metallicFactor": 0.2, "roughnessFactor": 0.6, "metallicRoughnessTexture": { "index": 1, "texCoord": 1 } },
        "normalTexture": { "index": 2, "texCoord": 2, "scale": 0.5 },
        "emissiveTexture": { "index": 0 }, "emissiveFactor": [0.5, 0.25, 0.0],
        "occlusionTexture": { "index": 1, "texCoord": 1, "strength": 0.7 },
        "alphaMode": "MASK", "alphaCutoff": 0.3, "doubleSided": true,
        "extensions": { "KHR_materials_emissive_strength": { "emissiveStrength": 4.0 } } }]);
    document
}

#[test]
fn a_lit_material_keeps_every_factor_and_map() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let (ir, _, _) = cook(&lit_document(), None).unwrap();
    assert_eq!(ir.pbr.len(), ir.materials.len());
    let lit = &ir.pbr[0];
    assert_eq!(lit.base_color, [1.0, 0.5, 0.25, 1.0]);
    assert_eq!(lit.emissive, [2.0, 1.0, 0.0]);
    assert_eq!((lit.metal, lit.roughness, lit.normal_scale, lit.occlusion_strength, lit.alpha_cutoff), (0.2, 0.6, 0.5, 0.7, 0.3));
    assert_eq!((lit.alpha_mode, lit.flags), (PBR_MASK, PBR_DOUBLE_SIDED));
    assert_eq!(lit.coords, [0, 1, 2, 0, 1]);
    // One texture per image, whichever maps read it.
    assert_eq!((lit.maps[0], lit.maps[1]), (lit.maps[3], lit.maps[4]));
    let mut numbers = vec![lit.maps[0], lit.maps[1], lit.maps[2]];
    numbers.sort_unstable();
    numbers.dedup();
    assert_eq!(numbers.len(), 3);
    assert!(numbers.iter().all(|n| (*n as usize) < ir.textures.len()), "{numbers:?} of {}", ir.textures.len());

    // The model file: this version, its header and every block inside the file.
    let (model, _) = to_files(&serde_json::to_vec(&lit_document()).unwrap(), None, "Shell").unwrap();
    let word = |at: usize| u32::from_le_bytes(model[at..at + 4].try_into().unwrap());
    assert_eq!((word(0), word(4), word(8) as usize), (super::super::model_write::MAGIC, super::super::model_write::VERSION, model.len()));
    let table = super::super::model_write::sections(&model).unwrap();
    assert!(table.iter().all(|s| (s.at + s.size) as usize <= model.len()));

    // A material with nothing said is glTF's default; a file with no material has no glTF materials.
    let (plain, _, _) = cook(&document_of(&triangle(1)), None).unwrap();
    assert_eq!(plain.pbr, [PbrMaterial::default()]);
    let (none, _, _) = cook(&document_of(&many_triangles(1)), None).unwrap();
    assert!(none.pbr.is_empty());
}

#[test]
fn a_lit_material_is_drawn_by_the_pbr_template() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    // The lit template (program_pbr.hlsli) lights the stages' base colour by metal and roughness, in
    // every form, before the glow; its maps are slots of their own, an ORM texture one slot.
    use super::super::programs::{generate, forms_of, Registers};
    let (ir, _, _) = cook(&lit_document(), None).unwrap();
    let m = &ir.materials[0];
    assert_eq!(m.pbr, Some(PbrShading { metal: 0.2, roughness: 0.6, metal_rough: Some(3), occlusion: Some((3, 0.7)), glow_last: true }));
    assert_eq!(super::super::model_write::pbr_slots(m), [3]);
    let forms = forms_of("Shell", m, 0).unwrap();
    assert!(forms.len() >= 2, "{forms:?}");
    for form in &forms {
        let t = generate(m, *form, false, &Registers::of(m)).unwrap();
        let px = &t.pixel;
        for wanted in ["#include \"program_pbr.hlsli\"", "pbr_begin(", "pbr_add_light(i.lit_light, 0u",
                       "ENV_FETCH(pbr_reflection(", "pbr_finish(prev.rgb", "pbr_metal *= pbr_mr.b", "pbr_ao = 1.0 + 0.7"] {
            assert!(px.contains(wanted), "{form:?} lacks {wanted}:\n{px}");
        }
        // Not the game's enhanced finish: the template's own; and the light before the glow (stage 1).
        assert!(!px.contains("enh_finish"), "{px}");
        assert!(px.find("pbr_finish").unwrap() < px.find("NATIVE_STAGE(1,").unwrap(), "{px}");
        assert!(px.find("NATIVE_STAGE(0,").unwrap() < px.find("pbr_finish").unwrap(), "{px}");
        // The stages see the material colour unlit: the light is the template's.
        assert!(px.contains("lit0.rgb = "), "{px}");
        assert!(t.textures.contains(&3), "{:?}", t.textures);
    }
    // The plain form is the lit one: a mod's metal looks the same with any lighting setting.
    let texts: Vec<String> = forms.iter().map(|f| generate(m, *f, false, &Registers::of(m)).unwrap().pixel).collect();
    assert!(texts.windows(2).all(|w| w[0] == w[1]));

    // No glow: the light after the last stage. Unlit: no template.
    let mut black = lit_document();
    black["materials"][0]["emissiveFactor"] = serde_json::json!([0.0, 0.0, 0.0]);
    let (ir, _, _) = cook(&black, None).unwrap();
    let m = &ir.materials[0];
    assert_eq!(m.pbr.map(|p| p.glow_last), Some(false));
    let px = generate(m, forms_of("Shell", m, 0).unwrap()[0], false, &Registers::of(m)).unwrap().pixel;
    assert!(px.find("NATIVE_STAGE(0,").unwrap() < px.find("pbr_finish").unwrap(), "{px}");
    let mut unlit = lit_document();
    unlit["materials"][0]["extensions"]["KHR_materials_unlit"] = serde_json::json!({});
    let (ir, _, _) = cook(&unlit, None).unwrap();
    assert_eq!(ir.materials[0].pbr, None);

    // Occlusion through another set: a slot of its own.
    let mut own = lit_document();
    own["materials"][0]["occlusionTexture"]["texCoord"] = 2.into();
    let (ir, _, _) = cook(&own, None).unwrap();
    let p = ir.materials[0].pbr.unwrap();
    assert_eq!((p.metal_rough, p.occlusion.map(|o| o.0)), (Some(3), Some(4)));

    // The conformance sphere: metal 1, roughness 0.05, no maps, no glow.
    let (ir, _, _) = to_ir_for(include_bytes!("../../data/metal-smooth.gltf"), None, None).unwrap();
    assert_eq!(ir.materials[0].pbr, Some(PbrShading { metal: 1.0, roughness: 0.05, metal_rough: None, occlusion: None, glow_last: false }));
}

/// The template's texts for a compile on both back ends (dxc DXIL and SPIR-V):
/// NEBULA_PBR_DUMP=<folder> cargo test -p nebula-import --lib -- --ignored dump_the_pbr_template
#[test]
#[ignore]
fn dump_the_pbr_template() {
    use super::super::programs::{finish_text, forms_of, generate, Registers};
    let Ok(dir) = std::env::var("NEBULA_PBR_DUMP") else { return };
    let dir = std::path::PathBuf::from(dir);
    std::fs::create_dir_all(&dir).unwrap();
    let mut black = lit_document();
    black["materials"][0]["emissiveFactor"] = serde_json::json!([0.0, 0.0, 0.0]);
    let mut vertex_coloured = lit_document();
    vertex_coloured["meshes"][0]["primitives"][0]["attributes"]["COLOR_0"] =
        add_floats(&mut vertex_coloured, &[(3, "VEC4", vec![1.0; 12])])[0].into();
    let sources: [(&str, Vec<u8>); 4] = [
        ("lit", serde_json::to_vec(&lit_document()).unwrap()),
        ("black", serde_json::to_vec(&black).unwrap()),
        ("coloured", serde_json::to_vec(&vertex_coloured).unwrap()),
        ("metal", include_bytes!("../../data/metal-smooth.gltf").to_vec()),
    ];
    for (name, source) in sources {
        let (ir, _, _) = to_ir_for(&source, None, None).unwrap();
        let m = &ir.materials[0];
        for (k, form) in forms_of("Shell", m, 0).unwrap().into_iter().enumerate() {
            for skinned in [false, true] {
                let mut t = generate(m, form, skinned, &Registers::of(m)).unwrap();
                finish_text(&m.name, form, &mut t);
                let stem = format!("{name}-{k}{}", if skinned { "s" } else { "" });
                std::fs::write(dir.join(format!("{stem}.vs.hlsl")), &t.vertex).unwrap();
                std::fs::write(dir.join(format!("{stem}.ps.hlsl")), &t.pixel).unwrap();
            }
        }
    }
}

#[test]
fn a_glow_is_one_more_stage_and_factors_are_srgb() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    // MOD-MATERIALS-FC.md 1: the raster's glTF material is stages; the glow adds emissive texture x constant 1.
    let (ir, _, _) = cook(&lit_document(), None).unwrap();
    let m = &ir.materials[0];
    // Base colour factor (1, 0.5, 0.25) linear is sRGB (255, 188, 137); alpha is a coverage, kept.
    assert_eq!(m.material_color[0], [255, 188, 137, 255]);
    assert_eq!((m.stages.len(), m.pixel_stage_count), (2, 2));
    let glow = &m.stages[1];
    // previous + texture x constant; alpha stays the previous stage's; constant colour 1 (selector 13).
    assert_eq!((glow.color_in, glow.alpha_in, glow.color_constant), ([15, 8, 14, 0], [7, 7, 7, 0], 13));
    assert_eq!((glow.map, glow.coord, glow.channel), (1, 1, 255));
    // The glow 0.5 x 4, 0.25 x 4, 0 is held to 1: (255, 255, 0).
    assert_eq!(m.constants[1], [255, 255, 0, 255]);
    // Its map is the emissive texture, read through the emissive map's own set (0 here), slot 1 = map 1 = coord 1;
    // the normal map's slot is 2 (set 2); the lit template's metal-roughness and occlusion map (one image, one set)
    // slot 3.
    assert_eq!((m.textures.len(), m.textures[1], m.coordinate_maps.as_slice()), (4, ir.pbr[0].maps[PBR_EMISSIVE], [0, 1, 2, 3].as_slice()));
    assert_eq!((m.coords[1].kind, m.coords[1].source, m.coords[1].matrix), (1, 4, NONE));
    assert_eq!((m.textures[2], m.coords[2].source, m.normal_map), (ir.pbr[0].maps[PBR_NORMAL], 6, Some((2, 0.5))));

    // A glow with no map adds constant 1 alone; a material with no base texture has the glow's map as map 0.
    let mut factor_only = lit_document();
    factor_only["materials"][0].as_object_mut().unwrap().remove("emissiveTexture");
    let (ir, _, _) = cook(&factor_only, None).unwrap();
    let glow = &ir.materials[0].stages[1];
    assert_eq!((glow.color_in, glow.map, glow.coord), ([14, 15, 15, 0], 255, 255));
    let mut no_base = lit_document();
    no_base["materials"][0]["pbrMetallicRoughness"].as_object_mut().unwrap().remove("baseColorTexture");
    no_base["materials"][0]["emissiveTexture"]["texCoord"] = 2.into();
    let (ir, _, _) = cook(&no_base, None).unwrap();
    let m = &ir.materials[0];
    assert_eq!((m.stages[0].map, m.stages[1].map, m.stages[1].coord, m.textures.len()), (255, 0, 0, 3));
    assert_eq!((m.coords.len(), m.coords[0].source, m.coordinate_maps.as_slice(), m.normal_map), (3, 6, [0, 1, 2].as_slice(), Some((1, 0.5))));

    // No glow stage when the glow is black or the material unlit (it shows its base colour alone).
    let mut black = lit_document();
    black["materials"][0]["emissiveFactor"] = serde_json::json!([0.0, 0.0, 0.0]);
    let mut unlit = lit_document();
    unlit["materials"][0]["extensions"]["KHR_materials_unlit"] = serde_json::json!({});
    // Black: the base, normal and metal-roughness maps; unlit: the base map alone, no normal map either.
    for (document, textures, normal) in [(black, 3, Some((1, 0.5))), (unlit, 1, None)] {
        let (ir, _, _) = cook(&document, None).unwrap();
        let m = &ir.materials[0];
        assert_eq!((m.stages.len(), m.textures.len(), m.normal_map), (1, textures, normal));
    }
    assert_eq!(colour_bytes([0.0, 0.003, 0.5, 0.5]), [0, 10, 188, 128]);
}

#[test]
fn a_normal_map_turns_the_lit_form_s_pixel_normal() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    use super::super::programs::{forms_of, generate, Input, Registers};
    // MOD-MATERIALS-FC.md 1: inside generate(), behind the material's normal map; no second generator.
    let (ir, _, _) = cook(&lit_document(), None).unwrap();
    let m = &ir.materials[0];
    let forms = forms_of("fixture", m, 0).unwrap();
    let lit = forms.iter().find(|f| f.lit && f.replace == 0).copied().expect("a lit form");
    let text = generate(m, lit, false, &Registers::of(m)).unwrap();
    // The vertex turns the tangent and bitangent as the normal; the pixel turns its normal before enh_begin.
    assert!(text.inputs.contains(&Input::Tangent) && text.inputs.contains(&Input::Binormal), "{:?}", text.inputs);
    assert!(text.vertex.contains("o.lit_tan = lit_normal(p, v.tangent);") && text.vertex.contains("o.lit_bin = lit_normal(p, v.binormal);"));
    assert!(text.vertex.contains("lit_tan : TEXCOORD") && text.pixel.contains("lit_bin : TEXCOORD"));
    let turned = text.pixel.find("i.lit_nrm = (nm_t * nm.x + nm_b * nm.y) * 0.5 + nm_n * nm.z;").expect(&text.pixel);
    assert!(turned < text.pixel.find("g_enh = enh_begin(").unwrap());
    // It reads the normal map's own slot: the program's textures name slot 2.
    assert!(text.textures.contains(&2), "{:?}", text.textures);
    // A glTF material lights at the pixel in its plain form too (the material template): every form reads it.
    for form in forms.iter().filter(|f| !f.lit) {
        let text = generate(m, *form, false, &Registers::of(m)).unwrap();
        assert!(text.pixel.contains("lit_tan") && text.inputs.contains(&Input::Tangent));
    }
    // Its slot is marked as read, so its texture is bound.
    let block = super::super::model_write::write(&ir, "fixture", &super::super::programs::Written::unknown(), &mut Default::default()).unwrap();
    let word = |at: usize| u32::from_le_bytes(block[at..at + 4].try_into().unwrap());
    let table = super::super::model_write::sections(&block).unwrap();
    let at = super::super::model_write::section(&table, b"MATL").unwrap().at as usize;
    // MATL's first_slot (word 9) in SLOT.
    let slots = super::super::model_write::section(&table, b"SLOT").unwrap().at as usize
        + word(at + 36) as usize * super::super::model_write::SLOT_BYTES;
    let slot = slots + 2 * super::super::model_write::SLOT_BYTES;
    assert_eq!(block[slot + 6], super::super::model_write::SLOT_USES_MAP | super::super::model_write::SLOT_USES_COORD);
}

#[test]
fn a_vertex_coloured_mesh_s_animated_factor_is_its_constant_colour() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    // The factor of a vertex-coloured mesh is constant colour 0 (its channel takes the vertex's colour): an animated
    // factor is a register animation's constant 0 too, by the same name, beside the material colour's.
    let mut document = textured(serde_json::json!({ "offset": [0.0, 0.0] }));
    let a = add_floats(&mut document, &[
        (2, "SCALAR", vec![0.0, 1.0]),
        (2, "VEC4", vec![1.0, 1.0, 1.0, 1.0, 1.0, 0.5, 0.0, 1.0]),
        (3, "VEC4", vec![1.0; 12]),
    ]);
    document["meshes"][0]["primitives"][0]["attributes"]["COLOR_0"] = a[2].into();
    document["animations"] = serde_json::json!([{ "name": "Glow", "samplers": [{ "input": a[0], "output": a[1] }],
        "channels": [{ "sampler": 0, "target": { "path": "pointer",
            "extensions": { "KHR_animation_pointer": { "pointer": "/materials/0/pbrMetallicRoughness/baseColorFactor" } } } }] }]);
    let (ir, clips) = to_ir(&serde_json::to_vec(&document).unwrap(), None).unwrap();
    assert_eq!(ir.materials[0].channels[0].material_source, 1);
    let kinds: Vec<(&str, u8)> = clips.iter().map(|(name, c)| (name.as_str(), c.kind)).collect();
    assert_eq!(kinds, [("Glow", KIND_COLORS), ("Glow", KIND_REGISTERS)]);
    let (colours, constants) = (&clips[0].1, &clips[1].1);
    assert_eq!((constants.first_count, constants.second_count, constants.tracks.len()), (0, 1, 4));
    assert_eq!((constants.targets[0].name.as_slice(), constants.targets[0].slot), (b"Face".as_slice(), 0));
    assert_eq!(constants.targets[0].material, colours.targets[0].material);
    // The same curves: green's last key is the material colour's.
    let (c, k) = (&colours.tracks[1], &constants.tracks[1]);
    let last = |clip: &ClipIr, t: &Track| clip.values[t.at as usize + (t.count as usize - 1) * 3 + 1];
    assert_eq!(last(colours, c), last(constants, k));
    assert_eq!(clip_extension(KIND_REGISTERS), "brk");
}

fn material_notes() -> Vec<(&'static str, Vec<(&'static str, String)>)> {
    notes().into_iter().filter(|(why, _)| why.starts_with("Cook.Material.")).collect()
}

#[test]
fn a_lit_material_says_what_it_cannot_be() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let shell = || own(("material", "Shell"));
    let mut missing = lit_document();
    missing["textures"][1]["source"] = 9.into();
    assert_eq!(
        refused(&cook(&missing, None).err().unwrap()),
        ("Cook.Material.MissingTexture", vec![shell(), own(("map", "metallicRoughnessTexture")), own(("texture", "1"))])
    );
    let mut no_set = lit_document();
    no_set["meshes"][0]["primitives"][0]["attributes"].as_object_mut().unwrap().remove("TEXCOORD_1");
    assert_eq!(
        refused(&cook(&no_set, None).err().unwrap()),
        ("Cook.Material.CoordSet", vec![shell(), own(("map", "metallicRoughnessTexture")), own(("set", "1")), own(("mesh", "#0"))])
    );
    // A transform of a map on a set another map reads can't be baked into it.
    let mut shared = lit_document();
    shared["materials"][0]["occlusionTexture"]["extensions"] = serde_json::json!({ "KHR_texture_transform": { "offset": [0.5, 0.0] } });
    assert_eq!(
        refused(&cook(&shared, None).err().unwrap()),
        ("Cook.Material.TransformShared", vec![shell(), own(("map", "occlusionTexture")), own(("set", "1"))])
    );

    // Notes: the normal map's own transform is baked, factors past glTF's bounds are brought in.
    material_notes();
    let mut noted = lit_document();
    noted["materials"][0]["normalTexture"]["extensions"] = serde_json::json!({ "KHR_texture_transform": { "scale": [2.0, 2.0], "offset": [0.5, 0.0] } });
    noted["materials"][0]["pbrMetallicRoughness"]["metallicFactor"] = 1.5.into();
    noted["materials"][0]["occlusionTexture"]["strength"] = (-1.0).into();
    let (ir, _, _) = cook(&noted, None).unwrap();
    assert_eq!((ir.pbr[0].metal, ir.pbr[0].occlusion_strength), (1.0, 0.0));
    assert_eq!(material_notes(), [
        ("Cook.Material.Clamped", vec![shell(), own(("factor", "metallicFactor")), own(("found", "1.5"))]),
        ("Cook.Material.Clamped", vec![shell(), own(("factor", "occlusionTexture.strength")), own(("found", "-1"))]),
        ("Cook.Material.TextureTransform", vec![shell(), own(("map", "normalTexture"))]),
    ]);
    // Set 2 was 0.25 everywhere: scaled by 2 and moved by 0.5 in u.
    let (plain, _, _) = cook(&lit_document(), None).unwrap();
    assert_eq!((plain.arrays.coords[2].as_slice(), ir.arrays.coords[2].as_slice()), ([[0.25, 0.25]].as_slice(), [[1.0, 0.5]].as_slice()));
    assert_eq!(plain.arrays.coords[1], ir.arrays.coords[1]);
    assert_eq!(transformed_coordinate(&[2.0, 2.0, 0.0, 0.5, 0.0], [0.25, 0.25]), [1.0, 0.5]);
    let [u, v] = transformed_coordinate(&[1.0, 1.0, std::f32::consts::FRAC_PI_2, 0.0, 0.0], [1.0, 0.0]);
    assert!(u.abs() < 1e-6 && (v + 1.0).abs() < 1e-6, "{u} {v}");

    // KHR_materials_unlit, and both material extensions may be required.
    let mut unlit = lit_document();
    unlit["materials"][0]["extensions"]["KHR_materials_unlit"] = serde_json::json!({});
    unlit["extensionsRequired"] = serde_json::json!(["KHR_materials_unlit", "KHR_materials_emissive_strength"]);
    let (ir, _, _) = cook(&unlit, None).unwrap();
    assert_eq!(ir.pbr[0].flags, PBR_DOUBLE_SIDED | PBR_UNLIT);
}

/// The triangle (0,0,0) (1,0,0) (0,1,0) with a normal map read through coordinates `uv` (by vertex).
fn normal_mapped(uv: [f32; 6]) -> Value {
    let mut document = document_of(&triangle(1));
    let a = add_floats(&mut document, &[(3, "VEC2", uv.to_vec())]);
    document["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"] = a[0].into();
    document["images"] = serde_json::json!([{ "uri": PIXEL }]);
    document["textures"] = serde_json::json!([{ "source": 0 }]);
    document["materials"] = serde_json::json!([{ "name": "Bumps", "normalTexture": { "index": 0 } }]);
    document
}

/// Each corner's normal, tangent and bitangent as cooked, rounded to thousandths.
fn frames(ir: &ModelIr) -> Vec<[[f32; 3]; 3]> {
    let group = &ir.shapes[0].groups[0];
    assert_eq!(group.attributes & (ATTR_NBT | ATTR_NBT_THREE), ATTR_NBT | ATTR_NBT_THREE);
    let at = |n: u32| ir.arrays.normals[n as usize].map(|v| (v * 1000.0).round() / 1000.0 + 0.0);
    group.corners.iter().map(|c| [at(c.normal), at(c.tangent), at(c.binormal)]).collect()
}

#[test]
fn a_normal_map_has_tangents_from_the_file_or_mikktspace() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    // No normals, no tangents: flat normals and MikkTSpace's frame, u along x. A v that runs down y turns the
    // bitangent (glTF 2.0: bitangent = cross(normal, tangent) x w).
    let (ir, _, _) = cook(&normal_mapped([0.0, 0.0, 1.0, 0.0, 0.0, 1.0]), None).unwrap();
    assert_eq!(frames(&ir), [[[0.0, 0.0, 1.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]]; 3]);
    let (ir, _, _) = cook(&normal_mapped([0.0, 0.0, 1.0, 0.0, 0.0, -1.0]), None).unwrap();
    assert_eq!(frames(&ir), [[[0.0, 0.0, 1.0], [1.0, 0.0, 0.0], [0.0, -1.0, 0.0]]; 3]);
    // u along y: the tangent follows it.
    let (ir, _, _) = cook(&normal_mapped([0.0, 0.0, 0.0, 1.0, 1.0, 0.0]), None).unwrap();
    assert_eq!(frames(&ir)[0][1], [0.0, 1.0, 0.0]);

    // The file's own, w -1: bitangent = -(z x y) = x. Made square to the normal.
    let mut own_frame = normal_mapped([0.0, 0.0, 1.0, 0.0, 0.0, 1.0]);
    let a = add_floats(&mut own_frame, &[(3, "VEC3", [0.0, 0.0, 1.0].repeat(3)), (3, "VEC4", [0.0, 1.0, 0.5, -1.0].repeat(3))]);
    own_frame["meshes"][0]["primitives"][0]["attributes"]["NORMAL"] = a[0].into();
    own_frame["meshes"][0]["primitives"][0]["attributes"]["TANGENT"] = a[1].into();
    let (ir, _, _) = cook(&own_frame, None).unwrap();
    assert_eq!(frames(&ir), [[[0.0, 0.0, 1.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0]]; 3]);

    // No normal map: no tangents, the stream stays as it was.
    let (ir, _, _) = cook(&document_of(&triangle(1)), None).unwrap();
    assert_eq!(ir.shapes[0].groups[0].attributes & ATTR_NBT, 0);
}

#[test]
fn a_mirroring_node_keeps_its_faces_and_normals_outward() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    let (plain, _, _) = cook(&document_of(&triangle(1)), None).unwrap();
    let mut mirrored = document_of(&triangle(1));
    mirrored["nodes"][0]["scale"] = serde_json::json!([-1.0, 1.0, 1.0]);
    let (ir, _, _) = cook(&mirrored, None).unwrap();
    // The game draws clockwise: glTF's counter-clockwise triangle turned, its mirrored one (already clockwise) not.
    assert_eq!((plain.shapes[0].groups[0].indices.as_slice(), ir.shapes[0].groups[0].indices.as_slice()), ([0, 2, 1].as_slice(), [0, 1, 2].as_slice()));

    // Normals by the inverse transpose; a mirror keeps them outward and turns the bitangent's side.
    let mirror: Matrix = [-1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0];
    assert!(determinant(&mirror) < 0.0);
    assert_eq!(normal_through(&mirror, [1.0, 0.0, 0.0]), [-1.0, 0.0, 0.0]);
    assert_eq!(normal_through(&mirror, [0.0, 0.0, 1.0]), [0.0, 0.0, 1.0]);
    // Stretched 2 along x: the normal of the plane x + y = 1 leans away from x (inverse transpose), not toward it.
    let stretch: Matrix = [2.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0];
    let n = normal_through(&stretch, [1.0, 1.0, 0.0]);
    assert!(n[1] > n[0], "{n:?}");
    assert_eq!(square_to([0.0, 0.0, 1.0], [0.0, 0.0, 3.0]), [0.0, 1.0, 0.0]);
}

