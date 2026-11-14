//! A model as typed values: one input side per source format (J3D here, glTF in gltf.rs), one output (model_write.rs).
//! Everything big-endian ends in this file.

use super::{formats, Stats};
use anyhow::{bail, Context, Result};
use std::collections::HashMap;

pub use super::limits::NONE;
pub const ANGLE_UNIT: f32 = std::f32::consts::PI / 32768.0;
pub fn source_index(n: u32) -> u32 { if n == 0xFFFF { NONE } else { n } }

pub const ATTR_NORMAL: u64 = 1 << 0;
pub const ATTR_COLOR0: u64 = 1 << 1;
pub const ATTR_NBT: u64 = 1 << 19;
pub const ATTR_NBT_THREE: u64 = 1 << 20;
pub const ATTR_MATRIX: u64 = 1 << 21;
/// J3D's vertex formats: eight coordinate attributes, each with an optional texture matrix index.
pub const J3D_COORDINATES: usize = 8;

#[derive(Clone, Default)]
pub struct JointIr {
    pub name: String,
    pub kind: u8,
    pub scale_compensate: u8,
    pub billboard: u8,
    pub scale: [f32; 3],
    pub rotation: [f32; 3],
    pub translation: [f32; 3],
    pub radius: f32,
    pub min: [f32; 3],
    pub max: [f32; 3],
    pub parent: u32,
    pub child: u32,
    pub younger: u32,
    /// Materials in entry order.
    pub meshes: Vec<u32>,
}

pub const NO_TEXTURE_MATRIX: u8 = 0xFF;

/// One corner of a source group, by what it holds. `coord` and `texture_matrix` by coordinate number, as many as its
/// group's `coordinates`; NO_TEXTURE_MATRIX where the group has no texture matrix number.
#[derive(Clone, PartialEq, Eq, Hash)]
pub struct Corner {
    pub position: u32,
    pub normal: u32,
    pub tangent: u32,
    pub binormal: u32,
    pub color: [u32; 2],
    pub coord: Vec<u32>,
    pub matrix: u32,
    pub texture_matrix: Vec<u8>,
}

impl Corner {
    pub fn coord_at(&self, k: usize) -> u32 {
        self.coord.get(k).copied().unwrap_or(0)
    }
}

#[derive(Clone)]
pub struct GroupIr {
    pub attributes: u64,
    /// Which coordinate numbers its corners carry.
    pub coordinates: Vec<bool>,
    pub corners: Vec<Corner>,
    pub indices: Vec<u32>,
    pub places: u32,
    /// Draw matrices this group's corners name, by corner matrix number, 0xFFFF kept from the group before.
    pub use_table: Vec<u32>,
}

#[derive(Clone)]
pub struct ShapeIr {
    pub matrix_type: u8,
    pub radius: f32,
    pub min: [f32; 3],
    pub max: [f32; 3],
    pub groups: Vec<GroupIr>,
    pub material: u32,
}

#[derive(Clone, Copy, Default)]
pub struct ChannelIr {
    pub enable: u8,
    pub material_source: u8,
    pub ambient_source: u8,
    pub light_mask: u8,
    pub diffuse: u8,
    pub attenuation: u8,
}

#[derive(Clone, Copy, Default)]
pub struct CoordIr {
    pub kind: u8,
    pub source: u8,
    pub matrix: u32,
}

#[derive(Clone, Copy)]
pub struct CoordMatrixIr {
    pub slot: u8,
    pub projection: u8,
    pub info: u8,
    pub center: [f32; 3],
    pub scale: [f32; 2],
    pub rotation: f32,
    pub translation: [f32; 2],
    pub effect: [[f32; 4]; 4],
}

#[derive(Clone, Copy, Default)]
pub struct WarpIr {
    pub rows: [[f32; 3]; 2],
}

#[derive(Clone, Copy, Default)]
pub struct FogIr {
    pub kind: u8,
    pub start: f32,
    pub end: f32,
    pub near: f32,
    pub far: f32,
    pub color: [u8; 4],
}

/// One colour stage: what it combines, as the file orders it.
#[derive(Clone, Copy, Default, PartialEq, Eq, Hash)]
pub struct StageIr {
    pub coord: u8,
    pub map: u8,
    pub channel: u8,
    pub color_in: [u8; 4],
    pub color_op: u8,
    pub color_bias: u8,
    pub color_scale: u8,
    pub color_clamp: u8,
    pub color_out: u8,
    pub alpha_in: [u8; 4],
    pub alpha_op: u8,
    pub alpha_bias: u8,
    pub alpha_scale: u8,
    pub alpha_clamp: u8,
    pub alpha_out: u8,
    pub raster_swap: u8,
    pub texture_swap: u8,
    pub color_constant: u8,
    pub alpha_constant: u8,
    pub warp: [u8; 9],
}

#[derive(Clone, Default)]
pub struct MaterialIr {
    pub name: String,
    pub same_as: u32,
    pub on_billboard: bool,
    pub mesh: u32,
    pub joint: u32,
    pub mode: u8,
    pub cull: u8,
    pub has_ambient: bool,
    pub has_stage_colors: bool,
    pub has_indirect: bool,
    pub has_fog: bool,
    pub early_depth: u8,
    pub material_color: [[u8; 4]; 2],
    pub ambient_color: [[u8; 4]; 2],
    pub registers: [[i16; 4]; 4],
    pub constants: [[u8; 4]; 4],
    pub channel_count: u32,
    pub channels: [ChannelIr; 4],
    pub coords: Vec<CoordIr>,
    pub textures: Vec<u32>,
    pub matrices: Vec<CoordMatrixIr>,
    pub warps: Vec<WarpIr>,
    pub fog: FogIr,
    pub alpha_comp0: u8,
    pub alpha_op: u8,
    pub alpha_comp1: u8,
    pub alpha_ref0: u8,
    pub alpha_ref1: u8,
    pub blend_type: u8,
    pub blend_src: u8,
    pub blend_dst: u8,
    pub depth_test: u8,
    pub depth_func: u8,
    pub depth_write: u8,
    pub has_scale: u8,
    pub normal_scale: [f32; 3],
    /// What the generator reads.
    pub stages: Vec<StageIr>,
    pub swaps: [[u8; 4]; 4],
    pub warp_orders: Vec<[u8; 4]>,
    pub pixel_stage_count: u32,
    /// The map each coordinate's scale comes from (255 none), by coordinate number.
    pub coordinate_maps: Vec<u8>,
    /// The maps read through a normal-made coordinate that hold a colour ramp, not a reflection (c56,
    /// hit_record.rs mark_ramps): they keep their own fetch.
    pub ramp_maps: Vec<u8>,
    /// The maps read through a normal-made coordinate that are painted highlight sprites (c58, hit_record.rs
    /// mark_highlights), and the first one's roughness and peak as the enhanced highlight takes them.
    pub highlight_maps: Vec<u8>,
    pub highlight_rough: f32,
    pub highlight_gain: f32,
    /// Drawn blended by alpha for a fade of the whole material: its alpha 0.9 or more over its maps' texels and its
    /// vertices (c61, hit_record.rs mark_fades). The class rules take it as opaque.
    pub fade_opaque: bool,
    /// The alpha's low and high ends over those (c61, the census); -1 for a material not blended by alpha.
    pub fade_alpha: [f32; 2],
    /// A tangent-space normal map (glTF normalTexture; MOD-MATERIALS-FC.md 1: inside generate(), behind this): its slot
    /// (map n through coordinate n, which no stage reads) and its scale. The lit form turns its pixel normal by it
    /// before any light is summed; the mesh holds tangents and bitangents (ATTR_NBT, ATTR_NBT_THREE).
    pub normal_map: Option<(u8, f32)>,
    /// A glTF material's light (program_pbr.hlsli): its lit form lights the stages' result as metal and
    /// roughness say, with the stage's reflection. None for the game's materials and an unlit glTF material.
    pub pbr: Option<PbrShading>,
    /// A mod's hand-written surface shader (materials.md section 3.4; surface_shader.rs): its lit forms call its
    /// `surface` in place of the stages and light what it fills as `pbr` says. Only with `pbr`.
    pub surface: Option<SurfaceIr>,
    /// What a ray takes of it beyond the cook's reading: a glTF material's `extras.nebula.ray` over its shader's
    /// shader.json `ray` (RayRows::over). None given: the record is the cook's, byte for byte.
    pub ray: RayRows,
}

/// A material's hand-written shader as the generator writes it in (MaterialIr surface).
#[derive(Clone, Debug, PartialEq, Default)]
pub struct SurfaceIr {
    /// The shader's file as the mod has it (`assets/shaders/Stripes.hlsl`): the compiler's lines name it.
    pub file: String,
    pub code: String,
    /// Each parameter: its name, float4 or float, its place (0 to 2: registers 1 to 3; 3 to 6: constant colours 0 to 3).
    pub params: Vec<(String, bool, u8)>,
    /// Each texture: its name and the material's map slot (map n through coordinate n).
    pub textures: Vec<(String, u8)>,
    /// Whether the mesh has TEXCOORD_0 and TEXCOORD_1: SurfaceIn uv0 and uv1 read them, else 0.
    pub uv: [bool; 2],
    /// surface_shader::READ_*.
    pub reads: u8,
    /// The emission it starts from: the glTF emissive factor, display-encoded.
    pub emission: [f32; 3],
    /// What its shader.json `ray` gives a ray in place of the reading of its factors (materials.md section 3.4, layer
    /// 3).
    pub ray: RayRows,
}

/// A material's rows for a ray (shader.json `ray`, a glTF material's `extras.nebula.ray`): each one given replaces what
/// the cook reads of the material in its hit record (nebula_model.h NebulaModelHit); none given, the record is the
/// cook's. Colours display-encoded, 0 to 1; emission 0 or more.
#[derive(Clone, Debug, PartialEq, Default)]
pub struct RayRows {
    /// hit_record::Class by name: dielectric, gloss, metal, glass, cutout, translucent, emissive or painted.
    pub class: Option<String>,
    pub base: Option<[f32; 3]>,
    pub roughness: Option<f32>,
    pub metal: Option<f32>,
    pub emission: Option<[f32; 3]>,
    /// The share of light that goes through it (the record's transmission).
    pub through: Option<f32>,
    /// The index of refraction, 1 to 3 (the record's ior; a metal with a kind keeps its kind's code there).
    pub ior: Option<f32>,
    /// Units light crosses inside it, 0 or more, in place of what its meshes measure.
    pub thickness: Option<f32>,
    /// A see-through body's coverage 0 to 1, the reflection's gain 0 to 16, how much of its tint stays 0 to 1.
    pub body: Option<f32>,
    pub shine: Option<f32>,
    pub tint_keep: Option<f32>,
    /// `traced`: true for "hidden" (a stand-in a ray never meets), false for "seen".
    pub traced_hidden: Option<bool>,
    /// The share of the sun its far side takes, 0 to 1 (the record's `through` byte, not transmission).
    pub sun_through: Option<f32>,
    /// A metal's kind: hit_record::METAL_KINDS' place + 1, or 255 for `art` (its colour as it is).
    pub metal_kind: Option<u8>,
    /// The light it gives (the record's glow).
    pub glow: Option<RayGlow>,
    /// What its vertex colour is (None: `auto`, the cook's rule, which reads it as painted light).
    pub vertex_colour: Option<VertexColour>,
}

impl RayRows {
    /// These rows over `under`: each row given here, else under's.
    pub fn over(&self, under: &RayRows) -> RayRows {
        RayRows {
            class: self.class.clone().or_else(|| under.class.clone()),
            base: self.base.or(under.base),
            roughness: self.roughness.or(under.roughness),
            metal: self.metal.or(under.metal),
            emission: self.emission.or(under.emission),
            through: self.through.or(under.through),
            ior: self.ior.or(under.ior),
            thickness: self.thickness.or(under.thickness),
            body: self.body.or(under.body),
            shine: self.shine.or(under.shine),
            tint_keep: self.tint_keep.or(under.tint_keep),
            traced_hidden: self.traced_hidden.or(under.traced_hidden),
            sun_through: self.sun_through.or(under.sun_through),
            metal_kind: self.metal_kind.or(under.metal_kind),
            glow: self.glow.or(under.glow),
            vertex_colour: self.vertex_colour.or(under.vertex_colour),
        }
    }

    /// `vertex_colour` is `albedo`: the vertex colour is the surface's own colour, not painted light.
    pub fn vertex_albedo(&self) -> bool {
        self.vertex_colour == Some(VertexColour::Albedo)
    }
}

/// A material's vertex colour for a ray (`ray.vertex_colour`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VertexColour {
    /// The cook's rule (`auto`): painted light, de-lit by the cooker's gain where the material reads it.
    Auto,
    /// The surface's own colour (glTF's COLOR_0 by its specification): no environment share, no gain stream, no
    /// painted-shadow bit.
    Albedo,
    /// Painted light, as the game's own models hold it: the cook's rule, said.
    Light,
}

/// A material's light for a ray: its colour (None: the material's own, as the cook finds it) and its strength, above 0
/// candela, below 0 a luminance in candela a square metre, 0 no light.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RayGlow {
    pub colour: Option<[f32; 3]>,
    pub strength: f32,
}

/// What a glTF material's lit template reads beyond its stages (MaterialIr pbr). Its maps are slots of their own (map n
/// through coordinate n, which no stage reads), sampled as data, not as sRGB colour.
#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct PbrShading {
    /// metallicFactor and roughnessFactor (0 to 1).
    pub metal: f32,
    pub roughness: f32,
    /// metallicRoughnessTexture's slot: its blue scales the metal, its green the roughness.
    pub metal_rough: Option<u8>,
    /// occlusionTexture's slot (its red) and strength: it darkens the indirect light only.
    pub occlusion: Option<(u8, f32)>,
    /// The material's last stage is its glow (gltf.rs Glow), added after the light: the light goes before it.
    pub glow_last: bool,
}

impl GroupIr {
    pub fn has_coord(&self, k: usize) -> bool {
        self.coordinates.get(k).copied().unwrap_or(false)
    }
}

#[derive(Clone)]
pub struct TextureIr {
    pub name: String,
    pub format: u8,
    pub width: u32,
    pub height: u32,
    pub wrap_s: u8,
    pub wrap_t: u8,
    pub mipmap: u8,
    pub anisotropy: u8,
    pub min_filter: u8,
    pub mag_filter: u8,
    pub min_lod: f32,
    pub max_lod: f32,
    pub levels: u32,
    pub lod_bias: f32,
    /// Which record of `ModelIr::records` holds its image.
    pub record: usize,
}

#[derive(Default)]
pub struct ArraysIr {
    pub positions: Vec<[f32; 3]>,
    pub normals: Vec<[f32; 3]>,
    pub nbt: Vec<[f32; 9]>,
    pub normal_floats: u32,
    pub colors: [Vec<u32>; 2],
    /// By coordinate number.
    pub coords: Vec<Vec<[f32; 2]>>,
}

impl ArraysIr {
    pub fn coord(&self, k: usize, index: u32) -> [f32; 2] {
        self.coords.get(k).and_then(|c| c.get(index as usize)).copied().unwrap_or([0.0; 2])
    }
}

#[derive(Default)]
pub struct ModelIr {
    pub scale_mode: u8,
    pub source_flags: u32,
    pub has_billboard: bool,
    pub materials_only: bool,
    pub source_vertex_count: u32,
    pub joints: Vec<JointIr>,
    pub draw_matrices: Vec<(u8, u32)>,
    pub full_weight_count: u32,
    pub envelopes: Vec<Vec<(u32, f32)>>,
    pub inverse_binds: Vec<[f32; 12]>,
    pub important: Vec<u32>,
    pub arrays: ArraysIr,
    pub shapes: Vec<ShapeIr>,
    pub materials: Vec<MaterialIr>,
    pub textures: Vec<TextureIr>,
    pub records: Vec<Vec<u8>>,
    /// By record of a disc model: its source's palette hash (texture::source_palette_hash), for its name. Empty for a
    /// model never on the disc (a mod's glTF).
    pub record_palettes: Vec<u64>,
    /// A glTF model's materials as glTF says them, one per material (material n's at n); none for a game model. The
    /// cook reads them for what a ray meets (hit_record::pbr_reading); the block holds no table of them.
    pub pbr: Vec<PbrMaterial>,
}

/// A glTF material's alpha modes and flags.
pub const PBR_OPAQUE: u8 = 0;
pub const PBR_MASK: u8 = 1;
pub const PBR_BLEND: u8 = 2;
pub const PBR_DOUBLE_SIDED: u8 = 1 << 0;
pub const PBR_UNLIT: u8 = 1 << 1;
/// The maps of a glTF material, in PBR_MAPS' order (gltf.rs).
pub const PBR_BASE: usize = 0;
pub const PBR_METAL_ROUGH: usize = 1;
pub const PBR_NORMAL: usize = 2;
pub const PBR_EMISSIVE: usize = 3;
pub const PBR_OCCLUSION: usize = 4;

/// A glTF material (glTF 2.0's metallic-roughness model and the KHR_materials extensions the cook takes) as the cook
/// read it. Colours linear; `maps` model texture numbers (NONE: none), each read through `coords`.
#[derive(Clone, Debug, PartialEq)]
pub struct PbrMaterial {
    pub base_color: [f32; 4],
    pub emissive: [f32; 3],
    pub metal: f32,
    pub roughness: f32,
    pub normal_scale: f32,
    pub occlusion_strength: f32,
    pub alpha_cutoff: f32,
    /// KHR_materials_ior's index (1.5 without it).
    pub ior: f32,
    /// KHR_materials_transmission's factor, if the material has the extension.
    pub transmission: Option<f32>,
    /// PBR_BASE, PBR_METAL_ROUGH, PBR_NORMAL, PBR_EMISSIVE, PBR_OCCLUSION.
    pub maps: [u32; 5],
    pub coords: [u8; 5],
    pub alpha_mode: u8,
    pub flags: u8,
}

impl Default for PbrMaterial {
    /// glTF 2.0's default material: white, fully metal and rough, no maps.
    fn default() -> PbrMaterial {
        PbrMaterial {
            base_color: [1.0; 4],
            emissive: [0.0; 3],
            metal: 1.0,
            roughness: 1.0,
            normal_scale: 1.0,
            occlusion_strength: 1.0,
            alpha_cutoff: 0.0,
            ior: 1.5,
            transmission: None,
            maps: [NONE; 5],
            coords: [0; 5],
            alpha_mode: PBR_OPAQUE,
            flags: 0,
        }
    }
}

fn be16(b: &[u8], at: usize) -> Result<u32> {
    Ok(u16::from_be_bytes(b.get(at..at + 2).context("model field past its block")?.try_into().unwrap()) as u32)
}

fn be32(b: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(b.get(at..at + 4).context("model field past its block")?.try_into().unwrap()))
}

fn bef(b: &[u8], at: usize) -> Result<f32> {
    Ok(f32::from_bits(be32(b, at)?))
}

fn vec3(b: &[u8], at: usize) -> Result<[f32; 3]> {
    Ok([bef(b, at)?, bef(b, at + 4)?, bef(b, at + 8)?])
}

/// A name table: count, entries of hash and offset, strings.
fn names(b: &[u8], table: usize) -> Result<Vec<String>> {
    if table == 0 {
        return Ok(Vec::new());
    }
    let count = be16(b, table)? as usize;
    let mut out = Vec::with_capacity(count);
    for i in 0..count {
        let at = table + be16(b, table + 4 + i * 4 + 2)? as usize;
        let text = b.get(at..).context("a name past its block")?;
        let end = text.iter().position(|c| *c == 0).context("a name without its end")?;
        // A byte at a time keeps names the console's text encoding wrote as they were.
        out.push(text[..end].iter().map(|&c| c as char).collect());
    }
    Ok(out)
}

/// The blocks of a J3D file, by tag. A last block's size may count padding the file does not have.
fn blocks(bytes: &[u8]) -> Result<HashMap<[u8; 4], &[u8]>> {
    let count = be32(bytes, 12)? as usize;
    let mut at = 32usize;
    let mut out = HashMap::new();
    for _ in 0..count {
        let tag: [u8; 4] = bytes.get(at..at + 4).context("a block past the file's end")?.try_into().unwrap();
        let size = be32(bytes, at + 4)? as usize;
        if size < 8 {
            bail!("a block too short");
        }
        let end = (at + size).min(bytes.len());
        out.insert(tag, &bytes[at..end]);
        at += size;
    }
    Ok(out)
}

#[derive(Clone, Copy)]
struct Format {
    cnt: u32,
    kind: u32,
    frac: u32,
}

fn format_of(list: &[(u32, Format)], attr: u32) -> Format {
    let mut f = match attr {
        9 => Format { cnt: 1, kind: 4, frac: 0 },
        10 => Format { cnt: 0, kind: 4, frac: 0 },
        11 | 12 => Format { cnt: 1, kind: 5, frac: 0 },
        _ => Format { cnt: 1, kind: 4, frac: 0 },
    };
    for &(a, g) in list {
        let listed = if a == 25 { 10 } else { a };
        if listed == attr {
            f = g;
        }
    }
    f
}

fn component(p: &[u8], at: usize, kind: u32, scale: f32) -> f32 {
    let two = |at: usize| -> u16 { p.get(at..at + 2).map_or(0, |s| u16::from_be_bytes([s[0], s[1]])) };
    match kind {
        0 => f32::from(p.get(at).copied().unwrap_or(0)) * scale,
        1 => f32::from(p.get(at).copied().unwrap_or(0) as i8) * scale,
        2 => f32::from(two(at)) * scale,
        3 => f32::from(two(at) as i16) * scale,
        _ => p.get(at..at + 4).map_or(0.0, |s| f32::from_be_bytes([s[0], s[1], s[2], s[3]])),
    }
}

/// A colour entry as R << 24 | G << 16 | B << 8 | A, each format widened by repeating its high bits.
fn color(p: &[u8], kind: u32) -> u32 {
    let (a, b, c, d) = (p[0] as u32, p[1] as u32, p[2] as u32, p[3] as u32);
    match kind {
        0 => {
            let v = (a << 8) | b;
            let (r, g, bl) = ((v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F);
            ((r << 3 | r >> 2) << 24) | ((g << 2 | g >> 4) << 16) | ((bl << 3 | bl >> 2) << 8) | 0xFF
        }
        1 | 2 => (a << 24) | (b << 16) | (c << 8) | 0xFF,
        3 => {
            let v = (a << 8) | b;
            let (r, g, bl, al) = ((v >> 12) & 0xF, (v >> 8) & 0xF, (v >> 4) & 0xF, v & 0xF);
            ((r | r << 4) << 24) | ((g | g << 4) << 16) | ((bl | bl << 4) << 8) | (al | al << 4)
        }
        4 => {
            let v = (a << 16) | (b << 8) | c;
            let (r, g, bl, al) = ((v >> 18) & 0x3F, (v >> 12) & 0x3F, (v >> 6) & 0x3F, v & 0x3F);
            ((r << 2 | r >> 4) << 24) | ((g << 2 | g >> 4) << 16) | ((bl << 2 | bl >> 4) << 8) | (al << 2 | al >> 4)
        }
        _ => (a << 24) | (b << 16) | (c << 8) | d,
    }
}

fn fraction_scale(frac: u32) -> f32 {
    1.0 / 2f32.powi(frac as i32)
}

fn type_bytes(kind: u32) -> usize {
    match kind {
        0 | 1 => 1,
        2 | 3 => 2,
        _ => 4,
    }
}

/// VTX1: J3D's strides for each array, the file's dequantisation.
fn read_arrays(b: &[u8]) -> Result<(ArraysIr, Vec<(u32, Format)>)> {
    let size = (be32(b, 4)? as usize).min(b.len());
    let mut formats = Vec::new();
    let list = be32(b, 8)? as usize;
    if list != 0 {
        let mut e = list;
        loop {
            let attr = be32(b, e)?;
            if attr == 0xFF {
                break;
            }
            formats.push((attr, Format { cnt: be32(b, e + 4)?, kind: be32(b, e + 8)?, frac: u32::from(*b.get(e + 12).context("format list")?) }));
            e += 16;
        }
    }
    let mut starts: Vec<usize> = (0xC..0x40).step_by(4).map(|o| be32(b, o).unwrap_or(0) as usize).filter(|&v| v != 0).collect();
    starts.sort_unstable();
    starts.dedup();
    let bound = |s: usize| starts.iter().copied().find(|&x| x > s).unwrap_or(size);
    let normal = format_of(&formats, 10);
    let mut out = ArraysIr { normal_floats: if normal.cnt != 0 { 9 } else { 3 }, coords: vec![Vec::new(); J3D_COORDINATES], ..Default::default() };
    for array in 0..5 + J3D_COORDINATES {
        let start = be32(b, 0xC + array * 4)? as usize;
        if start == 0 {
            continue;
        }
        let region = b.get(start..bound(start)).context("vertex array past its block")?;
        match array {
            0 => {
                let f = format_of(&formats, 9);
                let stride = if f.kind == 4 { 12 } else { 6 };
                let scale = fraction_scale(f.frac);
                let (bytes, n) = (type_bytes(f.kind), if f.cnt == 1 { 3 } else { 2 });
                for i in 0..region.len() / stride {
                    let mut v = [0f32; 3];
                    for (c, slot) in v.iter_mut().enumerate().take(n) {
                        *slot = component(region, i * stride + c * bytes, f.kind, scale);
                    }
                    out.positions.push(v);
                }
            }
            1 | 2 => {
                let f = normal;
                let nbt = array == 2;
                let stride = (if f.kind == 4 { 12 } else { 6 }) * if nbt { 3 } else { 1 };
                let shift = match f.kind {
                    0 => 7,
                    1 => 6,
                    2 => 15,
                    _ => 14,
                };
                let scale = if f.kind == 4 { 1.0 } else { 1.0 / (1u32 << shift) as f32 };
                let bytes = type_bytes(f.kind);
                let vectors = if nbt || f.cnt != 0 { 3 } else { 1 };
                let whole = b.get(start..size).context("vertex array past its block")?;
                let count = region.len() / stride;
                for i in 0..count {
                    let mut nine = [0f32; 9];
                    for v in 0..vectors {
                        for c in 0..3 {
                            nine[v * 3 + c] = component(whole, i * stride + (v * 3 + c) * bytes, f.kind, scale);
                        }
                    }
                    if nbt || vectors == 3 {
                        out.nbt.push(nine);
                    }
                    if !nbt {
                        out.normals.push([nine[0], nine[1], nine[2]]);
                    }
                }
            }
            3 | 4 => {
                let f = format_of(&formats, 11 + array as u32 - 3);
                for e in region.chunks_exact(4) {
                    out.colors[array - 3].push(color(e, f.kind));
                }
            }
            _ => {
                let f = format_of(&formats, 13 + array as u32 - 5);
                let stride = if f.kind == 4 { 8 } else { 4 };
                let scale = fraction_scale(f.frac);
                let (bytes, n) = (type_bytes(f.kind), if f.cnt == 1 { 2 } else { 1 });
                let count = region.len() / stride;
                for i in 0..count {
                    let mut v = [0f32; 2];
                    for (c, slot) in v.iter_mut().enumerate().take(n) {
                        *slot = component(region, i * stride + c * bytes, f.kind, scale);
                    }
                    out.coords[array - 5].push(v);
                }
            }
        }
    }
    Ok((out, formats))
}

struct Layout {
    stride: usize,
    offset: [i32; 21],
    bytes: [usize; 21],
    nbt: bool,
    nbt_three: bool,
    /// One past the last coordinate and texture matrix index the format carries.
    coordinates: usize,
    texture_matrices: usize,
}

const ATTR_PNMTX: usize = 0;
const ATTR_POS: usize = 9;
const ATTR_NRM: usize = 10;
const ATTR_CLR0: usize = 11;
const ATTR_TEX0: usize = 13;

fn layout_of(desc: &[(u32, u32)], formats: &[(u32, Format)]) -> Result<Layout> {
    let mut kind = [0u32; 21];
    let mut nbt = false;
    for &(attr, ty) in desc {
        let a = if attr == 25 { 10 } else { attr };
        if a > 20 || ty == 0 {
            continue;
        }
        if a >= 9 && ty == 1 {
            bail!("a shape with values inside its display list");
        }
        kind[a as usize] = ty;
        nbt |= attr == 25;
    }
    let normal = format_of(formats, 10);
    let nbt_three = normal.cnt == 2;
    let wide_nbt = nbt || normal.cnt != 0;
    let mut l = Layout { stride: 0, offset: [-1; 21], bytes: [0; 21], nbt: wide_nbt, nbt_three, coordinates: 0, texture_matrices: 0 };
    let mut at = 0;
    for a in 0..21 {
        if kind[a] == 0 {
            continue;
        }
        let mut n = if a < 9 || kind[a] == 2 { 1 } else { 2 };
        if a == ATTR_NRM && wide_nbt && nbt_three {
            n *= 3;
        }
        l.offset[a] = at as i32;
        l.bytes[a] = n;
        at += n;
    }
    l.stride = at;
    l.coordinates = (0..J3D_COORDINATES).rposition(|k| l.offset[ATTR_TEX0 + k] >= 0).map_or(0, |k| k + 1);
    l.texture_matrices = (0..J3D_COORDINATES).rposition(|k| l.offset[1 + k] >= 0).map_or(0, |k| k + 1);
    if l.offset[ATTR_POS] < 0 {
        bail!("a shape without positions");
    }
    Ok(l)
}

fn index_at(v: &[u8], l: &Layout, attr: usize, which: usize) -> u32 {
    if l.offset[attr] < 0 {
        return 0;
    }
    let width = if attr == ATTR_NRM && l.nbt_three && l.nbt { l.bytes[attr] / 3 } else { l.bytes[attr] };
    let p = l.offset[attr] as usize + which * width;
    if width == 1 {
        u32::from(v[p])
    } else {
        u32::from(u16::from_be_bytes([v[p], v[p + 1]]))
    }
}

fn corner_of(v: &[u8], l: &Layout) -> Corner {
    let three = l.nbt && l.nbt_three;
    let mut c = Corner {
        position: index_at(v, l, ATTR_POS, 0),
        normal: 0,
        tangent: 0,
        binormal: 0,
        color: [index_at(v, l, ATTR_CLR0, 0), index_at(v, l, ATTR_CLR0 + 1, 0)],
        coord: (0..l.coordinates).map(|k| index_at(v, l, ATTR_TEX0 + k, 0)).collect(),
        matrix: 0,
        texture_matrix: (0..l.texture_matrices)
            .map(|k| {
                let at = l.offset[1 + k];
                if at < 0 { NO_TEXTURE_MATRIX } else if v[at as usize] >= 30 { (v[at as usize] - 30) / 3 } else { 0 }
            })
            .collect(),
    };
    if l.offset[ATTR_NRM] >= 0 {
        c.normal = index_at(v, l, ATTR_NRM, 0);
        c.tangent = if three { index_at(v, l, ATTR_NRM, 1) } else { c.normal };
        c.binormal = if three { index_at(v, l, ATTR_NRM, 2) } else { c.normal };
    }
    if l.offset[ATTR_PNMTX] >= 0 {
        c.matrix = u32::from(v[l.offset[ATTR_PNMTX] as usize] / 3);
    }
    c
}

/// A group's display list: quads, strips and fans as triangles (a strip's odd triangles turned to keep their winding),
/// each distinct corner once in the order first met.
fn read_group(list: &[u8], l: &Layout) -> Result<GroupIr> {
    let mut corners: Vec<Corner> = Vec::new();
    let mut seen: HashMap<Corner, u32> = HashMap::new();
    let mut indices: Vec<u32> = Vec::new();
    let mut at = 0;
    while at < list.len() {
        let op = list[at];
        at += 1;
        if op == 0 {
            continue;
        }
        let primitive = op & 0xF8;
        if op & 0x80 == 0 || op & 7 != 0 || primitive == 0xA8 || primitive == 0xB0 || primitive == 0xB8 {
            bail!("a display list holds more than triangles of one vertex format (command {op:#04x})");
        }
        let count = be16(list, at)? as usize;
        at += 2;
        let vertices = list.get(at..at + count * l.stride).context("vertices past the display list")?;
        at += count * l.stride;
        let triangles = match primitive {
            0x90 => count / 3,
            0x80 | 0x88 => count / 4 * 2,
            _ if count >= 3 => count - 2,
            _ => 0,
        };
        for t in 0..triangles {
            let v: [usize; 3] = match primitive {
                0x90 => [3 * t, 3 * t + 1, 3 * t + 2],
                0x80 | 0x88 => {
                    let q = 4 * (t / 2);
                    [q, q + 1 + (t & 1), q + 2 + (t & 1)]
                }
                0x98 => [if t & 1 != 0 { t + 1 } else { t }, if t & 1 != 0 { t } else { t + 1 }, t + 2],
                _ => [0, t + 1, t + 2],
            };
            for k in v {
                let c = corner_of(&vertices[k * l.stride..(k + 1) * l.stride], l);
                let index = match seen.get(&c) {
                    Some(&index) => index,
                    None => {
                        let index = corners.len() as u32;
                        corners.push(c.clone());
                        seen.insert(c, index);
                        index
                    }
                };
                indices.push(index);
            }
        }
    }
    let mut attributes = 0u64;
    if l.offset[ATTR_NRM] >= 0 {
        attributes |= ATTR_NORMAL | if l.nbt { ATTR_NBT } else { 0 } | if l.nbt && l.nbt_three { ATTR_NBT_THREE } else { 0 };
    }
    for k in 0..2 {
        if l.offset[ATTR_CLR0 + k] >= 0 {
            attributes |= ATTR_COLOR0 << k;
        }
    }
    if l.offset[ATTR_PNMTX] >= 0 {
        attributes |= ATTR_MATRIX;
    }
    let coordinates = (0..l.coordinates).map(|t| l.offset[ATTR_TEX0 + t] >= 0).collect();
    let mut places = 0u32;
    if l.offset[ATTR_PNMTX] >= 0 {
        for c in &corners {
            places |= 1u32.checked_shl(u32::from(c.matrix)).unwrap_or(0);
        }
    }
    Ok(GroupIr { attributes, coordinates, corners, indices, places, use_table: Vec::new() })
}

fn read_shapes(b: &[u8], formats: &[(u32, Format)]) -> Result<Vec<ShapeIr>> {
    let count = be16(b, 8)? as usize;
    let (init, remap, descs, table, lists, mtx_init, draw) = (
        be32(b, 0xC)? as usize,
        be32(b, 0x10)? as usize,
        be32(b, 0x18)? as usize,
        be32(b, 0x1C)? as usize,
        be32(b, 0x20)? as usize,
        be32(b, 0x24)? as usize,
        be32(b, 0x28)? as usize,
    );
    let mut out = Vec::with_capacity(count);
    for no in 0..count {
        let i = init + be16(b, remap + no * 2)? as usize * 0x28;
        let matrix_type = b[i];
        let group_count = be16(b, i + 2)? as usize;
        let desc_at = descs + be16(b, i + 4)? as usize;
        let mtx_first = be16(b, i + 6)? as usize;
        let draw_first = be16(b, i + 8)? as usize;
        let mut desc = Vec::new();
        let mut e = desc_at;
        loop {
            let attr = be32(b, e)?;
            if attr == 0xFF {
                break;
            }
            desc.push((attr, be32(b, e + 4)?));
            e += 8;
        }
        let layout = layout_of(&desc, formats)?;
        let mut groups = Vec::with_capacity(group_count);
        for g in 0..group_count {
            let d = draw + (draw_first + g) * 8;
            let (size, offset) = (be32(b, d)? as usize, be32(b, d + 4)? as usize);
            let list = b.get(lists + offset..lists + offset + size).context("a display list past its block")?;
            let mut group = read_group(list, &layout)?;
            let m = mtx_init + (mtx_first + g) * 8;
            let use_index = be16(b, m)?;
            if matrix_type == 3 {
                let (n, first) = (be16(b, m + 2)? as usize, be32(b, m + 4)? as usize);
                for k in 0..n {
                    group.use_table.push(source_index(be16(b, table + (first + k) * 2)?));
                }
            } else {
                group.use_table.push(source_index(use_index));
            }
            groups.push(group);
        }
        out.push(ShapeIr {
            matrix_type,
            radius: bef(b, i + 0x0C)?,
            min: vec3(b, i + 0x10)?,
            max: vec3(b, i + 0x1C)?,
            groups,
            material: NONE,
        });
    }
    Ok(out)
}

fn read_joints(b: &[u8]) -> Result<Vec<JointIr>> {
    let count = be16(b, 8)? as usize;
    let (init, remap, name_table) = (be32(b, 0xC)? as usize, be32(b, 0x10)? as usize, be32(b, 0x14)? as usize);
    let label = names(b, name_table)?;
    let mut out = Vec::with_capacity(count);
    for j in 0..count {
        let at = init + be16(b, remap + j * 2)? as usize * 0x40;
        out.push(JointIr {
            name: label.get(j).cloned().unwrap_or_default(),
            kind: be16(b, at)? as u8,
            scale_compensate: b[at + 2],
            billboard: 0,
            scale: vec3(b, at + 4)?,
            rotation: [be16(b, at + 0x10)? as i16 as f32 * ANGLE_UNIT, be16(b, at + 0x12)? as i16 as f32 * ANGLE_UNIT, be16(b, at + 0x14)? as i16 as f32 * ANGLE_UNIT],
            translation: vec3(b, at + 0x18)?,
            radius: bef(b, at + 0x24)?,
            min: vec3(b, at + 0x28)?,
            max: vec3(b, at + 0x34)?,
            parent: NONE,
            child: NONE,
            younger: NONE,
            meshes: Vec::new(),
        });
    }
    Ok(out)
}

/// MAT3 tables. Offsets are relative to the block; source tables may overlap, their offsets carry no lengths.
struct Mat3<'a> {
    b: &'a [u8],
    offsets: Vec<usize>,
    count: usize,
}

impl<'a> Mat3<'a> {
    fn parse(b: &'a [u8]) -> Result<Self> {
        if b.len() < 0x84 {
            bail!("a material block shorter than its header");
        }
        let offsets = b[0xC..0x84].chunks_exact(4).map(|c| u32::from_be_bytes(c.try_into().unwrap()) as usize).collect();
        Ok(Self { b, offsets, count: be16(b, 8)? as usize })
    }

    fn table(&self, field: usize) -> usize {
        self.offsets[(field - 0xC) / 4]
    }

    fn record(&self, field: usize, index: usize, stride: usize) -> Result<&'a [u8]> {
        let start = self.table(field);
        if start == 0 {
            bail!("a material names an absent table at {field:#x}");
        }
        let at = start + index * stride;
        self.b.get(at..at + stride).with_context(|| format!("table {field:#x} index {index} past its block"))
    }

    fn id(&self, material: usize) -> Result<u32> {
        be16(self.b, self.table(0x10) + material * 2)
    }

    fn init(&self, material: usize) -> Result<&'a [u8]> {
        self.record(0xC, self.id(material)? as usize, 0x14C)
    }

    fn pick(&self, material: usize, selector: usize, table: usize, stride: usize) -> Result<Option<&'a [u8]>> {
        let index = be16(self.init(material)?, selector)?;
        if index == 0xFFFF {
            return Ok(None);
        }
        Ok(Some(self.record(table, index as usize, stride)?))
    }

    fn byte(&self, material: usize, selector: usize, table: usize) -> Result<Option<u8>> {
        let index = self.init(material)?[selector];
        if index == 0xFF {
            return Ok(None);
        }
        Ok(Some(self.record(table, index as usize, 1)?[0]))
    }

    fn indirect(&self, material: usize) -> Result<Option<&'a [u8]>> {
        if self.offsets[3] != 0 && self.offsets[3].saturating_sub(self.offsets[2]) > 4 {
            Ok(Some(self.record(0x18, material, 0x138)?))
        } else {
            Ok(None)
        }
    }
}

const NATIVE_ALWAYS: u8 = 7;
const BLEND_REPLACE: u8 = 0;
const BLEND_ADD: u8 = 1;
const BLEND_SUBTRACT: u8 = 2;
const FACTOR_SRC_COLOR: u8 = 2;
const FACTOR_INV_SRC_COLOR: u8 = 3;
const FACTOR_DST_COLOR: u8 = 8;
const FACTOR_INV_DST_COLOR: u8 = 9;
const CULL_NONE: u8 = 0;
const CULL_BACK: u8 = 1;
const CULL_FRONT: u8 = 2;

fn factor(stored: u8, source: bool) -> u8 {
    match (stored, source) {
        (2, true) => FACTOR_DST_COLOR,
        (3, true) => FACTOR_INV_DST_COLOR,
        (2, false) => FACTOR_SRC_COLOR,
        (3, false) => FACTOR_INV_SRC_COLOR,
        _ => stored,
    }
}

/// The loader's three material tiers: a BDL file's materials carry every table, a BMD file's take no ambient.
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Source {
    Bdl,
    Bmd,
    Table,
}

impl Source {
    /// Which tier a model file's first eight bytes name.
    pub fn of(bytes: &[u8]) -> Self {
        if bytes.starts_with(b"J3D2bdl4") {
            Source::Bdl
        } else if bytes.starts_with(b"J3D2bmd3") {
            Source::Bmd
        } else {
            Source::Table
        }
    }
}

fn read_material(m: &Mat3, i: usize, names: &[String], source: Source) -> Result<MaterialIr> {
    let init = m.init(i)?;
    // The game's loader asks for every table of a BDL file's materials, and for no ambient colours of the others'.
    let has_ambient = source == Source::Bdl;
    let mode = init[0];
    let mut out = MaterialIr {
        on_billboard: false,
        name: names.get(i).cloned().unwrap_or_default(),
        same_as: m.id(i)?,
        mesh: NONE,
        joint: NONE,
        mode,
        has_ambient,
        has_indirect: true,
        has_fog: true,
        ..Default::default()
    };
    let tex_num = 8;
    out.has_stage_colors = true;
    out.channel_count = u32::from(m.byte(i, 2, 0x24)?.unwrap_or(0));
    out.cull = if init[1] == 0xFF {
        CULL_NONE
    } else {
        match u32::from_be_bytes(m.record(0x1C, init[1] as usize, 4)?.try_into().unwrap()) {
            2 => CULL_BACK,
            1 => CULL_FRONT,
            _ => CULL_NONE,
        }
    };
    let coord_count = m.byte(i, 3, 0x34)?.unwrap_or(0) as usize;
    match m.pick(i, 0x14A, 0x80, 16)? {
        Some(n) => {
            out.has_scale = n[0];
            out.normal_scale = vec3(n, 4)?;
        }
        None => out.normal_scale = [1.0; 3],
    }
    out.fog = match m.pick(i, 0x144, 0x68, 0x2C)? {
        Some(f) => {
            FogIr {
                kind: f[0],
                start: bef(f, 4)?,
                end: bef(f, 8)?,
                near: bef(f, 12)?,
                far: bef(f, 16)?,
                color: f[20..24].try_into().unwrap(),
            }
        }
        None => no_fog(),
    };
    let alpha = m.pick(i, 0x146, 0x6C, 8)?;
    let (c0, r0, op, c1, r1) = match alpha {
        Some(a) => (a[0], a[1], a[2], a[3], a[4]),
        None => (NATIVE_ALWAYS, 0, 1, NATIVE_ALWAYS, 0),
    };
    out.alpha_comp0 = c0;
    out.alpha_ref0 = r0;
    out.alpha_op = op;
    out.alpha_comp1 = c1;
    out.alpha_ref1 = r1;
    let blend = m.pick(i, 0x148, 0x70, 4)?.unwrap_or(&[1, 4, 5, 0]);
    out.blend_type = match blend[0] {
        1 => BLEND_ADD,
        3 => BLEND_SUBTRACT,
        _ => BLEND_REPLACE,
    };
    out.blend_src = factor(blend[1], true);
    out.blend_dst = factor(blend[2], false);
    let depth = if init[6] != 0xFF { m.record(0x74, init[6] as usize, 4)? } else { &[1, 3, 1, 0] };
    out.depth_test = depth[0];
    out.depth_func = depth[1];
    out.depth_write = depth[2];
    out.early_depth = m.byte(i, 5, 0x78)?.unwrap_or(0);
    out.textures = vec![NONE; tex_num];
    for k in 0..tex_num {
        let index = be16(init, 0x84 + k * 2)?;
        if index != 0xFFFF {
            out.textures[k] = source_index(be16(m.b, m.table(0x48) + index as usize * 2)?);
        }
    }
    out.registers = [[0xFF; 4]; 4];
    out.constants = [[0xFF; 4]; 4];
    if out.has_stage_colors {
        for k in 0..4 {
            out.constants[k] = match m.pick(i, 0x94 + k * 2, 0x54, 4)? {
                Some(c) => c.try_into().unwrap(),
                None => [0xFF; 4],
            };
            out.registers[k] = match m.pick(i, 0xDC + k * 2, 0x50, 8)? {
                Some(c) => [be16(c, 0)? as i16, be16(c, 2)? as i16, be16(c, 4)? as i16, be16(c, 6)? as i16],
                None => [0; 4],
            };
        }
    }
    out.ambient_color = [[0x32; 4]; 2];
    if has_ambient {
        for k in 0..2 {
            out.ambient_color[k] = match m.pick(i, 0x14 + k * 2, 0x2C, 4)? {
                Some(c) => c.try_into().unwrap(),
                None => [0x32; 4],
            };
        }
    }
    for k in 0..2 {
        out.material_color[k] = match m.pick(i, 8 + k * 2, 0x20, 4)? {
            Some(c) => c.try_into().unwrap(),
            None => [0xFF; 4],
        };
    }
    for k in 0..4 {
        let c = m.pick(i, 0xC + k * 2, 0x28, 8)?;
        let (enable, material, mask, diffuse, attn, ambient) = match c {
            Some(c) => (c[0], c[1], c[2], c[3], c[4], if c[5] == 0xFF { 0 } else { c[5] }),
            None => (0, 0, 0, 2, 2, 0),
        };
        out.channels[k] = ChannelIr {
            enable: enable & 1,
            material_source: material & 1,
            ambient_source: ambient & 1,
            light_mask: mask,
            diffuse: if attn == 0 { 0 } else { diffuse & 3 },
            attenuation: attn,
        };
    }
    for k in 0..coord_count {
        let c = m.pick(i, 0x28 + k * 2, 0x38, 4)?.unwrap_or(&[1, 4, 60, 0]);
        out.coords.push(CoordIr { kind: c[0], source: if c[1] >= 19 { c[1] + 13 } else { c[1] }, matrix: if c[2] == 60 { NONE } else { u32::from(c[2].saturating_sub(30) / 3) } });
    }
    for k in 0..8 {
        if let Some(t) = m.pick(i, 0x48 + k * 2, 0x40, 100)? {
            let mut effect = [[0f32; 4]; 4];
            for (row, values) in effect.iter_mut().enumerate() {
                for (col, v) in values.iter_mut().enumerate() {
                    *v = bef(t, 0x24 + (row * 4 + col) * 4)?;
                }
            }
            out.matrices.push(CoordMatrixIr {
                slot: k as u8,
                projection: t[0],
                info: t[1],
                center: vec3(t, 4)?,
                scale: [bef(t, 0x10)?, bef(t, 0x14)?],
                rotation: be16(t, 0x18)? as i16 as f32 * ANGLE_UNIT,
                translation: [bef(t, 0x1C)?, bef(t, 0x20)?],
                effect,
            });
        }
    }
    let ind = m.indirect(i)?;
    let enabled = ind.is_some_and(|b| b[0] != 0);
    if let (Some(data), true) = (ind, enabled) {
        for k in 0..usize::from(data[1]) {
            let at = 0x14 + k * 0x1C;
            let mut w = WarpIr::default();
            for row in 0..2 {
                for col in 0..3 {
                    w.rows[row][col] = bef(data, at + (row * 3 + col) * 4)?;
                }
            }
            let factor = 2.0f32.powi(data[at + 0x18] as i8 as i32);
            for row in &mut w.rows { for value in row { *value *= factor; } }
            out.warps.push(w);
        }
    }
    read_stages(m, i, &mut out, ind, enabled)?;
    Ok(out)
}

fn read_stages(m: &Mat3, i: usize, out: &mut MaterialIr, ind: Option<&[u8]>, enabled: bool) -> Result<()> {
    let init = m.init(i)?;
    let count = usize::from(m.byte(i, 4, 0x58)?.unwrap_or(0xFF));
    if count > 16 {
        bail!("a material of {count} stages");
    }
    out.pixel_stage_count = count as u32;
    const DEFAULT_STAGE: [u8; 20] = [4, 10, 15, 15, 0, 0, 0, 0, 1, 0, 5, 7, 7, 0, 0, 0, 0, 1, 0, 0];
    for s in 0..count {
        let order = m.pick(i, 0xBC + 2 * s, 0x4C, 4)?.unwrap_or(&[255, 255, 255, 0]);
        let stage = m.pick(i, 0xE4 + 2 * s, 0x5C, 20)?.unwrap_or(&DEFAULT_STAGE);
        let mut st = StageIr {
            coord: order[0],
            map: order[1],
            channel: order[2],
            color_in: stage[1..5].try_into().unwrap(),
            color_op: stage[5],
            color_bias: stage[6],
            color_scale: stage[7],
            color_clamp: stage[8],
            color_out: stage[9],
            alpha_in: stage[10..14].try_into().unwrap(),
            alpha_op: stage[14],
            alpha_bias: stage[15],
            alpha_scale: stage[16],
            alpha_clamp: stage[17],
            alpha_out: stage[18],
            ..Default::default()
        };
        if let Some(swap) = m.pick(i, 0x104 + 2 * s, 0x60, 4)? {
            st.raster_swap = swap[0];
            st.texture_swap = swap[1];
        }
        st.color_constant = if init[0x9C + s] != 255 { init[0x9C + s] } else { 12 };
        st.alpha_constant = if init[0xAC + s] != 255 { init[0xAC + s] } else { 28 };
        if let (Some(data), true) = (ind, enabled) {
            st.warp.copy_from_slice(&data[0x78 + 12 * s..0x81 + 12 * s]);
        }
        out.stages.push(st);
    }
    // Decode the source pair/indirect order registers into final scale sources.
    let mut scale_sources = vec![255u8; J3D_COORDINATES];
    for stage in &out.stages {
        scale_sources[usize::from(stage.coord & 7)] = stage.map & 7;
    }
    if count % 2 == 1 && count < 16 && count != 1 {
        let order = m.pick(i, 0xBC + 2 * count, 0x4C, 4)?.unwrap_or(&[255, 255, 255, 0]);
        scale_sources[usize::from(order[0] & 7)] = order[1] & 7;
    }
    if let (Some(data), true) = (ind, enabled) {
        for k in 0..4 {
            let order = if k < usize::from(data[1]) { &data[4+4*k..6+4*k] } else { &[255,255] };
            scale_sources[usize::from(order[0] & 7)] = order[1] & 7;
        }
    }
    out.coordinate_maps = scale_sources;
    out.swaps = [[0, 1, 2, 3]; 4];
    for k in 0..4 {
        if let Some(swap) = m.pick(i, 0x124 + k * 2, 0x64, 4)? {
            out.swaps[k] = swap.try_into().unwrap();
        }
    }
    if let (Some(data), true) = (ind, enabled) {
        for k in 0..usize::from(data[1]).min(4) {
            out.warp_orders.push([data[4 + 4 * k], data[5 + 4 * k], data[0x68 + 4 * k], data[0x69 + 4 * k]]);
        }
    }
    Ok(())
}

fn read_textures(b: &[u8], ir: &mut ModelIr, stats: &mut Stats) -> Result<()> {
    let count = be16(b, 8)? as usize;
    let (headers, name_table) = (be32(b, 0xC)? as usize, be32(b, 0x10)? as usize);
    let label = names(b, name_table)?;
    let mut cooked: HashMap<Vec<u8>, usize> = HashMap::new();
    for i in 0..count {
        let h = headers + i * 0x20;
        let header = b.get(h..h + 0x20).context("a texture header past its block")?;
        // Two headers name one image when their image, palette and the fields that shape them agree.
        let mut key = header[..0x10].to_vec();
        key.extend_from_slice(&(h as i64 + be32(b, h + 0x0C)? as i32 as i64).to_le_bytes());
        key.extend_from_slice(&(h as i64 + be32(b, h + 0x1C)? as i32 as i64).to_le_bytes());
        key.push(header[0x18]);
        let record = match cooked.get(&key) {
            Some(&r) => r,
            None => {
                let bytes = formats::timg_record(b, h, stats)?;
                ir.records.push(bytes);
                ir.record_palettes.push(formats::timg_palette_hash(b, h)?);
                cooked.insert(key, ir.records.len() - 1);
                ir.records.len() - 1
            }
        };
        let indexed = super::texture::Format::from_code(header[0] as u32).is_some_and(super::texture::Format::indexed);
        ir.textures.push(TextureIr {
            name: label.get(i).cloned().unwrap_or_default(),
            format: if indexed { 6 } else { header[0] },
            width: be16(b, h + 2)?,
            height: be16(b, h + 4)?,
            wrap_s: header[6],
            wrap_t: header[7],
            mipmap: header[0x10],
            anisotropy: 1u8 << header[0x13].min(2),
            min_filter: header[0x14],
            mag_filter: header[0x15],
            min_lod: header[0x16] as i8 as f32 * 0.125,
            max_lod: header[0x17] as i8 as f32 * 0.125,
            levels: u32::from(header[0x18]),
            lod_bias: be16(b, h + 0x1A)? as i16 as f32 * 0.01,
            record,
        });
    }
    Ok(())
}

fn read_hierarchy(b: &[u8], ir: &mut ModelIr) -> Result<()> {
    let mut at = be32(b, 0x14)? as usize;
    // (joint under which the walk stands, latest joint at this level)
    let mut stack: Vec<(u32, u32)> = vec![(NONE, NONE)];
    let mut last_child: HashMap<u32, u32> = HashMap::new();
    let mut last_material: HashMap<u32, u32> = HashMap::new();
    loop {
        let kind = be16(b, at)?;
        let value = be16(b, at + 2)?;
        at += 4;
        match kind {
            0 => break,
            1 => {
                let current = stack.last().unwrap().1;
                stack.push((current, current));
            }
            2 => {
                stack.pop();
                if stack.is_empty() {
                    bail!("a hierarchy that ends more levels than it begins");
                }
            }
            0x10 => {
                let top = stack.last_mut().unwrap();
                top.1 = value;
                let parent = top.0;
                let j = ir.joints.get_mut(value as usize).context("a hierarchy names a joint past the table")?;
                j.parent = parent;
                if parent != NONE {
                    match last_child.insert(parent, value) {
                        Some(before) => ir.joints[before as usize].younger = value,
                        None => ir.joints[parent as usize].child = value,
                    }
                }
            }
            0x11 => {
                let joint = stack.last().unwrap().0;
                if joint == NONE {
                    bail!("a material outside any joint");
                }
                ir.joints[joint as usize].meshes.insert(0, value);
                let m = ir.materials.get_mut(value as usize).context("a hierarchy names a material past the table")?;
                m.joint = joint;
                last_material.insert(joint, value);
            }
            0x12 => {
                let joint = stack.last().unwrap().0;
                let material = *ir.joints.get(joint as usize).and_then(|j| j.meshes.first()).context("a shape before its material")?;
                ir.materials[material as usize].mesh = value;
                ir.shapes.get_mut(value as usize).context("a hierarchy names a shape past the table")?.material = material;
            }
            _ => bail!("a hierarchy entry of kind {kind:#x}"),
        }
    }
    Ok(())
}

fn no_fog() -> FogIr {
    FogIr { kind: 0, start: 0.0, end: 0.0, near: 0.1, far: 10000.0, color: [255, 255, 255, 0] }
}

/// A draw matrix's most important joint: its joint, or an envelope's heaviest.
pub fn important_joints(draw_matrices: &[(u8, u32)], full_weight_count: u32, envelopes: &[Vec<(u32, f32)>]) -> Vec<u32> {
    let mut important = vec![0; draw_matrices.len()];
    for i in 0..full_weight_count as usize {
        important[i] = draw_matrices[i].1;
    }
    for (e, mixes) in envelopes.iter().enumerate() {
        let mut best = (0u32, -0.1f32);
        for &(joint, weight) in mixes {
            if best.1 < weight {
                best = (joint, weight);
            }
        }
        if let Some(slot) = important.get_mut(e + full_weight_count as usize) {
            *slot = best.0;
        }
    }
    important
}

/// One stage: the lit colour, times an image when it has one. Blended, or cut off at an alpha, or neither.
pub fn plain_material(name: &str, color: [u8; 4], image: Option<u32>, blend: bool, cutoff: Option<u8>, two_sided: bool) -> MaterialIr {
    let channel = |enable, mask| ChannelIr { enable, material_source: 0, ambient_source: 0, light_mask: mask, diffuse: 2, attenuation: 2 };
    let textured = image.is_some();
    let textures = vec![image.unwrap_or(NONE)];
    let stage = StageIr {
        coord: if textured { 0 } else { 255 },
        map: if textured { 0 } else { 255 },
        channel: 4,
        color_in: if textured { [15, 8, 10, 15] } else { [10, 15, 15, 15] },
        color_clamp: 1,
        alpha_in: if textured { [7, 4, 5, 7] } else { [5, 7, 7, 7] },
        alpha_clamp: 1,
        color_constant: 12,
        alpha_constant: 28,
        ..Default::default()
    };
    MaterialIr {
        on_billboard: false,
        name: name.to_string(),
        mode: if blend { 4 } else { 1 },
        cull: if two_sided { CULL_NONE } else { CULL_BACK },
        has_stage_colors: true,
        has_indirect: true,
        has_fog: true,
        early_depth: 1,
        material_color: [color, [0xFF; 4]],
        ambient_color: [[0x32; 4]; 2],
        registers: [[0; 4]; 4],
        constants: [[0xFF; 4]; 4],
        channel_count: 1,
        channels: [channel(1, 3), channel(0, 0), channel(0, 0), channel(0, 0)],
        coords: if textured { vec![CoordIr { kind: 1, source: 4, matrix: NONE }] } else { Vec::new() },
        textures,
        fog: no_fog(),
        alpha_comp0: if cutoff.is_some() { 6 } else { NATIVE_ALWAYS },
        alpha_ref0: cutoff.unwrap_or(0),
        alpha_comp1: NATIVE_ALWAYS,
        blend_type: if blend { BLEND_ADD } else { BLEND_REPLACE },
        blend_src: factor(if blend { 4 } else { 1 }, true),
        blend_dst: factor(if blend { 5 } else { 0 }, false),
        depth_test: 1,
        depth_func: 3,
        depth_write: u8::from(!blend),
        normal_scale: [1.0; 3],
        stages: vec![stage],
        swaps: [[0, 1, 2, 3]; 4],
        pixel_stage_count: 1,
        coordinate_maps: vec![if textured { 0 } else { 255 }],
        ..Default::default()
    }
}

/// An image a model's material samples: repeats, linear, its record in `ModelIr::records`.
pub fn plain_texture(name: String, width: u32, height: u32, record: usize) -> TextureIr {
    TextureIr {
        name,
        format: 6,
        width,
        height,
        wrap_s: 1,
        wrap_t: 1,
        mipmap: 0,
        anisotropy: 1,
        min_filter: 1,
        mag_filter: 1,
        min_lod: 0.0,
        max_lod: 0.0,
        levels: 1,
        lod_bias: 0.0,
        record,
    }
}

/// The J3D input: a BDL, a BMD or a BMT file's bytes as the disc holds them.
pub fn read_file(bytes: &[u8], source: Source, stats: &mut Stats) -> Result<ModelIr> {
    let parts = blocks(bytes)?;
    let mut ir = ModelIr { materials_only: source == Source::Table, ..Default::default() };
    if let Some(b) = parts.get(b"TEX1") {
        read_textures(b, &mut ir, stats)?;
    }
    if let Some(b) = parts.get(b"MAT3") {
        let m = Mat3::parse(b)?;
        let label = names(b, m.table(0x14))?;
        for i in 0..m.count {
            ir.materials.push(read_material(&m, i, &label, source).with_context(|| format!("material {i}"))?);
        }
    }
    // c56: a normal-indexed map that holds a colour ramp, not a reflection, by its content.
    super::hit_record::mark_ramps(&mut ir);
    // c58: and one that is a painted highlight sprite.
    super::hit_record::mark_highlights(&mut ir);
    if source == Source::Table {
        // c63: a table's fades too (no vertices: their alpha counts 1); a model's are marked at the end, after VTX1.
        super::hit_record::mark_fades(&mut ir);
        return Ok(ir);
    }
    let inf = parts.get(b"INF1").context("a model without its information block")?;
    ir.source_flags = u32::from(be16(inf, 8)?);
    ir.scale_mode = (ir.source_flags & 0xF) as u8;
    ir.source_vertex_count = be32(inf, 0x10)?;
    let (arrays, formats) = read_arrays(parts.get(b"VTX1").context("a model without vertices")?)?;
    ir.arrays = arrays;
    let evp = parts.get(b"EVP1").context("a model without envelopes")?;
    let drw = parts.get(b"DRW1").context("a model without draw matrices")?;
    let jnt = parts.get(b"JNT1").context("a model without joints")?;
    let shp = parts.get(b"SHP1").context("a model without shapes")?;
    ir.joints = read_joints(jnt)?;
    let envelope_count = be16(evp, 8)? as usize;
    let (mix_num, mix_index, mix_weight, inverse) = (be32(evp, 0xC)? as usize, be32(evp, 0x10)? as usize, be32(evp, 0x14)? as usize, be32(evp, 0x18)? as usize);
    let mut k = 0usize;
    for e in 0..envelope_count {
        let n = *evp.get(mix_num + e).context("envelope table")? as usize;
        let mut mixes = Vec::with_capacity(n);
        for _ in 0..n {
            mixes.push((be16(evp, mix_index + k * 2)?, bef(evp, mix_weight + k * 4)?));
            k += 1;
        }
        ir.envelopes.push(mixes);
    }
    if inverse != 0 {
        for j in 0..ir.joints.len() {
            let mut row = [0f32; 12];
            for (c, v) in row.iter_mut().enumerate() {
                *v = bef(evp, inverse + j * 48 + c * 4)?;
            }
            ir.inverse_binds.push(row);
        }
    }
    let draw_total = be16(drw, 8)? as usize;
    let (flags_at, index_at_) = (be32(drw, 0xC)? as usize, be32(drw, 0x10)? as usize);
    let entries = draw_total.checked_sub(envelope_count).context("fewer draw matrices than envelopes")?;
    for i in 0..entries {
        ir.draw_matrices.push((*drw.get(flags_at + i).context("draw matrix table")?, be16(drw, index_at_ + i * 2)?));
    }
    ir.full_weight_count = ir.draw_matrices.iter().position(|d| d.0 == 1).unwrap_or(entries) as u32;
    ir.important = important_joints(&ir.draw_matrices, ir.full_weight_count, &ir.envelopes);
    ir.shapes = read_shapes(shp, &formats)?;
    read_hierarchy(inf, &mut ir)?;
    // Billboards from each joint's first mesh, as the walk draws them.
    for j in 0..ir.joints.len() {
        if let Some(&material) = ir.joints[j].meshes.first() {
            let shape = ir.materials[material as usize].mesh;
            let kind = ir.shapes.get(shape as usize).context("a material without its shape")?.matrix_type;
            match kind {
                1 | 2 => {
                    ir.joints[j].billboard = kind;
                    ir.has_billboard = true;
                    for &on in &ir.joints[j].meshes {
                        ir.materials[on as usize].on_billboard = true;
                    }
                }
                _ => ir.joints[j].billboard = 0,
            }
        }
    }
    // c61: a material blended only for a fade (its alpha high over its texels and vertices) is opaque to the classes.
    super::hit_record::mark_fades(&mut ir);
    Ok(ir)
}
