//! Every pipeline the game can ask for, written with the game's files: the game registers them at boot and the host
//! prepares them in the background. Each model's meshes under each form of their material, then what no model file
//! says: sites and per-object states (`data/site_pipelines.txt`, one record in hexadecimal per line, from
//! `tools/dev/site_pipelines.py`). Records are `NebulaPipelineDesc` (nebula_frame_list.h).

use std::collections::BTreeSet;
use std::sync::Mutex;

use super::model_write::{further_stride, MeshStreams, MESH_COLOR0, MESH_COLOR1, MESH_COORD0, MESH_NORMAL, MESH_TANGENTS};

pub const PATH: &str = "NebulaData/Pipelines.bin";
const MAX_INPUTS: usize = super::limits::MATERIAL_SLOTS + 8;
const RECORD: usize = 32 + MAX_INPUTS * 12;
const STREAM_ZERO: u8 = 15;
const INPUT_COORD0: u8 = 16;
const STATE_ALPHA_WRITE: u32 = 1 << 18;
const STATE_ALPHA_PASS: u32 = 1 << 19;
const STATE_STENCIL_OWN: u32 = 1 << 24;
const STATE_STENCIL_MASK: u32 = 0xF << 20;

static LIST: Mutex<BTreeSet<Vec<u8>>> = Mutex::new(BTreeSet::new());

pub fn reset() {
    LIST.lock().unwrap().clear();
}

fn input(code: u8, program_number: u8, m: &MeshStreams) -> [u8; 12] {
    let further = further_stride(m);
    let coords = (1..super::limits::MATERIAL_SLOTS).filter(|n| m.attributes & (MESH_COORD0 << n) != 0).count() as u32;
    let after_coords = coords * 8;
    let after_color = after_coords + if m.attributes & MESH_COLOR1 != 0 { 4 } else { 0 };
    let (stream, format, offset, stride): (u8, u8, u32, u32) = match code {
        0 if m.attributes & MESH_NORMAL != 0 => (1, 0, 0, 24),
        1 if m.attributes & MESH_COLOR0 != 0 => (1, 2, 12, 24),
        2 if m.attributes & MESH_COLOR1 != 0 => (3, 2, after_coords, further),
        3 if m.attributes & MESH_TANGENTS != 0 => (3, 0, after_color, further),
        4 if m.attributes & MESH_TANGENTS != 0 => (3, 0, after_color + 12, further),
        c if c >= INPUT_COORD0 && m.attributes & (MESH_COORD0 << (c - INPUT_COORD0)) != 0 => {
            let number = c - INPUT_COORD0;
            if number == 0 {
                (1, 1, 16, 24)
            } else {
                let before = (1..number).filter(|n| m.attributes & (MESH_COORD0 << n) != 0).count() as u32;
                (3, 1, before * 8, further)
            }
        }
        c => (STREAM_ZERO, if c >= INPUT_COORD0 { 1 } else if c == 1 || c == 2 { 2 } else { 0 }, 0, 0),
    };
    let semantic = if code >= INPUT_COORD0 { INPUT_COORD0 + program_number } else { code };
    let mut out = [0u8; 12];
    out[0] = semantic;
    out[1] = stream;
    out[2] = format;
    out[4..8].copy_from_slice(&offset.to_le_bytes());
    out[8..12].copy_from_slice(&stride.to_le_bytes());
    out
}

fn record(vertex: u64, pixel: u64, state: u32, inputs: &[u8], m: &MeshStreams) -> Vec<u8> {
    let mut out = vec![0u8; RECORD];
    out[0..8].copy_from_slice(&vertex.to_le_bytes());
    out[8..16].copy_from_slice(&pixel.to_le_bytes());
    out[16..20].copy_from_slice(&state.to_le_bytes());
    out[20..24].copy_from_slice(&(inputs.len() as u32).to_le_bytes());
    let mut program_number = 0u8;
    for (i, &code) in inputs.iter().enumerate() {
        out[28 + i * 12..40 + i * 12].copy_from_slice(&input(code, program_number, m));
        if code >= INPUT_COORD0 {
            program_number += 1;
        }
    }
    out
}

/// A mesh under one form. A material's state word holds what the pass decides as two bits: stencil is the pass's
/// (none, unless a pass asks: the site list holds those), alpha either.
pub fn add_form(vertex: u64, pixel: u64, state: u32, inputs: &[u8], m: &MeshStreams) {
    let plain = if state & STATE_STENCIL_OWN != 0 { state & !STATE_STENCIL_OWN } else { state & !STATE_STENCIL_MASK };
    let mut list = LIST.lock().unwrap();
    if plain & STATE_ALPHA_PASS != 0 {
        let off = plain & !STATE_ALPHA_PASS;
        list.insert(record(vertex, pixel, off, inputs, m));
        list.insert(record(vertex, pixel, off | STATE_ALPHA_WRITE, inputs, m));
    } else {
        list.insert(record(vertex, pixel, plain, inputs, m));
    }
}

const SITE: u64 = 1 << 63;
/// The particle shaders' NebulaShaderId values (nebula_game_api.h; tests/shader_ids.rs holds them to it).
pub const SHADER_PARTICLE: u64 = 30;
pub const SHADER_PARTICLE_INSTANCES: u64 = 42;
const PARTICLE_INSTANCE_BYTES: u32 = 160;
const STATE_COLOR_WRITE: u32 = 1 << 17;
const STATE_PIXEL_SHADING: u32 = 1 << 25;
const INPUT_NORMAL: u8 = 0;
const INPUT_COLOR0: u8 = 1;
const INPUT_COLOR1: u8 = 2;
const INPUT_TANGENT: u8 = 3;
const INPUT_WIDEN: u8 = 5;
const FORMAT_FLOAT3: u8 = 0;
const FORMAT_FLOAT2: u8 = 1;
const FORMAT_RGBA8: u8 = 2;
const FORMAT_UINT: u8 = 3;
// ParticleShapeData and ExTexShapeData (ParticleData.hpp) as particles.rs writes them.
const SHAPE_TYPE: usize = 48;
const SHAPE_BLEND_OP: usize = 56;
const SHAPE_ALPHA_COMPARE0: usize = 62;
const SHAPE_PROJECTED: usize = 69;
const EXTEX_USE_INDIRECT: usize = 27;
const EXTEX_USE_SECOND: usize = 28;

/// A site record: programs, state, the position stride and the inputs (semantic, format, offset) of one stream.
fn site_record(vertex: u64, pixel: u64, state: u32, stride: u32, inputs: &[(u8, u8, u32)]) -> Vec<u8> {
    let mut out = vec![0u8; RECORD];
    out[0..8].copy_from_slice(&vertex.to_le_bytes());
    out[8..16].copy_from_slice(&pixel.to_le_bytes());
    out[16..20].copy_from_slice(&state.to_le_bytes());
    out[20..24].copy_from_slice(&(inputs.len() as u32).to_le_bytes());
    out[24..28].copy_from_slice(&stride.to_le_bytes());
    for (i, &(semantic, format, offset)) in inputs.iter().enumerate() {
        let at = 28 + i * 12;
        out[at] = semantic;
        out[at + 2] = format;
        out[at + 4..at + 8].copy_from_slice(&offset.to_le_bytes());
        out[at + 8..at + 12].copy_from_slice(&stride.to_le_bytes());
    }
    out
}

/// nebula_alpha_test_always (nebula_model.h).
fn alpha_test_always(compare0: u8, compare1: u8, logic: u8) -> bool {
    match logic {
        0 => compare0 == 7 && compare1 == 7,
        1 => compare0 == 7 || compare1 == 7,
        2 => (compare0 == 0 && compare1 == 7) || (compare0 == 7 && compare1 == 0),
        3 => (compare0 == 0 && compare1 == 0) || (compare0 == 7 && compare1 == 7),
        _ => false,
    }
}

/// The state word sendParticleMaterial gives every draw of a resource: the base shape's blending, depth and alpha test
/// on SiteMaterial::init's plain state (no culling, colour written, the pass's alpha and stencil).
fn particle_state(shape: &[u8]) -> u32 {
    let op = shape[SHAPE_BLEND_OP];
    let (src, dst) = if op == 1 { (shape[SHAPE_BLEND_OP + 1], shape[SHAPE_BLEND_OP + 2]) } else { (0, 0) };
    let mut state = u32::from(op) | u32::from(src) << 2 | u32::from(dst) << 6 | STATE_COLOR_WRITE;
    if shape[SHAPE_BLEND_OP + 3] != 0 {
        state |= 1 << 10 | u32::from(shape[SHAPE_BLEND_OP + 4]) << 11;
    }
    if shape[SHAPE_BLEND_OP + 5] != 0 {
        state |= 1 << 14;
    }
    let a = SHAPE_ALPHA_COMPARE0;
    if alpha_test_always(shape[a], shape[a + 1], shape[a + 2]) {
        state |= STATE_PIXEL_SHADING;
    }
    state
}

/// One draw of a particle shape type: quads as instances (Frame::particlePipeline), points and lines as widened
/// corners (TypedDraw::widen), stripes as corners (ParticleDraw::flush's VertexCorner, or VertexCornerExtras).
fn particle_draw(list: &mut BTreeSet<Vec<u8>>, state: u32, shape_type: u8, extras: bool) {
    let pixel = SITE | SHADER_PARTICLE << 1;
    if !matches!(shape_type, 0 | 1 | 5 | 6) {
        list.insert(site_record(SITE | SHADER_PARTICLE_INSTANCES << 1, pixel, state, PARTICLE_INSTANCE_BYTES, &[]));
        return;
    }
    let mut inputs = vec![(INPUT_NORMAL, FORMAT_FLOAT3, 12), (INPUT_COLOR0, FORMAT_RGBA8, 24), (INPUT_COLOR1, FORMAT_RGBA8, 28),
                          (INPUT_COORD0, FORMAT_FLOAT2, 32)];
    let mut stride = 40;
    if extras {
        inputs.extend([(INPUT_COORD0 + 1, FORMAT_FLOAT2, 40), (INPUT_COORD0 + 2, FORMAT_FLOAT2, 48)]);
        stride = 56;
    }
    let wide = shape_type <= 1;
    if wide {
        inputs.extend([(INPUT_TANGENT, FORMAT_FLOAT3, stride), (INPUT_WIDEN, FORMAT_UINT, stride + 12)]);
        stride += 16;
    }
    list.insert(site_record(SITE | SHADER_PARTICLE << 1 | u64::from(wide), pixel, state, stride, &inputs));
}

/// Every pipeline a particle file's draws can take, with the pass's alpha written and not: each base shape's draw
/// (corners with the extra coordinates under an indirect or second texture or a projection, as setPTev's flags) and
/// the child's (setCTev: none).
pub fn add_particles(ir: &super::particles::ParticleIr) {
    let mut list = LIST.lock().unwrap();
    for r in &ir.resources {
        let shape = &r.shape.record;
        let state = particle_state(shape);
        let extras = shape[SHAPE_PROJECTED] != 0
            || r.extex.as_ref().is_some_and(|e| e[EXTEX_USE_INDIRECT] != 0 || e[EXTEX_USE_SECOND] != 0);
        for alpha in [0, STATE_ALPHA_WRITE] {
            particle_draw(&mut list, state | alpha, shape[SHAPE_TYPE], extras);
            if let Some(child) = &r.child {
                particle_draw(&mut list, state | alpha, child[0], false);
            }
        }
    }
}

/// The whole list, in a stable order, with the sites'.
pub fn bytes() -> Vec<u8> {
    let mut list = LIST.lock().unwrap();
    for line in include_str!("../../data/site_pipelines.txt").lines() {
        let line = line.trim();
        if line.len() == RECORD * 2 {
            let parsed: Vec<u8> = (0..RECORD).filter_map(|i| u8::from_str_radix(&line[i * 2..i * 2 + 2], 16).ok()).collect();
            if parsed.len() == RECORD {
                list.insert(parsed);
            }
        }
    }
    println!("pipelines: {}", list.len());
    list.iter().flatten().copied().collect()
}

/// A base-game import must only name the programs shipped with the renderer. Mods use their own compiler path.
pub fn checked_bytes() -> anyhow::Result<Vec<u8>> {
    let bytes = bytes();
    let mut vertex = BTreeSet::new();
    let mut pixel = BTreeSet::new();
    for line in include_str!("../../../../runtime/src/render/programs/table.tsv").lines() {
        let mut fields = line.split('\t');
        let stage = fields.next().unwrap_or("");
        if let Some(hash) = fields.next().and_then(|h| u64::from_str_radix(h, 16).ok()) {
            if stage == "v" { vertex.insert(hash); } else if stage == "p" { pixel.insert(hash); }
        }
    }
    // The written programs add_particles names (tools/dev/compile_programs.py): the particle shader's vertex stage in
    // both forms and its pixel stage; particle_instances is VERTEX_ONLY, a base vertex stage alone.
    vertex.extend([SITE | SHADER_PARTICLE << 1, SITE | SHADER_PARTICLE << 1 | 1, SITE | SHADER_PARTICLE_INSTANCES << 1]);
    pixel.insert(SITE | SHADER_PARTICLE << 1);
    for line in include_str!("../../data/site_pipelines.txt").lines() {
        let line = line.trim();
        if line.len() != RECORD * 2 { continue; }
        let id = |at| -> Option<u64> {
            let mut words = [0u8; 8];
            for (i, word) in words.iter_mut().enumerate() {
                *word = u8::from_str_radix(&line[at + i * 2..at + i * 2 + 2], 16).ok()?;
            }
            Some(u64::from_le_bytes(words))
        };
        if let Some(v) = id(0) { vertex.insert(v); }
        if let Some(p) = id(16) { pixel.insert(p); }
    }
    for record in bytes.chunks_exact(RECORD) {
        let v = u64::from_le_bytes(record[..8].try_into().unwrap());
        let p = u64::from_le_bytes(record[8..16].try_into().unwrap());
        anyhow::ensure!(vertex.contains(&v) && pixel.contains(&p),
            "The imported material names an uncompiled program ({v:016x}, {p:016x}); the game store was not installed");
    }
    Ok(bytes)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn recorded(hex: &str) -> Vec<u8> {
        let mut out: Vec<u8> = (0..hex.len() / 2).map(|i| u8::from_str_radix(&hex[i * 2..i * 2 + 2], 16).unwrap()).collect();
        out.resize(RECORD, 0);
        out
    }

    /// The records a game run registered (additive blend by source and inverse source colour, depth tested less or
    /// equal, alpha test greater or equal and always): instances, corners, corners with extras, widened corners.
    #[test]
    fn particle_records_are_the_ones_the_game_registers() {
        let mut shape = vec![0u8; 120];
        shape[SHAPE_BLEND_OP..SHAPE_BLEND_OP + 9].copy_from_slice(&[1, 4, 5, 1, 3, 0, 6, 7, 0]);
        let state = particle_state(&shape);
        assert_eq!(state, 0x21D51);
        let mut list = BTreeSet::new();
        particle_draw(&mut list, state, 2, false);
        particle_draw(&mut list, state, 5, false);
        particle_draw(&mut list, state, 5, true);
        particle_draw(&mut list, state, 0, false);
        let expected: BTreeSet<Vec<u8>> = [
            "54000000000000803c00000000000080511d020000000000a0",
            "3c000000000000803c00000000000080511d02000400000028000000000000000c00000028000000010002001800000028000000020002001c00000028000000100001002000000028",
            "3c000000000000803c00000000000080511d02000600000038000000000000000c00000038000000010002001800000038000000020002001c00000038000000100001002000000038000000110001002800000038000000120001003000000038",
            "3d000000000000803c00000000000080511d02000600000038000000000000000c00000038000000010002001800000038000000020002001c00000038000000100001002000000038000000030000002800000038000000050003003400000038",
        ]
        .iter()
        .map(|h| recorded(h))
        .collect();
        assert_eq!(list, expected);
    }

    #[test]
    fn an_alpha_test_that_cannot_reject_shades_per_pixel() {
        let mut shape = vec![0u8; 120];
        shape[SHAPE_ALPHA_COMPARE0..SHAPE_ALPHA_COMPARE0 + 3].copy_from_slice(&[7, 0, 1]);
        assert_eq!(particle_state(&shape), STATE_COLOR_WRITE | STATE_PIXEL_SHADING);
    }
}
