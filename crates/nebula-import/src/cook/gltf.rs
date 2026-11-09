//! A modern model in place of the game's own format: glTF 2.0 (`.gltf` with its files, or `.glb`) read straight into
//! `ModelIr` and `ClipIr`, with a clip for each animation. Triangles, one texture and colour per material, skins and node
//! animation. Lengths are metres in glTF: one metre is 100 of the game's units. A glTF triangle winds counter-clockwise;
//! the game's wind clockwise (measured on Kuribo, Coin and Mario: 650 to 2, 268 to 0, 4514 to 363).

use super::clip::{ClipIr, Track, KIND_JOINTS};
use super::model_ir::CoordIr;
use super::model_ir::{important_joints, plain_material, plain_texture, ArraysIr, Corner, GroupIr, JointIr, ModelIr, ShapeIr, ATTR_MATRIX, ATTR_NORMAL, NONE};
use anyhow::{bail, Context, Result};
use serde_json::Value;
use std::collections::BTreeMap;
use std::path::Path;

const UNITS_PER_METRE: f32 = 100.0;

struct Document {
    root: Value,
    buffers: Vec<Vec<u8>>,
    folder: Option<std::path::PathBuf>,
}

fn base64(text: &str) -> Result<Vec<u8>> {
    let mut out = Vec::with_capacity(text.len() / 4 * 3);
    let mut bits = 0u32;
    let mut count = 0;
    for byte in text.bytes() {
        let value = match byte {
            b'A'..=b'Z' => byte - b'A',
            b'a'..=b'z' => byte - b'a' + 26,
            b'0'..=b'9' => byte - b'0' + 52,
            b'+' => 62,
            b'/' => 63,
            b'=' => break,
            b'\r' | b'\n' => continue,
            _ => bail!("not base64"),
        };
        bits = (bits << 6) | u32::from(value);
        count += 1;
        if count == 4 {
            out.extend_from_slice(&bits.to_be_bytes()[1..]);
            bits = 0;
            count = 0;
        }
    }
    match count {
        2 => out.push((bits >> 4) as u8),
        3 => {
            out.push((bits >> 10) as u8);
            out.push((bits >> 2) as u8);
        }
        _ => {}
    }
    Ok(out)
}

fn read_uri(uri: &str, folder: Option<&Path>) -> Result<Vec<u8>> {
    if let Some(rest) = uri.strip_prefix("data:") {
        let (_, data) = rest.split_once(";base64,").context("a data address that isn't base64")?;
        return base64(data);
    }
    if uri.contains("..") || uri.starts_with('/') || uri.contains(':') {
        bail!("{uri} leaves the model's folder");
    }
    let folder = folder.context("the model's other files aren't known")?;
    std::fs::read(folder.join(uri)).with_context(|| format!("{uri} can't be read"))
}

impl Document {
    fn parse(bytes: &[u8], folder: Option<&Path>) -> Result<Self> {
        let (json, chunk): (Vec<u8>, Option<Vec<u8>>) = if bytes.starts_with(b"glTF") {
            let word = |at: usize| -> Result<usize> {
                Ok(u32::from_le_bytes(bytes.get(at..at + 4).context("a cut-off .glb")?.try_into()?) as usize)
            };
            let json_size = word(12)?;
            let json = bytes.get(20..20 + json_size).context("a cut-off .glb")?.to_vec();
            let next = 20 + json_size;
            let chunk = if next + 8 <= bytes.len() {
                let size = word(next)?;
                Some(bytes.get(next + 8..next + 8 + size).context("a cut-off .glb")?.to_vec())
            } else {
                None
            };
            (json, chunk)
        } else {
            (bytes.to_vec(), None)
        };
        let root: Value = serde_json::from_slice(&json).context("its JSON can't be read")?;
        if root["asset"]["version"].as_str().is_none_or(|v| !v.starts_with('2')) {
            bail!("only glTF 2 can be read");
        }
        let mut buffers = Vec::new();
        for (i, buffer) in root["buffers"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
            buffers.push(match buffer["uri"].as_str() {
                Some(uri) => read_uri(uri, folder)?,
                None if i == 0 => chunk.clone().context("the .glb has no binary chunk")?,
                None => bail!("buffer {i} has no data"),
            });
        }
        Ok(Self { root, buffers, folder: folder.map(Path::to_path_buf) })
    }

    fn view(&self, index: usize) -> Result<&[u8]> {
        let view = &self.root["bufferViews"][index];
        let buffer = view["buffer"].as_u64().unwrap_or(0) as usize;
        let start = view["byteOffset"].as_u64().unwrap_or(0) as usize;
        let length = view["byteLength"].as_u64().context("a buffer view with no length")? as usize;
        self.buffers.get(buffer).and_then(|b| b.get(start..start + length)).context("a buffer view past its buffer")
    }

    /// An accessor's numbers, each element as `width` floats.
    fn floats(&self, index: usize, width: usize) -> Result<Vec<f32>> {
        let accessor = &self.root["accessors"][index];
        let count = accessor["count"].as_u64().context("an accessor with no count")? as usize;
        let kind = accessor["componentType"].as_u64().context("an accessor with no type")?;
        let normalized = accessor["normalized"].as_bool().unwrap_or(false);
        let (size, read): (usize, fn(&[u8]) -> f32) = match kind {
            5126 => (4, |b| f32::from_le_bytes([b[0], b[1], b[2], b[3]])),
            5121 => (1, |b| f32::from(b[0])),
            5123 => (2, |b| f32::from(u16::from_le_bytes([b[0], b[1]]))),
            5125 => (4, |b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]) as f32),
            other => bail!("numbers of type {other}"),
        };
        let view = self.view(accessor["bufferView"].as_u64().context("a sparse or empty accessor")? as usize)?;
        let start = accessor["byteOffset"].as_u64().unwrap_or(0) as usize;
        let stride = self.root["bufferViews"][accessor["bufferView"].as_u64().unwrap_or(0) as usize]["byteStride"]
            .as_u64()
            .map_or(size * width, |s| s as usize);
        let mut out = Vec::with_capacity(count * width);
        for element in 0..count {
            for c in 0..width {
                let at = start + element * stride + c * size;
                let bytes = view.get(at..at + size).context("an accessor past its buffer view")?;
                let value = read(bytes);
                out.push(if normalized && kind == 5121 {
                    value / 255.0
                } else if normalized && kind == 5123 {
                    value / 65535.0
                } else {
                    value
                });
            }
        }
        Ok(out)
    }

    fn indices(&self, primitive: &Value, vertices: usize) -> Result<Vec<u32>> {
        match primitive["indices"].as_u64() {
            Some(index) => {
                let a = &self.root["accessors"][index as usize];
                let kind = a["componentType"].as_u64().context("index component type")?;
                let size = match kind { 5121 => 1, 5123 => 2, 5125 => 4, _ => bail!("non-integer triangle indices") };
                let view_index = a["bufferView"].as_u64().context("index buffer view")? as usize;
                let view = self.view(view_index)?;
                let start = a["byteOffset"].as_u64().unwrap_or(0) as usize;
                let count = a["count"].as_u64().context("index count")? as usize;
                let stride = self.root["bufferViews"][view_index]["byteStride"].as_u64().map_or(size, |n| n as usize);
                (0..count).map(|i| {
                    let b = view.get(start + i * stride..start + i * stride + size).context("indices past their buffer view")?;
                    Ok(match size { 1 => u32::from(b[0]), 2 => u32::from(u16::from_le_bytes(b.try_into().unwrap())), _ => u32::from_le_bytes(b.try_into().unwrap()) })
                }).collect()
            },
            None => Ok((0..vertices as u32).collect()),
        }
    }

    /// An image as RGBA, top row first.
    fn image(&self, index: usize) -> Result<(u32, u32, Vec<u8>)> {
        let image = &self.root["images"][index];
        let bytes = if let Some(view) = image["bufferView"].as_u64() {
            self.view(view as usize)?.to_vec()
        } else {
            read_uri(image["uri"].as_str().context("an image with no data")?, self.folder.as_deref())?
        };
        decode_image(&bytes)
    }
}

/// A model as solid ground: its triangles in the game's units, and a table of attributes with one row per material.
/// A material's `extras` are the row's columns: `Floor_code`, `Wall_code`, `Sound_code`, `Camera_id`, `Camera_through`.
pub fn to_collision(bytes: &[u8], folder: Option<&Path>) -> Result<(Vec<u8>, Vec<u8>)> {
    let document = Document::parse(bytes, folder)?;
    let root = &document.root;
    let scene = &root["scenes"][root["scene"].as_u64().unwrap_or(0) as usize];
    let nodes = root["nodes"].as_array().map(Vec::as_slice).unwrap_or_default();
    let materials = root["materials"].as_array().map(Vec::as_slice).unwrap_or_default();
    let mut triangles = Vec::new();
    let mut stack: Vec<(usize, Matrix)> = scene["nodes"].as_array().map(Vec::as_slice).unwrap_or_default().iter().filter_map(|n| n.as_u64()).map(|n| (n as usize, IDENTITY)).collect();
    while let Some((node, parent)) = stack.pop() {
        let matrix = multiply(&parent, &node_matrix(nodes.get(node).context("a scene naming a node it doesn't have")?));
        for child in nodes[node]["children"].as_array().map(Vec::as_slice).unwrap_or_default().iter().filter_map(|c| c.as_u64()) {
            stack.push((child as usize, matrix));
        }
        let Some(mesh) = nodes[node]["mesh"].as_u64() else {
            continue;
        };
        for primitive in root["meshes"][mesh as usize]["primitives"].as_array().map(Vec::as_slice).unwrap_or_default() {
            if primitive["mode"].as_u64().unwrap_or(4) != 4 {
                continue;
            }
            let positions = document.floats(primitive["attributes"]["POSITION"].as_u64().context("a mesh with no positions")? as usize, 3)?;
            let indices = document.indices(primitive, positions.len() / 3)?;
            let attribute = u16::try_from(primitive["material"].as_u64().unwrap_or(0)).context("collision attribute outside its source domain")?;
            let corner = |index: u32| -> [f32; 3] {
                let at = index as usize * 3;
                let p = transform(&matrix, [positions[at], positions[at + 1], positions[at + 2]], 1.0);
                [p[0] * 100.0, p[1] * 100.0, p[2] * 100.0]
            };
            for triple in indices.chunks_exact(3) {
                triangles.push(super::kcl::Triangle { corners: [corner(triple[0]), corner(triple[1]), corner(triple[2])], attribute });
            }
        }
    }
    let mut rows = Vec::new();
    for index in 0..materials.len().max(1) {
        let extras = materials.get(index).map(|m| &m["extras"]);
        let number = |name: &str| extras.and_then(|e| e[name].as_i64()).unwrap_or(0);
        rows.push(serde_json::json!({
            "Camera_id": number("Camera_id"),
            "Sound_code": number("Sound_code"),
            "Floor_code": number("Floor_code"),
            "Wall_code": number("Wall_code"),
            "Camera_through": number("Camera_through"),
        }));
    }
    let table = super::table::TableIr::from_json(serde_json::json!({ "rows": rows }).to_string().as_bytes())?.to_ntbl();
    Ok((super::kcl::from_triangles(&triangles)?, table))
}

/// A PNG as RGBA, top row first.
pub fn decode_image(bytes: &[u8]) -> Result<(u32, u32, Vec<u8>)> {
    if !bytes.starts_with(&[0x89, b'P', b'N', b'G']) {
        bail!("images have to be PNG");
    }
    let mut decoder = png::Decoder::new(bytes);
    decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16 | png::Transformations::ALPHA);
    let mut reader = decoder.read_info().context("the PNG can't be read")?;
    let mut pixels = vec![0u8; reader.output_buffer_size()];
    let info = reader.next_frame(&mut pixels).context("the PNG can't be read")?;
    let mut rgba = Vec::with_capacity(info.width as usize * info.height as usize * 4);
    match info.color_type {
        png::ColorType::Rgba => rgba.extend_from_slice(&pixels[..info.buffer_size()]),
        png::ColorType::GrayscaleAlpha => {
            for p in pixels[..info.buffer_size()].chunks_exact(2) {
                rgba.extend_from_slice(&[p[0], p[0], p[0], p[1]]);
            }
        }
        other => bail!("a PNG of {other:?}"),
    }
    Ok((info.width, info.height, rgba))
}

/// A BTI-format image: RGBA8, in the console's 4x4 tiles, in big-endian order. `header` is ResTIMG's 32 bytes before
/// the data, whose offset is `data_offset` from them.
pub fn rgba8_tiled(width: u32, height: u32, rgba: &[u8]) -> Vec<u8> {
    let (w, h) = (width as usize, height as usize);
    let mut out = Vec::with_capacity(w * h * 4);
    for ty in (0..h).step_by(4) {
        for tx in (0..w).step_by(4) {
            let mut ar = Vec::with_capacity(32);
            let mut gb = Vec::with_capacity(32);
            for y in ty..ty + 4 {
                for x in tx..tx + 4 {
                    let p = if x < w && y < h { &rgba[(y * w + x) * 4..(y * w + x) * 4 + 4] } else { &[0, 0, 0, 0][..] };
                    ar.extend_from_slice(&[p[3], p[0]]);
                    gb.extend_from_slice(&[p[1], p[2]]);
                }
            }
            out.extend_from_slice(&ar);
            out.extend_from_slice(&gb);
        }
    }
    out
}

/// ResTIMG for an RGBA8 image, wrapping, filtered.
pub fn image_header(width: u32, height: u32, data_offset: u32, wrap: u8, has_alpha: bool) -> [u8; 32] {
    let mut h = [0u8; 32];
    h[0] = 6;
    h[1] = u8::from(has_alpha);
    h[2..4].copy_from_slice(&(width as u16).to_be_bytes());
    h[4..6].copy_from_slice(&(height as u16).to_be_bytes());
    h[6] = wrap;
    h[7] = wrap;
    h[0x14] = 1; // minification: linear
    h[0x15] = 1; // magnification: linear
    h[0x18] = 1; // one image
    h[0x1C..0x20].copy_from_slice(&data_offset.to_be_bytes());
    h
}

/// A standalone image file (`.bti`) from a PNG, as the game's own images are.
pub fn png_to_bti(bytes: &[u8]) -> Result<Vec<u8>> {
    let (width, height, rgba) = decode_image(bytes)?;
    let record = super::texture::cook_rgba(width, height, &rgba)?;
    let mut out = Vec::with_capacity(64 + record.len());
    for word in [0x4954_424Eu32, 1, width, height, 1, 64, 0, 0, 0] { out.extend_from_slice(&word.to_le_bytes()); }
    out.extend_from_slice(&[6, u8::from(rgba.chunks_exact(4).any(|p| p[3] != 255)), 0, 0, 0, 0, 1, 1]);
    out.resize(64, 0); out.extend(record);
    Ok(out)
}

/// A PNG as a TPL, the game's form for its interface: one image, RGBA8, big-endian as the disc's are.
pub fn png_to_tpl(bytes: &[u8]) -> Result<Vec<u8>> {
    let (width, height, rgba) = decode_image(bytes)?;
    const TABLE_AT: u32 = 12;
    const HEADER_AT: u32 = TABLE_AT + 8;
    const DATA_AT: u32 = 64;
    let mut out = Vec::new();
    out.extend_from_slice(&0x0020_AF30u32.to_be_bytes());
    out.extend_from_slice(&1u32.to_be_bytes());
    out.extend_from_slice(&TABLE_AT.to_be_bytes());
    out.extend_from_slice(&HEADER_AT.to_be_bytes());
    out.extend_from_slice(&0u32.to_be_bytes());
    out.extend_from_slice(&(height as u16).to_be_bytes());
    out.extend_from_slice(&(width as u16).to_be_bytes());
    out.extend_from_slice(&6u32.to_be_bytes());
    out.extend_from_slice(&DATA_AT.to_be_bytes());
    // Clamp both ways, linear both ways, no bias, one level.
    for value in [0u32, 0, 1, 1] {
        out.extend_from_slice(&value.to_be_bytes());
    }
    out.extend_from_slice(&0f32.to_be_bytes());
    out.extend_from_slice(&[0, 0, 0, 0]);
    out.resize(DATA_AT as usize, 0);
    out.extend_from_slice(&rgba8_tiled(width, height, &rgba));
    Ok(out)
}

type Matrix = [f32; 16];

const IDENTITY: Matrix = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0];

fn multiply(a: &Matrix, b: &Matrix) -> Matrix {
    let mut out = [0.0; 16];
    for c in 0..4 {
        for r in 0..4 {
            out[c * 4 + r] = (0..4).map(|k| a[k * 4 + r] * b[c * 4 + k]).sum();
        }
    }
    out
}

fn transform(m: &Matrix, p: [f32; 3], w: f32) -> [f32; 3] {
    [
        m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12] * w,
        m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13] * w,
        m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14] * w,
    ]
}

struct Trs {
    translation: [f32; 3],
    rotation: [f32; 4],
    scale: [f32; 3],
}

fn numbers<const N: usize>(value: &Value, default: [f32; N]) -> [f32; N] {
    let mut out = default;
    if let Some(list) = value.as_array() {
        for (slot, v) in out.iter_mut().zip(list) {
            *slot = v.as_f64().unwrap_or(0.0) as f32;
        }
    }
    out
}

fn node_trs(node: &Value) -> Trs {
    Trs {
        translation: numbers(&node["translation"], [0.0; 3]),
        rotation: numbers(&node["rotation"], [0.0, 0.0, 0.0, 1.0]),
        scale: numbers(&node["scale"], [1.0; 3]),
    }
}

fn trs_matrix(t: &Trs) -> Matrix {
    let [x, y, z, w] = t.rotation;
    let [sx, sy, sz] = t.scale;
    [
        (1.0 - 2.0 * (y * y + z * z)) * sx,
        2.0 * (x * y + z * w) * sx,
        2.0 * (x * z - y * w) * sx,
        0.0,
        2.0 * (x * y - z * w) * sy,
        (1.0 - 2.0 * (x * x + z * z)) * sy,
        2.0 * (y * z + x * w) * sy,
        0.0,
        2.0 * (x * z + y * w) * sz,
        2.0 * (y * z - x * w) * sz,
        (1.0 - 2.0 * (x * x + y * y)) * sz,
        0.0,
        t.translation[0],
        t.translation[1],
        t.translation[2],
        1.0,
    ]
}

fn node_matrix(node: &Value) -> Matrix {
    if node["matrix"].is_array() {
        numbers(&node["matrix"], IDENTITY)
    } else {
        trs_matrix(&node_trs(node))
    }
}

/// Euler angles in radians, Z then Y then X, without source-format quantization.
fn angles(q: [f32; 4]) -> [f32; 3] {
    let [x, y, z, w] = q.map(f64::from);
    let (sinr, cosr) = (2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y));
    let sinp = (2.0 * (w * y - z * x)).clamp(-1.0, 1.0);
    let (siny, cosy) = (2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
    let to = |radians: f64| radians as f32;
    [to(sinr.atan2(cosr)), to(sinp.atan2((1.0 - sinp * sinp).max(0.0).sqrt())), to(siny.atan2(cosy))]
}

struct Joint {
    name: String,
    parent: Option<usize>,
    trs: Trs,
    inverse: Matrix,
}

/// Keeps one copy of each value, by its bytes: the curve analysis finds shared corners by these numbers.
struct Pool<const N: usize> {
    values: Vec<[f32; N]>,
    seen: BTreeMap<[u32; N], u32>,
}

impl<const N: usize> Pool<N> {
    fn new() -> Self {
        Self { values: Vec::new(), seen: BTreeMap::new() }
    }

    fn add(&mut self, value: [f32; N]) -> u32 {
        let key = value.map(f32::to_bits);
        *self.seen.entry(key).or_insert_with(|| {
            self.values.push(value);
            self.values.len() as u32 - 1
        })
    }
}

/// A file's material: colour, one image, how it blends.
struct SourceMaterial {
    name: String,
    color: [u8; 4],
    images: Vec<(usize, u8)>,
    blend: bool,
    cutoff: Option<u8>,
    two_sided: bool,
}

fn bounds_radius(low: &[f32; 3], high: &[f32; 3]) -> f32 {
    let center = [0, 1, 2].map(|c| (low[c] + high[c]) / 2.0);
    let far = [0, 1, 2].map(|c| (high[c] - center[c]).abs().max((low[c] - center[c]).abs()));
    (far[0] * far[0] + far[1] * far[1] + far[2] * far[2]).sqrt()
}

fn widen(low: &mut [f32; 3], high: &mut [f32; 3], at: [f32; 3]) {
    for c in 0..3 {
        low[c] = low[c].min(at[c]);
        high[c] = high[c].max(at[c]);
    }
}

/// A glTF document as typed values: the model, and each animation by its name.
pub fn to_ir(bytes: &[u8], folder: Option<&Path>) -> Result<(ModelIr, Vec<(String, ClipIr)>)> {
    let document = Document::parse(bytes, folder)?;
    let root = &document.root;
    let scene = &root["scenes"][root["scene"].as_u64().unwrap_or(0) as usize];
    let nodes = root["nodes"].as_array().map(Vec::as_slice).unwrap_or_default();

    // Every node in the scene with its whole transform, parents first.
    let mut world = vec![IDENTITY; nodes.len()];
    let mut parents = vec![None; nodes.len()];
    let mut order = Vec::new();
    let mut stack: Vec<(usize, Matrix)> = scene["nodes"].as_array().map(Vec::as_slice).unwrap_or_default().iter().rev().filter_map(|n| n.as_u64()).map(|n| (n as usize, IDENTITY)).collect();
    while let Some((node, parent)) = stack.pop() {
        let matrix = multiply(&parent, &node_matrix(nodes.get(node).context("a scene naming a node it doesn't have")?));
        world[node] = matrix;
        order.push(node);
        for child in nodes[node]["children"].as_array().map(Vec::as_slice).unwrap_or_default().iter().rev().filter_map(Value::as_u64) {
            parents[child as usize] = Some(node);
            stack.push((child as usize, matrix));
        }
    }
    if order.is_empty() {
        bail!("the scene has no nodes");
    }

    // Joints: a skin's, or every node in the scene. One root at the top: the game's tree has one.
    let mut joints: Vec<Joint> = Vec::new();
    let mut joint_of_node: BTreeMap<usize, usize> = BTreeMap::new();
    let skin = order.iter().find_map(|n| nodes[*n]["skin"].as_u64()).map(|s| &root["skins"][s as usize]);
    let mut listed: Vec<usize> = order.clone();
    let mut inverse_of: BTreeMap<usize, Matrix> = BTreeMap::new();
    if let Some(skin) = skin {
        let list: Vec<usize> = skin["joints"].as_array().context("a skin with no joints")?.iter().filter_map(|j| j.as_u64().map(|v| v as usize)).collect();
        let inverses = match skin["inverseBindMatrices"].as_u64() {
            Some(index) => document.floats(index as usize, 16)?,
            None => IDENTITY.repeat(list.len()),
        };
        for (place, node) in list.iter().enumerate() {
            let inverse: Matrix = inverses.get(place * 16..place * 16 + 16).context("a skin with too few matrices")?.try_into()?;
            inverse_of.insert(*node, inverse);
        }
        listed = order.iter().copied().filter(|n| list.contains(n)).collect();
        if listed.len() != list.len() {
            bail!("a skin names a joint outside the scene");
        }
    }
    if listed.len() + 1 >= NONE as usize {
        bail!("too many joints");
    }
    let roots = listed.iter().filter(|n| parents[**n].is_none_or(|p| !listed.contains(&p))).count();
    if roots != 1 {
        joints.push(Joint { name: "root".to_string(), parent: None, trs: Trs { translation: [0.0; 3], rotation: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] }, inverse: IDENTITY });
    }
    for node in &listed {
        joint_of_node.insert(*node, joints.len());
        let parent = parents[*node].and_then(|p| joint_of_node.get(&p).copied()).or(if joints.is_empty() { None } else { Some(0) });
        joints.push(Joint {
            name: nodes[*node]["name"].as_str().map_or_else(|| format!("joint{node}"), str::to_string),
            parent,
            trs: node_trs(&nodes[*node]),
            inverse: inverse_of.get(node).copied().unwrap_or(IDENTITY),
        });
    }

    // Materials.
    let mut materials: Vec<SourceMaterial> = Vec::new();
    for (i, material) in root["materials"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
        let pbr = &material["pbrMetallicRoughness"];
        let factor: [f32; 4] = numbers(&pbr["baseColorFactor"], [1.0; 4]);
        let image = pbr["baseColorTexture"]["index"].as_u64().and_then(|t| root["textures"][t as usize]["source"].as_u64()).map(|s| s as usize);
        let mut images = Vec::new();
        if let Some(image) = image {
            images.push((image, pbr["baseColorTexture"]["texCoord"].as_u64().unwrap_or(0)));
        }
        // Additional authored multiply layers use ordinary glTF texture indices
        // and UV sets, through the same material programs as the game's models.
        for layer in material["extras"]["nebula"]["textures"].as_array().map(Vec::as_slice).unwrap_or_default() {
            let index = layer["index"].as_u64().context("a material layer with no texture index")? as usize;
            let image = root["textures"][index]["source"].as_u64().context("a material layer with no image")? as usize;
            images.push((image, layer["texCoord"].as_u64().unwrap_or(0)));
        }
        // A program's inputs name coordinates in a byte past NEBULA_INPUT_COORD0, maps and coordinates in a byte.
        if images.len() > 240 || images.iter().any(|(_, uv)| *uv >= 240) {
            bail!("a material with more than 240 textures or coordinates");
        }
        let images = images.into_iter().map(|(image, uv)| (image, uv as u8)).collect();
        materials.push(SourceMaterial {
            name: material["name"].as_str().map_or_else(|| format!("material{i}"), str::to_string),
            color: factor.map(|v| (v.clamp(0.0, 1.0) * 255.0).round() as u8),
            images,
            blend: material["alphaMode"].as_str() == Some("BLEND"),
            cutoff: (material["alphaMode"].as_str() == Some("MASK")).then(|| (material["alphaCutoff"].as_f64().unwrap_or(0.5).clamp(0.0, 1.0) * 255.0) as u8),
            two_sided: material["doubleSided"].as_bool().unwrap_or(false),
        });
    }
    let default_material = materials.len();
    materials.push(SourceMaterial { name: "material".to_string(), color: [255; 4], images: Vec::new(), blend: false, cutoff: None, two_sided: false });

    let mut ir = ModelIr::default();
    let mut positions = Pool::<3>::new();
    let mut normals = Pool::<3>::new();
    let mut coordinates: Vec<Pool<2>> = Vec::new();
    // A vertex's draw matrix: joints first (by joint number), then envelopes.
    let mut envelope_ids: BTreeMap<Vec<(u32, u32)>, u32> = BTreeMap::new();
    let mut first_of_material: BTreeMap<usize, u32> = BTreeMap::new();
    let mut image_of: BTreeMap<usize, u32> = BTreeMap::new();
    let (mut whole_low, mut whole_high) = ([f32::MAX; 3], [f32::MIN; 3]);
    for node in &order {
        let Some(mesh) = nodes[*node]["mesh"].as_u64() else { continue };
        let skinned = nodes[*node]["skin"].is_u64();
        let skin_joints: Vec<usize> = if skinned {
            root["skins"][nodes[*node]["skin"].as_u64().unwrap_or(0) as usize]["joints"].as_array().map(Vec::as_slice).unwrap_or_default().iter().filter_map(|j| j.as_u64().map(|v| v as usize)).collect()
        } else {
            Vec::new()
        };
        for primitive in root["meshes"][mesh as usize]["primitives"].as_array().map(Vec::as_slice).unwrap_or_default() {
            if primitive["mode"].as_u64().unwrap_or(4) != 4 {
                bail!("a mesh that isn't triangles");
            }
            let attributes = &primitive["attributes"];
            let position_values = document.floats(attributes["POSITION"].as_u64().context("a mesh with no positions")? as usize, 3)?;
            let count = position_values.len() / 3;
            let normal_values = match attributes["NORMAL"].as_u64() {
                Some(index) => document.floats(index as usize, 3)?,
                None => [0.0, 1.0, 0.0].repeat(count),
            };
            let weights = match (attributes["JOINTS_0"].as_u64(), attributes["WEIGHTS_0"].as_u64(), skinned) {
                (Some(j), Some(w), true) => Some((document.floats(j as usize, 4)?, document.floats(w as usize, 4)?)),
                _ => None,
            };
            let material = primitive["material"].as_u64().map_or(default_material, |m| m as usize);
            let source = materials.get(material).context("a mesh naming a material it doesn't have")?;
            // Every TEXCOORD_n the primitive has, and every set its material's images read (zeros where it has none).
            let named = attributes.as_object().map_or(0, |a| {
                a.keys().filter_map(|k| k.strip_prefix("TEXCOORD_")?.parse::<usize>().ok()).map(|k| k + 1).max().unwrap_or(0)
            });
            let read = source.images.iter().map(|&(_, uv)| usize::from(uv) + 1).max().unwrap_or(0);
            let room = named.max(read);
            let mut coordinate_present = vec![false; room];
            let mut coordinate_values = Vec::with_capacity(room);
            for (k, present) in coordinate_present.iter_mut().enumerate() {
                let values = match attributes[format!("TEXCOORD_{k}")].as_u64() {
                    Some(index) => { *present = true; document.floats(index as usize, 2)? },
                    None => vec![0.0; count * 2],
                };
                coordinate_values.push(values);
            }
            for &(_, uv) in &source.images { coordinate_present[usize::from(uv)] = true; }
            if coordinates.len() < room {
                coordinates.resize_with(room, Pool::new);
            }
            let source_indices = document.indices(primitive, count)?;
            let mut corners: Vec<Corner> = Vec::new();
            let mut draws: Vec<u32> = Vec::new();
            let mut indices: Vec<u32> = Vec::with_capacity(source_indices.len());
            let mut corner_of: BTreeMap<u32, u32> = BTreeMap::new();
            let (mut low, mut high) = ([f32::MAX; 3], [f32::MIN; 3]);
            for index in &source_indices {
                if let Some(found) = corner_of.get(index) {
                    indices.push(*found);
                    continue;
                }
                let i = *index as usize;
                if i >= count {
                    bail!("a triangle naming a vertex past the mesh");
                }
                let position: [f32; 3] = position_values[i * 3..i * 3 + 3].try_into()?;
                let normal: [f32; 3] = normal_values[i * 3..i * 3 + 3].try_into()?;
                let draw = match &weights {
                    Some((joint_values, weight_values)) => {
                        let mut set: Vec<(u32, f32)> = Vec::new();
                        for k in 0..4 {
                            let weight = weight_values[i * 4 + k];
                            if weight > 0.001 {
                                let node_of = *skin_joints.get(joint_values[i * 4 + k] as usize).context("a vertex naming a joint past the skin")?;
                                set.push((*joint_of_node.get(&node_of).context("a vertex's joint isn't in the skin")? as u32, weight));
                            }
                        }
                        set.sort_by_key(|(j, _)| *j);
                        let total: f32 = set.iter().map(|(_, w)| w).sum();
                        if set.is_empty() {
                            0
                        } else if set.len() == 1 {
                            u32::from(set[0].0)
                        } else {
                            for item in &mut set {
                                item.1 /= total;
                            }
                            let key: Vec<(u32, u32)> = set.iter().map(|(j, w)| (*j, w.to_bits())).collect();
                            let id = *envelope_ids.entry(key).or_insert_with(|| {
                                ir.envelopes.push(set.clone());
                                ir.envelopes.len() as u32 - 1
                            });
                            joints.len() as u32 + id
                        }
                    }
                    None => joint_of_node.get(node).map_or(0, |j| *j as u32),
                };
                if draw >= u32::from(NONE) {
                    bail!("too many blended vertices");
                }
                // One joint's vertex is in that joint's space; a blended one stays in the model's.
                let (place, turned) = if weights.is_some() && (draw as usize) < joints.len() {
                    let inverse = &joints[draw as usize].inverse;
                    (transform(inverse, position, 1.0), transform(inverse, normal, 0.0))
                } else {
                    (position, normal)
                };
                let place = place.map(|v| v * UNITS_PER_METRE);
                widen(&mut low, &mut high, place);
                widen(&mut whole_low, &mut whole_high, transform(&world[*node], position, 1.0).map(|v| v * UNITS_PER_METRE));
                let length = (turned[0] * turned[0] + turned[1] * turned[1] + turned[2] * turned[2]).sqrt().max(1e-6);
                let normal_number = normals.add(turned.map(|v| v / length));
                let mut coord = vec![0u32; room];
                for k in 0..room {
                    coord[k] = coordinates[k].add(coordinate_values[k][i * 2..i * 2 + 2].try_into()?);
                }
                let number = corners.len() as u32;
                corners.push(Corner {
                    position: positions.add(place),
                    normal: normal_number,
                    tangent: normal_number,
                    binormal: normal_number,
                    color: [0; 2],
                    coord,
                    matrix: 0,
                    texture_matrix: Vec::new(),
                });
                draws.push(draw);
                corner_of.insert(*index, number);
                indices.push(number);
            }
            ir.source_vertex_count += corners.len() as u32;
            indices.truncate(indices.len() / 3 * 3);
            if indices.is_empty() {
                continue;
            }
            for triangle in indices.chunks_exact_mut(3) {
                triangle.swap(1, 2);
            }

            // The draw matrices this mesh's corners name, by first use.
            let mut use_table: Vec<u32> = Vec::new();
            for (corner, draw) in corners.iter_mut().zip(&draws) {
                let draw = *draw as u32;
                let local = use_table.iter().position(|u| *u == draw).unwrap_or_else(|| {
                    use_table.push(draw);
                    use_table.len() - 1
                });
                corner.matrix = local as u32;
            }
            let blended = use_table.len() > 1;
            let mut places = 0u32;
            if blended {
                for corner in &corners {
                    places |= 1u32.checked_shl(u32::from(corner.matrix)).unwrap_or(0);
                }
            }

            // Its own material: a mesh's draw and the material drawing it are one to one.
            let shape = ir.shapes.len() as u32;
            if ir.shapes.len() + 1 >= NONE as usize {
                bail!("too many meshes");
            }
            let mut texture_ids = Vec::new();
            for &(image, _) in &source.images {
                let number = match image_of.get(&image) {
                    Some(number) => *number,
                    None => {
                        let (width, height, rgba) = document.image(image)?;
                        let number = ir.textures.len() as u32;
                        ir.records.push(super::texture::cook_rgba(width, height, &rgba)?);
                        ir.textures.push(plain_texture(format!("image{image}"), width, height, ir.records.len() - 1));
                        image_of.insert(image, number);
                        number
                    }
                };
                texture_ids.push(number);
            }
            let texture = texture_ids.first().copied();
            let same_as = *first_of_material.entry(material).or_insert(shape);
            let mut made = plain_material(&source.name, source.color, texture, source.blend, source.cutoff, source.two_sided);
            if !texture_ids.is_empty() {
                made.coords.clear();
                made.textures = texture_ids.clone();
                made.coordinate_maps = (0..texture_ids.len()).map(|map| map as u8).collect();
                let first = made.stages[0];
                made.stages.clear();
                for (map, &(_, uv)) in source.images.iter().enumerate() {
                    made.coords.push(CoordIr { kind: 1, source: 4 + uv, matrix: NONE });
                    let mut stage = first;
                    stage.map = map as u8; stage.coord = map as u8;
                    if map != 0 { stage.color_in = [15, 8, 0, 15]; stage.alpha_in = [7, 4, 0, 7]; stage.channel = 255; }
                    made.stages.push(stage);
                }
                made.pixel_stage_count = made.stages.len() as u32;
            }
            made.same_as = same_as;
            made.mesh = shape;
            made.joint = 0;
            ir.materials.push(made);
            ir.shapes.push(ShapeIr {
                matrix_type: if blended { 3 } else { 0 },
                radius: bounds_radius(&low, &high),
                min: low,
                max: high,
                groups: vec![GroupIr {
                    attributes: ATTR_NORMAL | if blended { ATTR_MATRIX } else { 0 },
                    coordinates: coordinate_present,
                    corners,
                    indices,
                    places,
                    use_table,
                }],
                material: shape,
            });
        }
    }
    if ir.shapes.is_empty() {
        bail!("the model has no triangles");
    }

    ir.arrays = ArraysIr { normal_floats: 3, ..Default::default() };
    ir.arrays.positions = positions.values;
    ir.arrays.normals = normals.values;
    ir.arrays.coords = coordinates.into_iter().map(|pool| pool.values).collect();

    let whole_radius = bounds_radius(&whole_low, &whole_high);
    ir.joints = joints
        .iter()
        .map(|joint| JointIr {
            name: joint.name.clone(),
            kind: 0,
            scale_compensate: 0,
            billboard: 0,
            scale: joint.trs.scale,
            rotation: angles(joint.trs.rotation),
            translation: joint.trs.translation.map(|v| v * UNITS_PER_METRE),
            radius: whole_radius,
            min: whole_low,
            max: whole_high,
            parent: joint.parent.map_or(NONE, |p| p as u32),
            child: NONE,
            younger: NONE,
            meshes: Vec::new(),
        })
        .collect();
    let mut last_child: BTreeMap<usize, u32> = BTreeMap::new();
    for (number, joint) in joints.iter().enumerate() {
        if let Some(parent) = joint.parent {
            match last_child.insert(parent, number as u32) {
                Some(before) => ir.joints[before as usize].younger = number as u32,
                None => ir.joints[parent].child = number as u32,
            }
        }
    }
    ir.joints[0].meshes = (0..ir.shapes.len() as u32).rev().collect();

    ir.draw_matrices = (0..joints.len() as u32).map(|j| (0u8, j)).chain((0..ir.envelopes.len() as u32).map(|e| (1u8, e))).collect();
    ir.full_weight_count = joints.len() as u32;
    ir.important = important_joints(&ir.draw_matrices, ir.full_weight_count, &ir.envelopes);
    if !ir.envelopes.is_empty() {
        ir.inverse_binds = joints
            .iter()
            .map(|joint| {
                let m = &joint.inverse;
                let u = UNITS_PER_METRE;
                [m[0], m[4], m[8], m[12] * u, m[1], m[5], m[9], m[13] * u, m[2], m[6], m[10], m[14] * u]
            })
            .collect();
    }

    let mut clips = Vec::new();
    for (i, animation) in root["animations"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
        let name = animation["name"].as_str().map_or_else(|| format!("animation{i}"), str::to_string);
        clips.push((name, clip_of(&document, animation, &joints, &joint_of_node)?));
    }
    Ok((ir, clips))
}

/// A glTF document as the store's blocks: the model, and each animation by its name.
pub fn to_files(bytes: &[u8], folder: Option<&Path>, stem: &str) -> Result<(Vec<u8>, Vec<(String, Vec<u8>)>)> {
    let (ir, clips) = to_ir(bytes, folder)?;
    let model = super::model_write::write(&ir, stem, &super::programs::written_or_unknown())?;
    Ok((model, clips.iter().map(|(name, c)| (name.clone(), super::clip::write(c))).collect()))
}

/// The game counts animation frames at 60 a second.
const FRAMES_PER_SECOND: f32 = 60.0;

/// A sampled curve of one joint's value.
struct Curve {
    times: Vec<f32>,
    values: Vec<f32>,
    width: usize,
    step: bool,
    cubic: bool,
}

impl Curve {
    fn at(&self, time: f32) -> Vec<f32> {
        let stride = if self.cubic { self.width * 3 } else { self.width };
        let offset = if self.cubic { self.width } else { 0 };
        let value = |k: usize| &self.values[k * stride + offset..k * stride + offset + self.width];
        let last = self.times.len() - 1;
        if time <= self.times[0] {
            return value(0).to_vec();
        }
        if time >= self.times[last] {
            return value(last).to_vec();
        }
        let k = self.times.partition_point(|t| *t <= time) - 1;
        if self.step {
            return value(k).to_vec();
        }
        let f = (time - self.times[k]) / (self.times[k + 1] - self.times[k]).max(1e-6);
        let (a, b) = (value(k), value(k + 1));
        // Rotations by the shorter way, then renormalized.
        let flip = if self.width == 4 && a.iter().zip(b).map(|(x, y)| x * y).sum::<f32>() < 0.0 { -1.0 } else { 1.0 };
        let mut out: Vec<f32> = a.iter().zip(b).map(|(x, y)| x + (y * flip - x) * f).collect();
        if self.width == 4 {
            let length = out.iter().map(|v| v * v).sum::<f32>().sqrt().max(1e-6);
            out.iter_mut().for_each(|v| *v /= length);
        }
        out
    }
}

/// An animation, each joint sampled on every other frame.

/// One axis of a joint: a single value if it never changes, else time, value and slope per sample.
fn axis_track(out: &mut Vec<f32>, frames: &[usize], values: &[f32]) -> Result<Track> {
    let at = out.len() as u32;
    if values.iter().all(|v| (*v - values[0]).abs() < 1e-5) {
        out.push(values[0]);
        return Ok(Track { count: 1, stride: 3, integer_keys: false, at });
    }
    let count = u32::try_from(values.len()).context("an animation with too many samples")?;
    for (k, value) in values.iter().enumerate() {
        let (a, b) = (k.saturating_sub(1), (k + 1).min(values.len() - 1));
        let slope = (values[b] - values[a]) / ((frames[b] - frames[a]).max(1) as f32);
        out.extend_from_slice(&[frames[k] as f32, *value, slope]);
    }
    Ok(Track { count, stride: 3, integer_keys: false, at })
}

fn clip_of(document: &Document, animation: &Value, joints: &[Joint], joint_of_node: &BTreeMap<usize, usize>) -> Result<ClipIr> {
    let samplers = animation["samplers"].as_array().context("an animation with no samplers")?;
    let mut curves: BTreeMap<(usize, &str), Curve> = BTreeMap::new();
    let mut length = 0.0f32;
    for channel in animation["channels"].as_array().map(Vec::as_slice).unwrap_or_default() {
        let Some(node) = channel["target"]["node"].as_u64() else { continue };
        let Some(&joint) = joint_of_node.get(&(node as usize)) else { continue };
        let path = channel["target"]["path"].as_str().unwrap_or("");
        let (key, width) = match path {
            "translation" => ("t", 3),
            "rotation" => ("r", 4),
            "scale" => ("s", 3),
            _ => continue,
        };
        let sampler = samplers.get(channel["sampler"].as_u64().unwrap_or(0) as usize).context("a channel naming a sampler it doesn't have")?;
        let times = document.floats(sampler["input"].as_u64().context("a sampler with no times")? as usize, 1)?;
        let values = document.floats(sampler["output"].as_u64().context("a sampler with no values")? as usize, width)?;
        if times.is_empty() {
            continue;
        }
        length = length.max(*times.last().unwrap());
        let mode = sampler["interpolation"].as_str().unwrap_or("LINEAR");
        curves.insert((joint, key), Curve { times, values, width, step: mode == "STEP", cubic: mode == "CUBICSPLINE" });
    }
    let frames = ((length * FRAMES_PER_SECOND).ceil() as usize).max(1);
    let samples: Vec<usize> = (0..=frames).step_by(2).chain(std::iter::once(frames)).collect::<std::collections::BTreeSet<_>>().into_iter().collect();

    let mut clip = ClipIr { kind: KIND_JOINTS, loop_mode: 2, frames: frames as i32, first_count: joints.len() as u32, ..ClipIr::default() };
    for (index, joint) in joints.iter().enumerate() {
        let mut scale_values = [vec![], vec![], vec![]];
        let mut angle_values: [Vec<f32>; 3] = [vec![], vec![], vec![]];
        let mut translate_values = [vec![], vec![], vec![]];
        for frame in &samples {
            let time = *frame as f32 / FRAMES_PER_SECOND;
            let s = curves.get(&(index, "s")).map_or(joint.trs.scale.to_vec(), |c| c.at(time));
            let r = curves.get(&(index, "r")).map_or(joint.trs.rotation.to_vec(), |c| c.at(time));
            let t = curves.get(&(index, "t")).map_or(joint.trs.translation.to_vec(), |c| c.at(time));
            let turned = angles([r[0], r[1], r[2], r[3]]);
            for axis in 0..3 {
                scale_values[axis].push(s[axis]);
                translate_values[axis].push(t[axis] * UNITS_PER_METRE);
                // The nearest way round from the last sample: the clip's numbers run on past a half turn, and the
                // float rotations remain continuous through a full turn.
                let values = &mut angle_values[axis];
                let mut value = turned[axis];
                if let Some(last) = values.last() {
                    let last = *last;
                    while value - last > std::f32::consts::PI {
                        value -= std::f32::consts::TAU;
                    }
                    while value - last < -std::f32::consts::PI {
                        value += std::f32::consts::TAU;
                    }
                }
                values.push(value);
            }
        }
        for axis in 0..3 {
            let scale = axis_track(&mut clip.values, &samples, &scale_values[axis])?;
            let rotation = axis_track(&mut clip.values, &samples, &angle_values[axis])?;
            let translation = axis_track(&mut clip.values, &samples, &translate_values[axis])?;
            clip.tracks.extend_from_slice(&[scale, rotation, translation]);
        }
    }
    Ok(clip)
}

#[cfg(test)]
mod tests {
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
        let (model, clips) = to_files(&triangle(1), None, "Triangle").unwrap();
        assert_eq!(u32::from_le_bytes(model[0..4].try_into().unwrap()), super::super::model_write::MAGIC);
        assert!(clips.is_empty());
        let (ir, _) = to_ir(&triangle(1), None).unwrap();
        assert_eq!(ir.materials.len(), 1);
        assert_eq!(ir.materials[0].name, "material");
        assert_eq!(ir.joints.len(), 1);
    }

    #[test]
    fn two_meshes_of_one_material_both_draw() {
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
        let bytes = many_triangles(24_000);
        let (ir, _) = to_ir(&bytes, None).unwrap();
        assert_eq!(ir.arrays.positions.len() > 0x1_0000, true);
        assert_eq!(ir.shapes[0].groups[0].corners.len(), 72_000);
        to_files(&bytes, None, "Many").unwrap();
    }

    #[test]
    fn triangles_wind_as_the_games_do() {
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
        let block = super::super::model_write::write(&ir, "fixture", &super::super::programs::Written::unknown()).unwrap();
        let word = |at| u32::from_le_bytes(block[at..at + 4].try_into().unwrap());
        assert_eq!(word(4), super::super::model_write::VERSION);
        let at = word(24 * 4) as usize;
        assert_eq!(word(at + 36), 12);
        let slots = word(at + super::super::model_write::MATERIAL_BYTES - 16) as usize;
        for n in 0..12 {
            let slot = slots + n * super::super::model_write::SLOT_BYTES;
            assert_eq!(word(slot), n as u32);
            assert_eq!(block[slot + 6], super::super::model_write::SLOT_USES_MAP | super::super::model_write::SLOT_USES_COORD);
        }
    }

    #[test]
    fn a_bad_file_says_what_is_wrong() {
        assert!(format!("{:#}", to_ir(b"nope", None).err().unwrap()).contains("JSON"));
        let old = br#"{"asset":{"version":"1.0"}}"#;
        assert!(format!("{:#}", to_ir(old, None).err().unwrap()).contains("glTF 2"));
    }
}
