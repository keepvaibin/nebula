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
