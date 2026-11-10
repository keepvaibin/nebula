//! A modern model in place of the game's own format: glTF 2.0 (`.gltf` with its files, or `.glb`) read straight into
//! `ModelIr` and `ClipIr`, with a clip for each animation. Triangles, a colour and up to sixteen texture layers per
//! material (the base colour texture, then `extras.nebula.textures`), skins and node animation. Lengths are metres in glTF: one metre is 100 of the game's units. A glTF triangle winds counter-clockwise;
//! the game's wind clockwise (measured on Kuribo, Coin and Mario: 650 to 2, 268 to 0, 4514 to 363).

use super::clip::{ClipIr, Target, Track, KIND_COLORS, KIND_JOINTS, KIND_PATTERN, KIND_TEXTURE_MATRICES, STEP};
use super::model_ir::{CoordIr, CoordMatrixIr};
use super::model_ir::{important_joints, plain_material, plain_texture, ArraysIr, Corner, GroupIr, JointIr, ModelIr, ShapeIr, ATTR_MATRIX, ATTR_NORMAL, NONE};
use anyhow::{bail, Context, Result};
use serde_json::Value;
use std::collections::{BTreeMap, BTreeSet};
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
        let count = accessor["count"].as_u64().context("an accessor with no count")?;
        // Far past any model the game draws: a damaged count would ask for memory no device has.
        if count > MAX_ELEMENTS {
            bail!("an accessor of {count} elements: a model has up to {MAX_ELEMENTS}");
        }
        let count = count as usize;
        let kind = accessor["componentType"].as_u64().context("an accessor with no type")?;
        let normalized = accessor["normalized"].as_bool().unwrap_or(false);
        let (size, read): (usize, fn(&[u8]) -> f32) = match kind {
            5126 => (4, |b| f32::from_le_bytes([b[0], b[1], b[2], b[3]])),
            5120 => (1, |b| f32::from(b[0] as i8)),
            5121 => (1, |b| f32::from(b[0])),
            5122 => (2, |b| f32::from(i16::from_le_bytes([b[0], b[1]]))),
            5123 => (2, |b| f32::from(u16::from_le_bytes([b[0], b[1]]))),
            5125 => (4, |b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]) as f32),
            other => bail!("numbers of type {other}"),
        };
        // Normalized integers as glTF reads them: unsigned to 0..1, signed to -1..1.
        let scale = |value: f32| match (normalized, kind) {
            (true, 5120) => (value / 127.0).max(-1.0),
            (true, 5121) => value / 255.0,
            (true, 5122) => (value / 32767.0).max(-1.0),
            (true, 5123) => value / 65535.0,
            _ => value,
        };
        // No buffer view: all zero, unless sparse values say otherwise.
        let mut out = match accessor["bufferView"].as_u64() {
            Some(view_index) => {
                let view = self.view(view_index as usize)?;
                let start = accessor["byteOffset"].as_u64().unwrap_or(0) as usize;
                let stride = self.root["bufferViews"][view_index as usize]["byteStride"].as_u64().map_or(size * width, |s| s as usize);
                let mut out = Vec::with_capacity(count * width);
                for element in 0..count {
                    for c in 0..width {
                        let at = start + element * stride + c * size;
                        out.push(scale(read(view.get(at..at + size).context("an accessor past its buffer view")?)));
                    }
                }
                out
            }
            None => vec![0.0; count * width],
        };
        self.sparse_into(accessor, &mut out, width, size, read, scale)?;
        Ok(out)
    }

    /// An accessor's sparse values over its others.
    fn sparse_into(&self, accessor: &Value, out: &mut [f32], width: usize, size: usize, read: fn(&[u8]) -> f32, scale: impl Fn(f32) -> f32) -> Result<()> {
        let sparse = &accessor["sparse"];
        if sparse.is_null() {
            return Ok(());
        }
        let count = sparse["count"].as_u64().context("sparse values with no count")? as usize;
        let indices = &sparse["indices"];
        let index_view = self.view(indices["bufferView"].as_u64().context("sparse values with no indices")? as usize)?;
        let index_start = indices["byteOffset"].as_u64().unwrap_or(0) as usize;
        let index_size = match indices["componentType"].as_u64() {
            Some(5121) => 1,
            Some(5123) => 2,
            Some(5125) => 4,
            _ => bail!("sparse indices that aren't whole numbers"),
        };
        let values = &sparse["values"];
        let value_view = self.view(values["bufferView"].as_u64().context("sparse values with no values")? as usize)?;
        let value_start = values["byteOffset"].as_u64().unwrap_or(0) as usize;
        for k in 0..count {
            let at = index_start + k * index_size;
            let b = index_view.get(at..at + index_size).context("sparse indices past their buffer view")?;
            let element = match index_size {
                1 => usize::from(b[0]),
                2 => usize::from(u16::from_le_bytes([b[0], b[1]])),
                _ => u32::from_le_bytes([b[0], b[1], b[2], b[3]]) as usize,
            };
            for c in 0..width {
                let at = value_start + (k * width + c) * size;
                let value = scale(read(value_view.get(at..at + size).context("sparse values past their buffer view")?));
                *out.get_mut(element * width + c).context("a sparse value past its accessor")? = value;
            }
        }
        Ok(())
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

/// The extensions a file can require and still be read as made: anything else would cook into something it isn't.
const EXTENSIONS_READ: [&str; 3] = ["KHR_texture_transform", "KHR_animation_pointer", "KHR_mesh_quantization"];

fn required_extensions(root: &Value) -> Result<()> {
    for name in root["extensionsRequired"].as_array().map(Vec::as_slice).unwrap_or_default() {
        let name = name.as_str().unwrap_or("");
        if !EXTENSIONS_READ.contains(&name) {
            bail!("the file needs {name}, which Nebula doesn't read: export without it (it reads {})", EXTENSIONS_READ.join(", "));
        }
    }
    Ok(())
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

/// The most elements one accessor of a model may have: a primitive's vertices, far past any the game draws.
const MAX_ELEMENTS: u64 = 1 << 22;

/// The widest and tallest image a mod may bring: past any GPU's textures, and a damaged header can't ask for more
/// memory than a gigabyte of pixels.
const MAX_IMAGE_SIDE: u32 = 16384;

/// A PNG as RGBA, top row first.
pub fn decode_image(bytes: &[u8]) -> Result<(u32, u32, Vec<u8>)> {
    if bytes.starts_with(&[0xFF, 0xD8, 0xFF]) {
        return decode_jpeg(bytes);
    }
    if !bytes.starts_with(&[0x89, b'P', b'N', b'G']) {
        bail!("images have to be PNG or JPEG");
    }
    let mut decoder = png::Decoder::new(bytes);
    decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16 | png::Transformations::ALPHA);
    let mut reader = decoder.read_info().context("the PNG can't be read")?;
    let (width, height) = reader.info().size();
    if width > MAX_IMAGE_SIDE || height > MAX_IMAGE_SIDE {
        bail!("a {width} by {height} image: images go up to {MAX_IMAGE_SIDE} pixels a side");
    }
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

/// A JPEG as RGBA, top row first.
fn decode_jpeg(bytes: &[u8]) -> Result<(u32, u32, Vec<u8>)> {
    use zune_jpeg::zune_core::{bytestream::ZCursor, colorspace::ColorSpace, options::DecoderOptions};
    let options = DecoderOptions::default().jpeg_set_out_colorspace(ColorSpace::RGBA).set_max_width(MAX_IMAGE_SIDE as usize).set_max_height(MAX_IMAGE_SIDE as usize);
    let mut decoder = zune_jpeg::JpegDecoder::new_with_options(ZCursor::new(bytes), options);
    let rgba = decoder.decode().map_err(|e| anyhow::anyhow!("the JPEG can't be read: {e:?}"))?;
    let info = decoder.info().context("the JPEG can't be read")?;
    let (width, height) = (u32::from(info.width), u32::from(info.height));
    if rgba.len() != width as usize * height as usize * 4 {
        bail!("the JPEG can't be read: {} bytes for {width} by {height}", rgba.len());
    }
    Ok((width, height, rgba))
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
    /// Its base colour texture's KHR_texture_transform, still or animated: scale x, y, rotation, offset x, y.
    transform: Option<[f32; 5]>,
}

/// The texture transform no KHR_texture_transform gives.
const NO_TRANSFORM: [f32; 5] = [1.0, 1.0, 0.0, 0.0, 0.0];

/// A texture transform as the game's texture matrix (the classic one, about no centre): glTF's turns the other way, and
/// scales before it turns where the game scales after, which is the same only when both scales are equal.
fn texture_transform(t: &[f32; 5], material: &str) -> Result<[f32; 5]> {
    if t[2].abs() > 1e-6 && (t[0] - t[1]).abs() > 1e-5 * t[0].abs().max(t[1].abs()).max(1.0) {
        bail!("material {material}: a texture turned with unequal scales ({} and {}): scale it evenly, or not turn it", t[0], t[1]);
    }
    Ok([t[0], t[1], -t[2], t[3], t[4]])
}

/// A KHR_animation_pointer channel's pointer, if it is one.
fn pointer_of(channel: &Value) -> Option<&str> {
    (channel["target"]["path"].as_str() == Some("pointer")).then(|| channel["target"]["extensions"]["KHR_animation_pointer"]["pointer"].as_str()).flatten()
}

/// "/materials/<n>/<rest>" as (n, rest).
fn material_pointer(pointer: &str) -> Option<(usize, &str)> {
    let rest = pointer.strip_prefix("/materials/")?;
    let (number, rest) = rest.split_once('/')?;
    Some((number.parse().ok()?, rest))
}

const BASE_COLOR: &str = "pbrMetallicRoughness/baseColorFactor";
const TEXTURE_TRANSFORM: &str = "pbrMetallicRoughness/baseColorTexture/extensions/KHR_texture_transform/";

/// The materials whose texture transform an animation moves, and the textures (glTF texture numbers) animations swap in.
fn animated_materials(root: &Value) -> (BTreeSet<usize>, BTreeSet<usize>) {
    let (mut moved, mut swapped) = (BTreeSet::new(), BTreeSet::new());
    for animation in root["animations"].as_array().map(Vec::as_slice).unwrap_or_default() {
        for channel in animation["channels"].as_array().map(Vec::as_slice).unwrap_or_default() {
            if let Some((material, rest)) = pointer_of(channel).and_then(material_pointer) {
                if rest.starts_with(TEXTURE_TRANSFORM) {
                    moved.insert(material);
                }
            }
        }
        for swap in animation["extras"]["nebula"]["texture_swaps"].as_array().map(Vec::as_slice).unwrap_or_default() {
            for key in swap["keys"].as_array().map(Vec::as_slice).unwrap_or_default() {
                if let Some(texture) = key[1].as_u64() {
                    swapped.insert(texture as usize);
                }
            }
        }
    }
    (moved, swapped)
}

/// The model's texture number of a glTF image, cooked into the model the first time it is wanted.
fn texture_number(document: &Document, ir: &mut ModelIr, image_of: &mut BTreeMap<usize, u32>, image: usize) -> Result<u32> {
    if let Some(number) = image_of.get(&image) {
        return Ok(*number);
    }
    let (width, height, rgba) = document.image(image)?;
    let number = ir.textures.len() as u32;
    ir.records.push(super::texture::cook_rgba(width, height, &rgba)?);
    ir.textures.push(plain_texture(format!("image{image}"), width, height, ir.records.len() - 1));
    image_of.insert(image, number);
    Ok(number)
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
    required_extensions(root)?;
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
    let (moved, swapped) = animated_materials(root);
    let mut materials: Vec<SourceMaterial> = Vec::new();
    for (i, material) in root["materials"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
        let pbr = &material["pbrMetallicRoughness"];
        let factor: [f32; 4] = numbers(&pbr["baseColorFactor"], [1.0; 4]);
        let image = pbr["baseColorTexture"]["index"].as_u64().and_then(|t| root["textures"][t as usize]["source"].as_u64()).map(|s| s as usize);
        let still = &pbr["baseColorTexture"]["extensions"]["KHR_texture_transform"];
        let transform = (still.is_object() || moved.contains(&i)).then(|| {
            let [sx, sy]: [f32; 2] = numbers(&still["scale"], [1.0; 2]);
            let [ox, oy]: [f32; 2] = numbers(&still["offset"], [0.0; 2]);
            [sx, sy, still["rotation"].as_f64().unwrap_or(0.0) as f32, ox, oy]
        });
        let mut images = Vec::new();
        if let Some(image) = image {
            let coordinates = still["texCoord"].as_u64().or(pbr["baseColorTexture"]["texCoord"].as_u64()).unwrap_or(0);
            images.push((image, coordinates));
        } else if transform.is_some() {
            bail!("material {i}: a texture transform with no base colour texture");
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
            transform,
        });
    }
    let default_material = materials.len();
    materials.push(SourceMaterial { name: "material".to_string(), color: [255; 4], images: Vec::new(), blend: false, cutoff: None, two_sided: false, transform: None });

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
            if room > super::limits::MATERIAL_SLOTS {
                bail!("TEXCOORD_{}: a model has up to {} sets of texture coordinates", room - 1, super::limits::MATERIAL_SLOTS);
            }
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
                texture_ids.push(texture_number(&document, &mut ir, &mut image_of, image)?);
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
            // Its base texture's transform: texture matrix 0 on its first coordinates.
            if let Some(t) = source.transform {
                let [sx, sy, rotation, tx, ty] = texture_transform(&t, &source.name)?;
                made.coords[0].matrix = 0;
                let mut effect = [[0.0f32; 4]; 4];
                for (k, row) in effect.iter_mut().enumerate() {
                    row[k] = 1.0;
                }
                made.matrices.push(CoordMatrixIr { slot: 0, projection: 1, info: 0, center: [0.0; 3], scale: [sx, sy], rotation, translation: [tx, ty], effect });
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
    // Textures an animation swaps in, which no material shows to begin with.
    for texture in &swapped {
        let image = root["textures"][*texture]["source"].as_u64().with_context(|| format!("an animation swaps in texture {texture}, which has no image"))?;
        texture_number(&document, &mut ir, &mut image_of, image as usize)?;
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

    // Each animation: what moves the skeleton (.bck), a material's colour (.bpk), its texture's transform (.btk), its
    // texture swaps (.btp), each by the animation's name; one moving nothing is a still .bck.
    let mut clips = Vec::new();
    for (i, animation) in root["animations"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
        let name = animation["name"].as_str().map_or_else(|| format!("animation{i}"), str::to_string);
        let timing = Timing::of(&document, animation)?;
        let of_materials = material_clips(&document, animation, &materials, &first_of_material, &image_of, &timing)?;
        let moves_joints = animation["channels"].as_array().map(Vec::as_slice).unwrap_or_default().iter().any(|c| {
            c["target"]["node"].as_u64().is_some_and(|n| joint_of_node.contains_key(&(n as usize)))
                && matches!(c["target"]["path"].as_str(), Some("translation" | "rotation" | "scale"))
        });
        if moves_joints || of_materials.is_empty() {
            clips.push((name.clone(), clip_of(&document, animation, &joints, &joint_of_node, &timing)?));
        }
        clips.extend(of_materials.into_iter().map(|clip| (name.clone(), clip)));
    }
    Ok((ir, clips))
}

/// An animation's length and how it plays, the same for each of its files.
struct Timing {
    frames: usize,
    /// The frames sampled: every other one, and the last.
    samples: Vec<usize>,
    loop_mode: u8,
}

impl Timing {
    fn of(document: &Document, animation: &Value) -> Result<Timing> {
        let samplers = animation["samplers"].as_array().map(Vec::as_slice).unwrap_or_default();
        let mut length = 0.0f32;
        for channel in animation["channels"].as_array().map(Vec::as_slice).unwrap_or_default() {
            let sampler = samplers.get(channel["sampler"].as_u64().unwrap_or(0) as usize).context("a channel naming a sampler it doesn't have")?;
            let times = document.floats(sampler["input"].as_u64().context("a sampler with no times")? as usize, 1)?;
            length = length.max(times.last().copied().unwrap_or(0.0));
        }
        for swap in animation["extras"]["nebula"]["texture_swaps"].as_array().map(Vec::as_slice).unwrap_or_default() {
            for key in swap["keys"].as_array().map(Vec::as_slice).unwrap_or_default() {
                length = length.max(key[0].as_f64().unwrap_or(0.0) as f32);
            }
        }
        // How it plays: `extras.nebula.play` once, once_then_first (back to its first frame), loop, there_and_back (once
        // each way) or loop_there_and_back; left out, loop. One standing for a game class's animation of the same name
        // (a mod's class made like it) plays as that one does, as the class's code waits for it (the game sets it).
        let loop_mode = match animation["extras"]["nebula"]["play"].as_str() {
            None | Some("loop") => 2,
            Some("once") => 0,
            Some("once_then_first") => 1,
            Some("there_and_back") => 3,
            Some("loop_there_and_back") => 4,
            Some(other) => bail!("an animation plays {other:?}: once, once_then_first, loop, there_and_back or loop_there_and_back"),
        };
        let frames = ((length * FRAMES_PER_SECOND).ceil() as usize).max(1);
        let samples = (0..=frames).step_by(2).chain(std::iter::once(frames)).collect::<BTreeSet<_>>().into_iter().collect();
        Ok(Timing { frames, samples, loop_mode })
    }
}

/// The file a cooked animation is, by what it moves.
fn clip_extension(kind: u8) -> &'static str {
    match kind {
        KIND_COLORS => "bpk",
        KIND_TEXTURE_MATRICES => "btk",
        KIND_PATTERN => "btp",
        _ => "bck",
    }
}

/// A material's curve read from a pointer channel.
fn pointer_curve(document: &Document, samplers: &[Value], channel: &Value, width: usize) -> Result<Curve> {
    let sampler = samplers.get(channel["sampler"].as_u64().unwrap_or(0) as usize).context("a channel naming a sampler it doesn't have")?;
    let times = document.floats(sampler["input"].as_u64().context("a sampler with no times")? as usize, 1)?;
    let values = document.floats(sampler["output"].as_u64().context("a sampler with no values")? as usize, width)?;
    if times.is_empty() {
        bail!("an animation channel with no times");
    }
    let mode = sampler["interpolation"].as_str().unwrap_or("LINEAR");
    Ok(Curve { times, values, width, step: mode == "STEP", cubic: mode == "CUBICSPLINE", turns: false })
}

/// What an animation does to materials: their colours, their textures' transforms, their texture swaps.
fn material_clips(
    document: &Document,
    animation: &Value,
    materials: &[SourceMaterial],
    first_of_material: &BTreeMap<usize, u32>,
    image_of: &BTreeMap<usize, u32>,
    timing: &Timing,
) -> Result<Vec<ClipIr>> {
    let samplers = animation["samplers"].as_array().map(Vec::as_slice).unwrap_or_default();
    let mut colours: BTreeMap<usize, Curve> = BTreeMap::new();
    let mut moves: BTreeMap<usize, BTreeMap<&str, Curve>> = BTreeMap::new();
    for channel in animation["channels"].as_array().map(Vec::as_slice).unwrap_or_default() {
        if channel["target"]["path"].as_str() != Some("pointer") {
            continue;
        }
        let pointer = pointer_of(channel).context("an animation channel with a pointer path and no KHR_animation_pointer pointer")?;
        let animates = "Nebula animates a material's baseColorFactor and its base colour texture's KHR_texture_transform offset, scale and rotation";
        let Some((material, rest)) = material_pointer(pointer) else { bail!("an animation moves {pointer}: {animates}") };
        if material + 1 >= materials.len() {
            bail!("an animation moves {pointer}, a material the file doesn't have");
        }
        if rest == BASE_COLOR {
            colours.insert(material, pointer_curve(document, samplers, channel, 4)?);
        } else if let Some(part @ ("offset" | "scale" | "rotation")) = rest.strip_prefix(TEXTURE_TRANSFORM) {
            let width = if part == "rotation" { 1 } else { 2 };
            moves.entry(material).or_default().insert(part, pointer_curve(document, samplers, channel, width)?);
        } else {
            bail!("an animation moves {pointer}: {animates}");
        }
    }
    let drawn = |material: usize| {
        first_of_material.get(&material).copied().with_context(|| format!("an animation changes material {}, which no mesh draws", materials[material].name))
    };
    let target = |material: usize, number: u32| Target { name: materials[material].name.as_bytes().to_vec(), material: number, slot: 0, center: [0.0; 3] };
    let clip = |kind: u8, count: usize| ClipIr { kind, loop_mode: timing.loop_mode, frames: timing.frames as i32, first_count: count as u32, ..ClipIr::default() };
    let mut out = Vec::new();

    if !colours.is_empty() {
        let mut c = clip(KIND_COLORS, colours.len());
        for (&material, curve) in &colours {
            let number = drawn(material)?;
            let mut channels: [Vec<f32>; 4] = Default::default();
            for frame in &timing.samples {
                let value = curve.at(*frame as f32 / FRAMES_PER_SECOND);
                for (k, channel) in channels.iter_mut().enumerate() {
                    channel.push(value[k].clamp(0.0, 1.0) * 255.0);
                }
            }
            for channel in &channels {
                let track = axis_track(&mut c.values, &timing.samples, channel)?;
                c.tracks.push(track);
            }
            c.targets.push(target(material, number));
        }
        out.push(c);
    }

    if !moves.is_empty() {
        let mut c = clip(KIND_TEXTURE_MATRICES, moves.len());
        for (&material, curves) in &moves {
            let number = drawn(material)?;
            let rest = materials[material].transform.unwrap_or(NO_TRANSFORM);
            let mut parts: [Vec<f32>; 5] = Default::default();
            for frame in &timing.samples {
                let time = *frame as f32 / FRAMES_PER_SECOND;
                let mut t = rest;
                if let Some(curve) = curves.get("scale") {
                    let v = curve.at(time);
                    t[0] = v[0];
                    t[1] = v[1];
                }
                if let Some(curve) = curves.get("rotation") {
                    t[2] = curve.at(time)[0];
                }
                if let Some(curve) = curves.get("offset") {
                    let v = curve.at(time);
                    t[3] = v[0];
                    t[4] = v[1];
                }
                for (part, value) in parts.iter_mut().zip(texture_transform(&t, &materials[material].name)?) {
                    part.push(value);
                }
            }
            for part in &parts {
                let track = axis_track(&mut c.values, &timing.samples, part)?;
                c.tracks.push(track);
            }
            c.targets.push(target(material, number));
        }
        out.push(c);
    }

    // Texture swaps: `extras.nebula.texture_swaps`, each { "material": <n>, "keys": [[<second>, <texture>], ...] }, the
    // texture shown from that second on (before the first key, the first key's).
    let swaps = animation["extras"]["nebula"]["texture_swaps"].as_array().map(Vec::as_slice).unwrap_or_default();
    if !swaps.is_empty() {
        let mut c = clip(KIND_PATTERN, swaps.len());
        for swap in swaps {
            let material = swap["material"].as_u64().context("a texture swap with no material number")? as usize;
            if material + 1 >= materials.len() {
                bail!("a texture swap of material {material}, which the file doesn't have");
            }
            if materials[material].images.is_empty() {
                bail!("material {}: texture swaps with no base colour texture to swap", materials[material].name);
            }
            let number = drawn(material)?;
            let mut keys: Vec<(f32, u32)> = Vec::new();
            for key in swap["keys"].as_array().map(Vec::as_slice).unwrap_or_default() {
                let second = key[0].as_f64().context("a texture swap's key is [second, texture]")? as f32;
                let texture = key[1].as_u64().context("a texture swap's key is [second, texture]")? as usize;
                let image = document.root["textures"][texture]["source"].as_u64().with_context(|| format!("texture {texture} has no image"))?;
                keys.push((second * FRAMES_PER_SECOND, *image_of.get(&(image as usize)).context("a swapped texture the model lacks")?));
            }
            if keys.is_empty() {
                bail!("material {}: a texture swap with no keys", materials[material].name);
            }
            keys.sort_by(|a, b| a.0.total_cmp(&b.0));
            let at = c.indices.len() as u32;
            for frame in 0..=timing.frames {
                let shown = keys.iter().rev().find(|(from, _)| *from <= frame as f32 + 1e-3).unwrap_or(&keys[0]);
                c.indices.push(shown.1);
            }
            c.tracks.push(Track { count: (timing.frames + 1) as u32, stride: STEP, integer_keys: false, at });
            c.targets.push(target(material, number));
        }
        out.push(c);
    }
    Ok(out)
}

/// A glTF document as the store's blocks: the model, and each animation by its name.
pub fn to_files(bytes: &[u8], folder: Option<&Path>, stem: &str) -> Result<(Vec<u8>, Vec<(String, Vec<u8>)>)> {
    let (ir, clips) = to_ir(bytes, folder)?;
    let model = super::model_write::write(&ir, stem, &super::programs::written_or_unknown())?;
    Ok((model, clips.iter().map(|(name, c)| (format!("{name}.{}", clip_extension(c.kind)), super::clip::write(c))).collect()))
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
    /// A rotation (a quaternion): blended the shorter way, and kept a rotation.
    turns: bool,
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
        let flip = if self.turns && a.iter().zip(b).map(|(x, y)| x * y).sum::<f32>() < 0.0 { -1.0 } else { 1.0 };
        let mut out: Vec<f32> = a.iter().zip(b).map(|(x, y)| x + (y * flip - x) * f).collect();
        if self.turns {
            let length = out.iter().map(|v| v * v).sum::<f32>().sqrt().max(1e-6);
            out.iter_mut().for_each(|v| *v /= length);
        }
        out
    }
}

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

/// An animation, each joint sampled on every other frame.
fn clip_of(document: &Document, animation: &Value, joints: &[Joint], joint_of_node: &BTreeMap<usize, usize>, timing: &Timing) -> Result<ClipIr> {
    let samplers = animation["samplers"].as_array().context("an animation with no samplers")?;
    let mut curves: BTreeMap<(usize, &str), Curve> = BTreeMap::new();
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
        let mode = sampler["interpolation"].as_str().unwrap_or("LINEAR");
        curves.insert((joint, key), Curve { times, values, width, step: mode == "STEP", cubic: mode == "CUBICSPLINE", turns: width == 4 });
    }
    let samples = &timing.samples;
    let mut clip = ClipIr { kind: KIND_JOINTS, loop_mode: timing.loop_mode, frames: timing.frames as i32, first_count: joints.len() as u32, ..ClipIr::default() };
    for (index, joint) in joints.iter().enumerate() {
        let mut scale_values = [vec![], vec![], vec![]];
        let mut angle_values: [Vec<f32>; 3] = [vec![], vec![], vec![]];
        let mut translate_values = [vec![], vec![], vec![]];
        for frame in samples {
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
            let scale = axis_track(&mut clip.values, samples, &scale_values[axis])?;
            let rotation = axis_track(&mut clip.values, samples, &angle_values[axis])?;
            let translation = axis_track(&mut clip.values, samples, &translate_values[axis])?;
            clip.tracks.extend_from_slice(&[scale, rotation, translation]);
        }
    }
    Ok(clip)
}

/// A camera animation (the game's CameraAnim, NCAM) from the first camera of a glTF file and the first animation that
/// moves it. Its node's place and turn at each frame (1/60 s) give the eye, the point it watches (`watch_distance`
/// metres ahead, `extras.nebula.watch_distance` on the camera's node, default 10) and the twist about the view; the
/// camera's `yfov` (or an animation of it, `/cameras/<n>/perspective/yfov`) the field of view. The game plays the clip
/// relative to what it follows (a cutscene's actor or point): the glTF's origin is that, its +Y that one's up. Parents
/// of the camera's node may place it but not move. Cuts are found as a disc clip's are.
pub fn to_camera(bytes: &[u8], folder: Option<&Path>) -> Result<Vec<u8>> {
    let document = Document::parse(bytes, folder)?;
    let root = &document.root;
    let nodes = root["nodes"].as_array().map(Vec::as_slice).unwrap_or_default();
    let node = nodes.iter().position(|n| n["camera"].is_u64()).context("no camera: a node with a \"camera\"")?;
    let camera_index = nodes[node]["camera"].as_u64().unwrap_or(0) as usize;
    let camera = &root["cameras"][camera_index];
    if camera["type"].as_str() != Some("perspective") {
        bail!("camera {camera_index} is not a perspective camera");
    }
    let yfov = camera["perspective"]["yfov"].as_f64().context("the camera has no yfov")? as f32;
    let watch = nodes[node]["extras"]["nebula"]["watch_distance"].as_f64().map_or(10.0, |d| d as f32) * UNITS_PER_METRE;
    // Its parents' placing, root first.
    let mut parent_of = BTreeMap::new();
    for (p, n) in nodes.iter().enumerate() {
        for c in n["children"].as_array().map(Vec::as_slice).unwrap_or_default() {
            parent_of.insert(c.as_u64().unwrap_or(0) as usize, p);
        }
    }
    let mut chain = Vec::new();
    let mut at = node;
    while let Some(&p) = parent_of.get(&at) {
        chain.push(p);
        at = p;
    }
    let parents = chain.iter().rev().fold(IDENTITY, |m, &p| multiply(&m, &node_matrix(&nodes[p])));
    let animations = root["animations"].as_array().map(Vec::as_slice).unwrap_or_default();
    let fov_pointer = format!("/cameras/{camera_index}/perspective/yfov");
    let moves = |a: &Value| {
        a["channels"].as_array().map(Vec::as_slice).unwrap_or_default().iter().any(|c| {
            c["target"]["node"].as_u64() == Some(node as u64) || pointer_of(c) == Some(fov_pointer.as_str())
        })
    };
    let animation = animations.iter().find(|a| moves(a)).context("no animation moves the camera")?;
    for c in animation["channels"].as_array().map(Vec::as_slice).unwrap_or_default() {
        if c["target"]["node"].as_u64().is_some_and(|n| chain.contains(&(n as usize))) {
            bail!("the camera's parent node {} moves: animate the camera's own node", c["target"]["node"]);
        }
    }
    let samplers = animation["samplers"].as_array().map(Vec::as_slice).unwrap_or_default();
    let (mut translation, mut rotation, mut fov) = (None, None, None);
    for c in animation["channels"].as_array().map(Vec::as_slice).unwrap_or_default() {
        if pointer_of(c) == Some(fov_pointer.as_str()) {
            fov = Some(pointer_curve(&document, samplers, c, 1)?);
        } else if c["target"]["node"].as_u64() == Some(node as u64) {
            match c["target"]["path"].as_str() {
                Some("translation") => translation = Some(pointer_curve(&document, samplers, c, 3)?),
                Some("rotation") => {
                    let mut curve = pointer_curve(&document, samplers, c, 4)?;
                    curve.turns = true;
                    rotation = Some(curve);
                }
                _ => {}
            }
        }
    }
    let length = [&translation, &rotation, &fov].iter().filter_map(|c| c.as_ref().and_then(|c| c.times.last().copied())).fold(0.0f32, f32::max);
    let frames = (length * FRAMES_PER_SECOND).round() as usize + 1;
    let rest = node_trs(&nodes[node]);
    let mut tracks: [Vec<f32>; super::camera::TRACKS] = Default::default();
    for frame in 0..frames {
        let time = frame as f32 / FRAMES_PER_SECOND;
        let place = translation.as_ref().map_or(rest.translation, |c| { let v = c.at(time); [v[0], v[1], v[2]] });
        let turn = rotation.as_ref().map_or(rest.rotation, |c| { let v = c.at(time); [v[0], v[1], v[2], v[3]] });
        let m = multiply(&parents, &trs_matrix(&Trs { translation: place, rotation: turn, scale: [1.0; 3] }));
        let unit = |v: [f32; 3]| { let l = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt().max(1e-6); v.map(|x| x / l) };
        let eye = transform(&m, [0.0; 3], 1.0).map(|v| v * UNITS_PER_METRE);
        let front = unit(transform(&m, [0.0, 0.0, -1.0], 0.0));
        let up = unit(transform(&m, [0.0, 1.0, 0.0], 0.0));
        // The twist CameraDirector::calcViewMtxFromPoseParam turns the untwisted view by (about its Z) to reach this up:
        // untwisted, X is -(world up x front), Y is front x side.
        let cross = |a: [f32; 3], b: [f32; 3]| [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
        let dot = |a: [f32; 3], b: [f32; 3]| a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        let side = unit(cross([0.0, 1.0, 0.0], front));
        let (x0, y0) = (side.map(|v| -v), unit(cross(front, side)));
        let twist = (-dot(up, x0)).atan2(dot(up, y0)).to_degrees();
        let fovy = fov.as_ref().map_or(yfov, |c| c.at(time)[0]).to_degrees();
        let values = [eye[0], eye[1], eye[2], eye[0] + front[0] * watch, eye[1] + front[1] * watch, eye[2] + front[2] * watch, twist, fovy];
        for (track, v) in tracks.iter_mut().zip(values) {
            track.push(v);
        }
    }
    let mut ncam = super::camera::Ncam { frames: frames as u32, ..Default::default() };
    for (i, values) in tracks.iter().enumerate() {
        // A track that never changes is one value.
        let held = values.iter().all(|v| (v - values[0]).abs() < 1e-4);
        let kept: &[f32] = if held { &values[..1] } else { values };
        ncam.tracks[i] = super::camera::Track { count: kept.len() as u32, first: ncam.values.len() as u32, stride: 1 };
        ncam.values.extend_from_slice(kept);
    }
    ncam.cuts = super::camera::find_cuts(&ncam);
    Ok(ncam.to_bytes())
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
        // Its names read back from the cooked model, as `lumalink list models` reads the game's.
        let names = super::super::model_write::names(&model).unwrap();
        assert_eq!(names.joints, ir.joints.iter().map(|j| j.name.clone()).collect::<Vec<_>>());
        assert_eq!(names.materials, ["material"]);
        assert_eq!(names.textures, ir.textures.iter().map(|t| t.name.clone()).collect::<Vec<_>>());
        assert!(super::super::model_write::names(&model[..8]).is_err());
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
    fn an_animation_plays_as_its_extras_say() {
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
        let error = to_camera(serde_json::json!({ "asset": { "version": "2.0" }, "nodes": [{}] }).to_string().as_bytes(), None).unwrap_err();
        assert!(error.to_string().contains("no camera"));
    }

    #[test]
    fn a_bad_file_says_what_is_wrong() {
        assert!(format!("{:#}", to_ir(b"nope", None).err().unwrap()).contains("JSON"));
        let old = br#"{"asset":{"version":"1.0"}}"#;
        assert!(format!("{:#}", to_ir(old, None).err().unwrap()).contains("glTF 2"));
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
        document["images"] = serde_json::json!([{ "uri": PIXEL }, { "uri": PIXEL }]);
        document["textures"] = serde_json::json!([{ "source": 0 }, { "source": 1 }]);
        document["materials"] = serde_json::json!([{ "name": "Face", "pbrMetallicRoughness": {
            "baseColorTexture": { "index": 0, "extensions": { "KHR_texture_transform": transform } } } }]);
        document
    }

    #[test]
    fn a_material_s_colour_scroll_and_swaps_cook_as_the_games() {
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
        // Red stays 255; green falls from 255 to half by the end.
        assert_eq!((glow.tracks[0].count, glow.values[glow.tracks[0].at as usize]), (1, 255.0));
        let green = &glow.tracks[1];
        let last = green.at as usize + (green.count as usize - 1) * 3;
        assert_eq!((glow.values[last], glow.values[last + 1]), (60.0, 127.5));
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
        let error = |document: &Value| format!("{:#}", to_ir(&serde_json::to_vec(document).unwrap(), None).err().expect("it cooked"));
        // Turned with uneven scales: glTF and the game scale and turn in different orders.
        let uneven = textured(serde_json::json!({ "scale": [2.0, 1.0], "rotation": 0.5 }));
        assert!(error(&uneven).contains("unequal"), "{}", error(&uneven));
        // Something else of a material.
        let mut other = textured(serde_json::json!({}));
        let a = add_floats(&mut other, &[(1, "SCALAR", vec![0.0]), (1, "VEC3", vec![1.0, 0.0, 0.0])]);
        other["animations"] = serde_json::json!([{ "name": "Shine", "samplers": [{ "input": a[0], "output": a[1] }],
            "channels": [{ "sampler": 0, "target": { "path": "pointer",
                "extensions": { "KHR_animation_pointer": { "pointer": "/materials/0/emissiveFactor" } } } }] }]);
        assert!(error(&other).contains("baseColorFactor"), "{}", error(&other));
        // A swap of a material that isn't there.
        let mut missing = textured(serde_json::json!({}));
        missing["animations"] = serde_json::json!([{ "name": "Blink", "extras": { "nebula": { "texture_swaps": [{ "material": 3, "keys": [[0.0, 1]] }] } } }]);
        assert!(error(&missing).contains("material 3"), "{}", error(&missing));
        // An extension the file needs and Nebula doesn't read.
        let mut draco = textured(serde_json::json!({}));
        draco["extensionsRequired"] = serde_json::json!(["KHR_draco_mesh_compression"]);
        assert!(error(&draco).contains("KHR_draco_mesh_compression"), "{}", error(&draco));
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
        let error = format!("{:#}", decode_image(&png).unwrap_err());
        assert!(error.contains("pixels a side"), "{error}");
    }

    #[test]
    fn a_jpeg_is_an_image() {
        // A 2 by 1 baseline JPEG of orange (200, 100, 50).
        let jpeg = base64("/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAIBAQEBAQIBAQECAgICAgQDAgICAgUEBAMEBgUGBgYFBgYGBwkIBgcJBwYGCAsICQoKCgoKBggLDAsKDAkKCgr/2wBDAQICAgICAgUDAwUKBwYHCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgr/wAARCAABAAIDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDPooor+Vz+2D//2Q==").unwrap();
        let (width, height, rgba) = decode_image(&jpeg).unwrap();
        assert_eq!((width, height, rgba.len()), (2, 1, 8));
        assert!(rgba[0].abs_diff(200) < 8 && rgba[1].abs_diff(100) < 8 && rgba[2].abs_diff(50) < 8 && rgba[3] == 255, "{rgba:?}");
        assert!(format!("{:#}", decode_image(b"GIF89a").err().unwrap()).contains("PNG or JPEG"));
    }
}
