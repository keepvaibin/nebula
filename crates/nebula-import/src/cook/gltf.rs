//! A modern model in place of the game's own format: glTF 2.0 (`.gltf` with its files, or `.glb`) read straight into
//! `ModelIr` and `ClipIr`, with a clip for each animation. Triangles, a colour and up to sixteen texture layers per
//! material (the base colour texture, then `extras.nebula.textures`), skins and node animation. Lengths are metres in glTF: one metre is 100 of the game's units (`UNITS_PER_METRE`, models and collision alike). A glTF triangle winds counter-clockwise;
//! the game's wind clockwise (measured on Kuribo, Coin and Mario: 650 to 2, 268 to 0, 4514 to 363).

use super::clip::{ClipIr, Target, Track, KIND_COLORS, KIND_JOINTS, KIND_PATTERN, KIND_REGISTERS, KIND_TEXTURE_MATRICES, STEP};
use super::model_ir::{CoordIr, CoordMatrixIr, MaterialIr};
use super::model_ir::{important_joints, plain_material, plain_texture, ArraysIr, Corner, GroupIr, JointIr, PbrMaterial, ModelIr, ShapeIr, ATTR_COLOR0, ATTR_MATRIX, ATTR_NORMAL, NONE};
use super::model_ir::{ATTR_NBT, ATTR_NBT_THREE, PBR_BASE, PBR_BLEND, PBR_DOUBLE_SIDED, PBR_EMISSIVE, PBR_MASK, PBR_NORMAL, PBR_UNLIT};
use super::model_ir::{PbrShading, SurfaceIr, PBR_METAL_ROUGH, PBR_OCCLUSION};
use super::refuse::{invalid, note, refuse, OrInvalid};
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

/// `bytes` as base64 text (with `=` padding), for an inline `data:` URI.
pub(crate) fn base64_text(bytes: &[u8]) -> String {
    const DIGITS: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity(bytes.len().div_ceil(3) * 4);
    for chunk in bytes.chunks(3) {
        let word = (u32::from(chunk[0]) << 16) | (u32::from(*chunk.get(1).unwrap_or(&0)) << 8) | u32::from(*chunk.get(2).unwrap_or(&0));
        for k in 0..4 {
            if k <= chunk.len() {
                out.push(char::from(DIGITS[((word >> (18 - 6 * k)) & 63) as usize]));
            } else {
                out.push('=');
            }
        }
    }
    out
}

fn base64(text: &str) -> Option<Vec<u8>> {
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
            _ => return None,
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
    Some(out)
}

/// A buffer's or an image's bytes by its `uri`, which `pointer` names: inline (`data:...;base64,`) or a file beside the
/// model, never outside its folder.
/// The uri is read by luma_validate::uri::model_uri, the reading L304 makes of it.
fn read_uri(uri: &str, folder: Option<&Path>, pointer: &str) -> Result<Vec<u8>> {
    use luma_validate::uri::{model_uri, ModelUri};
    let file = match model_uri(uri) {
        ModelUri::Inline(rest) => return rest.split_once(";base64,").and_then(|(_, data)| base64(data)).or_invalid(|| pointer.to_string()),
        ModelUri::Leaves => return Err(refuse("Cook.Model.Leaves", &[("uri", uri.to_string())])),
        ModelUri::File(file) => file,
    };
    let unreadable = || refuse("Cook.Model.Unreadable", &[("uri", uri.to_string())]);
    let folder = folder.ok_or_else(unreadable)?;
    super::beside::read(&file.split('/').fold(folder.to_path_buf(), |at, part| at.join(part))).map_err(|_| unreadable())
}

impl Document {
    fn parse(bytes: &[u8], folder: Option<&Path>) -> Result<Self> {
        let cut = || refuse("Cook.Model.Cut", &[]);
        let (json, chunk): (Vec<u8>, Option<Vec<u8>>) = if bytes.starts_with(b"glTF") {
            let word = |at: usize| -> Result<usize> {
                Ok(u32::from_le_bytes(bytes.get(at..at.checked_add(4).ok_or_else(cut)?).ok_or_else(cut)?.try_into()?) as usize)
            };
            let json_size = word(12)?;
            let next = 20usize.checked_add(json_size).ok_or_else(cut)?;
            let json = bytes.get(20..next).ok_or_else(cut)?.to_vec();
            let data = next.checked_add(8).ok_or_else(cut)?;
            let chunk = if data <= bytes.len() {
                let size = word(next)?;
                Some(bytes.get(data..data.checked_add(size).ok_or_else(cut)?).ok_or_else(cut)?.to_vec())
            } else {
                None
            };
            (json, chunk)
        } else {
            (bytes.to_vec(), None)
        };
        let root: Value = serde_json::from_slice(&json)
            .map_err(|e| refuse("Cook.Model.Json", &[("line", e.line().to_string()), ("column", e.column().to_string())]))?;
        if !root["asset"]["version"].as_str().is_some_and(|v| v.starts_with('2')) {
            let found = root["asset"]["version"].as_str().unwrap_or("").to_string();
            return Err(refuse("Cook.Model.Version", &[("found", found)]));
        }
        let mut buffers = Vec::new();
        for (i, buffer) in root["buffers"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
            let pointer = format!("/buffers/{i}/uri");
            buffers.push(match buffer["uri"].as_str() {
                Some(uri) => read_uri(uri, folder, &pointer)?,
                None if i == 0 => chunk.clone().or_invalid(|| pointer.clone())?,
                None => return Err(invalid(pointer)),
            });
        }
        Ok(Self { root, buffers, folder: folder.map(Path::to_path_buf) })
    }

    fn view(&self, index: usize) -> Result<&[u8]> {
        let view = &self.root["bufferViews"][index];
        let buffer = view["buffer"].as_u64().or_invalid(|| format!("/bufferViews/{index}/buffer"))?;
        let start = view["byteOffset"].as_u64().unwrap_or(0);
        let length = view["byteLength"].as_u64().or_invalid(|| format!("/bufferViews/{index}/byteLength"))?;
        let (buffer, start, length) = (place(buffer), place(start), place(length));
        buffer
            .and_then(|b| self.buffers.get(b))
            .and_then(|b| b.get(start?..start?.checked_add(length?)?))
            .ok_or_else(|| refuse("Cook.Model.ViewPast", &[("view", index.to_string())]))
    }

    /// The `byteStride` of a buffer view, or `packed` (its elements side by side) when it has none.
    fn stride(&self, view: u64, packed: usize) -> Option<usize> {
        match &self.root["bufferViews"][place(view)?]["byteStride"] {
            Value::Null => Some(packed),
            s => place(s.as_u64()?),
        }
    }

    /// An accessor's numbers, each element as `width` floats.
    fn floats(&self, index: usize, width: usize) -> Result<Vec<f32>> {
        let accessor = &self.root["accessors"][index];
        let count = accessor["count"].as_u64().or_invalid(|| format!("/accessors/{index}/count"))?;
        // Far past any model the game draws: a damaged count would ask for memory no device has.
        if count > MAX_ELEMENTS {
            return Err(refuse("Cook.Model.Accessor", &[("accessor", index.to_string()), ("count", count.to_string()), ("max", MAX_ELEMENTS.to_string())]));
        }
        let count = count as usize;
        let past = || refuse("Cook.Model.AccessorPast", &[("accessor", index.to_string())]);
        let kind = accessor["componentType"].as_u64().or_invalid(|| format!("/accessors/{index}/componentType"))?;
        let normalized = accessor["normalized"].as_bool().unwrap_or(false);
        let (size, read): (usize, fn(&[u8]) -> f32) = match kind {
            5126 => (4, |b| f32::from_le_bytes([b[0], b[1], b[2], b[3]])),
            5120 => (1, |b| f32::from(b[0] as i8)),
            5121 => (1, |b| f32::from(b[0])),
            5122 => (2, |b| f32::from(i16::from_le_bytes([b[0], b[1]]))),
            5123 => (2, |b| f32::from(u16::from_le_bytes([b[0], b[1]]))),
            5125 => (4, |b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]) as f32),
            _ => return Err(invalid(format!("/accessors/{index}/componentType"))),
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
                let view = self.view(place(view_index).ok_or_else(past)?)?;
                let start = place(accessor["byteOffset"].as_u64().unwrap_or(0)).ok_or_else(past)?;
                let stride = self.stride(view_index, size * width).ok_or_else(past)?;
                // The last number is in the view, before any memory is taken for them all.
                if count > 0 {
                    element_at(view, start, count - 1, stride, (width - 1) * size, size).ok_or_else(past)?;
                }
                let mut out = Vec::with_capacity(count * width);
                for element in 0..count {
                    for c in 0..width {
                        out.push(scale(read(element_at(view, start, element, stride, c * size, size).ok_or_else(past)?)));
                    }
                }
                out
            }
            None => vec![0.0; count * width],
        };
        self.sparse_into(index, &mut out, width, size, read, scale)?;
        Ok(out)
    }

    /// An accessor's sparse values over its others.
    fn sparse_into(&self, index: usize, out: &mut [f32], width: usize, size: usize, read: fn(&[u8]) -> f32, scale: impl Fn(f32) -> f32) -> Result<()> {
        let sparse = &self.root["accessors"][index]["sparse"];
        if sparse.is_null() {
            return Ok(());
        }
        let at_sparse = |rest: &str| format!("/accessors/{index}/sparse/{rest}");
        let past = || refuse("Cook.Model.AccessorPast", &[("accessor", index.to_string())]);
        // At most one value for each element the accessor has.
        let count = place(sparse["count"].as_u64().or_invalid(|| at_sparse("count"))?).filter(|&n| n <= out.len() / width.max(1)).ok_or_else(past)?;
        let indices = &sparse["indices"];
        let index_view = self.view(place(indices["bufferView"].as_u64().or_invalid(|| at_sparse("indices/bufferView"))?).ok_or_else(past)?)?;
        let index_start = place(indices["byteOffset"].as_u64().unwrap_or(0)).ok_or_else(past)?;
        let index_size = match indices["componentType"].as_u64() {
            Some(5121) => 1,
            Some(5123) => 2,
            Some(5125) => 4,
            _ => return Err(invalid(at_sparse("indices/componentType"))),
        };
        let values = &sparse["values"];
        let value_view = self.view(place(values["bufferView"].as_u64().or_invalid(|| at_sparse("values/bufferView"))?).ok_or_else(past)?)?;
        let value_start = place(values["byteOffset"].as_u64().unwrap_or(0)).ok_or_else(past)?;
        for k in 0..count {
            let b = element_at(index_view, index_start, k, index_size, 0, index_size).ok_or_else(past)?;
            let element = match index_size {
                1 => usize::from(b[0]),
                2 => usize::from(u16::from_le_bytes([b[0], b[1]])),
                _ => place(u64::from(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))).ok_or_else(past)?,
            };
            for c in 0..width {
                let value = scale(read(element_at(value_view, value_start, k, width * size, c * size, size).ok_or_else(past)?));
                let to = element.checked_mul(width).and_then(|e| e.checked_add(c)).ok_or_else(past)?;
                *out.get_mut(to).ok_or_else(past)? = value;
            }
        }
        Ok(())
    }

    fn indices(&self, primitive: &Value, vertices: usize) -> Result<Vec<u32>> {
        match primitive["indices"].as_u64() {
            Some(index) => {
                let index = index as usize;
                let a = &self.root["accessors"][index];
                let size = match a["componentType"].as_u64() {
                    Some(5121) => 1,
                    Some(5123) => 2,
                    Some(5125) => 4,
                    _ => return Err(invalid(format!("/accessors/{index}/componentType"))),
                };
                let past = || refuse("Cook.Model.AccessorPast", &[("accessor", index.to_string())]);
                let view_index = a["bufferView"].as_u64().or_invalid(|| format!("/accessors/{index}/bufferView"))?;
                let view = self.view(place(view_index).ok_or_else(past)?)?;
                let start = place(a["byteOffset"].as_u64().unwrap_or(0)).ok_or_else(past)?;
                let count = a["count"].as_u64().or_invalid(|| format!("/accessors/{index}/count"))?;
                if count > MAX_ELEMENTS {
                    return Err(refuse("Cook.Model.Accessor", &[("accessor", index.to_string()), ("count", count.to_string()), ("max", MAX_ELEMENTS.to_string())]));
                }
                let count = count as usize;
                let stride = self.stride(view_index, size).ok_or_else(past)?;
                if count > 0 {
                    element_at(view, start, count - 1, stride, 0, size).ok_or_else(past)?;
                }
                (0..count).map(|i| {
                    let b = element_at(view, start, i, stride, 0, size).ok_or_else(past)?;
                    Ok(match size { 1 => u32::from(b[0]), 2 => u32::from(u16::from_le_bytes(b.try_into().unwrap())), _ => u32::from_le_bytes(b.try_into().unwrap()) })
                }).collect()
            },
            None => Ok((0..vertices as u32).collect()),
        }
    }

    /// An image as RGBA, top row first.
    fn image(&self, index: usize) -> Result<(u32, u32, Vec<u8>)> {
        let image = &self.root["images"][index];
        let pointer = format!("/images/{index}/uri");
        let bytes = if let Some(view) = image["bufferView"].as_u64() {
            self.view(view as usize)?.to_vec()
        } else {
            read_uri(image["uri"].as_str().or_invalid(|| pointer.clone())?, self.folder.as_deref(), &pointer)?
        };
        decode_image(&bytes, &image["name"].as_str().map_or_else(|| format!("image {index}"), str::to_string))
    }
}

/// A number of the file as a place, a length or an index in memory.
fn place(v: u64) -> Option<usize> {
    usize::try_from(v).ok()
}

/// The `size` bytes of element `element` of a view (`stride` bytes apart from `start`), `offset` bytes in: every place
/// a model's accessor reads goes through it, so a sum past the range or past the view is None, never a panic.
fn element_at(view: &[u8], start: usize, element: usize, stride: usize, offset: usize, size: usize) -> Option<&[u8]> {
    let at = start.checked_add(element.checked_mul(stride)?)?.checked_add(offset)?;
    view.get(at..at.checked_add(size)?)
}

/// The extensions a file can require and still be read as made: anything else would cook into something it isn't.
const EXTENSIONS_READ: [&str; 7] = [
    "KHR_texture_transform",
    "KHR_animation_pointer",
    "KHR_mesh_quantization",
    "KHR_materials_emissive_strength",
    "KHR_materials_unlit",
    "KHR_materials_ior",
    "KHR_materials_transmission",
];

fn required_extensions(root: &Value) -> Result<()> {
    for name in root["extensionsRequired"].as_array().map(Vec::as_slice).unwrap_or_default() {
        let name = name.as_str().unwrap_or("");
        if !EXTENSIONS_READ.contains(&name) {
            return Err(refuse("Cook.Model.Extension", &[("name", name.to_string()), ("read", EXTENSIONS_READ.join(", "))]));
        }
    }
    Ok(())
}

/// A model as solid ground: its triangles in the game's units, and a table of attributes with one row per material.
/// A material's `extras` are the row's columns: `Floor_code`, `Wall_code`, `Sound_code`, `Camera_id`, `Camera_through`.
pub fn to_collision(bytes: &[u8], folder: Option<&Path>) -> Result<(Vec<u8>, Vec<u8>)> {
    let document = Document::parse(bytes, folder)?;
    let root = &document.root;
    let nodes = root["nodes"].as_array().map(Vec::as_slice).unwrap_or_default();
    let materials = root["materials"].as_array().map(Vec::as_slice).unwrap_or_default();
    let mut triangles = Vec::new();
    // The scene's tree as the model's is walked: each node once, its place from its parents'.
    let (order, world, _) = scene_tree(root, place(root["scene"].as_u64().unwrap_or(0)).unwrap_or(usize::MAX))?;
    for node in order {
        let matrix = world[node];
        let Some(mesh) = nodes[node]["mesh"].as_u64() else {
            continue;
        };
        let mesh_number = place(mesh).unwrap_or(usize::MAX);
        for primitive in root["meshes"][mesh_number]["primitives"].as_array().map(Vec::as_slice).unwrap_or_default() {
            if primitive["mode"].as_u64().unwrap_or(4) != 4 {
                continue;
            }
            let positions = primitive["attributes"]["POSITION"].as_u64().ok_or_else(|| refuse("Cook.Model.NoPositions", &[("mesh", mesh.to_string())]))?;
            let positions = document.floats(positions as usize, 3)?;
            let count = positions.len() / 3;
            let indices = document.indices(primitive, count)?;
            let material = primitive["material"].as_u64().unwrap_or(0);
            let attribute = u16::try_from(material).map_err(|_| refuse("Cook.Collision.Attribute", &[("found", material.to_string()), ("max", u16::MAX.to_string())]))?;
            let corner = |index: u32| -> Result<[f32; 3]> {
                let p: [f32; 3] = place(u64::from(index))
                    .and_then(|i| positions.get(i.checked_mul(3)?..i.checked_mul(3)?.checked_add(3)?))
                    .and_then(|p| p.try_into().ok())
                    .ok_or_else(|| refuse("Cook.Model.VertexPast", &[("mesh", named(&root["meshes"][mesh_number], mesh_number)), ("vertex", index.to_string()), ("count", count.to_string())]))?;
                Ok(transform(&matrix, p, 1.0).map(|v| v * UNITS_PER_METRE))
            };
            for triple in indices.chunks_exact(3) {
                triangles.push(super::kcl::Triangle { corners: [corner(triple[0])?, corner(triple[1])?, corner(triple[2])?], attribute });
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

/// A PNG or a JPEG as RGBA, top row first. `image` names it in a refusal (a file, or a model's image).
pub fn decode_image(bytes: &[u8], image: &str) -> Result<(u32, u32, Vec<u8>)> {
    let unreadable = || refuse("Cook.Image.Unreadable", &[("image", image.to_string())]);
    let (width, height, rgba) = if bytes.starts_with(&[0xFF, 0xD8, 0xFF]) {
        decode_jpeg(bytes).map_err(|_| unreadable())?
    } else if bytes.starts_with(&[0x89, b'P', b'N', b'G']) {
        decode_png(bytes, image).map_err(|e| if super::refuse::refusal_of(&e).is_some() { e } else { unreadable() })?
    } else {
        return Err(refuse("Cook.Image.Kind", &[("image", image.to_string())]));
    };
    if width > MAX_IMAGE_SIDE || height > MAX_IMAGE_SIDE {
        return Err(image_side(image, width, height));
    }
    Ok((width, height, rgba))
}

fn image_side(image: &str, width: u32, height: u32) -> anyhow::Error {
    refuse("Cook.Image.Side", &[("image", image.to_string()), ("width", width.to_string()), ("height", height.to_string()), ("max", MAX_IMAGE_SIDE.to_string())])
}

fn decode_png(bytes: &[u8], image: &str) -> Result<(u32, u32, Vec<u8>)> {
    let mut decoder = png::Decoder::new(bytes);
    decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16 | png::Transformations::ALPHA);
    let mut reader = decoder.read_info().context("the PNG can't be read")?;
    let (width, height) = reader.info().size();
    // Before its pixels are kept: a damaged size would ask for memory no device has.
    if width > MAX_IMAGE_SIDE || height > MAX_IMAGE_SIDE {
        return Err(image_side(image, width, height));
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
pub fn png_to_bti(bytes: &[u8], image: &str) -> Result<Vec<u8>> {
    let (width, height, rgba) = decode_image(bytes, image)?;
    let record = super::texture::cook_rgba(width, height, &rgba)?;
    let mut out = Vec::with_capacity(64 + record.len());
    for word in [0x4954_424Eu32, 1, width, height, 1, 64, 0, 0, 0] { out.extend_from_slice(&word.to_le_bytes()); }
    out.extend_from_slice(&[6, u8::from(rgba.chunks_exact(4).any(|p| p[3] != 255)), 0, 0, 0, 0, 1, 1]);
    out.resize(64, 0); out.extend(record);
    Ok(out)
}

/// A PNG as a TPL, the game's form for its interface: one image, RGBA8, big-endian as the disc's are.
pub fn png_to_tpl(bytes: &[u8], image: &str) -> Result<Vec<u8>> {
    let (width, height, rgba) = decode_image(bytes, image)?;
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

/// A model's animations by name, as typed values or as the store's blocks.
pub type Clips = Vec<(String, ClipIr)>;
pub type ClipFiles = Vec<(String, Vec<u8>)>;

const IDENTITY: Matrix = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0];

fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
}

fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

/// The rows of a matrix's turning part.
fn rows(m: &Matrix) -> [[f32; 3]; 3] {
    [0, 1, 2].map(|r| [m[r], m[4 + r], m[8 + r]])
}

/// The determinant of a matrix's turning part: below zero, it mirrors (glTF 2.0: the faces wind the other way).
fn determinant(m: &Matrix) -> f32 {
    let [a0, a1, a2] = rows(m);
    dot(a0, cross(a1, a2))
}

/// A normal through `m`: by its inverse transpose (the cofactors, signed by the determinant), so a surface scaled on
/// one axis keeps its normals square to it and a mirrored one keeps them outward. Its length is not kept.
fn normal_through(m: &Matrix, n: [f32; 3]) -> [f32; 3] {
    let [a0, a1, a2] = rows(m);
    let cofactors = [cross(a1, a2), cross(a2, a0), cross(a0, a1)];
    let sign = if dot(a0, cofactors[0]) < 0.0 { -1.0 } else { 1.0 };
    cofactors.map(|c| sign * dot(c, n))
}

/// `along` made square to the unit normal `n` and of length 1 (Gram-Schmidt); any square direction when it lies along
/// `n` or has no length.
fn square_to(n: [f32; 3], along: [f32; 3]) -> [f32; 3] {
    let d = dot(n, along);
    let t = [along[0] - n[0] * d, along[1] - n[1] * d, along[2] - n[2] * d];
    let length = dot(t, t).sqrt();
    if length > 1e-6 {
        return t.map(|v| v / length);
    }
    let other = if n[0].abs() < 0.9 { [1.0, 0.0, 0.0] } else { [0.0, 1.0, 0.0] };
    let t = cross(n, other);
    let length = dot(t, t).sqrt().max(1e-12);
    t.map(|v| v / length)
}

/// A primitive as MikkTSpace reads it (glTF 2.0: the tangents a file without them has; materials.md): its triangles,
/// its normals (one a triangle when flat) and the coordinate set its normal map reads. Out: each corner's tangent and
/// handedness.
struct TangentInput<'a> {
    indices: &'a [u32],
    positions: &'a [f32],
    normals: &'a [f32],
    flat: bool,
    coords: &'a [f32],
    out: Vec<[f32; 4]>,
}

impl TangentInput<'_> {
    fn of<const N: usize>(values: &[f32], at: usize) -> [f32; N] {
        values.get(at * N..at * N + N).and_then(|v| v.try_into().ok()).unwrap_or([0.0; N])
    }
}

impl mikktspace::Geometry for TangentInput<'_> {
    fn num_faces(&self) -> usize {
        self.indices.len() / 3
    }
    fn num_vertices_of_face(&self, _face: usize) -> usize {
        3
    }
    fn position(&self, face: usize, vert: usize) -> [f32; 3] {
        Self::of(self.positions, self.indices[face * 3 + vert] as usize)
    }
    fn normal(&self, face: usize, vert: usize) -> [f32; 3] {
        Self::of(self.normals, if self.flat { face } else { self.indices[face * 3 + vert] as usize })
    }
    fn tex_coord(&self, face: usize, vert: usize) -> [f32; 2] {
        Self::of(self.coords, self.indices[face * 3 + vert] as usize)
    }
    fn set_tangent_encoded(&mut self, tangent: [f32; 4], face: usize, vert: usize) {
        self.out[face * 3 + vert] = tangent;
    }
}

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
    /// Its node; None for a root the cook adds.
    node: Option<usize>,
    parent: Option<usize>,
    /// At rest, from its parent joint.
    trs: Trs,
    /// The nodes between its parent joint and its own parent node, which no animation moves: None when its parent node
    /// is its parent joint's.
    between: Option<Matrix>,
    inverse: Matrix,
    /// A `socket:` node's joint (models.md section 6).
    socket: bool,
}

/// A material's layers and coordinate sets at most: a program's inputs name them in a byte past NEBULA_INPUT_COORD0.
const MATERIAL_LAYERS: usize = 240;

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
    /// Its lit part (materials.md): the record with no maps yet, and each map as the file names it.
    lit: PbrMaterial,
    pbr_maps: [Option<PbrMap>; 5],
    /// Its glow as the raster draws it (MOD-MATERIALS-FC.md 1: one more stage, previous + emissive texture x constant
    /// 1): the emissive factor x strength, sRGB-encoded and held to 1, and the emissive map's image and coordinate set.
    /// None when the material is unlit (KHR_materials_unlit shows its base colour alone) or its factor is black.
    glow: Option<Glow>,
    /// Its hand-written shader (surface_shader.rs): the source, its values here, each texture's image.
    shader: Option<(super::surface_shader::Source, super::surface_shader::Use, Vec<usize>)>,
}

/// A glTF material's glow on the raster (SourceMaterial glow): constant colour 1, and the emissive map (image, set).
#[derive(Clone, Copy)]
struct Glow {
    constant: [u8; 4],
    map: Option<(usize, u8)>,
}

/// A map of a lit material as the file names it: its image, its coordinate set, and a KHR_texture_transform to bake
/// into that set's coordinates (every map but the base colour's, whose transform is the material's texture matrix 0).
#[derive(Clone, Copy, Debug)]
struct PbrMap {
    image: usize,
    coord: u64,
    bake: Option<[f32; 5]>,
}

/// The maps of a glTF material, in PbrMaterial's order (PBR_BASE ...): where each is, and its name for a refusal.
const PBR_MAPS: [(&str, &str); 5] = [
    ("pbrMetallicRoughness", "baseColorTexture"),
    ("pbrMetallicRoughness", "metallicRoughnessTexture"),
    ("", "normalTexture"),
    ("", "emissiveTexture"),
    ("", "occlusionTexture"),
];

/// A glTF material's lit part (materials.md): its factors, clamped where glTF bounds them (a Note each), and its maps.
fn pbr_of(root: &Value, material: &Value, number: usize, name: &str) -> Result<(PbrMaterial, [Option<PbrMap>; 5])> {
    let pbr = &material["pbrMetallicRoughness"];
    let clamp = |factor: &str, value: f32, low: f32, high: f32| -> f32 {
        if value.is_nan() || value < low || value > high {
            note("Cook.Material.Clamped", &[("material", name.to_string()), ("factor", factor.to_string()), ("found", value.to_string())]);
        }
        if value.is_nan() { low } else { value.clamp(low, high) }
    };
    let base = numbers(&pbr["baseColorFactor"], [1.0f32; 4]);
    let emissive = numbers(&material["emissiveFactor"], [0.0f32; 3]);
    let strength = material["extensions"]["KHR_materials_emissive_strength"]["emissiveStrength"].as_f64().unwrap_or(1.0) as f32;
    let strength = clamp("emissiveStrength", strength, 0.0, f32::MAX);
    let mut lit = PbrMaterial {
        base_color: base.map(|v| clamp("baseColorFactor", v, 0.0, 1.0)),
        emissive: emissive.map(|v| clamp("emissiveFactor", v, 0.0, 1.0) * strength),
        metal: clamp("metallicFactor", pbr["metallicFactor"].as_f64().unwrap_or(1.0) as f32, 0.0, 1.0),
        roughness: clamp("roughnessFactor", pbr["roughnessFactor"].as_f64().unwrap_or(1.0) as f32, 0.0, 1.0),
        normal_scale: material["normalTexture"]["scale"].as_f64().unwrap_or(1.0) as f32,
        occlusion_strength: clamp("occlusionTexture.strength", material["occlusionTexture"]["strength"].as_f64().unwrap_or(1.0) as f32, 0.0, 1.0),
        // KHR_materials_ior: 0 is its "no refraction" (a surface with no inside); glTF bounds it at 1 otherwise.
        ior: match material["extensions"]["KHR_materials_ior"]["ior"].as_f64() {
            Some(0.0) => 1.0,
            Some(v) => clamp("ior", v as f32, 1.0, 3.0),
            None => 1.5,
        },
        transmission: material["extensions"]["KHR_materials_transmission"]
            .is_object()
            .then(|| clamp("transmissionFactor", material["extensions"]["KHR_materials_transmission"]["transmissionFactor"].as_f64().unwrap_or(0.0) as f32, 0.0, 1.0)),
        ..PbrMaterial::default()
    };
    match material["alphaMode"].as_str() {
        Some("MASK") => {
            lit.alpha_mode = PBR_MASK;
            lit.alpha_cutoff = clamp("alphaCutoff", material["alphaCutoff"].as_f64().unwrap_or(0.5) as f32, 0.0, f32::MAX);
        }
        Some("BLEND") => lit.alpha_mode = PBR_BLEND,
        _ => {}
    }
    if material["doubleSided"].as_bool() == Some(true) {
        lit.flags |= PBR_DOUBLE_SIDED;
    }
    if material["extensions"]["KHR_materials_unlit"].is_object() {
        lit.flags |= PBR_UNLIT;
    }
    let mut maps: [Option<PbrMap>; 5] = [None; 5];
    for (k, (group, map)) in PBR_MAPS.iter().enumerate() {
        let info = if group.is_empty() { &material[*map] } else { &material[*group][*map] };
        if info.is_null() {
            continue;
        }
        let at = if group.is_empty() { format!("/materials/{number}/{map}") } else { format!("/materials/{number}/{group}/{map}") };
        let texture = info["index"].as_u64().or_invalid(|| format!("{at}/index"))?;
        let image = root["textures"][texture as usize]["source"]
            .as_u64()
            .filter(|i| root["images"].as_array().is_some_and(|images| (*i as usize) < images.len()))
            .ok_or_else(|| refuse("Cook.Material.MissingTexture", &[("material", name.to_string()), ("map", map.to_string()), ("texture", texture.to_string())]))?;
        let mut coord = info["texCoord"].as_u64().unwrap_or(0);
        let transform = &info["extensions"]["KHR_texture_transform"];
        let mut bake = None;
        if transform.is_object() {
            coord = transform["texCoord"].as_u64().unwrap_or(coord);
            if k != PBR_BASE {
                let [sx, sy]: [f32; 2] = numbers(&transform["scale"], [1.0; 2]);
                let [ox, oy]: [f32; 2] = numbers(&transform["offset"], [0.0; 2]);
                bake = Some([sx, sy, transform["rotation"].as_f64().unwrap_or(0.0) as f32, ox, oy]);
                note("Cook.Material.TextureTransform", &[("material", name.to_string()), ("map", map.to_string())]);
            }
        }
        maps[k] = Some(PbrMap { image: image as usize, coord, bake });
    }
    // A transform is baked into a coordinate set's numbers: no other map of the material may read that set.
    let mut sets: Vec<u64> = material["extras"]["nebula"]["textures"].as_array().map(Vec::as_slice).unwrap_or_default().iter().map(|l| l["texCoord"].as_u64().unwrap_or(0)).collect();
    sets.extend(maps.iter().flatten().map(|m| m.coord));
    for (k, m) in maps.iter().enumerate() {
        if let Some(PbrMap { coord, bake: Some(_), .. }) = m {
            if sets.iter().filter(|s| *s == coord).count() > 1 {
                return Err(refuse("Cook.Material.TransformShared", &[("material", name.to_string()), ("map", PBR_MAPS[k].1.to_string()), ("set", coord.to_string())]));
            }
        }
    }
    Ok((lit, maps))
}

/// glTF's KHR_texture_transform of coordinates (u, v): offset x rotation x scale, as the extension's matrix is.
fn transformed_coordinate(t: &[f32; 5], uv: [f32; 2]) -> [f32; 2] {
    let [sx, sy, rotation, ox, oy] = *t;
    let (sin, cos) = rotation.sin_cos();
    [cos * sx * uv[0] + sin * sy * uv[1] + ox, -sin * sx * uv[0] + cos * sy * uv[1] + oy]
}

/// The texture transform no KHR_texture_transform gives.
const NO_TRANSFORM: [f32; 5] = [1.0, 1.0, 0.0, 0.0, 0.0];

/// A texture transform as the game's texture matrix (the classic one, about no centre): glTF's turns the other way, and
/// scales before it turns where the game scales after, which is the same only when both scales are equal.
fn texture_transform(t: &[f32; 5], material: &str) -> Result<[f32; 5]> {
    if t[2].abs() > 1e-6 && (t[0] - t[1]).abs() > 1e-5 * t[0].abs().max(t[1].abs()).max(1.0) {
        return Err(refuse("Cook.Model.UnevenTransform", &[("material", material.to_string())]));
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

/// The game's model a mod's model stands in for (models.md sections 2 and 4): its store path, and its joints' names by
/// number, which the mod's joints keep.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct GameModel {
    pub path: String,
    pub joints: Vec<String>,
    /// Each joint's parent joint (None for the root); empty when not known (then nothing is checked against it).
    pub parents: Vec<Option<usize>>,
    /// Its materials' and textures' names, in its order (what an animation file's material tracks and texture swaps
    /// stand for, animation.md section 2); empty when not known.
    pub materials: Vec<String>,
    pub textures: Vec<String>,
}

impl GameModel {
    /// The joints of a model block as the store keeps it (nebula_model.h): None when `block` is no model block.
    pub fn read(path: &str, block: &[u8]) -> Option<GameModel> {
        let word = |at: usize| Some(u32::from_le_bytes(block.get(at..at.checked_add(4)?)?.try_into().ok()?) as usize);
        let table = super::model_write::sections(block).ok()?;
        let joints_section = super::model_write::section(&table, b"JNTS").ok()?;
        let (count, joints_at) = (joints_section.count as usize, joints_section.at as usize);
        if count > block.len() / super::model_write::JOINT_BYTES {
            return None;
        }
        let mut joints = Vec::with_capacity(count);
        let mut parents = Vec::with_capacity(count);
        for j in 0..count {
            let record = joints_at.checked_add(j * super::model_write::JOINT_BYTES)?;
            let name_at = word(record.checked_add(88)?)?;
            let rest = block.get(name_at..)?;
            let end = rest.iter().position(|b| *b == 0)?;
            joints.push(rest[..end].iter().map(|&b| char::from(b)).collect());
            // NebulaModelJoint.parent (nebula_model.h), NEBULA_MODEL_NONE for the root.
            let parent = word(record.checked_add(24)?)?;
            parents.push((parent != NONE as usize).then_some(parent).filter(|p| *p < count));
        }
        let names = super::model_write::names(block).ok();
        let (materials, textures) = names.map_or_else(Default::default, |n| (n.materials, n.textures));
        Some(GameModel { path: path.to_string(), joints, parents, materials, textures })
    }
}

/// A glTF of a game model's skeleton alone: its joints with their names, parents and rest pose, and no meshes. A hidden
/// model (`<path>.model.json` holding `{"hide": true}`) is cooked from it: the game's joints stay, so
/// its animations and the code that names them still find them, and nothing is drawn. None when `block` is no model
/// block.
pub fn skeleton_gltf(game: &GameModel, block: &[u8]) -> Option<Vec<u8>> {
    let float = |at: usize| Some(f32::from_le_bytes(block.get(at..at.checked_add(4)?)?.try_into().ok()?));
    let table = super::model_write::sections(block).ok()?;
    let joints_at = super::model_write::section(&table, b"JNTS").ok()?.at as usize;
    let mut nodes = Vec::with_capacity(game.joints.len());
    for (j, name) in game.joints.iter().enumerate() {
        let at = joints_at.checked_add(j.checked_mul(super::model_write::JOINT_BYTES)?)?;
        let three = |o: usize| -> Option<[f32; 3]> { Some([float(at + o)?, float(at + o + 4)?, float(at + o + 8)?]) };
        let (scale, angles, translation) = (three(0)?, three(12)?, three(28)?);
        let children: Vec<usize> = (0..game.joints.len()).filter(|c| *c != j && game.parents.get(*c).copied().flatten() == Some(j)).collect();
        let mut node = serde_json::json!({
            "name": name,
            "translation": translation.map(|v| v / UNITS_PER_METRE),
            "rotation": quaternion_of(angles),
            "scale": scale,
        });
        if !children.is_empty() {
            node["children"] = serde_json::json!(children);
        }
        nodes.push(node);
    }
    let roots: Vec<usize> = (0..game.joints.len()).filter(|j| game.parents.get(*j).copied().flatten().is_none()).collect();
    let document = serde_json::json!({ "asset": { "version": "2.0" }, "nodes": nodes, "scenes": [{ "nodes": roots }], "scene": 0 });
    serde_json::to_vec(&document).ok()
}

/// The game's joint angles (X, Y, Z radians composed Z·Y·X, models.md section 4) as a quaternion: what [`angles`] reads
/// back.
fn quaternion_of(r: [f32; 3]) -> [f32; 4] {
    let (sx, cx) = (r[0] / 2.0).sin_cos();
    let (sy, cy) = (r[1] / 2.0).sin_cos();
    let (sz, cz) = (r[2] / 2.0).sin_cos();
    [sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz]
}

/// What the cook made of a model file, for the fact `cook.model` (models.md section 9).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ModelFacts {
    /// The game model it stands in for; empty for a new model.
    pub replaced: String,
    /// Joints numbered by the game model (all of a new model's), and the source's others after them.
    pub joints: usize,
    pub extra_joints: usize,
    pub meshes: usize,
    pub vertices: usize,
    pub materials: usize,
    pub textures: usize,
    pub clips: usize,
}

/// A mesh, node or material in a refusal: its glTF name, else its number.
fn named(value: &Value, n: usize) -> String {
    value["name"].as_str().filter(|s| !s.is_empty()).map_or_else(|| format!("#{n}"), str::to_string)
}

/// An affine matrix's inverse (glTF's node matrices are affine); a singular one has none, and is the identity.
fn affine_inverse(m: &Matrix) -> Matrix {
    let a = |r: usize, c: usize| f64::from(m[c * 4 + r]);
    let det = a(0, 0) * (a(1, 1) * a(2, 2) - a(1, 2) * a(2, 1)) - a(0, 1) * (a(1, 0) * a(2, 2) - a(1, 2) * a(2, 0))
        + a(0, 2) * (a(1, 0) * a(2, 1) - a(1, 1) * a(2, 0));
    if det.abs() < 1e-18 {
        return IDENTITY;
    }
    let i = [
        [(a(1, 1) * a(2, 2) - a(1, 2) * a(2, 1)) / det, (a(0, 2) * a(2, 1) - a(0, 1) * a(2, 2)) / det, (a(0, 1) * a(1, 2) - a(0, 2) * a(1, 1)) / det],
        [(a(1, 2) * a(2, 0) - a(1, 0) * a(2, 2)) / det, (a(0, 0) * a(2, 2) - a(0, 2) * a(2, 0)) / det, (a(0, 2) * a(1, 0) - a(0, 0) * a(1, 2)) / det],
        [(a(1, 0) * a(2, 1) - a(1, 1) * a(2, 0)) / det, (a(0, 1) * a(2, 0) - a(0, 0) * a(2, 1)) / det, (a(0, 0) * a(1, 1) - a(0, 1) * a(1, 0)) / det],
    ];
    let t = [a(0, 3), a(1, 3), a(2, 3)];
    let mut out = IDENTITY;
    for r in 0..3 {
        for c in 0..3 {
            out[c * 4 + r] = i[r][c] as f32;
        }
        out[12 + r] = -(i[r][0] * t[0] + i[r][1] * t[1] + i[r][2] * t[2]) as f32;
    }
    out
}

/// An affine matrix as translation, rotation and scale (no shear: what glTF's nodes compose to).
fn decompose(m: &Matrix) -> Trs {
    let column = |c: usize| [m[c * 4], m[c * 4 + 1], m[c * 4 + 2]];
    let length = |v: [f32; 3]| (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();
    let mut scale = [length(column(0)), length(column(1)), length(column(2))];
    let det = {
        let (x, y, z) = (column(0), column(1), column(2));
        x[0] * (y[1] * z[2] - y[2] * z[1]) - x[1] * (y[0] * z[2] - y[2] * z[0]) + x[2] * (y[0] * z[1] - y[1] * z[0])
    };
    if det < 0.0 {
        scale[0] = -scale[0];
    }
    let r = |row: usize, c: usize| f64::from(m[c * 4 + row] / if scale[c].abs() > 1e-12 { scale[c] } else { 1.0 });
    let trace = r(0, 0) + r(1, 1) + r(2, 2);
    let q = if trace > 0.0 {
        let s = (trace + 1.0).sqrt() * 2.0;
        [(r(2, 1) - r(1, 2)) / s, (r(0, 2) - r(2, 0)) / s, (r(1, 0) - r(0, 1)) / s, 0.25 * s]
    } else if r(0, 0) > r(1, 1) && r(0, 0) > r(2, 2) {
        let s = (1.0 + r(0, 0) - r(1, 1) - r(2, 2)).sqrt() * 2.0;
        [0.25 * s, (r(0, 1) + r(1, 0)) / s, (r(0, 2) + r(2, 0)) / s, (r(2, 1) - r(1, 2)) / s]
    } else if r(1, 1) > r(2, 2) {
        let s = (1.0 + r(1, 1) - r(0, 0) - r(2, 2)).sqrt() * 2.0;
        [(r(0, 1) + r(1, 0)) / s, 0.25 * s, (r(1, 2) + r(2, 1)) / s, (r(0, 2) - r(2, 0)) / s]
    } else {
        let s = (1.0 + r(2, 2) - r(0, 0) - r(1, 1)).sqrt() * 2.0;
        [(r(0, 2) + r(2, 0)) / s, (r(1, 2) + r(2, 1)) / s, 0.25 * s, (r(1, 0) - r(0, 1)) / s]
    };
    let n = q.iter().map(|v| v * v).sum::<f64>().sqrt().max(1e-12);
    Trs { translation: [m[12], m[13], m[14]], rotation: q.map(|v| (v / n) as f32), scale }
}

/// The images and sets the lit template samples (MaterialIr pbr): metal-roughness, then occlusion unless it is the same
/// image through the same set.
fn pbr_light_maps(maps: &[Option<PbrMap>; 5]) -> Vec<(usize, u64)> {
    let mut out: Vec<(usize, u64)> = Vec::new();
    for k in [PBR_METAL_ROUGH, PBR_OCCLUSION] {
        if let Some(m) = maps[k] {
            if !out.contains(&(m.image, m.coord)) {
                out.push((m.image, m.coord));
            }
        }
    }
    out
}

/// The material's colour places its stages read (surface_shader::places): registers 1 to 3 (places 0 to 2) by a
/// stage's inputs or outputs, constant colours 0 to 3 (places 3 to 6) by a stage that takes its constant.
fn places_used(made: &MaterialIr) -> [bool; super::surface_shader::PLACES] {
    let mut used = [false; super::surface_shader::PLACES];
    for s in &made.stages[..(made.pixel_stage_count as usize).min(made.stages.len())] {
        // Colour inputs 2 to 7 are registers 1 to 3 (colour, alpha); alpha inputs 1 to 3 the same.
        for &c in &s.color_in {
            if (2..=7).contains(&c) {
                used[usize::from((c - 2) / 2)] = true;
            }
        }
        for &a in &s.alpha_in {
            if (1..=3).contains(&a) {
                used[usize::from(a - 1)] = true;
            }
        }
        for out in [s.color_out, s.alpha_out] {
            if (1..=3).contains(&out) {
                used[usize::from(out - 1)] = true;
            }
        }
        if s.color_in.contains(&14) {
            match s.color_constant {
                0x0C..=0x0F => used[3 + usize::from(s.color_constant - 0x0C)] = true,
                0x10..=0x1F => used[3 + usize::from((s.color_constant - 0x10) % 4)] = true,
                _ => {}
            }
        }
        if s.alpha_in.contains(&6) && (0x10..=0x1F).contains(&s.alpha_constant) {
            used[3 + usize::from((s.alpha_constant - 0x10) % 4)] = true;
        }
    }
    used
}

/// A map no plain stage reads (a glow's, a normal map): a slot of its own after the material's (map n through coordinate
/// n), the first one in place of a material's "no texture".
fn own_slot(made: &mut MaterialIr, texture: u32, uv: u8) -> u8 {
    if made.coords.is_empty() && made.textures.iter().all(|&t| t == NONE) {
        made.textures.clear();
        made.coordinate_maps.clear();
    }
    debug_assert_eq!(made.textures.len(), made.coords.len());
    let slot = made.textures.len() as u8;
    made.textures.push(texture);
    made.coords.push(CoordIr { kind: 1, source: 4 + uv, matrix: NONE });
    made.coordinate_maps.push(slot);
    slot
}

/// glTF's linear colour (0 to 1) sRGB-encoded (0 to 1), as the game's own art and its colour registers hold colours.
fn srgb(linear: f32) -> f32 {
    let c = if linear.is_nan() { 0.0 } else { linear.clamp(0.0, 1.0) };
    // White stays exactly white (1.055 - 0.055 is 0.99999994 in f32: an animated 1 would be 254.99998, not 255).
    if c >= 1.0 {
        1.0
    } else if c <= 0.003_130_8 {
        c * 12.92
    } else {
        1.055 * c.powf(1.0 / 2.4) - 0.055
    }
}

/// glTF's linear colour as an 8-bit colour of the game (vertex colours, material colours, constants): sRGB-encoded.
fn srgb_byte(linear: f32) -> u8 {
    (srgb(linear) * 255.0).round() as u8
}

/// A glTF colour factor (linear RGB, alpha a coverage) as the game's 8-bit colour: RGB sRGB-encoded, alpha as it is.
/// A texel is sRGB and the stages multiply what they are given, so factor x texel is drawn as sRGB(factor) x texel,
/// which is sRGB(factor x linear texel) for sRGB's power curve.
fn colour_bytes(factor: [f32; 4]) -> [u8; 4] {
    [srgb_byte(factor[0]), srgb_byte(factor[1]), srgb_byte(factor[2]), (factor[3].clamp(0.0, 1.0) * 255.0).round() as u8]
}

/// A primitive's triangles as vertex numbers, from its mode (glTF 2.0 3.7.2.1): triangles, a strip or a fan.
fn triangles_of(mode: u64, indices: &[u32], pointer: &str) -> Result<Vec<u32>> {
    let mut out = Vec::with_capacity(indices.len());
    match mode {
        4 => out.extend_from_slice(&indices[..indices.len() / 3 * 3]),
        5 => {
            for i in 0..indices.len().saturating_sub(2) {
                let (b, c) = if i % 2 == 0 { (i + 1, i + 2) } else { (i + 2, i + 1) };
                out.extend_from_slice(&[indices[i], indices[b], indices[c]]);
            }
        }
        6 => {
            for i in 1..indices.len().saturating_sub(1) {
                out.extend_from_slice(&[indices[i], indices[i + 1], indices[0]]);
            }
        }
        // Points and lines are refused by name before this; anything else is no glTF mode.
        _ => return Err(invalid(pointer.to_string())),
    }
    Ok(out)
}

/// A colour accessor's numbers as RGBA (glTF allows VEC3 and VEC4).
fn colour_values_of(document: &Document, index: usize) -> Result<Vec<f32>> {
    match document.root["accessors"][index]["type"].as_str() {
        Some("VEC4") => document.floats(index, 4),
        Some("VEC3") => Ok(document.floats(index, 3)?.chunks_exact(3).flat_map(|c| [c[0], c[1], c[2], 1.0]).collect()),
        _ => Err(invalid(format!("/accessors/{index}/type"))),
    }
}

/// A glTF document as typed values: the model, and each animation by its name.
pub fn to_ir(bytes: &[u8], folder: Option<&Path>) -> Result<(ModelIr, Vec<(String, ClipIr)>)> {
    to_ir_for(bytes, folder, None).map(|(ir, clips, _)| (ir, clips))
}

/// The nodes of a scene parents first, each node's transform in the scene, each node's parent.
type SceneTree = (Vec<usize>, Vec<Matrix>, Vec<Option<usize>>);

/// Every node of scene `scene_number` with its whole transform, parents first: (the nodes in that order, each node's
/// transform in the scene, each node's parent).
fn scene_tree(root: &Value, scene_number: usize) -> Result<SceneTree> {
    let nodes = root["nodes"].as_array().map(Vec::as_slice).unwrap_or_default();
    let scene = &root["scenes"][scene_number];
    let mut world = vec![IDENTITY; nodes.len()];
    let mut parents: Vec<Option<usize>> = vec![None; nodes.len()];
    let mut seen = vec![false; nodes.len()];
    let mut order = Vec::new();
    let mut stack: Vec<(usize, Matrix)> = Vec::new();
    for n in scene["nodes"].as_array().map(Vec::as_slice).unwrap_or_default().iter().rev() {
        stack.push((n.as_u64().or_invalid(|| format!("/scenes/{scene_number}/nodes"))? as usize, IDENTITY));
    }
    while let Some((node, parent)) = stack.pop() {
        let value = nodes.get(node).ok_or_else(|| refuse("Cook.Model.NodeMissing", &[("node", node.to_string())]))?;
        // A node is in one place of the tree (glTF 2.0 3.5.3): a second visit is a cycle or a shared child.
        if std::mem::replace(&mut seen[node], true) {
            return Err(invalid(format!("/nodes/{node}/children")));
        }
        let matrix = multiply(&parent, &node_matrix(value));
        world[node] = matrix;
        order.push(node);
        for child in value["children"].as_array().map(Vec::as_slice).unwrap_or_default().iter().rev() {
            let child = child.as_u64().or_invalid(|| format!("/nodes/{node}/children"))? as usize;
            if child >= nodes.len() {
                return Err(refuse("Cook.Model.NodeMissing", &[("node", child.to_string())]));
            }
            parents[child] = Some(node);
            stack.push((child, matrix));
        }
    }
    Ok((order, world, parents))
}

/// A glTF document as the model that stands in for `game` (None: a new model), its clips and what was made.
pub fn to_ir_for(bytes: &[u8], folder: Option<&Path>, game: Option<&GameModel>) -> Result<(ModelIr, Clips, ModelFacts)> {
    ir_for(bytes, folder, game, false)
}

/// The model of `bytes`; `hidden`: a hidden model (skeleton_gltf's), whose joints are all it has, so no triangles is
/// what it is and not a refusal.
fn ir_for(bytes: &[u8], folder: Option<&Path>, game: Option<&GameModel>, hidden: bool) -> Result<(ModelIr, Clips, ModelFacts)> {
    let document = Document::parse(bytes, folder)?;
    let root = &document.root;
    required_extensions(root)?;
    let scene_number = root["scene"].as_u64().unwrap_or(0) as usize;
    let nodes = root["nodes"].as_array().map(Vec::as_slice).unwrap_or_default();
    let mesh_name = |m: usize| named(&root["meshes"][m], m);
    let (order, world, parents) = scene_tree(root, scene_number)?;
    if order.is_empty() {
        return Err(refuse("Cook.Model.NoTriangles", &[]));
    }

    // One skin at most (models.md section 4): the first skinned mesh's, and no other.
    let mut skin_number: Option<(usize, usize)> = None;
    for node in &order {
        if let Some(s) = nodes[*node]["skin"].as_u64() {
            let mesh = nodes[*node]["mesh"].as_u64().unwrap_or(0) as usize;
            match skin_number {
                None => skin_number = Some((s as usize, mesh)),
                Some((first, first_mesh)) if first != s as usize => {
                    return Err(refuse("Cook.Model.TwoSkins", &[("mesh", mesh_name(first_mesh)), ("other", mesh_name(mesh))]));
                }
                _ => {}
            }
        }
    }
    let skin = skin_number.map(|(s, _)| &root["skins"][s]);

    // Sockets: `socket:<name>` nodes with no mesh, joints of their own whatever else the file has (models.md section
    // 6).
    let socket_of = |n: usize| -> Option<&str> { nodes[n]["name"].as_str()?.strip_prefix("socket:").filter(|_| nodes[n]["mesh"].is_null()) };

    // Joints: a skin's, or every node in the scene; one root at the top, as the game's tree has one.
    let mut inverse_of: BTreeMap<usize, Matrix> = BTreeMap::new();
    let listed: Vec<usize> = match skin {
        Some(skin) => {
            let s = skin_number.map_or(0, |(s, _)| s);
            let list: Vec<usize> = skin["joints"]
                .as_array()
                .or_invalid(|| format!("/skins/{s}/joints"))?
                .iter()
                .map(|j| j.as_u64().map(|v| v as usize).or_invalid(|| format!("/skins/{s}/joints")))
                .collect::<Result<_>>()?;
            let inverses = match skin["inverseBindMatrices"].as_u64() {
                Some(index) => document.floats(index as usize, 16)?,
                None => IDENTITY.repeat(list.len()),
            };
            for (place, node) in list.iter().enumerate() {
                let inverse: Matrix = inverses.get(place * 16..place * 16 + 16).or_invalid(|| format!("/skins/{s}/inverseBindMatrices"))?.try_into()?;
                inverse_of.insert(*node, inverse);
            }
            let listed: Vec<usize> = order.iter().copied().filter(|n| list.contains(n)).collect();
            if let Some(outside) = list.iter().find(|n| !listed.contains(n)) {
                return Err(refuse("Cook.Model.SkinOutside", &[("node", named(&nodes.get(*outside).cloned().unwrap_or(Value::Null), *outside))]));
            }
            listed
        }
        None => order.iter().copied().filter(|n| socket_of(*n).is_none()).collect(),
    };
    let ancestor_in = |n: usize, set: &[usize]| -> Option<usize> {
        let mut at = parents[n];
        while let Some(p) = at {
            if set.contains(&p) {
                return Some(p);
            }
            at = parents[p];
        }
        None
    };
    let mut joints: Vec<Joint> = Vec::new();
    let mut joint_of_node: BTreeMap<usize, usize> = BTreeMap::new();
    let roots = listed.iter().filter(|n| ancestor_in(**n, &listed).is_none()).count();
    if roots != 1 {
        let name = game.and_then(|g| g.joints.first().cloned()).unwrap_or_else(|| "root".to_string());
        joints.push(Joint { name, node: None, parent: None, trs: Trs { translation: [0.0; 3], rotation: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] }, between: None, inverse: IDENTITY, socket: false });
    }
    // A joint's rest place from its parent joint's: the node's own when that is its parent, else what lies between.
    let local = |node: usize, parent_node: Option<usize>| -> (Trs, Option<Matrix>) {
        if parents[node] == parent_node {
            return (node_trs(&nodes[node]), None);
        }
        let above = parent_node.map_or(IDENTITY, |p| affine_inverse(&world[p]));
        let between = multiply(&above, &parents[node].map_or(IDENTITY, |p| world[p]));
        (decompose(&multiply(&above, &world[node])), Some(between))
    };
    for node in &listed {
        let parent_node = ancestor_in(*node, &listed);
        let parent = parent_node.and_then(|p| joint_of_node.get(&p).copied()).or(if joints.is_empty() { None } else { Some(0) });
        let (trs, between) = local(*node, parent_node);
        joint_of_node.insert(*node, joints.len());
        joints.push(Joint { name: named(&nodes[*node], *node), node: Some(*node), parent, trs, between, inverse: inverse_of.get(node).copied().unwrap_or(IDENTITY), socket: false });
    }
    let mut socket_names: BTreeSet<String> = BTreeSet::new();
    for node in order.iter().copied().filter(|n| socket_of(*n).is_some()) {
        let name = socket_of(node).unwrap_or_default().to_string();
        if joints.iter().any(|j| !j.socket && j.name == name) || game.is_some_and(|g| g.joints.contains(&name)) {
            return Err(refuse("Cook.Model.SocketIsJoint", &[("name", name)]));
        }
        if !socket_names.insert(name.clone()) {
            return Err(refuse("Cook.Model.SocketTwice", &[("name", name)]));
        }
        let parent_node = ancestor_in(node, &listed);
        let parent = parent_node.and_then(|p| joint_of_node.get(&p).copied()).unwrap_or(0);
        let (trs, between) = local(node, parent_node);
        joint_of_node.insert(node, joints.len());
        joints.push(Joint { name, node: Some(node), parent: Some(parent), trs, between, inverse: IDENTITY, socket: true });
    }
    let joint_twice = |joints: &[Joint]| -> Result<()> {
        let mut names_seen = BTreeSet::new();
        for joint in joints {
            if !names_seen.insert(joint.name.as_str()) {
                return Err(refuse("Cook.Model.JointTwice", &[("joint", joint.name.clone())]));
            }
        }
        Ok(())
    };
    joint_twice(&joints)?;

    // Numbers by the game model (models.md section 4): a one-joint model's root is joint 0 whatever its name; otherwise
    // each of the game's joints by name, then the source's others in order, parents before children.
    let mut facts = ModelFacts { replaced: game.map(|g| g.path.clone()).unwrap_or_default(), ..ModelFacts::default() };
    let mut new_order: Vec<usize> = (0..joints.len()).collect();
    if let Some(game) = game {
        if game.joints.len() == 1 {
            joints[0].name = game.joints[0].clone();
            joint_twice(&joints)?;
            facts.joints = 1;
        } else {
            new_order.clear();
            for name in &game.joints {
                let old = joints
                    .iter()
                    .position(|j| !j.socket && &j.name == name)
                    .ok_or_else(|| refuse("Cook.Model.MissingJoint", &[("joint", name.clone()), ("model", game.path.clone())]))?;
                new_order.push(old);
            }
            facts.joints = new_order.len();
            for old in 0..joints.len() {
                if !new_order.contains(&old) {
                    new_order.push(old);
                }
            }
        }
    } else {
        facts.joints = joints.len();
    }
    if joints.len() + 1 >= NONE as usize {
        return Err(refuse("Cook.Model.Joints", &[("count", joints.len().to_string()), ("max", (NONE - 2).to_string())]));
    }
    let mut new_of_old = vec![0usize; joints.len()];
    for (new, old) in new_order.iter().enumerate() {
        new_of_old[*old] = new;
    }
    let mut taken: Vec<Option<Joint>> = joints.into_iter().map(Some).collect();
    let mut joints: Vec<Joint> = new_order.iter().map(|old| taken[*old].take().expect("each joint once")).collect();
    for joint in &mut joints {
        joint.parent = joint.parent.map(|p| new_of_old[p]);
    }
    for joint in joint_of_node.values_mut() {
        *joint = new_of_old[*joint];
    }
    facts.extra_joints = joints.len() - facts.joints;
    // Each joint's place in the model at rest.
    let joint_world: Vec<Matrix> = joints.iter().map(|j| j.node.map_or(IDENTITY, |n| world[n])).collect();
    let skin_root = listed.first().and_then(|n| joint_of_node.get(n)).copied().unwrap_or(0);
    // Billboards from the node's `extras.nebula.billboard` (models.md section 4): 1 faces the screen, 2 turns about Y.
    let mut billboard = vec![0u8; joints.len()];
    for (number, joint) in joints.iter().enumerate() {
        let Some(node) = joint.node else { continue };
        match &nodes[node]["extras"]["nebula"]["billboard"] {
            Value::Null => {}
            Value::String(s) if s == "xy" => billboard[number] = 1,
            Value::String(s) if s == "y" => billboard[number] = 2,
            other => {
                return Err(refuse("Cook.Model.Billboard", &[("node", named(&nodes[node], node)), ("value", other.to_string())]));
            }
        }
    }

    // Materials.
    let (moved, swapped) = animated_materials(root);
    let mut materials: Vec<SourceMaterial> = Vec::new();
    let mut ray_data = false;
    for (i, material) in root["materials"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
        let material_name = named(material, i);
        let pbr = &material["pbrMetallicRoughness"];
        let factor: [f32; 4] = numbers(&pbr["baseColorFactor"], [1.0; 4]);
        let image_of_texture = |t: u64| -> Result<usize> {
            root["textures"][t as usize]["source"].as_u64().map(|s| s as usize).or_invalid(|| format!("/textures/{t}/source"))
        };
        let (lit, pbr_maps) = pbr_of(root, material, i, &material_name)?;
        // Its hand-written shader (materials.md section 3.4): `extras.nebula.shader` names `assets/<name>.hlsl` of the
        // mod the file is in; `params` and `textures` (an object, by the shader's names) are this material's.
        let nebula = &material["extras"]["nebula"];
        let shader = match &nebula["shader"] {
            Value::Null => None,
            Value::String(name) => {
                let (file, json, hlsl) = super::surface_shader::find(folder, &material_name, name)?;
                let source = super::surface_shader::read(name, &file, &json, &hlsl)?;
                let used = super::surface_shader::use_of(&source, &material_name, nebula)?;
                let images = used.textures.iter().map(|&t| image_of_texture(t)).collect::<Result<Vec<_>>>()?;
                Some((source, used, images))
            }
            other => return Err(refuse("Cook.Shader.Name", &[("material", material_name.clone()), ("name", other.to_string())])),
        };
        // The shelved ray tracing's rows, here or in its shader.json: not read, said once for the file.
        if !ray_data && (!nebula["ray"].is_null() || shader.as_ref().is_some_and(|(source, _, _)| source.ray)) {
            ray_data = true;
            super::refuse::note("Cook.RayData", &[]);
        }
        let image = match pbr["baseColorTexture"]["index"].as_u64() {
            Some(t) => Some(image_of_texture(t)?),
            None => None,
        };
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
            return Err(refuse("Cook.Model.TransformNoTexture", &[("material", material_name)]));
        }
        // Additional authored multiply layers use ordinary glTF texture indices
        // and UV sets, through the same material programs as the game's models.
        for (k, layer) in material["extras"]["nebula"]["textures"].as_array().map(Vec::as_slice).unwrap_or_default().iter().enumerate() {
            let index = layer["index"].as_u64().or_invalid(|| format!("/materials/{i}/extras/nebula/textures/{k}/index"))?;
            images.push((image_of_texture(index)?, layer["texCoord"].as_u64().unwrap_or(0)));
        }
        let glowing = lit.flags & PBR_UNLIT == 0 && lit.emissive.iter().any(|&v| v > 0.0);
        let glow_map = pbr_maps[PBR_EMISSIVE].filter(|_| glowing).map(|m| (m.image, m.coord));
        let normal_slot = pbr_maps[PBR_NORMAL].is_some() && lit.flags & PBR_UNLIT == 0;
        // The lit template's own maps (MaterialIr pbr): metal-roughness, and occlusion unless it is the same image
        // through the same set (an ORM texture: one slot).
        let light_maps: Vec<(usize, u64)> = if lit.flags & PBR_UNLIT == 0 { pbr_light_maps(&pbr_maps) } else { Vec::new() };
        // A program's inputs name coordinates in a byte past NEBULA_INPUT_COORD0, maps and coordinates in a byte.
        let layers = images.len() + usize::from(glow_map.is_some()) + usize::from(normal_slot) + light_maps.len();
        if layers > MATERIAL_LAYERS || images.iter().chain(glow_map.as_ref()).chain(light_maps.iter()).any(|(_, uv)| *uv >= MATERIAL_LAYERS as u64) {
            let count = layers.max(images.iter().chain(glow_map.as_ref()).chain(light_maps.iter()).map(|(_, uv)| *uv as usize + 1).max().unwrap_or(0));
            return Err(refuse("Cook.Model.Layers", &[("material", material_name), ("count", count.to_string()), ("max", MATERIAL_LAYERS.to_string())]));
        }
        let images = images.into_iter().map(|(image, uv)| (image, uv as u8)).collect();
        let glow = glowing.then(|| {
            let [r, g, b] = lit.emissive.map(srgb_byte);
            Glow { constant: [r, g, b, 0xFF], map: glow_map.map(|(image, uv)| (image, uv as u8)) }
        });
        materials.push(SourceMaterial {
            name: material_name,
            color: colour_bytes(factor),
            glow,
            images,
            blend: material["alphaMode"].as_str() == Some("BLEND"),
            cutoff: (material["alphaMode"].as_str() == Some("MASK")).then(|| (material["alphaCutoff"].as_f64().unwrap_or(0.5).clamp(0.0, 1.0) * 255.0) as u8),
            two_sided: material["doubleSided"].as_bool().unwrap_or(false),
            transform,
            lit,
            pbr_maps,
            shader,
        });
    }
    let default_material = materials.len();
    materials.push(SourceMaterial {
        name: "material".to_string(),
        color: [255; 4],
        images: Vec::new(),
        blend: false,
        cutoff: None,
        two_sided: false,
        transform: None,
        lit: PbrMaterial::default(),
        pbr_maps: [None; 5],
        glow: None,
        shader: None,
    });
    // A lit record for each material when the file has any material (materials.md); a mesh with none has glTF's default.
    let has_materials = root["materials"].as_array().is_some_and(|m| !m.is_empty());
    let mut pbr: Vec<PbrMaterial> = Vec::new();

    let mut ir = ModelIr::default();
    let mut positions = Pool::<3>::new();
    let mut normals = Pool::<3>::new();
    let mut colour_pools: [Pool<1>; 2] = [Pool::new(), Pool::new()];
    let mut coordinates: Vec<Pool<2>> = Vec::new();
    // A vertex's draw matrix: joints first (by joint number), then envelopes.
    let mut envelope_ids: BTreeMap<Vec<(u32, u32)>, u64> = BTreeMap::new();
    let mut first_of_material: BTreeMap<usize, u32> = BTreeMap::new();
    // Materials a vertex-coloured mesh draws with: their base colour factor is constant colour 0, not material colour 0.
    let mut vertex_coloured: BTreeSet<usize> = BTreeSet::new();
    let mut image_of: BTreeMap<usize, u32> = BTreeMap::new();
    // Each joint's own vertices at rest, in the model's space and the game's units.
    let mut joint_low = vec![[f32::MAX; 3]; joints.len()];
    let mut joint_high = vec![[f32::MIN; 3]; joints.len()];
    let mut meshes_of_joint: Vec<Vec<u32>> = vec![Vec::new(); joints.len()];
    let mut flat_meshes: BTreeSet<usize> = BTreeSet::new();
    for node in &order {
        let Some(mesh) = nodes[*node]["mesh"].as_u64() else { continue };
        let mesh = mesh as usize;
        let name = mesh_name(mesh);
        let skinned = nodes[*node]["skin"].is_u64();
        let skin_joints: Vec<usize> = if skinned {
            skin.map(|s| s["joints"].as_array().map(Vec::as_slice).unwrap_or_default().iter().filter_map(|j| j.as_u64().map(|v| v as usize)).collect()).unwrap_or_default()
        } else {
            Vec::new()
        };
        // A mesh that no skin moves draws with the joint at or above its node, in that joint's space (models.md section
        // 5).
        let static_joint = joint_of_node
            .get(node)
            .copied()
            .or_else(|| ancestor_in(*node, &listed).and_then(|p| joint_of_node.get(&p).copied()))
            .unwrap_or(0);
        let to_joint = multiply(&affine_inverse(&joint_world[static_joint]), &world[*node]);
        let mut unweighted = 0usize;
        let primitives = root["meshes"][mesh]["primitives"].as_array().or_invalid(|| format!("/meshes/{mesh}/primitives"))?;
        for (p, primitive) in primitives.iter().enumerate() {
            let at = |rest: &str| format!("/meshes/{mesh}/primitives/{p}/{rest}");
            let mode = primitive["mode"].as_u64().unwrap_or(4);
            if mode <= 3 {
                return Err(refuse("Cook.Model.NotTriangles", &[("mesh", name.clone()), ("mode", mode.to_string())]));
            }
            let attributes = &primitive["attributes"];
            let position_values = document.floats(attributes["POSITION"].as_u64().or_invalid(|| at("attributes/POSITION"))? as usize, 3)?;
            let count = position_values.len() / 3;
            let source_indices = triangles_of(mode, &document.indices(primitive, count)?, &at("mode"))?;
            if let Some(i) = source_indices.iter().find(|i| **i as usize >= count) {
                return Err(refuse("Cook.Model.VertexPast", &[("mesh", name.clone()), ("vertex", i.to_string()), ("count", count.to_string())]));
            }
            // No normals: each triangle's own, as glTF 2.0 says a reader makes them (flat), each corner a vertex.
            let flat = attributes["NORMAL"].is_null();
            let normal_values = match attributes["NORMAL"].as_u64() {
                Some(index) => document.floats(index as usize, 3)?,
                None => {
                    if flat_meshes.insert(mesh) {
                        note("Cook.Model.FlatNormals", &[("mesh", name.clone())]);
                    }
                    let mut out = Vec::with_capacity(source_indices.len());
                    for t in source_indices.chunks_exact(3) {
                        let v = |k: usize| -> [f32; 3] {
                            let at = t[k] as usize * 3;
                            [position_values[at], position_values[at + 1], position_values[at + 2]]
                        };
                        let (a, b, c) = (v(0), v(1), v(2));
                        let (e, f) = ([b[0] - a[0], b[1] - a[1], b[2] - a[2]], [c[0] - a[0], c[1] - a[1], c[2] - a[2]]);
                        let n = [e[1] * f[2] - e[2] * f[1], e[2] * f[0] - e[0] * f[2], e[0] * f[1] - e[1] * f[0]];
                        let length = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt();
                        out.extend_from_slice(&if length > 1e-12 { n.map(|x| x / length) } else { [0.0, 1.0, 0.0] });
                    }
                    out
                }
            };
            let weights = if skinned {
                match (attributes["JOINTS_0"].as_u64(), attributes["WEIGHTS_0"].as_u64()) {
                    (Some(j), Some(w)) => Some((document.floats(j as usize, 4)?, document.floats(w as usize, 4)?)),
                    _ => return Err(refuse("Cook.Model.SkinNoWeights", &[("mesh", name.clone())])),
                }
            } else {
                None
            };
            // Four weights a vertex: a second set that weighs anything is refused, never dropped (models.md section 4).
            if let (true, Some(w)) = (skinned, attributes["WEIGHTS_1"].as_u64()) {
                let heavy = document.floats(w as usize, 4)?.chunks_exact(4).filter(|v| v.iter().any(|x| *x > 0.001)).count();
                if heavy > 0 {
                    return Err(refuse("Cook.Model.WeightsOver4", &[("mesh", name.clone()), ("vertices", heavy.to_string())]));
                }
            }
            // Vertex colours: two sets, as the block has (models.md section 5).
            let sets = attributes.as_object().map_or(0, |a| a.keys().filter_map(|k| k.strip_prefix("COLOR_")?.parse::<usize>().ok()).map(|k| k + 1).max().unwrap_or(0));
            if sets > 2 {
                return Err(refuse("Cook.Model.ColorSets", &[("mesh", name.clone()), ("count", sets.to_string()), ("max", "2".to_string())]));
            }
            let mut colour_values: [Option<Vec<f32>>; 2] = [None, None];
            for (k, values) in colour_values.iter_mut().enumerate() {
                if let Some(index) = attributes[format!("COLOR_{k}")].as_u64() {
                    *values = Some(colour_values_of(&document, index as usize)?);
                }
            }
            let material = primitive["material"].as_u64().map_or(default_material, |m| m as usize);
            let source = materials.get(material).or_invalid(|| at("material"))?;
            // Every TEXCOORD_n the primitive has, and every set its material's images read (zeros where it has none).
            let named_sets = attributes.as_object().map_or(0, |a| {
                a.keys().filter_map(|k| k.strip_prefix("TEXCOORD_")?.parse::<usize>().ok()).map(|k| k + 1).max().unwrap_or(0)
            });
            // Each lit map reads a coordinate set the mesh has (materials.md: Cook.Material.CoordSet).
            for (k, m) in source.pbr_maps.iter().enumerate() {
                if let Some(m) = m {
                    if !attributes[format!("TEXCOORD_{}", m.coord)].is_u64() {
                        return Err(refuse(
                            "Cook.Material.CoordSet",
                            &[("material", source.name.clone()), ("map", PBR_MAPS[k].1.to_string()), ("set", m.coord.to_string()), ("mesh", name.clone())],
                        ));
                    }
                }
            }
            let read = source.images.iter().map(|&(_, uv)| usize::from(uv) + 1).max().unwrap_or(0);
            let room = named_sets.max(read);
            if room > super::limits::MATERIAL_SLOTS {
                return Err(refuse("Cook.Model.CoordSets", &[("mesh", name.clone()), ("count", room.to_string()), ("max", super::limits::MATERIAL_SLOTS.to_string())]));
            }
            let mut coordinate_present = vec![false; room];
            let mut coordinate_values = Vec::with_capacity(room);
            for (k, present) in coordinate_present.iter_mut().enumerate() {
                let values = match attributes[format!("TEXCOORD_{k}")].as_u64() {
                    Some(index) => {
                        *present = true;
                        document.floats(index as usize, 2)?
                    }
                    None => vec![0.0; count * 2],
                };
                coordinate_values.push(values);
            }
            for &(_, uv) in &source.images {
                coordinate_present[usize::from(uv)] = true;
            }
            // A map's own transform, baked into the set it alone reads.
            for m in source.pbr_maps.iter().flatten() {
                if let Some(t) = &m.bake {
                    for uv in coordinate_values[m.coord as usize].chunks_exact_mut(2) {
                        let [u, v] = transformed_coordinate(t, [uv[0], uv[1]]);
                        uv[0] = u;
                        uv[1] = v;
                    }
                }
            }
            // A normal-mapped material's tangents (materials.md): the file's TANGENT by vertex, or MikkTSpace's by
            // corner over the normal map's (baked) coordinates when it has none or its normals are made flat (glTF 2.0
            // then ignores the file's).
            let tangents: Option<(Vec<f32>, bool)> = match &source.pbr_maps[PBR_NORMAL] {
                None => None,
                Some(map) => match attributes["TANGENT"].as_u64() {
                    Some(index) if !flat => Some((document.floats(index as usize, 4)?, false)),
                    _ => {
                        let mut input = TangentInput {
                            indices: &source_indices,
                            positions: &position_values,
                            normals: &normal_values,
                            flat,
                            coords: &coordinate_values[map.coord as usize],
                            out: vec![[1.0, 0.0, 0.0, 1.0]; source_indices.len()],
                        };
                        mikktspace::generate_tangents(&mut input);
                        Some((input.out.concat(), true))
                    }
                },
            };
            if coordinates.len() < room {
                coordinates.resize_with(room, Pool::new);
            }
            let mut corners: Vec<Corner> = Vec::new();
            let mut draws: Vec<u32> = Vec::new();
            let mut indices: Vec<u32> = Vec::with_capacity(source_indices.len());
            let mut corner_of: BTreeMap<u32, u32> = BTreeMap::new();
            let (mut low, mut high) = ([f32::MAX; 3], [f32::MIN; 3]);
            for (k, index) in source_indices.iter().enumerate() {
                // Flat normals and MikkTSpace's tangents make each corner its own vertex; shared ones share it (the
                // writer joins the corners alike).
                let key = if flat || tangents.as_ref().is_some_and(|(_, by_corner)| *by_corner) { k as u32 } else { *index };
                if let Some(found) = corner_of.get(&key) {
                    indices.push(*found);
                    continue;
                }
                let i = *index as usize;
                let position = [position_values[i * 3], position_values[i * 3 + 1], position_values[i * 3 + 2]];
                let n = if flat { k / 3 } else { i };
                let normal: [f32; 3] = normal_values.get(n * 3..n * 3 + 3).or_invalid(|| at("attributes/NORMAL"))?.try_into()?;
                // The joints that move the vertex and how much.
                let set: Vec<(u32, f32)> = match &weights {
                    Some((joint_values, weight_values)) => {
                        let mut set: Vec<(u32, f32)> = Vec::new();
                        for w in 0..4 {
                            let weight = *weight_values.get(i * 4 + w).or_invalid(|| at("attributes/WEIGHTS_0"))?;
                            if weight > 0.001 {
                                let joint = *joint_values.get(i * 4 + w).or_invalid(|| at("attributes/JOINTS_0"))?;
                                let node_of = *skin_joints.get(joint as usize).or_invalid(|| at("attributes/JOINTS_0"))?;
                                set.push((*joint_of_node.get(&node_of).or_invalid(|| at("attributes/JOINTS_0"))? as u32, weight));
                            }
                        }
                        set.sort_by_key(|(j, _)| *j);
                        if set.is_empty() {
                            unweighted += 1;
                            set.push((skin_root as u32, 1.0));
                        }
                        let total: f32 = set.iter().map(|(_, w)| w).sum();
                        set.iter_mut().for_each(|item| item.1 /= total);
                        set
                    }
                    None => vec![(static_joint as u32, 1.0)],
                };
                // Where it is at rest in the model, for its joints' bounds.
                let rest = match &weights {
                    Some(_) => set.iter().fold([0.0f32; 3], |sum, (j, w)| {
                        let placed = transform(&multiply(&joint_world[*j as usize], &joints[*j as usize].inverse), position, 1.0);
                        [sum[0] + placed[0] * w, sum[1] + placed[1] * w, sum[2] + placed[2] * w]
                    }),
                    None => transform(&world[*node], position, 1.0),
                }
                .map(|v| v * UNITS_PER_METRE);
                for (j, _) in &set {
                    widen(&mut joint_low[*j as usize], &mut joint_high[*j as usize], rest);
                }
                // Draw matrices: joints, then envelopes, numbered in a u32 with NONE kept for none.
                let draw = if set.len() == 1 {
                    u64::from(set[0].0)
                } else {
                    let key: Vec<(u32, u32)> = set.iter().map(|(j, w)| (*j, w.to_bits())).collect();
                    let next = ir.envelopes.len() as u64;
                    let id = *envelope_ids.entry(key).or_insert_with(|| {
                        ir.envelopes.push(set.clone());
                        next
                    });
                    joints.len() as u64 + id
                };
                if draw >= u64::from(NONE) {
                    return Err(refuse("Cook.Model.Blends", &[("mesh", name.clone()), ("max", (NONE as usize - joints.len()).to_string())]));
                }
                let draw = draw as u32;
                // One joint's vertex is in that joint's space; a blended one stays in the model's.
                let through: Option<&Matrix> = match &weights {
                    Some(_) if set.len() == 1 => Some(&joints[draw as usize].inverse),
                    Some(_) => None,
                    None => Some(&to_joint),
                };
                let place = through.map_or(position, |m| transform(m, position, 1.0)).map(|v| v * UNITS_PER_METRE);
                let turned = through.map_or(normal, |m| normal_through(m, normal));
                widen(&mut low, &mut high, place);
                let length = dot(turned, turned).sqrt().max(1e-6);
                let unit = turned.map(|v| v / length);
                let normal_number = normals.add(unit);
                // Tangent and bitangent (glTF 2.0: cross(normal, tangent) x w), turned as the surface is; a mirror
                // turns the bitangent's side.
                let (tangent_number, binormal_number) = match &tangents {
                    Some((values, by_corner)) => {
                        let n4 = if *by_corner { k } else { i } * 4;
                        let t: [f32; 4] = values.get(n4..n4 + 4).or_invalid(|| at("attributes/TANGENT"))?.try_into()?;
                        let along = through.map_or([t[0], t[1], t[2]], |m| transform(m, [t[0], t[1], t[2]], 0.0));
                        let mirrored = through.is_some_and(|m| determinant(m) < 0.0);
                        let side = if (t[3] < 0.0) != mirrored { -1.0 } else { 1.0 };
                        let tangent = square_to(unit, along);
                        let binormal = cross(unit, tangent).map(|v| v * side);
                        (normals.add(tangent), normals.add(binormal))
                    }
                    None => (normal_number, normal_number),
                };
                let mut coord = vec![0u32; room];
                for (c, slot) in coord.iter_mut().enumerate() {
                    let uv = coordinate_values[c].get(i * 2..i * 2 + 2).or_invalid(|| at(&format!("attributes/TEXCOORD_{c}")))?;
                    *slot = coordinates[c].add([uv[0], uv[1]]);
                }
                let mut color = [0u32; 2];
                for (c, values) in colour_values.iter().enumerate() {
                    if let Some(values) = values {
                        let v = values.get(i * 4..i * 4 + 4).or_invalid(|| at(&format!("attributes/COLOR_{c}")))?;
                        let rgba = u32::from(srgb_byte(v[0])) << 24
                            | u32::from(srgb_byte(v[1])) << 16
                            | u32::from(srgb_byte(v[2])) << 8
                            | u32::from((v[3].clamp(0.0, 1.0) * 255.0).round() as u8);
                        color[c] = colour_pools[c].add([f32::from_bits(rgba)]);
                    }
                }
                let number = corners.len() as u32;
                corners.push(Corner {
                    position: positions.add(place),
                    normal: normal_number,
                    tangent: tangent_number,
                    binormal: binormal_number,
                    color,
                    coord,
                    matrix: 0,
                    texture_matrix: Vec::new(),
                });
                draws.push(draw);
                corner_of.insert(key, number);
                indices.push(number);
            }
            ir.source_vertex_count += corners.len() as u32;
            if indices.is_empty() {
                continue;
            }
            // glTF's front faces wind counter-clockwise, the game's clockwise; under a mirroring node glTF's wind the other
            // way already (glTF 2.0: the determinant of the node's global transform; a skinned mesh's node is not used).
            if skinned || determinant(&world[*node]) >= 0.0 {
                for triangle in indices.chunks_exact_mut(3) {
                    triangle.swap(1, 2);
                }
            }

            // The draw matrices this mesh's corners name, by first use.
            let mut use_table: Vec<u32> = Vec::new();
            for (corner, draw) in corners.iter_mut().zip(&draws) {
                let local = use_table.iter().position(|u| u == draw).unwrap_or_else(|| {
                    use_table.push(*draw);
                    use_table.len() - 1
                });
                corner.matrix = local as u32;
            }
            let blended = use_table.len() > 1;
            let mut places = 0u32;
            if blended {
                for corner in &corners {
                    places |= 1u32.checked_shl(corner.matrix).unwrap_or(0);
                }
            }

            // Its own material: a mesh's draw and the material drawing it are one to one.
            let shape = ir.shapes.len() as u32;
            if ir.shapes.len() + 1 >= NONE as usize {
                return Err(refuse("Cook.Model.Meshes", &[("count", (ir.shapes.len() + 1).to_string()), ("max", (NONE - 2).to_string())]));
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
                    stage.map = map as u8;
                    stage.coord = map as u8;
                    if map != 0 {
                        stage.color_in = [15, 8, 0, 15];
                        stage.alpha_in = [7, 4, 0, 7];
                        stage.channel = 255;
                    }
                    made.stages.push(stage);
                }
                made.pixel_stage_count = made.stages.len() as u32;
            }
            // Vertex colours (glTF: base colour = factor x texture x COLOR_0): the channel takes the vertex's colour, and
            // a last stage multiplies by the factor, held in constant colour 0.
            if colour_values[0].is_some() {
                vertex_coloured.insert(material);
                made.channels[0].material_source = 1;
                made.constants[0] = source.color;
                made.material_color[0] = [0xFF; 4];
                let mut last = made.stages[0];
                last.coord = 255;
                last.map = 255;
                last.channel = 255;
                last.color_in = [15, 0, 14, 15];
                last.alpha_in = [7, 0, 6, 7];
                last.color_constant = 12;
                last.alpha_constant = 28;
                made.stages.push(last);
                made.pixel_stage_count = made.stages.len() as u32;
            }
            // Its glow (glTF: + emissive factor x emissive texture), last and in no light: a stage adds constant 1 x the
            // emissive map, or constant 1 alone, to what the stages before made; alpha stays theirs. The sum is taken
            // on sRGB values as the game's stages take theirs.
            if let Some(Glow { constant, map }) = source.glow {
                made.constants[1] = constant;
                let mut glow = made.stages[0];
                glow.coord = 255;
                glow.map = 255;
                glow.channel = 255;
                glow.color_in = [14, 15, 15, 0];
                glow.alpha_in = [7, 7, 7, 0];
                glow.color_constant = 13;
                glow.alpha_constant = 28;
                if let Some((image, uv)) = map {
                    let slot = own_slot(&mut made, texture_number(&document, &mut ir, &mut image_of, image)?, uv);
                    glow.map = slot;
                    glow.coord = slot;
                    glow.color_in = [15, 8, 14, 0];
                }
                made.stages.push(glow);
                made.pixel_stage_count = made.stages.len() as u32;
            }
            // Its normal map, which the lit form's pixel stage reads (MaterialIr normal_map): a mesh with tangents.
            if let Some(map) = source.pbr_maps[PBR_NORMAL].filter(|_| tangents.is_some() && source.lit.flags & PBR_UNLIT == 0) {
                let slot = own_slot(&mut made, texture_number(&document, &mut ir, &mut image_of, map.image)?, map.coord as u8);
                made.normal_map = Some((slot, source.lit.normal_scale));
            }
            // Its light (program_pbr.hlsli): metal and roughness by their factors and map, occlusion on the
            // indirect light, the glow after it. An unlit material shows its base colour alone.
            if source.lit.flags & PBR_UNLIT == 0 {
                let mut slots: Vec<((usize, u64), u8)> = Vec::new();
                for map in pbr_light_maps(&source.pbr_maps) {
                    let slot = own_slot(&mut made, texture_number(&document, &mut ir, &mut image_of, map.0)?, map.1 as u8);
                    slots.push((map, slot));
                }
                let slot_of = |m: Option<PbrMap>| m.and_then(|m| slots.iter().find(|(k, _)| *k == (m.image, m.coord)).map(|(_, s)| *s));
                made.pbr = Some(PbrShading {
                    metal: source.lit.metal,
                    roughness: source.lit.roughness,
                    metal_rough: slot_of(source.pbr_maps[PBR_METAL_ROUGH]),
                    occlusion: slot_of(source.pbr_maps[PBR_OCCLUSION]).map(|s| (s, source.lit.occlusion_strength)),
                    glow_last: source.glow.is_some(),
                });
            }
            // Its hand-written shader: each texture a slot of its own, each parameter a place of the material's
            // colours the stages leave free, holding its value; the lit forms call its `surface` in place of the stages.
            if let Some((shader, used, images)) = &source.shader {
                if made.pbr.is_none() {
                    return Err(refuse("Cook.Shader.Unlit", &[("material", source.name.clone()), ("shader", shader.name.clone())]));
                }
                let mut textures = Vec::new();
                for ((_, name), &image) in shader.textures.iter().zip(images) {
                    let slot = own_slot(&mut made, texture_number(&document, &mut ir, &mut image_of, image)?, 0);
                    textures.push((name.clone(), slot));
                }
                let places = super::surface_shader::places(shader, &source.name, &used.values, places_used(&made))?;
                let mut params = Vec::new();
                for ((p, value), &place) in shader.params.iter().zip(&used.values).zip(&places) {
                    let width = if p.four { 4 } else { 1 };
                    for (k, &given) in value.iter().enumerate() {
                        let v = if k < width { given } else { 0.0 };
                        // Place 0 to 2 is register 1 to 3: registers[0] to [2] (NEBULA_ROW_REGISTER, Material::fillRow).
                        if place < 3 {
                            made.registers[usize::from(place)][k] = (v * 255.0).round() as i16;
                        } else {
                            made.constants[usize::from(place) - 3][k] = (v * 255.0).round().clamp(0.0, 255.0) as u8;
                        }
                    }
                    params.push((p.name.clone(), p.four, place));
                }
                let emission = source.glow.as_ref().map_or([0.0; 3], |g| [0, 1, 2].map(|k| f32::from(g.constant[k]) / 255.0));
                made.surface = Some(SurfaceIr {
                    file: shader.file.clone(),
                    code: shader.code.clone(),
                    params,
                    textures,
                    uv: [0, 1].map(|k| coordinate_present.get(k).copied().unwrap_or(false)),
                    reads: shader.reads,
                    emission,
                });
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
            // A mesh hangs on its node's joint, or the skin's root (models.md section 5); on a billboard joint it turns
            // with it.
            let attached = if skinned { skin_root } else { static_joint };
            let turns = if blended || skinned { 0 } else { billboard[attached] };
            made.on_billboard = turns != 0;
            made.same_as = same_as;
            made.mesh = shape;
            made.joint = attached as u32;
            ir.materials.push(made);
            if has_materials {
                let mut lit = source.lit.clone();
                for (k, m) in source.pbr_maps.iter().enumerate() {
                    if let Some(m) = m {
                        lit.maps[k] = texture_number(&document, &mut ir, &mut image_of, m.image)?;
                        lit.coords[k] = m.coord as u8;
                    }
                }
                pbr.push(lit);
            }
            meshes_of_joint[attached].push(shape);
            let mut attributes_used = ATTR_NORMAL | if blended { ATTR_MATRIX } else { 0 };
            if tangents.is_some() {
                attributes_used |= ATTR_NBT | ATTR_NBT_THREE;
            }
            for (c, values) in colour_values.iter().enumerate() {
                if values.is_some() {
                    attributes_used |= ATTR_COLOR0 << c;
                }
            }
            ir.shapes.push(ShapeIr {
                matrix_type: if blended { 3 } else { turns },
                radius: bounds_radius(&low, &high),
                min: low,
                max: high,
                groups: vec![GroupIr { attributes: attributes_used, coordinates: coordinate_present, corners, indices, places, use_table }],
                material: shape,
            });
            ir.has_billboard |= turns != 0;
        }
        if unweighted > 0 {
            note("Cook.Model.Unweighted", &[("mesh", name.clone()), ("count", unweighted.to_string())]);
        }
    }
    if ir.shapes.is_empty() && !hidden {
        return Err(refuse("Cook.Model.NoTriangles", &[]));
    }
    // Textures an animation swaps in, which no material shows to begin with.
    for texture in &swapped {
        let image = root["textures"][*texture]["source"].as_u64().or_invalid(|| format!("/textures/{texture}/source"))?;
        texture_number(&document, &mut ir, &mut image_of, image as usize)?;
    }

    ir.pbr = pbr;
    ir.arrays = ArraysIr { normal_floats: 3, ..Default::default() };
    ir.arrays.positions = positions.values;
    ir.arrays.normals = normals.values;
    ir.arrays.colors = colour_pools.map(|pool| pool.values.into_iter().map(|[v]| v.to_bits()).collect());
    ir.arrays.coords = coordinates.into_iter().map(|pool| pool.values).collect();

    // A joint's bounds hold every vertex it moves at rest: its own and its children's (models.md section 4).
    let mut depth = vec![0usize; joints.len()];
    for (j, d) in depth.iter_mut().enumerate() {
        let mut at = joints[j].parent;
        while let Some(p) = at {
            *d += 1;
            at = joints[p].parent;
        }
    }
    let mut deepest_first: Vec<usize> = (0..joints.len()).collect();
    deepest_first.sort_by_key(|j| std::cmp::Reverse(depth[*j]));
    for j in deepest_first {
        if let (Some(p), true) = (joints[j].parent, joint_low[j][0] <= joint_high[j][0]) {
            let (low, high) = (joint_low[j], joint_high[j]);
            widen(&mut joint_low[p], &mut joint_high[p], low);
            widen(&mut joint_low[p], &mut joint_high[p], high);
        }
    }
    ir.joints = joints
        .iter()
        .enumerate()
        .map(|(j, joint)| {
            let (low, high) = if joint_low[j][0] <= joint_high[j][0] { (joint_low[j], joint_high[j]) } else { ([0.0; 3], [0.0; 3]) };
            JointIr {
                name: joint.name.clone(),
                kind: 0,
                scale_compensate: 0,
                billboard: billboard[j],
                scale: joint.trs.scale,
                rotation: angles(joint.trs.rotation),
                translation: joint.trs.translation.map(|v| v * UNITS_PER_METRE),
                radius: bounds_radius(&low, &high),
                min: low,
                max: high,
                parent: joint.parent.map_or(NONE, |p| p as u32),
                child: NONE,
                younger: NONE,
                meshes: meshes_of_joint[j].iter().rev().copied().collect(),
            }
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
        let of_materials = material_clips(&document, animation, &materials, &first_of_material, &vertex_coloured, &image_of, &timing)?;
        let moves_joints = animation["channels"].as_array().map(Vec::as_slice).unwrap_or_default().iter().any(|c| {
            c["target"]["node"].as_u64().is_some_and(|n| joint_of_node.contains_key(&(n as usize)))
                && matches!(c["target"]["path"].as_str(), Some("translation" | "rotation" | "scale"))
        });
        if moves_joints || of_materials.is_empty() {
            clips.push((name.clone(), clip_of(&document, animation, &joints, &joint_of_node, &timing)?));
        }
        clips.extend(of_materials.into_iter().map(|clip| (name.clone(), clip)));
    }
    facts.meshes = ir.shapes.len();
    facts.vertices = ir.source_vertex_count as usize;
    facts.materials = materials.len() - 1;
    facts.textures = ir.textures.len();
    // The model keeps its joint and material names one byte a character, and its ray-hit forms name the materials in
    // UTF-8: a name outside ASCII would be cut in the one and not meet the other.
    if let Some(joint) = ir.joints.iter().find(|j| !j.name.is_ascii()) {
        return Err(refuse("Cook.Model.JointName", &[("joint", joint.name.clone())]));
    }
    if let Some(material) = ir.materials.iter().find(|m| !m.name.is_ascii()) {
        return Err(refuse("Cook.Model.MaterialName", &[("material", material.name.clone())]));
    }
    facts.clips = clips.len();
    Ok((ir, clips, facts))
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
            let sampler = samplers.get(channel["sampler"].as_u64().unwrap_or(0) as usize).or_invalid(|| format!("{}/samplers", animation_at(document, animation)))?;
            let input = sampler["input"].as_u64().or_invalid(|| "/animations".to_string())? as usize;
            let times = document.floats(input, 1)?;
            // A key that is no number (NaN, infinite) would play as nothing the modder made.
            let output = sampler["output"].as_u64().or_invalid(|| "/animations".to_string())? as usize;
            let width = match document.root["accessors"][output]["type"].as_str() {
                Some("VEC2") => 2,
                Some("VEC3") => 3,
                Some("VEC4") => 4,
                _ => 1,
            };
            if !times.iter().chain(document.floats(output, width)?.iter()).all(|v| v.is_finite()) {
                return Err(refuse("Cook.Anim.NotANumber", &[("animation", animation_name(document, animation))]));
            }
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
            Some(other) => return Err(refuse("Cook.Clip.Plays", &[("animation", animation_name(document, animation)), ("found", other.to_string())])),
        };
        if length * FRAMES_PER_SECOND > MAX_CLIP_FRAMES as f32 {
            let seconds = format!("{length:.1}");
            let max = format!("{:.1}", MAX_CLIP_FRAMES as f32 / FRAMES_PER_SECOND);
            return Err(refuse("Cook.Anim.TooLong", &[("animation", animation_name(document, animation)), ("seconds", seconds), ("max", max)]));
        }
        let frames = ((length * FRAMES_PER_SECOND).ceil() as usize).max(1);
        let samples = (0..=frames).step_by(2).chain(std::iter::once(frames)).collect::<BTreeSet<_>>().into_iter().collect();
        Ok(Timing { frames, samples, loop_mode })
    }
}

/// An animation's place in the file, as a JSON pointer (`/animations/2`).
fn animation_at(document: &Document, animation: &Value) -> String {
    let list = document.root["animations"].as_array().map(Vec::as_slice).unwrap_or_default();
    let n = list.iter().position(|a| std::ptr::eq(a, animation)).unwrap_or(0);
    format!("/animations/{n}")
}

/// An animation by its name for a refusal, else its number.
fn animation_name(document: &Document, animation: &Value) -> String {
    animation["name"].as_str().filter(|s| !s.is_empty()).map_or_else(|| format!("#{}", &animation_at(document, animation)["/animations/".len()..]), str::to_string)
}

/// The file a cooked animation is, by what it moves.
fn clip_extension(kind: u8) -> &'static str {
    match kind {
        KIND_COLORS => "bpk",
        KIND_REGISTERS => "brk",
        KIND_TEXTURE_MATRICES => "btk",
        KIND_PATTERN => "btp",
        _ => "bck",
    }
}

/// A material's curve read from a pointer channel.
fn pointer_curve(document: &Document, samplers: &[Value], channel: &Value, width: usize) -> Result<Curve> {
    let sampler = samplers.get(place(channel["sampler"].as_u64().unwrap_or(0)).unwrap_or(usize::MAX)).or_invalid(|| "/animations".to_string())?;
    let curve = Curve::of(document, sampler, width, false)?;
    if curve.times.is_empty() {
        return Err(invalid("/animations".to_string()));
    }
    Ok(curve)
}

/// What an animation does to materials: their colours, their textures' transforms, their texture swaps.
fn material_clips(
    document: &Document,
    animation: &Value,
    materials: &[SourceMaterial],
    first_of_material: &BTreeMap<usize, u32>,
    vertex_coloured: &BTreeSet<usize>,
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
        let pointer = pointer_of(channel).or_invalid(|| format!("{}/channels", animation_at(document, animation)))?;
        let moves_other = || refuse("Cook.Clip.Pointer", &[("animation", animation_name(document, animation)), ("pointer", pointer.to_string())]);
        let Some((material, rest)) = material_pointer(pointer) else { return Err(moves_other()) };
        if material + 1 >= materials.len() {
            return Err(invalid(format!("{}/channels", animation_at(document, animation))));
        }
        if rest == BASE_COLOR {
            colours.insert(material, pointer_curve(document, samplers, channel, 4)?);
        } else if let Some(part @ ("offset" | "scale" | "rotation")) = rest.strip_prefix(TEXTURE_TRANSFORM) {
            let width = if part == "rotation" { 1 } else { 2 };
            moves.entry(material).or_default().insert(part, pointer_curve(document, samplers, channel, width)?);
        } else {
            return Err(moves_other());
        }
    }
    let drawn = |material: usize| {
        first_of_material.get(&material).copied().ok_or_else(|| refuse("Cook.Clip.Undrawn", &[("animation", animation_name(document, animation)), ("material", materials[material].name.clone())]))
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
                    // As the still factor (colour_bytes): RGB sRGB-encoded, alpha as it is.
                    channel.push(if k < 3 { srgb(value[k]) } else { value[k].clamp(0.0, 1.0) } * 255.0);
                }
            }
            for channel in &channels {
                let track = axis_track(&mut c.values, &timing.samples, channel)?;
                c.tracks.push(track);
            }
            c.targets.push(target(material, number));
        }
        // A vertex-coloured mesh multiplies by the factor in constant colour 0 (its channel takes the vertex's colour), so
        // the same curves play there too: a register animation's second set, constant 0.
        let mut constants = clip(KIND_REGISTERS, 0);
        for (e, (&material, _)) in colours.iter().enumerate() {
            if vertex_coloured.contains(&material) {
                constants.tracks.extend_from_slice(&c.tracks[e * 4..e * 4 + 4]);
                constants.targets.push(Target { slot: 0, ..target(material, c.targets[e].material) });
            }
        }
        constants.second_count = constants.targets.len() as u32;
        constants.values = c.values.clone();
        out.push(c);
        if constants.second_count > 0 {
            out.push(constants);
        }
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
            let material = swap["material"].as_u64().or_invalid(|| format!("{}/extras/nebula/texture_swaps", animation_at(document, animation)))? as usize;
            if material + 1 >= materials.len() {
                return Err(invalid(format!("{}/extras/nebula/texture_swaps", animation_at(document, animation))));
            }
            if materials[material].images.is_empty() {
                return Err(refuse("Cook.Clip.SwapNoTexture", &[("animation", animation_name(document, animation)), ("material", materials[material].name.clone())]));
            }
            let number = drawn(material)?;
            let mut keys: Vec<(f32, u32)> = Vec::new();
            for key in swap["keys"].as_array().map(Vec::as_slice).unwrap_or_default() {
                let swaps = || format!("{}/extras/nebula/texture_swaps", animation_at(document, animation));
                let second = key[0].as_f64().or_invalid(swaps)? as f32;
                let texture = key[1].as_u64().or_invalid(swaps)? as usize;
                let image = document.root["textures"][texture]["source"].as_u64().or_invalid(|| format!("/textures/{texture}/source"))?;
                keys.push((second * FRAMES_PER_SECOND, *image_of.get(&(image as usize)).or_invalid(|| format!("/textures/{texture}/source"))?));
            }
            if keys.is_empty() {
                return Err(refuse("Cook.Clip.SwapNoKeys", &[("animation", animation_name(document, animation)), ("material", materials[material].name.clone())]));
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
pub fn to_files(bytes: &[u8], folder: Option<&Path>, stem: &str) -> Result<(Vec<u8>, ClipFiles)> {
    to_files_for(bytes, folder, stem, None).map(|(model, clips, _)| (model, clips))
}

/// The same for a model that stands in for `game`'s, and what was made.
pub fn to_files_for(bytes: &[u8], folder: Option<&Path>, stem: &str, game: Option<&GameModel>) -> Result<(Vec<u8>, ClipFiles, ModelFacts)> {
    let (ir, clips, facts) = to_ir_for(bytes, folder, game)?;
    // A mod's model: no list reads its pipelines (the store's list is the disc cook's, Stats::pipelines).
    let model = super::model_write::write(&ir, stem, &super::programs::written_or_unknown(), &mut Default::default())?;
    super::surface_shader::check_compile(&ir)?;
    let mut files: ClipFiles = clips.iter().map(|(name, c)| (format!("{name}.{}", clip_extension(c.kind)), super::clip::write(c))).collect();
    if let Some(table) = params_table(&ir)? {
        files.push((format!("{stem}.params"), table));
    }
    Ok((model, files, facts))
}


/// `<model>.params` (materials.md section 3.4; content.model set_param reads it): a row for each parameter of each
/// material with a surface shader: material (its name), param, register (its place, 0 to 6). None without one.
pub fn params_table(ir: &ModelIr) -> Result<Option<Vec<u8>>> {
    let mut rows: Vec<Value> = Vec::new();
    let mut seen = BTreeSet::new();
    for m in &ir.materials {
        let Some(sf) = &m.surface else { continue };
        for (param, _, place) in &sf.params {
            if seen.insert((m.name.clone(), param.clone())) {
                rows.push(serde_json::json!({ "material": m.name, "param": param, "register": place }));
            }
        }
    }
    if rows.is_empty() {
        return Ok(None);
    }
    let json = serde_json::json!({ "columns": { "material": "string", "param": "string", "register": "int" }, "rows": rows });
    Ok(Some(super::table::TableIr::from_json(json.to_string().as_bytes())?.to_ntbl()))
}

/// The hidden model of `game` (`<path>.model.json` holding `{"hide": true}`): `skeleton` (skeleton_gltf's) as the
/// model block at the game model's place, every joint of the game's and nothing drawn, and what was made.
pub fn hidden_files_for(skeleton: &[u8], stem: &str, game: &GameModel) -> Result<(Vec<u8>, ModelFacts)> {
    let (ir, _, facts) = ir_for(skeleton, None, Some(game), true)?;
    Ok((super::model_write::write(&ir, stem, &super::programs::written_or_unknown(), &mut Default::default())?, facts))
}

/// An animation file (`<archive>.arc/<any>.anim.glb`, animation.md section 2): animations only, each a clip of `game`
/// (the archive's model) by its name, which replaces the game's clip of that name. Its nodes carry the model's skeleton:
/// each game joint is the node of its name, under the node of the joint's game parent; a joint no channel moves keeps
/// its node's place at rest. Channels move joints only (`file` names the file in refusals).
pub fn to_clip_files(bytes: &[u8], folder: Option<&Path>, file: &str, game: &GameModel) -> Result<(ClipFiles, ModelFacts)> {
    let document = Document::parse(bytes, folder)?;
    let root = &document.root;
    required_extensions(root)?;
    let (order, world, parents) = scene_tree(root, root["scene"].as_u64().unwrap_or(0) as usize)?;
    let nodes = root["nodes"].as_array().map(Vec::as_slice).unwrap_or_default();
    let mut node_of: Vec<usize> = Vec::with_capacity(game.joints.len());
    for name in &game.joints {
        let node = order
            .iter()
            .copied()
            .find(|&n| nodes[n]["name"].as_str() == Some(name.as_str()))
            .ok_or_else(|| refuse("Cook.Model.MissingJoint", &[("joint", name.clone()), ("model", game.path.clone())]))?;
        node_of.push(node);
    }
    let joint_of_node: BTreeMap<usize, usize> = node_of.iter().enumerate().map(|(j, &n)| (n, j)).collect();
    let parent_joint = |node: usize| -> Option<usize> {
        let mut at = parents[node];
        while let Some(p) = at {
            if let Some(&j) = joint_of_node.get(&p) {
                return Some(j);
            }
            at = parents[p];
        }
        None
    };
    let mut joints = Vec::with_capacity(node_of.len());
    for (j, &node) in node_of.iter().enumerate() {
        let parent = parent_joint(node);
        if let Some(&wanted) = game.parents.get(j) {
            let name = |p: usize| game.joints[p].clone();
            match (wanted, parent) {
                (Some(w), Some(p)) if w != p => {
                    return Err(refuse("Cook.Anim.ParentDiffers", &[("joint", name(j)), ("parent", name(w)), ("found", name(p))]));
                }
                (Some(w), None) => return Err(refuse("Cook.Anim.ParentMissing", &[("joint", name(j)), ("parent", name(w))])),
                (None, Some(p)) => return Err(refuse("Cook.Anim.RootDiffers", &[("joint", name(j)), ("found", name(p))])),
                _ => {}
            }
        }
        // Its place at rest from its parent joint's, as a model's joint (to_ir_for's `local`).
        let parent_node = parent.map(|p| node_of[p]);
        let (trs, between) = if parents[node] == parent_node {
            (node_trs(&nodes[node]), None)
        } else {
            let above = parent_node.map_or(IDENTITY, |p| affine_inverse(&world[p]));
            let between = multiply(&above, &parents[node].map_or(IDENTITY, |p| world[p]));
            (decompose(&multiply(&above, &world[node])), Some(between))
        };
        joints.push(Joint { name: game.joints[j].clone(), node: Some(node), parent, trs, between, inverse: IDENTITY, socket: false });
    }
    let animations = root["animations"].as_array().map(Vec::as_slice).unwrap_or_default();
    if animations.is_empty() {
        return Err(refuse("Cook.Anim.None", &[("file", file.to_string())]));
    }
    let game_materials = game_materials(root, game)?;
    let mut names = BTreeSet::new();
    let mut files: ClipFiles = Vec::new();
    for (i, animation) in animations.iter().enumerate() {
        let name = animation["name"].as_str().filter(|s| !s.is_empty()).ok_or_else(|| refuse("Cook.Anim.NoName", &[("animation", format!("#{i}"))]))?.to_string();
        if !names.insert(name.clone()) {
            return Err(refuse("Cook.Anim.Twice", &[("name", name)]));
        }
        for channel in animation["channels"].as_array().map(Vec::as_slice).unwrap_or_default() {
            let path = channel["target"]["path"].as_str().unwrap_or("");
            match (channel["target"]["node"].as_u64(), path) {
                (Some(node), "translation" | "rotation" | "scale") => {
                    if !joint_of_node.contains_key(&(node as usize)) {
                        let shown = named(nodes.get(node as usize).unwrap_or(&Value::Null), node as usize);
                        return Err(refuse("Cook.Anim.NotAJoint", &[("animation", name.clone()), ("node", shown), ("model", game.path.clone())]));
                    }
                }
                // A material's track (layer 4): checked with the others in material_clips.
                (_, "pointer") if pointer_of(channel).and_then(material_pointer).is_some() => {}
                _ => {
                    let pointer = channel["target"]["extensions"]["KHR_animation_pointer"]["pointer"].as_str().unwrap_or(path);
                    return Err(refuse("Cook.Anim.OnlyJoints", &[("animation", name.clone()), ("path", pointer.to_string())]));
                }
            }
        }
        let timing = Timing::of(&document, animation)?;
        // The game model's materials: whether a mesh of one is vertex-coloured is the game file's, not this file's, so
        // its colour plays on its colour register only (as before the vertex-coloured constant).
        let of_materials = material_clips(&document, animation, &game_materials.materials, &game_materials.number, &BTreeSet::new(), &game_materials.texture, &timing)?;
        let moves_joints = animation["channels"]
            .as_array()
            .map(Vec::as_slice)
            .unwrap_or_default()
            .iter()
            .any(|c| matches!(c["target"]["path"].as_str(), Some("translation" | "rotation" | "scale")));
        // A clip of materials only leaves the game's joint clip of that name as it is; one moving nothing is a still .bck.
        if moves_joints || of_materials.is_empty() {
            let clip = clip_of(&document, animation, &joints, &joint_of_node, &timing)?;
            files.push((format!("{name}.{}", clip_extension(clip.kind)), super::clip::write(&clip)));
        }
        files.extend(of_materials.iter().map(|clip| (format!("{name}.{}", clip_extension(clip.kind)), super::clip::write(clip))));
    }
    let facts = ModelFacts {
        replaced: game.path.clone(),
        joints: joints.len(),
        materials: game_materials.number.len(),
        clips: files.len(),
        ..ModelFacts::default()
    };
    Ok((files, facts))
}

/// An animation file's materials as the game model's (layer 4): each material the file's animations move or swap
/// stands for the game model's material of the same name, each image they swap in for its texture of that name (the
/// image's `name`, else its file name without the ending).
struct GameMaterials {
    /// The file's materials by its numbers, named as the game's (a material no animation names keeps its own name),
    /// and one more at the end (material_clips' list has the default material last).
    materials: Vec<SourceMaterial>,
    /// The game material's number of each file material an animation names.
    number: BTreeMap<usize, u32>,
    /// The game texture's number of each image an animation swaps in.
    texture: BTreeMap<usize, u32>,
}

fn standing_material(name: String, images: Vec<(usize, u8)>, transform: Option<[f32; 5]>) -> SourceMaterial {
    SourceMaterial {
        name,
        color: [255; 4],
        images,
        blend: false,
        cutoff: None,
        two_sided: false,
        transform,
        lit: PbrMaterial::default(),
        pbr_maps: [None; 5],
        glow: None,
        shader: None,
    }
}

fn game_materials(root: &Value, game: &GameModel) -> Result<GameMaterials> {
    let file_materials = root["materials"].as_array().map(Vec::as_slice).unwrap_or_default();
    let mut named_by_animation: BTreeSet<usize> = BTreeSet::new();
    let mut swapped_textures: BTreeSet<usize> = BTreeSet::new();
    for animation in root["animations"].as_array().map(Vec::as_slice).unwrap_or_default() {
        for channel in animation["channels"].as_array().map(Vec::as_slice).unwrap_or_default() {
            if let Some((material, _)) = pointer_of(channel).and_then(material_pointer) {
                named_by_animation.insert(material);
            }
        }
        for swap in animation["extras"]["nebula"]["texture_swaps"].as_array().map(Vec::as_slice).unwrap_or_default() {
            if let Some(material) = swap["material"].as_u64() {
                named_by_animation.insert(material as usize);
            }
            for key in swap["keys"].as_array().map(Vec::as_slice).unwrap_or_default() {
                if let Some(texture) = key[1].as_u64() {
                    swapped_textures.insert(texture as usize);
                }
            }
        }
    }
    // A store from before the model kept its names (an older import) has none to match: said so, not "no such name".
    let unread = || refuse("Cook.Anim.ModelUnread", &[("model", game.path.clone())]);
    let find = |list: &[String], name: &str| -> Option<u32> {
        list.iter().position(|n| n == name).or_else(|| list.iter().position(|n| n.eq_ignore_ascii_case(name))).map(|n| n as u32)
    };
    let mut materials = Vec::with_capacity(file_materials.len() + 1);
    let mut number = BTreeMap::new();
    for (i, material) in file_materials.iter().enumerate() {
        let own = named(material, i);
        let still = &material["pbrMetallicRoughness"]["baseColorTexture"]["extensions"]["KHR_texture_transform"];
        let mut name = own.clone();
        if named_by_animation.contains(&i) {
            if game.materials.is_empty() {
                return Err(unread());
            }
            let n = find(&game.materials, &own).ok_or_else(|| {
                refuse("Cook.Anim.NoGameMaterial", &[("material", own.clone()), ("model", game.path.clone()), ("materials", game.materials.join(", "))])
            })?;
            name = game.materials[n as usize].clone();
            number.insert(i, n);
        }
        let [sx, sy]: [f32; 2] = numbers(&still["scale"], [1.0; 2]);
        let [ox, oy]: [f32; 2] = numbers(&still["offset"], [0.0; 2]);
        // The game material's own texture is what moves and what a swap replaces: the file's material needs none (the
        // image number is never read for it). Parts no channel moves keep the file material's still transform (glTF's
        // default when it has none), not the game material's.
        let transform = [sx, sy, still["rotation"].as_f64().unwrap_or(0.0) as f32, ox, oy];
        materials.push(standing_material(name, vec![(usize::MAX, 0)], Some(transform)));
    }
    materials.push(standing_material("material".to_string(), Vec::new(), None));
    let mut texture = BTreeMap::new();
    for t in swapped_textures {
        // A texture with no source: material_clips says where.
        let Some(image) = root["textures"][t]["source"].as_u64() else { continue };
        let image = image as usize;
        let shown = &root["images"][image];
        let own = shown["name"].as_str().filter(|s| !s.is_empty()).map(str::to_string).or_else(|| {
            let uri = shown["uri"].as_str().filter(|u| !u.starts_with("data:"))?;
            let file = uri.rsplit('/').next().unwrap_or(uri);
            // Only the last ending goes: the game's texture names have dots of their own (KuriboAll.2).
            Some(file.rsplit_once('.').map_or(file, |(stem, _)| stem).to_string()).filter(|s| !s.is_empty())
        });
        let Some(own) = own else {
            return Err(refuse("Cook.Anim.TextureUnnamed", &[("texture", format!("/textures/{t}")), ("model", game.path.clone())]));
        };
        if game.textures.is_empty() {
            return Err(unread());
        }
        let n = find(&game.textures, &own)
            .ok_or_else(|| refuse("Cook.Anim.NoGameTexture", &[("texture", own.clone()), ("model", game.path.clone()), ("textures", game.textures.join(", "))]))?;
        texture.insert(image, n);
    }
    Ok(GameMaterials { materials, number, texture })
}

/// The game counts animation frames at 60 a second.
const FRAMES_PER_SECOND: f32 = 60.0;

/// The longest clip: the game's clip files count frames in 16 bits, 18 minutes at 60 a second.
const MAX_CLIP_FRAMES: usize = 65_535;

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
    /// A sampler's curve: its keys' times (glTF 2.0 3.11: numbers, rising) and a value for each (three for a cubic
    /// spline), so `at` finds every value it reads.
    fn of(document: &Document, sampler: &Value, width: usize, turns: bool) -> Result<Curve> {
        let times = document.floats(place(sampler["input"].as_u64().or_invalid(|| "/animations".to_string())?).unwrap_or(usize::MAX), 1)?;
        let values = document.floats(place(sampler["output"].as_u64().or_invalid(|| "/animations".to_string())?).unwrap_or(usize::MAX), width)?;
        let mode = sampler["interpolation"].as_str().unwrap_or("LINEAR");
        let curve = Curve { times, values, width, step: mode == "STEP", cubic: mode == "CUBICSPLINE", turns };
        if !curve.times.iter().all(|t| t.is_finite()) || curve.times.windows(2).any(|w| w[1] < w[0]) {
            return Err(invalid("/animations/samplers/input".to_string()));
        }
        let per_key = if curve.cubic { width * 3 } else { width };
        if curve.values.len() < curve.times.len() * per_key {
            return Err(invalid("/animations/samplers/output".to_string()));
        }
        Ok(curve)
    }

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
    let count = u32::try_from(values.len()).ok().or_invalid(|| "/animations".to_string())?;
    for (k, value) in values.iter().enumerate() {
        let (a, b) = (k.saturating_sub(1), (k + 1).min(values.len() - 1));
        let slope = (values[b] - values[a]) / ((frames[b] - frames[a]).max(1) as f32);
        out.extend_from_slice(&[frames[k] as f32, *value, slope]);
    }
    Ok(Track { count, stride: 3, integer_keys: false, at })
}

/// An animation, each joint sampled on every other frame.
fn clip_of(document: &Document, animation: &Value, joints: &[Joint], joint_of_node: &BTreeMap<usize, usize>, timing: &Timing) -> Result<ClipIr> {
    let samplers = animation["samplers"].as_array().or_invalid(|| format!("{}/samplers", animation_at(document, animation)))?;
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
        let sampler = samplers.get(channel["sampler"].as_u64().unwrap_or(0) as usize).or_invalid(|| format!("{}/samplers", animation_at(document, animation)))?;
        let curve = Curve::of(document, sampler, width, width == 4)?;
        if curve.times.is_empty() {
            continue;
        }
        curves.insert((joint, key), curve);
    }
    let samples = &timing.samples;
    let mut clip = ClipIr { kind: KIND_JOINTS, loop_mode: timing.loop_mode, frames: timing.frames as i32, first_count: joints.len() as u32, ..ClipIr::default() };
    for (index, joint) in joints.iter().enumerate() {
        let mut scale_values = [vec![], vec![], vec![]];
        let mut angle_values: [Vec<f32>; 3] = [vec![], vec![], vec![]];
        let mut translate_values = [vec![], vec![], vec![]];
        for frame in samples {
            let time = *frame as f32 / FRAMES_PER_SECOND;
            let animated = |key: &str| curves.contains_key(&(index, key));
            let (s, r, t) = match (&joint.between, animated("s") || animated("r") || animated("t")) {
                // Moved by the clip, under nodes it does not move: those nodes' place, then the clip's.
                (Some(between), true) => {
                    let own = |key: &str, rest: &[f32]| curves.get(&(index, key)).map_or(rest.to_vec(), |c| c.at(time));
                    let n = joint.node.map_or_else(|| node_trs(&Value::Null), |node| node_trs(&document.root["nodes"][node]));
                    let sampled = Trs {
                        translation: own("t", &n.translation).try_into().unwrap_or(n.translation),
                        rotation: own("r", &n.rotation).try_into().unwrap_or(n.rotation),
                        scale: own("s", &n.scale).try_into().unwrap_or(n.scale),
                    };
                    let whole = decompose(&multiply(between, &trs_matrix(&sampled)));
                    (whole.scale.to_vec(), whole.rotation.to_vec(), whole.translation.to_vec())
                }
                _ => (
                    curves.get(&(index, "s")).map_or(joint.trs.scale.to_vec(), |c| c.at(time)),
                    curves.get(&(index, "r")).map_or(joint.trs.rotation.to_vec(), |c| c.at(time)),
                    curves.get(&(index, "t")).map_or(joint.trs.translation.to_vec(), |c| c.at(time)),
                ),
            };
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
    let node = nodes.iter().position(|n| n["camera"].is_u64()).ok_or_else(|| refuse("Cook.Camera.None", &[]))?;
    let camera_index = nodes[node]["camera"].as_u64().unwrap_or(0) as usize;
    let camera = &root["cameras"][camera_index];
    if camera["type"].as_str() != Some("perspective") {
        return Err(refuse("Cook.Camera.NotPerspective", &[("camera", camera_index.to_string())]));
    }
    let yfov = camera["perspective"]["yfov"].as_f64().ok_or_else(|| refuse("Cook.Camera.NoFov", &[("camera", camera_index.to_string())]))? as f32;
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
        // A node is in one place of the tree (glTF 2.0 3.5.3): more parents than nodes is a cycle.
        if chain.len() >= nodes.len() {
            return Err(invalid(format!("/nodes/{p}/children")));
        }
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
    let animation = animations.iter().find(|a| moves(a)).ok_or_else(|| refuse("Cook.Camera.NoAnimation", &[]))?;
    for c in animation["channels"].as_array().map(Vec::as_slice).unwrap_or_default() {
        if c["target"]["node"].as_u64().is_some_and(|n| chain.contains(&(n as usize))) {
            return Err(refuse("Cook.Camera.ParentMoves", &[("node", c["target"]["node"].to_string())]));
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
    if length * FRAMES_PER_SECOND > MAX_CLIP_FRAMES as f32 {
        let (seconds, max) = (format!("{length:.1}"), format!("{:.1}", MAX_CLIP_FRAMES as f32 / FRAMES_PER_SECOND));
        return Err(refuse("Cook.Anim.TooLong", &[("animation", animation_name(&document, animation)), ("seconds", seconds), ("max", max)]));
    }
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
#[path = "gltf_tests.rs"]
mod tests;
