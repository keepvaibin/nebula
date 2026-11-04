//! A modern model in place of the game's own format: glTF 2.0 (`.gltf` with its files, or `.glb`) written out as a J3D
//! model, which then cooks as any other, with a `.bck` for each animation. Triangles, one texture and colour per material,
//! skins and node animation. Lengths are metres in glTF: one metre is 100 of the game's units. A glTF triangle winds
//! counter-clockwise; the game's wind clockwise (measured on Kuribo, Coin and Mario: 650 to 2, 268 to 0, 4514 to 363).

use anyhow::{bail, Context, Result};
use serde_json::Value;
use std::collections::BTreeMap;
use std::path::Path;

const UNITS_PER_METRE: f32 = 100.0;
const MATRICES_PER_PACKET: usize = 10;

fn put16(out: &mut Vec<u8>, v: u16) {
    out.extend_from_slice(&v.to_be_bytes());
}

fn put32(out: &mut Vec<u8>, v: u32) {
    out.extend_from_slice(&v.to_be_bytes());
}

fn putf(out: &mut Vec<u8>, v: f32) {
    out.extend_from_slice(&v.to_be_bytes());
}

fn set32(out: &mut [u8], at: usize, v: u32) {
    out[at..at + 4].copy_from_slice(&v.to_be_bytes());
}

fn pad(out: &mut Vec<u8>, to: usize) {
    while out.len() % to != 0 {
        out.push(0);
    }
}

/// A name table: its hashes are the game's.
fn name_table(names: &[String]) -> Vec<u8> {
    let mut out = Vec::new();
    put16(&mut out, names.len() as u16);
    put16(&mut out, 0xFFFF);
    let mut at = 4 + names.len() * 4;
    for name in names {
        let hash = name.bytes().fold(0u16, |h, b| h.wrapping_mul(3).wrapping_add(u16::from(b)));
        put16(&mut out, hash);
        put16(&mut out, at as u16);
        at += name.len() + 1;
    }
    for name in names {
        out.extend_from_slice(name.as_bytes());
        out.push(0);
    }
    out
}

/// A block: its kind, its size, then `body`, on a 32-byte boundary.
fn block(kind: &[u8; 4], body: &[u8]) -> Vec<u8> {
    let mut out = kind.to_vec();
    out.extend_from_slice(&[0; 4]);
    out.extend_from_slice(body);
    pad(&mut out, 32);
    let size = out.len() as u32;
    set32(&mut out, 4, size);
    out
}

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
            Some(index) => Ok(self.floats(index as usize, 1)?.into_iter().map(|v| v as u32).collect()),
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
            let attribute = primitive["material"].as_u64().unwrap_or(0) as u16;
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
    let table = super::merge::table_from_json(serde_json::json!({ "rows": rows }).to_string().as_bytes())?;
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
    let has_alpha = rgba.chunks_exact(4).any(|p| p[3] != 255);
    let mut out = image_header(width, height, 32, 0, has_alpha).to_vec();
    out.extend_from_slice(&rgba8_tiled(width, height, &rgba));
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

/// A rotation as the game's three angles: Z then Y then X, in 1/32768ths of half a turn.
fn angles(q: [f32; 4]) -> [i16; 3] {
    let [x, y, z, w] = q.map(f64::from);
    let (sinr, cosr) = (2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y));
    let sinp = (2.0 * (w * y - z * x)).clamp(-1.0, 1.0);
    let (siny, cosy) = (2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
    let to = |radians: f64| (radians / std::f64::consts::PI * 32768.0).round().clamp(-32768.0, 32767.0) as i16;
    [to(sinr.atan2(cosr)), to(sinp.atan2((1.0 - sinp * sinp).max(0.0).sqrt())), to(siny.atan2(cosy))]
}

struct Joint {
    name: String,
    parent: Option<usize>,
    trs: Trs,
    inverse: Matrix,
}

/// What a vertex's corner is drawn with: which matrix (a joint or a blend of them), position, normal, coordinate.
#[derive(Clone, Copy)]
struct Corner {
    matrix: u32,
    position: u32,
    normal: u32,
    coordinate: u32,
}

struct Group {
    material: usize,
    corners: Vec<Corner>,
    radius: f32,
    low: [f32; 3],
    high: [f32; 3],
}

struct MaterialOut {
    name: String,
    color: [u8; 4],
    texture: Option<usize>,
    blend: bool,
    cutoff: Option<u8>,
    two_sided: bool,
}

/// Keeps one copy of each value, by its bytes.
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

/// glTF 2 to a J3D model. `folder`: where a `.gltf` finds its other files.
pub fn to_j3d(bytes: &[u8], folder: Option<&Path>) -> Result<Vec<u8>> {
    Ok(to_files(bytes, folder)?.0)
}

/// The model, and each animation by its name.
pub fn to_files(bytes: &[u8], folder: Option<&Path>) -> Result<(Vec<u8>, Vec<(String, Vec<u8>)>)> {
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
    if listed.len() + 1 > 0xFFFF {
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
    let mut materials: Vec<MaterialOut> = Vec::new();
    for (i, material) in root["materials"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
        let pbr = &material["pbrMetallicRoughness"];
        let factor: [f32; 4] = numbers(&pbr["baseColorFactor"], [1.0; 4]);
        let texture = pbr["baseColorTexture"]["index"].as_u64().and_then(|t| root["textures"][t as usize]["source"].as_u64()).map(|s| s as usize);
        materials.push(MaterialOut {
            name: material["name"].as_str().map_or_else(|| format!("material{i}"), str::to_string),
            color: factor.map(|v| (v.clamp(0.0, 1.0) * 255.0).round() as u8),
            texture,
            blend: material["alphaMode"].as_str() == Some("BLEND"),
            cutoff: (material["alphaMode"].as_str() == Some("MASK")).then(|| (material["alphaCutoff"].as_f64().unwrap_or(0.5).clamp(0.0, 1.0) * 255.0) as u8),
            two_sided: material["doubleSided"].as_bool().unwrap_or(false),
        });
    }
    let default_material = materials.len();
    materials.push(MaterialOut { name: "material".to_string(), color: [255; 4], texture: None, blend: false, cutoff: None, two_sided: false });

    // Geometry: rigid models are placed by their node; skinned ones stand in the pose the inverse matrices name.
    let mut positions = Pool::<3>::new();
    let mut normals = Pool::<3>::new();
    let mut coordinates = Pool::<2>::new();
    // A vertex's matrix: draw entries are joints first (index in the joint list), then envelopes.
    let mut envelopes: Vec<Vec<(u16, f32)>> = Vec::new();
    let mut envelope_ids: BTreeMap<Vec<(u16, u32)>, u32> = BTreeMap::new();
    let mut groups: Vec<Group> = Vec::new();
    let mut vertex_count = 0u32;
    for node in &order {
        let Some(mesh) = nodes[*node]["mesh"].as_u64() else { continue };
        let skinned = nodes[*node]["skin"].is_u64();
        // A node of its own: its joint places it. A skinned mesh's vertices stand where its matrices say.
        let place = IDENTITY;
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
            let coordinate_values = match attributes["TEXCOORD_0"].as_u64() {
                Some(index) => document.floats(index as usize, 2)?,
                None => vec![0.0; count * 2],
            };
            let weights = match (attributes["JOINTS_0"].as_u64(), attributes["WEIGHTS_0"].as_u64(), skinned) {
                (Some(j), Some(w), true) => Some((document.floats(j as usize, 4)?, document.floats(w as usize, 4)?)),
                _ => None,
            };
            let material = primitive["material"].as_u64().map_or(default_material, |m| m as usize);
            let indices = document.indices(primitive, count)?;
            let mut corners = Vec::with_capacity(indices.len());
            let mut low = [f32::MAX; 3];
            let mut high = [f32::MIN; 3];
            let mut map: BTreeMap<u32, Corner> = BTreeMap::new();
            for index in &indices {
                let i = *index as usize;
                let corner = *match map.entry(*index) {
                    std::collections::btree_map::Entry::Occupied(found) => found.into_mut(),
                    std::collections::btree_map::Entry::Vacant(slot) => {
                        if i >= count {
                            bail!("a triangle naming a vertex past the mesh");
                        }
                        let p = transform(&place, position_values[i * 3..i * 3 + 3].try_into()?, 1.0).map(|v| v * UNITS_PER_METRE);
                        let placed = transform(&world[*node], position_values[i * 3..i * 3 + 3].try_into()?, 1.0).map(|v| v * UNITS_PER_METRE);
                        for c in 0..3 {
                            low[c] = low[c].min(placed[c]);
                            high[c] = high[c].max(placed[c]);
                        }
                        let n = transform(&place, normal_values[i * 3..i * 3 + 3].try_into()?, 0.0);
                        let matrix = match &weights {
                            Some((joint_values, weight_values)) => {
                                let mut set: Vec<(u16, f32)> = Vec::new();
                                for k in 0..4 {
                                    let weight = weight_values[i * 4 + k];
                                    if weight > 0.001 {
                                        let node_of = *skin_joints.get(joint_values[i * 4 + k] as usize).context("a vertex naming a joint past the skin")?;
                                        set.push((*joint_of_node.get(&node_of).context("a vertex's joint isn't in the skin")? as u16, weight));
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
                                    let key: Vec<(u16, u32)> = set.iter().map(|(j, w)| (*j, w.to_bits())).collect();
                                    let id = *envelope_ids.entry(key).or_insert_with(|| {
                                        envelopes.push(set.clone());
                                        envelopes.len() as u32 - 1
                                    });
                                    joints.len() as u32 + id
                                }
                            }
                            None => joint_of_node.get(node).map_or(0, |j| *j as u32),
                        };
                        // One joint's vertex is in that joint's space; a blended one stays in the model's.
                        let (p, n) = if weights.is_some() && (matrix as usize) < joints.len() {
                            let inverse = &joints[matrix as usize].inverse;
                            let local = transform(inverse, position_values[i * 3..i * 3 + 3].try_into()?, 1.0).map(|v| v * UNITS_PER_METRE);
                            (local, transform(inverse, normal_values[i * 3..i * 3 + 3].try_into()?, 0.0))
                        } else {
                            (p, n)
                        };
                        let length = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt().max(1e-6);
                        vertex_count += 1;
                        slot.insert(Corner {
                            matrix,
                            position: positions.add(p),
                            normal: normals.add(n.map(|v| v / length)),
                            coordinate: coordinates.add(coordinate_values[i * 2..i * 2 + 2].try_into()?),
                        })
                    }
                };
                corners.push(corner);
            }
            if corners.len() < 3 {
                continue;
            }
            for triangle in corners.chunks_exact_mut(3) {
                triangle.swap(1, 2);
            }
            let center = [0, 1, 2].map(|c| (low[c] + high[c]) / 2.0);
            let radius = positions_radius(&center, &low, &high);
            groups.push(Group { material, corners, radius, low, high });
        }
    }
    if groups.is_empty() {
        bail!("the model has no triangles");
    }
    if positions.values.len() > 0xFFFF || normals.values.len() > 0xFFFF || coordinates.values.len() > 0xFFFF {
        bail!("too many vertices: at most 65535 of each kind");
    }

    // Only the materials used, in order.
    let mut used: Vec<usize> = groups.iter().map(|g| g.material).collect();
    used.sort_unstable();
    used.dedup();
    let material_index = |m: usize| used.iter().position(|u| *u == m).unwrap();

    // Images.
    let mut images: Vec<(String, u32, u32, Vec<u8>, bool)> = Vec::new();
    let mut image_of: BTreeMap<usize, usize> = BTreeMap::new();
    for m in &used {
        if let Some(source) = materials[*m].texture {
            if let std::collections::btree_map::Entry::Vacant(slot) = image_of.entry(source) {
                let (width, height, rgba) = document.image(source)?;
                let has_alpha = rgba.chunks_exact(4).any(|p| p[3] != 255);
                slot.insert(images.len());
                images.push((format!("image{source}"), width, height, rgba8_tiled(width, height, &rgba), has_alpha));
            }
        }
    }

    let inf1 = write_inf1(&joints, &groups, &material_index, vertex_count);
    let vtx1 = write_vtx1(&positions.values, &normals.values, &coordinates.values);
    let evp1 = write_evp1(&envelopes, &joints);
    let drw1 = write_drw1(joints.len(), envelopes.len());
    let jnt1 = write_jnt1(&joints, &groups);
    let (shp1, packets) = write_shp1(&groups);
    let mat3 = write_mat3(&used, &materials, &image_of);
    let tex1 = write_tex1(&images);

    let blocks = [inf1, vtx1, evp1, drw1, jnt1, shp1, mat3, tex1];
    let mut out = b"J3D2bmd3".to_vec();
    put32(&mut out, 0);
    put32(&mut out, blocks.len() as u32);
    out.extend_from_slice(b"SVR3");
    out.extend_from_slice(&[0xFF; 12]);
    for block in &blocks {
        out.extend_from_slice(block);
    }
    let size = out.len() as u32;
    set32(&mut out, 8, size);
    let _ = packets;
    let mut animations = Vec::new();
    for (i, animation) in root["animations"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
        let name = animation["name"].as_str().map_or_else(|| format!("animation{i}"), str::to_string);
        animations.push((name, write_animation(&document, animation, &joints, &joint_of_node)?));
    }
    Ok((out, animations))
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
fn write_animation(document: &Document, animation: &Value, joints: &[Joint], joint_of_node: &BTreeMap<usize, usize>) -> Result<Vec<u8>> {
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
    if frames > 0x7FFF {
        bail!("an animation over nine minutes long");
    }
    let samples: Vec<usize> = (0..=frames).step_by(2).chain(std::iter::once(frames)).collect::<std::collections::BTreeSet<_>>().into_iter().collect();

    let mut scales: Vec<f32> = Vec::new();
    let mut rotations: Vec<i16> = Vec::new();
    let mut translations: Vec<f32> = Vec::new();
    let mut table: Vec<u8> = Vec::new();

    // One axis: a single value if it never changes, else time, value and slope per sample. Returns (count, offset).
    fn float_axis(out: &mut Vec<f32>, frames: &[usize], values: &[f32]) -> (u16, u16) {
        let at = out.len() as u16;
        if values.iter().all(|v| (*v - values[0]).abs() < 1e-5) {
            out.push(values[0]);
            return (1, at);
        }
        for (k, value) in values.iter().enumerate() {
            let (a, b) = (k.saturating_sub(1), (k + 1).min(values.len() - 1));
            let slope = (values[b] - values[a]) / ((frames[b] - frames[a]).max(1) as f32);
            out.extend_from_slice(&[frames[k] as f32, *value, slope]);
        }
        (values.len() as u16, at)
    }
    fn angle_axis(out: &mut Vec<i16>, frames: &[usize], values: &[i32]) -> (u16, u16) {
        let at = out.len() as u16;
        if values.iter().all(|v| *v == values[0]) {
            out.push(values[0] as i16);
            return (1, at);
        }
        for (k, value) in values.iter().enumerate() {
            let (a, b) = (k.saturating_sub(1), (k + 1).min(values.len() - 1));
            let slope = (values[b] - values[a]) / ((frames[b] - frames[a]).max(1) as i32);
            out.extend_from_slice(&[frames[k] as i16, *value as i16, slope.clamp(-32768, 32767) as i16]);
        }
        (values.len() as u16, at)
    }

    for (index, joint) in joints.iter().enumerate() {
        let mut scale_values = [vec![], vec![], vec![]];
        let mut angle_values = [vec![], vec![], vec![]];
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
                // The nearest way round from the last sample: the game interpolates the numbers.
                let values: &mut Vec<i32> = &mut angle_values[axis];
                let mut value = i32::from(turned[axis]);
                if let Some(last) = values.last() {
                    while value - last > 32768 {
                        value -= 65536;
                    }
                    while value - last < -32768 {
                        value += 65536;
                    }
                }
                values.push(value);
            }
        }
        for axis in 0..3 {
            // Angles past half a turn wrap in 16 bits, as the game's own do when read back into s16.
            let (sc, so) = float_axis(&mut scales, &samples, &scale_values[axis]);
            let wrapped: Vec<i32> = angle_values[axis].iter().map(|v| ((*v + 32768).rem_euclid(65536)) - 32768).collect();
            let (rc, ro) = angle_axis(&mut rotations, &samples, &wrapped);
            let (tc, to) = float_axis(&mut translations, &samples, &translate_values[axis]);
            for (count, offset) in [(sc, so), (rc, ro), (tc, to)] {
                put16(&mut table, count);
                put16(&mut table, offset);
                put16(&mut table, 0);
            }
        }
    }

    let mut body = Vec::new();
    body.push(2); // loops
    body.push(0);
    put16(&mut body, frames as u16);
    put16(&mut body, joints.len() as u16);
    put16(&mut body, scales.len() as u16);
    put16(&mut body, rotations.len() as u16);
    put16(&mut body, translations.len() as u16);
    body.extend_from_slice(&[0; 16]);
    let align = |body: &mut Vec<u8>| {
        while (body.len() + 8) % 32 != 0 {
            body.push(0xFF);
        }
    };
    align(&mut body);
    let table_at = body.len() + 8;
    body.extend_from_slice(&table);
    align(&mut body);
    let scales_at = body.len() + 8;
    scales.iter().for_each(|v| putf(&mut body, *v));
    align(&mut body);
    let rotations_at = body.len() + 8;
    rotations.iter().for_each(|v| put16(&mut body, *v as u16));
    align(&mut body);
    let translations_at = body.len() + 8;
    translations.iter().for_each(|v| putf(&mut body, *v));
    for (field, value) in [(12, table_at), (16, scales_at), (20, rotations_at), (24, translations_at)] {
        set32(&mut body, field, value as u32);
    }
    let block = block(b"ANK1", &body);
    let mut out = b"J3D1bck1".to_vec();
    put32(&mut out, (0x20 + block.len()) as u32);
    put32(&mut out, 1);
    out.extend_from_slice(&[0xFF; 16]);
    out.extend_from_slice(&block);
    Ok(out)
}

fn positions_radius(center: &[f32; 3], low: &[f32; 3], high: &[f32; 3]) -> f32 {
    let far = [0, 1, 2].map(|c| (high[c] - center[c]).abs().max((low[c] - center[c]).abs()));
    (far[0] * far[0] + far[1] * far[1] + far[2] * far[2]).sqrt()
}

fn write_inf1(joints: &[Joint], groups: &[Group], material_index: &impl Fn(usize) -> usize, vertices: u32) -> Vec<u8> {
    let mut tree = Vec::new();
    let mut node = |kind: u16, index: u16| {
        put16(&mut tree, kind);
        put16(&mut tree, index);
    };
    // The joints as a tree, every shape under the root with its material.
    fn walk(joint: usize, joints: &[Joint], tree: &mut dyn FnMut(u16, u16), shapes: &mut dyn FnMut(&mut dyn FnMut(u16, u16))) {
        tree(0x10, joint as u16);
        tree(0x01, 0);
        if joint == 0 {
            shapes(tree);
        }
        for (i, child) in joints.iter().enumerate() {
            if child.parent == Some(joint) {
                walk(i, joints, tree, shapes);
            }
        }
        tree(0x02, 0);
    }
    let mut shapes = |tree: &mut dyn FnMut(u16, u16)| {
        for (s, group) in groups.iter().enumerate() {
            tree(0x11, material_index(group.material) as u16);
            tree(0x01, 0);
            tree(0x12, s as u16);
            tree(0x02, 0);
        }
    };
    walk(0, joints, &mut node, &mut shapes);
    node(0, 0);
    let packets = groups.len() as u32;
    let mut body = Vec::new();
    put16(&mut body, 0);
    put16(&mut body, 0xFFFF);
    put32(&mut body, packets);
    put32(&mut body, vertices);
    put32(&mut body, 0x18);
    body.extend_from_slice(&tree);
    block(b"INF1", &body)
}

fn write_vtx1(positions: &[[f32; 3]], normals: &[[f32; 3]], coordinates: &[[f32; 2]]) -> Vec<u8> {
    let mut body = vec![0u8; 0x38];
    set32(&mut body, 0, 0x40);
    for (attribute, count) in [(9u32, 1u32), (10, 0), (13, 1), (0xFF, 1)] {
        put32(&mut body, attribute);
        put32(&mut body, count);
        put32(&mut body, if attribute == 0xFF { 0 } else { 4 });
        body.extend_from_slice(&[0, 0xFF, 0xFF, 0xFF]);
    }
    let start = |body: &Vec<u8>| body.len() as u32 + 8;
    pad_to(&mut body, 24);
    let position_at = start(&body);
    for p in positions {
        p.iter().for_each(|v| putf(&mut body, *v));
    }
    pad_to(&mut body, 24);
    let normal_at = start(&body);
    for n in normals {
        n.iter().for_each(|v| putf(&mut body, *v));
    }
    pad_to(&mut body, 24);
    let coordinate_at = start(&body);
    for c in coordinates {
        c.iter().for_each(|v| putf(&mut body, *v));
    }
    set32(&mut body, 4, position_at);
    set32(&mut body, 8, normal_at);
    // nbt 12, colours 16 and 20, then the coordinate arrays from 24.
    set32(&mut body, 24, coordinate_at);
    block(b"VTX1", &body)
}

/// Pads a block's body so that, after the block's 8-byte head, it reaches a 32-byte boundary.
fn pad_to(body: &mut Vec<u8>, _unused: usize) {
    while (body.len() + 8) % 32 != 0 {
        body.push(0);
    }
}

fn write_evp1(envelopes: &[Vec<(u16, f32)>], joints: &[Joint]) -> Vec<u8> {
    let mut body = Vec::new();
    put16(&mut body, envelopes.len() as u16);
    put16(&mut body, 0xFFFF);
    if envelopes.is_empty() {
        body.extend_from_slice(&[0; 16]);
        return block(b"EVP1", &body);
    }
    body.extend_from_slice(&[0; 16]);
    let counts_at = body.len() + 8;
    for envelope in envelopes {
        body.push(envelope.len() as u8);
    }
    pad_to(&mut body, 0);
    let indices_at = body.len() + 8;
    for envelope in envelopes {
        envelope.iter().for_each(|(j, _)| put16(&mut body, *j));
    }
    pad_to(&mut body, 0);
    let weights_at = body.len() + 8;
    for envelope in envelopes {
        envelope.iter().for_each(|(_, w)| putf(&mut body, *w));
    }
    pad_to(&mut body, 0);
    let matrices_at = body.len() + 8;
    for joint in joints {
        let m = &joint.inverse;
        for r in 0..3 {
            putf(&mut body, m[r]);
            putf(&mut body, m[4 + r]);
            putf(&mut body, m[8 + r]);
            putf(&mut body, m[12 + r] * UNITS_PER_METRE);
        }
    }
    set32(&mut body, 4, counts_at as u32);
    set32(&mut body, 8, indices_at as u32);
    set32(&mut body, 12, weights_at as u32);
    set32(&mut body, 16, matrices_at as u32);
    block(b"EVP1", &body)
}

fn write_drw1(joints: usize, envelopes: usize) -> Vec<u8> {
    // The game takes the table's own entries as its count less the envelopes' (J3DModelLoader::readDraw): blended
    // entries come twice.
    let listed = joints + envelopes;
    let count = listed + envelopes;
    let entry = |i: usize| if i < joints { (0u8, i as u16) } else { (1u8, ((i - joints) % envelopes.max(1)) as u16) };
    let mut body = Vec::new();
    put16(&mut body, count as u16);
    put16(&mut body, 0xFFFF);
    put32(&mut body, 0x14);
    put32(&mut body, 0);
    for i in 0..count {
        body.push(entry(i).0);
    }
    while (body.len() + 8) % 2 != 0 {
        body.push(0xFF);
    }
    let table = body.len() + 8;
    for i in 0..count {
        put16(&mut body, entry(i).1);
    }
    set32(&mut body, 8, table as u32);
    block(b"DRW1", &body)
}

fn write_jnt1(joints: &[Joint], groups: &[Group]) -> Vec<u8> {
    let mut low = [f32::MAX; 3];
    let mut high = [f32::MIN; 3];
    for group in groups {
        for c in 0..3 {
            low[c] = low[c].min(group.low[c]);
            high[c] = high[c].max(group.high[c]);
        }
    }
    let center = [0, 1, 2].map(|c| (low[c] + high[c]) / 2.0);
    let radius = positions_radius(&center, &low, &high);
    let mut body = Vec::new();
    put16(&mut body, joints.len() as u16);
    put16(&mut body, 0xFFFF);
    put32(&mut body, 0x18);
    put32(&mut body, 0);
    put32(&mut body, 0);
    for joint in joints {
        put16(&mut body, 0);
        body.push(0);
        body.push(0xFF);
        joint.trs.scale.iter().for_each(|v| putf(&mut body, *v));
        angles(joint.trs.rotation).iter().for_each(|v| put16(&mut body, *v as u16));
        put16(&mut body, 0xFFFF);
        joint.trs.translation.iter().for_each(|v| putf(&mut body, *v * UNITS_PER_METRE));
        putf(&mut body, radius);
        low.iter().for_each(|v| putf(&mut body, *v));
        high.iter().for_each(|v| putf(&mut body, *v));
    }
    let remap = body.len() + 8;
    for i in 0..joints.len() {
        put16(&mut body, i as u16);
    }
    while (body.len() + 8) % 4 != 0 {
        body.push(0xFF);
    }
    let names = body.len() + 8;
    let list: Vec<String> = joints.iter().map(|j| j.name.clone()).collect();
    body.extend_from_slice(&name_table(&list));
    set32(&mut body, 8, remap as u32);
    set32(&mut body, 12, names as u32);
    block(b"JNT1", &body)
}

/// Each group's triangles, packets of at most ten matrices: what the game's draw reads.
fn write_shp1(groups: &[Group]) -> (Vec<u8>, usize) {
    let mut shapes = Vec::new();
    let mut matrix_table: Vec<u16> = Vec::new();
    let mut matrix_data = Vec::new();
    let mut packets = Vec::new();
    let mut lists = Vec::new();
    let mut first_group_data = 0u16;
    let mut first_packet = 0u16;
    let many = groups.iter().any(|g| g.corners.iter().any(|c| c.matrix != g.corners[0].matrix));
    for group in groups {
        // Triangles in order, a new packet when one would pass ten matrices.
        let mut parts: Vec<(Vec<u16>, Vec<Corner>)> = Vec::new();
        let mut matrices: Vec<u16> = Vec::new();
        let mut corners: Vec<Corner> = Vec::new();
        for triangle in group.corners.chunks_exact(3) {
            let needed: Vec<u16> = triangle.iter().map(|c| c.matrix as u16).filter(|m| !matrices.contains(m)).collect();
            let mut distinct = needed.clone();
            distinct.sort_unstable();
            distinct.dedup();
            if matrices.len() + distinct.len() > MATRICES_PER_PACKET {
                parts.push((std::mem::take(&mut matrices), std::mem::take(&mut corners)));
                distinct = triangle.iter().map(|c| c.matrix as u16).collect();
                distinct.sort_unstable();
                distinct.dedup();
            }
            matrices.extend(distinct);
            corners.extend_from_slice(triangle);
        }
        parts.push((matrices, corners));

        shapes.push(if many { 3u8 } else { 0u8 });
        shapes.push(0xFF);
        put16(&mut shapes, parts.len() as u16);
        put16(&mut shapes, 0);
        put16(&mut shapes, first_group_data);
        put16(&mut shapes, first_packet);
        put16(&mut shapes, 0xFFFF);
        putf(&mut shapes, group.radius);
        group.low.iter().for_each(|v| putf(&mut shapes, *v));
        group.high.iter().for_each(|v| putf(&mut shapes, *v));
        first_group_data += parts.len() as u16;
        first_packet += parts.len() as u16;

        for (matrices, corners) in parts {
            // The matrix a shape of one uses; the list a shape of many does.
            put16(&mut matrix_data, matrices[0]);
            put16(&mut matrix_data, matrices.len() as u16);
            put32(&mut matrix_data, matrix_table.len() as u32);
            matrix_table.extend_from_slice(&matrices);
            let start = lists.len();
            lists.push(0x90);
            put16(&mut lists, corners.len() as u16);
            for corner in &corners {
                if many {
                    let place = matrices.iter().position(|m| *m == corner.matrix as u16).unwrap_or(0);
                    lists.push((place * 3) as u8);
                }
                put16(&mut lists, corner.position as u16);
                put16(&mut lists, corner.normal as u16);
                put16(&mut lists, corner.coordinate as u16);
            }
            while lists.len() % 32 != 0 {
                lists.push(0);
            }
            put32(&mut packets, (lists.len() - start) as u32);
            put32(&mut packets, start as u32);
        }
    }
    let mut body = Vec::new();
    put16(&mut body, groups.len() as u16);
    put16(&mut body, 0xFFFF);
    body.extend_from_slice(&[0; 32]);
    let shapes_at = body.len() + 8;
    body.extend_from_slice(&shapes);
    let remap_at = body.len() + 8;
    for i in 0..groups.len() {
        put16(&mut body, i as u16);
    }
    while (body.len() + 8) % 4 != 0 {
        body.push(0xFF);
    }
    let attributes_at = body.len() + 8;
    if many {
        put32(&mut body, 0);
        put32(&mut body, 1);
    }
    for attribute in [9u32, 10, 13] {
        put32(&mut body, attribute);
        put32(&mut body, 3);
    }
    put32(&mut body, 0xFF);
    put32(&mut body, 0);
    let matrix_table_at = body.len() + 8;
    matrix_table.iter().for_each(|m| put16(&mut body, *m));
    pad_to(&mut body, 0);
    let lists_at = body.len() + 8;
    body.extend_from_slice(&lists);
    let matrix_data_at = body.len() + 8;
    body.extend_from_slice(&matrix_data);
    let packets_at = body.len() + 8;
    body.extend_from_slice(&packets);
    for (field, value) in [(4, shapes_at), (8, remap_at), (12, 0), (16, attributes_at), (20, matrix_table_at), (24, lists_at), (28, matrix_data_at), (32, packets_at)] {
        set32(&mut body, field, value as u32);
    }
    (block(b"SHP1", &body), packets.len() / 8)
}

/// One record per material: lit by the stage's lights, times its colour and texture.
fn write_mat3(used: &[usize], materials: &[MaterialOut], image_of: &BTreeMap<usize, usize>) -> Vec<u8> {
    const INIT: usize = 0x14C;
    let count = used.len();
    let mut init = vec![0u8; INIT * count];
    let mut colors: Vec<u8> = Vec::new();
    let mut texture_numbers: Vec<u16> = Vec::new();
    for (i, m) in used.iter().enumerate() {
        let material = &materials[*m];
        let r = &mut init[i * INIT..(i + 1) * INIT];
        r.fill(0xFF);
        r[0] = if material.blend { 4 } else { 1 };
        r[1] = u8::from(!material.two_sided); // the cull table: 0 none, 1 back
        r[2] = 0; // one channel pair
        r[3] = u8::from(material.texture.is_some()); // texture coordinates: table index 1 is one, 0 none
        r[4] = 0; // one stage
        r[5] = 0;
        r[6] = u8::from(material.blend); // depth: 0 writes, 1 doesn't
        r[7] = 0;
        let put = |r: &mut [u8], at: usize, v: u16| r[at..at + 2].copy_from_slice(&v.to_be_bytes());
        put(r, 0x08, i as u16); // colour
        put(r, 0x0C, 0); // lit colour
        put(r, 0x0E, 1); // unlit alpha
        put(r, 0x14, 0); // ambient
        if let Some(source) = material.texture {
            put(r, 0x28, 0); // coordinate 0 from the first set
            put(r, 0x84, texture_numbers.len() as u16);
            texture_numbers.push(image_of[&source] as u16);
            put(r, 0xBC, 0); // stage order: textured
            put(r, 0xE4, 0); // stage: texture times lit
        } else {
            put(r, 0xBC, 1); // stage order: colour only
            put(r, 0xE4, 1); // stage: lit
        }
        put(r, 0x104, 0); // swap mode
        put(r, 0x124, 0); // swap tables
        put(r, 0x126, 0);
        put(r, 0x128, 0);
        put(r, 0x12A, 0);
        put(r, 0x146, match material.cutoff { Some(_) => (i + 1) as u16, None => 0 }); // alpha test
        put(r, 0x148, u16::from(material.blend)); // blend
        colors.extend_from_slice(&material.color);
    }
    let mut alpha_tests: Vec<u8> = vec![7, 0, 0, 7, 0, 0xFF, 0xFF, 0xFF];
    for m in used {
        let reference = materials[*m].cutoff.unwrap_or(0);
        alpha_tests.extend_from_slice(&[if materials[*m].cutoff.is_some() { 6 } else { 7 }, reference, 0, 7, 0, 0xFF, 0xFF, 0xFF]);
    }
    let names: Vec<String> = used.iter().map(|m| materials[*m].name.clone()).collect();
    let remap: Vec<u8> = (0..count as u16).flat_map(u16::to_be_bytes).collect();
    let textures: Vec<u8> = texture_numbers.iter().flat_map(|t| t.to_be_bytes()).collect();
    // Each table in the block's header order, from 0x0C.
    let tables: Vec<(usize, Vec<u8>)> = vec![
        (0x0C, init),
        (0x10, remap),
        (0x14, name_table(&names)),
        (0x1C, vec![0, 0, 0, 0, 0, 0, 0, 2]),
        (0x20, colors),
        (0x24, vec![1]),
        (0x28, vec![1, 0, 3, 2, 2, 0, 0xFF, 0xFF, 0, 0, 0, 2, 2, 0, 0xFF, 0xFF]),
        (0x2C, vec![0x32, 0x32, 0x32, 0xFF]),
        (0x34, vec![0, 1]),
        (0x38, vec![1, 4, 60, 0xFF]),
        (0x48, textures),
        (0x4C, vec![0, 0, 4, 0xFF, 0xFF, 0xFF, 4, 0xFF]),
        (0x50, vec![0, 0, 0, 0xFF, 0, 0xFF, 0, 0xFF]),
        (0x58, vec![1]),
        (0x5C, vec![
            // Texture times lit, colour and alpha.
            0xFF, 15, 8, 10, 15, 0, 0, 0, 1, 0, 7, 4, 5, 7, 0, 0, 0, 1, 0, 0xFF,
            // Lit alone.
            0xFF, 10, 15, 15, 15, 0, 0, 0, 1, 0, 5, 7, 7, 7, 0, 0, 0, 1, 0, 0xFF,
        ]),
        (0x60, vec![0, 0, 0xFF, 0xFF]),
        (0x64, vec![0, 1, 2, 3]),
        (0x6C, alpha_tests),
        (0x70, vec![0, 1, 0, 3, 1, 4, 5, 3]),
        (0x74, vec![1, 3, 1, 0xFF, 1, 3, 0, 0xFF]),
        (0x78, vec![1, 0xFF, 0xFF, 0xFF]),
        (0x7C, vec![0, 0xFF, 0xFF, 0xFF]),
    ];
    let mut body = Vec::new();
    put16(&mut body, count as u16);
    put16(&mut body, 0xFFFF);
    body.extend_from_slice(&[0; 0x78]);
    for (field, table) in tables {
        while (body.len() + 8) % 4 != 0 {
            body.push(0xFF);
        }
        let at = body.len() + 8;
        set32(&mut body, field - 8, at as u32);
        body.extend_from_slice(&table);
    }
    block(b"MAT3", &body)
}

fn write_tex1(images: &[(String, u32, u32, Vec<u8>, bool)]) -> Vec<u8> {
    let mut body = Vec::new();
    put16(&mut body, images.len() as u16);
    put16(&mut body, 0xFFFF);
    put32(&mut body, 0x20);
    put32(&mut body, 0);
    body.extend_from_slice(&[0; 12]);
    let headers_at = body.len();
    body.resize(headers_at + images.len() * 32, 0);
    for (i, (_, width, height, data, has_alpha)) in images.iter().enumerate() {
        while (body.len() + 8) % 32 != 0 {
            body.push(0);
        }
        let header_at = headers_at + i * 32;
        let offset = (body.len() - header_at) as u32;
        body[header_at..header_at + 32].copy_from_slice(&image_header(*width, *height, offset, 1, *has_alpha));
        body.extend_from_slice(data);
    }
    let names_at = body.len() + 8;
    let names: Vec<String> = images.iter().map(|(n, ..)| n.clone()).collect();
    body.extend_from_slice(&name_table(&names));
    set32(&mut body, 8, names_at as u32);
    block(b"TEX1", &body)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A triangle, as a `.gltf` with its data in place.
    fn triangle() -> Vec<u8> {
        let mut data = Vec::new();
        for v in [0.0f32, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0] {
            data.extend_from_slice(&v.to_le_bytes());
        }
        for v in [0u16, 1, 2] {
            data.extend_from_slice(&v.to_le_bytes());
        }
        let encoded = encode(&data);
        format!(
            r#"{{"asset":{{"version":"2.0"}},"scene":0,"scenes":[{{"nodes":[0]}}],"nodes":[{{"mesh":0}}],
            "meshes":[{{"primitives":[{{"attributes":{{"POSITION":0}},"indices":1}}]}}],
            "accessors":[{{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}},{{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}}],
            "bufferViews":[{{"buffer":0,"byteLength":36}},{{"buffer":0,"byteOffset":36,"byteLength":6}}],
            "buffers":[{{"byteLength":42,"uri":"data:application/octet-stream;base64,{encoded}"}}]}}"#
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
    fn a_triangle_is_a_model_the_cooker_reads() {
        let model = to_j3d(&triangle(), None).unwrap();
        assert!(model.starts_with(b"J3D2bmd3"));
        assert_eq!(u32::from_be_bytes(model[8..12].try_into().unwrap()) as usize, model.len());
        // What the cook reads: its materials, then its blocks in host order.
        let shaders = super::super::material::generated_shaders(&model).unwrap();
        assert_eq!(shaders.len(), 1);
        assert_eq!(shaders[0].0, "material");
        super::super::native_data::j3d_model(&model).unwrap();
    }

    #[test]
    fn triangles_wind_as_the_games_do() {
        let model = to_j3d(&triangle(), None).unwrap();
        let shp = model.windows(4).position(|w| w == b"SHP1").unwrap();
        let word = |at: usize| u32::from_be_bytes(model[at..at + 4].try_into().unwrap()) as usize;
        let lists = shp + word(shp + 0x20);
        assert_eq!(model[lists], 0x90);
        let corner = |n: usize| u16::from_be_bytes(model[lists + 3 + n * 6..lists + 5 + n * 6].try_into().unwrap());
        assert_eq!((corner(0), corner(1), corner(2)), (0, 2, 1));
    }

    #[test]
    fn a_rotation_becomes_the_games_angles() {
        // A quarter turn about Y.
        let half = std::f32::consts::FRAC_1_SQRT_2;
        let quarter = angles([0.0, half, 0.0, half]);
        assert_eq!(quarter[0], 0);
        assert!((i32::from(quarter[1]) - 16384).abs() <= 8, "{quarter:?}");
        assert_eq!(angles([0.0, 0.0, 0.0, 1.0]), [0, 0, 0]);
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
    fn a_bad_file_says_what_is_wrong() {
        assert!(format!("{:#}", to_j3d(b"nope", None).unwrap_err()).contains("JSON"));
        let old = br#"{"asset":{"version":"1.0"}}"#;
        assert!(format!("{:#}", to_j3d(old, None).unwrap_err()).contains("glTF 2"));
    }
}
