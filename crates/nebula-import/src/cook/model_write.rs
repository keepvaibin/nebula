//! ModelIr to the flat block native/include/nebula_model.h describes.

use super::curves::{self, Curves, Vertex};
use super::mesh_order;
use super::model_ir::*;
use super::pipeline_list;
use super::programs::{self, Written};
use anyhow::{bail, Context, Result};
use std::collections::HashMap;

pub const MAGIC: u32 = 0x4C44_4D4E;
pub const VERSION: u32 = 11;
pub const HEADER_WORDS: usize = 31;
pub const HIT_BYTES: usize = 120;
pub const STAGE_BYTES: usize = 36;
pub const JOINT_BYTES: usize = 96;
pub const MESH_BYTES: usize = 100;
pub const MESH_COORD_BYTES: usize = 4;
pub const MATERIAL_BYTES: usize = 208;
pub const SLOT_BYTES: usize = 8;
pub const FORM_BYTES: usize = 64;
pub const TEXTURE_BYTES: usize = 40;
pub const COORD_MATRIX_BYTES: usize = 104;
pub const WARP_BYTES: usize = 24;

pub const MESH_NORMAL: u32 = 1 << 0;
pub const MESH_COLOR0: u32 = 1 << 1;
pub const MESH_COLOR1: u32 = 1 << 2;
pub const MESH_TANGENTS: u32 = 1 << 3;
/// Stream 3's de-light gain, a word a vertex (debake_gains; nebula_model.h NEBULA_MESH_GAIN).
pub const MESH_GAIN: u32 = 1 << 4;
/// NebulaModelMeshCoord.stream for a coordinate the mesh has none of.
pub const MESH_NO_COORD: u8 = 0xFF;
/// NebulaModelSlot.uses
pub const SLOT_USES_MAP: u8 = 1 << 0;
pub const SLOT_USES_COORD: u8 = 1 << 1;
pub const SLOT_NO_MAP: u16 = 0xFFFF;

/// A pipeline's fixed state as one word (nebula_model.h NEBULA_STATE_*).
pub fn pipeline_state(m: &MaterialIr) -> u32 {
    let (op, src, dst) = (u32::from(m.blend_type), u32::from(m.blend_src), u32::from(m.blend_dst));
    let blend = op | if op == 1 { src << 2 | dst << 6 } else { 0 };
    let test = m.depth_test != 0;
    let depth = if test { 1 << 10 | u32::from(m.depth_func) << 11 } else { 0 } | if m.depth_write != 0 { 1 << 14 } else { 0 };
    let cull = if m.cull == 3 { 0 } else { u32::from(m.cull) };
    blend | depth | cull << 15 | 1 << 17 | 1 << 19
}

pub fn name_hash(text: &str) -> u32 {
    let mut h = 0x811C_9DC5u32;
    for c in text.chars() {
        h = (h ^ (c as u32 & 0xFF)).wrapping_mul(0x0100_0193);
    }
    h
}

/// A material's slots: slot n is map n, coordinate n and matrix n. Its texture, the map its coordinate's scale comes from,
/// and whether its stages and warp orders read the map and the coordinate (255 names neither).
pub fn material_slots(m: &MaterialIr) -> Vec<(u32, u16, u8)> {
    let mut count = m.textures.len().max(m.coords.len()).max(m.coordinate_maps.len());
    for stage in &m.stages {
        for n in [stage.coord, stage.map] {
            if n != 255 {
                count = count.max(usize::from(n) + 1);
            }
        }
    }
    for order in &m.warp_orders {
        if order[0] != 255 {
            count = count.max(usize::from(order[0]) + 1);
        }
    }
    for t in &m.matrices {
        count = count.max(usize::from(t.slot) + 1);
    }
    if let Some((slot, _)) = m.normal_map {
        count = count.max(usize::from(slot) + 1);
    }
    for slot in pbr_slots(m) {
        count = count.max(usize::from(slot) + 1);
    }
    let mut out: Vec<(u32, u16, u8)> = (0..count)
        .map(|n| {
            let map = m.coordinate_maps.get(n).copied().unwrap_or(255);
            (m.textures.get(n).copied().unwrap_or(NONE), if map == 255 { SLOT_NO_MAP } else { u16::from(map) }, 0)
        })
        .collect();
    for stage in &m.stages {
        if stage.coord != 255 {
            out[usize::from(stage.coord)].2 |= SLOT_USES_COORD;
        }
        if stage.map != 255 {
            out[usize::from(stage.map)].2 |= SLOT_USES_MAP;
        }
    }
    // A normal map is read in the lit form's pixel stage, through its own slot (MaterialIr normal_map).
    if let Some((slot, _)) = m.normal_map {
        out[usize::from(slot)].2 |= SLOT_USES_COORD | SLOT_USES_MAP;
    }
    // So are a glTF material's metal-roughness and occlusion maps (MaterialIr pbr).
    for slot in pbr_slots(m) {
        out[usize::from(slot)].2 |= SLOT_USES_COORD | SLOT_USES_MAP;
    }
    for order in &m.warp_orders {
        if order[0] != 255 {
            out[usize::from(order[0])].2 |= SLOT_USES_COORD;
        }
    }
    out
}

/// The slots a glTF material's lit template samples (MaterialIr pbr): its metal-roughness map and its occlusion map.
pub fn pbr_slots(m: &MaterialIr) -> Vec<u8> {
    let Some(p) = m.pbr else { return Vec::new() };
    let mut out: Vec<u8> = p.metal_rough.into_iter().chain(p.occlusion.map(|(slot, _)| slot)).collect();
    out.dedup();
    out
}

/// A mesh's streams, every corner of the shape's groups once.
#[derive(Default, Clone)]
pub struct MeshStreams {
    pub attributes: u32,
    /// Which coordinate numbers it has; `coords` holds as many per vertex (zeros where it has none).
    pub coordinates: Vec<bool>,
    pub positions: Vec<[f32; 3]>,
    pub normals: Vec<[f32; 3]>,
    pub colors: Vec<u32>,
    pub coords: Vec<[f32; 2]>,
    pub colors1: Vec<u32>,
    pub tangents: Vec<[[f32; 3]; 2]>,
    /// With MESH_GAIN: each vertex's de-light gain, r g b signed bytes of log2 x GAIN_STEPS (debake_gains).
    pub gains: Vec<u32>,
    pub matrices: Vec<u32>,
    pub indices: Vec<u32>,
    pub single_matrix: u32,
    /// The plain mesh's vertices; those past it are refinement's.
    pub base_vertices: u32,
    pub refined_indices: Vec<u32>,
    pub flats: Vec<bool>,
}

impl MeshStreams {
    pub fn has_coord(&self, k: usize) -> bool {
        self.coordinates.get(k).copied().unwrap_or(false)
    }
    /// Vertex i's coordinate k.
    pub fn coord(&self, i: usize, k: usize) -> [f32; 2] {
        self.coords[i * self.coordinates.len() + k]
    }
    fn push_coords(&mut self, uv: &[[f32; 2]]) {
        for k in 0..self.coordinates.len() {
            self.coords.push(uv.get(k).copied().unwrap_or([0.0; 2]));
        }
    }
}

#[derive(PartialEq, Eq, Hash)]
struct CornerKey {
    position: u32,
    normal: u32,
    tangent: u32,
    binormal: u32,
    color: [u32; 2],
    coord: Vec<u32>,
    matrix: u32,
    nbt: bool,
}

pub fn corner_normal(a: &ArraysIr, attr: u64, c: &Corner) -> [[f32; 3]; 3] {
    let at3 = |i: u32| a.normals.get(i as usize).copied().unwrap_or([0.0; 3]);
    if attr & ATTR_NBT != 0 {
        if attr & ATTR_NBT_THREE != 0 {
            return [at3(c.normal), at3(c.tangent), at3(c.binormal)];
        }
        let n = a.nbt.get(c.normal as usize).copied().unwrap_or([0.0; 9]);
        return [[n[0], n[1], n[2]], [n[3], n[4], n[5]], [n[6], n[7], n[8]]];
    }
    if attr & ATTR_NORMAL != 0 {
        return [at3(c.normal), [0.0; 3], [0.0; 3]];
    }
    [[0.0; 3]; 3]
}

/// A shape's groups as one mesh. Each corner's matrix resolves through its group's table, an entry of NEBULA_NONE32 keeping
/// the previous group's.
pub fn flatten(a: &ArraysIr, shape: &ShapeIr, curved: Option<&Curves>) -> Result<MeshStreams> {
    let mut out = MeshStreams::default();
    out.coordinates = vec![false; shape.groups.iter().map(|g| g.coordinates.len()).max().unwrap_or(0)];
    for group in &shape.groups {
        for (k, &present) in group.coordinates.iter().enumerate() {
            out.coordinates[k] |= present;
        }
    }
    // Only coordinates some group carries take room.
    while out.coordinates.last() == Some(&false) {
        out.coordinates.pop();
    }
    let room = out.coordinates.len();
    let mut seen: HashMap<CornerKey, u32> = HashMap::new();
    let mut table: Vec<u32> = Vec::new();
    let mut plans: Vec<GroupRefine> = Vec::new();
    for group in &shape.groups {
        if table.len() < group.use_table.len() {
            table.resize(group.use_table.len(), NONE);
        }
        for (k, &entry) in group.use_table.iter().enumerate() {
            if entry != NONE {
                table[k] = entry;
            }
        }
        let attr = group.attributes;
        if attr & ATTR_NORMAL != 0 {
            out.attributes |= MESH_NORMAL;
        }
        if attr & ATTR_NBT != 0 {
            out.attributes |= MESH_TANGENTS;
        }
        for k in 0..2 {
            if attr & (ATTR_COLOR0 << k) != 0 {
                out.attributes |= MESH_COLOR0 << k;
            }
        }
        let curve_data = curved.filter(|_| attr & ATTR_NORMAL != 0 && attr & ATTR_NBT == 0);
        let mut mesh_of = vec![u32::MAX; group.corners.len()];
        for &i in &group.indices {
            let c = &group.corners[i as usize];
            let local = if attr & ATTR_MATRIX != 0 { c.matrix as usize } else { 0 };
            let resolved = u32::from(*table.get(local).unwrap_or(&0));
            let mut key = CornerKey {
                position: c.position,
                normal: 0,
                tangent: 0,
                binormal: 0,
                color: [0; 2],
                coord: (0..room).map(|k| if group.has_coord(k) { c.coord_at(k) } else { 0 }).collect(),
                matrix: resolved,
                nbt: attr & ATTR_NBT != 0,
            };
            if attr & ATTR_NORMAL != 0 {
                key.normal = c.normal;
                key.tangent = c.tangent;
                key.binormal = c.binormal;
            }
            for k in 0..2 {
                if attr & (ATTR_COLOR0 << k) != 0 {
                    key.color[k] = c.color[k];
                }
            }
            let next = out.positions.len() as u32;
            let index = *seen.entry(key).or_insert(next);
            if index == next {
                out.positions.push(a.positions.get(c.position as usize).copied().unwrap_or([0.0; 3]));
                let n = corner_normal(a, attr, c);
                out.normals.push(n[0]);
                out.tangents.push([n[1], n[2]]);
                out.colors.push(if attr & ATTR_COLOR0 != 0 { a.colors[0].get(c.color[0] as usize).copied().unwrap_or(0) } else { 0 });
                out.colors1.push(if attr & (ATTR_COLOR0 << 1) != 0 { a.colors[1].get(c.color[1] as usize).copied().unwrap_or(0) } else { 0 });
                for k in 0..room {
                    out.coords.push(if group.has_coord(k) { a.coord(k, c.coord_at(k)) } else { [0.0; 2] });
                }
                out.matrices.push(resolved);
                out.flats.push(curve_data.is_some_and(|cd| cd.flat_of(c.position, c.normal)));
            }
            mesh_of[i as usize] = index;
            out.indices.push(index);
        }
        // Refinement mixes a group's corners; its vertices follow the whole plain mesh.
        if let Some(cd) = curved.filter(|cd| cd.bends) {
            let corners: Vec<Vertex> = group.corners.iter().map(|c| corner_vertex(a, attr, &group.coordinates, c, curve_data)).collect();
            let (added, indices) = curves::refine(&corners, &group.indices);
            let _ = cd;
            let table_now: Vec<u32> = (0..table.len()).map(|k| u32::from(table[k])).collect();
            plans.push(GroupRefine { corners: corners.len() as u32, mesh_of, added, indices, table: table_now });
        }
    }
    let first = out.matrices.first().copied().unwrap_or(0);
    out.single_matrix = if out.matrices.iter().all(|&m| m == first) { first } else { NONE };
    out.base_vertices = out.positions.len() as u32;
    if plans.iter().any(|p| !p.added.is_empty()) {
        for plan in plans {
            let at = out.positions.len() as u32;
            for v in &plan.added {
                out.positions.push(v.position);
                out.normals.push(v.normal);
                out.tangents.push([v.tangent, v.binormal]);
                out.colors.push(v.color0);
                out.colors1.push(v.color1);
                out.push_coords(&v.uv);
                out.matrices.push(plan.table.get(v.matrix as usize).copied().unwrap_or(first));
                out.flats.push(v.flat);
            }
            out.refined_indices.extend(plan.indices.iter().map(|&i| if i < plan.corners { plan.mesh_of[i as usize] } else { at + i - plan.corners }));
        }
    }
    Ok(out)
}

struct GroupRefine {
    corners: u32,
    mesh_of: Vec<u32>,
    added: Vec<Vertex>,
    indices: Vec<u32>,
    table: Vec<u32>,
}

/// A group's corner as refinement reads it: its matrix by the group's own number, its curve data where the group has any.
pub fn corner_vertex(a: &ArraysIr, attr: u64, coordinates: &[bool], c: &Corner, curved: Option<&Curves>) -> Vertex {
    let n = corner_normal(a, attr, c);
    let mut v = Vertex {
        position: a.positions.get(c.position as usize).copied().unwrap_or([0.0; 3]),
        normal: n[0],
        tangent: n[1],
        binormal: n[2],
        matrix: if attr & ATTR_MATRIX != 0 { u32::from(c.matrix) } else { 0 },
        color0: if attr & ATTR_COLOR0 != 0 { a.colors[0].get(c.color[0] as usize).copied().unwrap_or(0) } else { 0 },
        color1: if attr & (ATTR_COLOR0 << 1) != 0 { a.colors[1].get(c.color[1] as usize).copied().unwrap_or(0) } else { 0 },
        ..Vertex::default()
    };
    v.uv = coordinates.iter().enumerate().map(|(k, &present)| if present { a.coord(k, c.coord_at(k)) } else { [0.0; 2] }).collect();
    if let Some(cd) = curved {
        if (c.position as usize) < cd.places.len() {
            v.bend = cd.bend_of(c.position, c.normal);
            v.crease = cd.crease[c.position as usize];
            v.place = cd.places[c.position as usize];
            v.neighbour = cd.neighbour[c.position as usize];
            v.second = cd.second[c.position as usize];
            v.flat = cd.flat_of(c.position, c.normal);
        } else {
            v.place = 0xFFFF_FFFF;
            v.second = curves::NO_PLACE;
        }
    }
    v
}

/// What the curve analysis reads of a model: its shared arrays up to the last entry a plain-normal group uses, and the
/// triangles of those groups.
pub fn curve_input(ir: &ModelIr) -> Option<(Vec<[f32; 3]>, Vec<[f32; 3]>, Vec<(u32, u32)>)> {
    let a = &ir.arrays;
    if a.positions.is_empty() || a.normals.is_empty() || !a.nbt.is_empty() {
        return None;
    }
    let mut corners = Vec::new();
    let (mut most_position, mut most_normal) = (0u32, 0u32);
    for shape in &ir.shapes {
        if shape.groups.first().is_some_and(|g| g.attributes & ATTR_NBT != 0) {
            continue;
        }
        for group in &shape.groups {
            if group.attributes & ATTR_NORMAL == 0 {
                continue;
            }
            for &i in &group.indices {
                let c = &group.corners[i as usize];
                corners.push((c.position, c.normal));
                most_position = most_position.max(c.position);
                most_normal = most_normal.max(c.normal);
            }
        }
    }
    if corners.is_empty() {
        return None;
    }
    let positions = a.positions.get(..=most_position as usize).unwrap_or(&a.positions).to_vec();
    let normals = a.normals.get(..=most_normal as usize).unwrap_or(&a.normals).to_vec();
    Some((positions, normals, corners))
}

pub fn model_curves(ir: &ModelIr) -> Option<Curves> {
    let (positions, normals, corners) = curve_input(ir)?;
    curves::analyse(&positions, &normals, &corners)
}

struct Blob(Vec<u8>);

impl Blob {
    fn align(&mut self, n: usize) {
        while self.0.len() % n != 0 {
            self.0.push(0);
        }
    }
    fn at(&self) -> u32 {
        self.0.len() as u32
    }
    fn u8(&mut self, v: u8) {
        self.0.push(v);
    }
    fn u16(&mut self, v: u16) {
        self.0.extend_from_slice(&v.to_le_bytes());
    }
    fn i16(&mut self, v: i16) {
        self.0.extend_from_slice(&v.to_le_bytes());
    }
    fn u32(&mut self, v: u32) {
        self.0.extend_from_slice(&v.to_le_bytes());
    }
    fn f32(&mut self, v: f32) {
        self.0.extend_from_slice(&v.to_le_bytes());
    }
    fn fs(&mut self, v: &[f32]) {
        for &x in v {
            self.f32(x);
        }
    }
    fn patch32(&mut self, at: usize, v: u32) {
        self.0[at..at + 4].copy_from_slice(&v.to_le_bytes());
    }
}

/// How much light crosses inside each material (RT-MASTER-PLAN 13.2): over the meshes it draws, their bounds, area and
/// enclosed volume, and whether together they close (every edge, corners welded by place, shared by two triangles).
#[derive(Clone, Copy)]
struct Measure {
    low: [f32; 3],
    high: [f32; 3],
    area: f64,
    volume: f64,
    closed: bool,
    seen: bool,
}

impl Measure {
    fn new() -> Measure {
        Measure { low: [f32::MAX; 3], high: [f32::MIN; 3], area: 0.0, volume: 0.0, closed: true, seen: false }
    }

    fn add(&mut self, positions: &[[f32; 3]], indices: &[u32]) {
        let (low, high) = mesh_bounds(positions);
        for k in 0..3 {
            self.low[k] = self.low[k].min(low[k]);
            self.high[k] = self.high[k].max(high[k]);
        }
        self.seen = self.seen || !positions.is_empty();
        let mut welded: HashMap<[u32; 3], u32> = HashMap::new();
        let mut id = |p: [f32; 3]| {
            let next = welded.len() as u32;
            *welded.entry(p.map(f32::to_bits)).or_insert(next)
        };
        let mut edges: HashMap<(u32, u32), u32> = HashMap::new();
        for t in indices.chunks_exact(3) {
            let (Some(&a), Some(&b), Some(&c)) = (positions.get(t[0] as usize), positions.get(t[1] as usize), positions.get(t[2] as usize)) else {
                self.closed = false;
                continue;
            };
            let (u, v) = ([b[0] - a[0], b[1] - a[1], b[2] - a[2]], [c[0] - a[0], c[1] - a[1], c[2] - a[2]]);
            let n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
            self.area += 0.5 * f64::from((n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt());
            self.volume += f64::from(a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0;
            let k = [id(a), id(b), id(c)];
            for (p, q) in [(k[0], k[1]), (k[1], k[2]), (k[2], k[0])] {
                *edges.entry((p.min(q), p.max(q))).or_default() += 1;
            }
        }
        if edges.values().any(|&n| n != 2) {
            self.closed = false;
        }
    }

    fn thickness(&self) -> f32 {
        if !self.seen {
            return 0.0;
        }
        if self.closed && self.area > 0.0 && self.volume.abs() > 0.0 {
            return (3.0 * self.volume.abs() / self.area) as f32;
        }
        let smallest = (0..3).map(|k| self.high[k] - self.low[k]).fold(f32::MAX, f32::min);
        (0.5 * smallest).max(1.0)
    }
}

/// Below this share of smooth residual a model's paint holds detail shadows of its own (RT-MASTER-PLAN 14.6).
const PAINTED_SHADOW_SMOOTH: f64 = 0.5;
/// From this share of a painted mesh's vertex light that one ambient and one directional light explain, its paint is
/// one light: the Physical style puts the real sun in its place (14.6, the study's r2_one_light).
const PAINT_LIGHT_FIT: f64 = 0.6;

/// A unit direction as two signed 16-bit octahedral numbers (nebula_model.h NebulaModelPaintLight).
fn octahedral(d: [f32; 3]) -> [i16; 2] {
    let sum = d[0].abs() + d[1].abs() + d[2].abs();
    let (mut x, mut y) = if sum > 0.0 { (d[0] / sum, d[1] / sum) } else { (0.0, 0.0) };
    if d[2] < 0.0 {
        let (ox, oy) = (x, y);
        x = (1.0 - oy.abs()) * if ox >= 0.0 { 1.0 } else { -1.0 };
        y = (1.0 - ox.abs()) * if oy >= 0.0 { 1.0 } else { -1.0 };
    }
    [(x.clamp(-1.0, 1.0) * 32767.0).round() as i16, (y.clamp(-1.0, 1.0) * 32767.0).round() as i16]
}

/// The painted light of a model's vertex-coloured meshes (RT-MASTER-PLAN 14.6; agents/rt/tools/debake_study.py does the
/// same over a pak): each mesh's vertex luminance fitted by an ambient and one directional light (least squares over
/// its vertices, unclamped), and the share of what the fit leaves that is smooth over the surface (the variance of
/// the residual's triangle means against the residual's own), weighted by vertices over the model.
#[derive(Default)]
struct PaintedLight {
    weight: f64,
    smooth: f64,
    /// The mesh last added, where its paint is one light (PAINT_LIGHT_FIT): b / (a + b) and d.
    last: Option<(f32, [f32; 3])>,
}

impl PaintedLight {
    fn add(&mut self, s: &MeshStreams) {
        self.last = None;
        let count = s.positions.len();
        if count < 16 || s.normals.len() != count || s.colors.len() != count {
            return;
        }
        // Luminance (Rec. 601, as the study) of colour 0, stored 0xRRGGBBAA.
        let luminance = |c: u32| (0.299 * f64::from(c >> 24) + 0.587 * f64::from(c >> 16 & 255) + 0.114 * f64::from(c >> 8 & 255)) / 255.0;
        let mut rows: Vec<Option<([f64; 4], f64)>> = Vec::with_capacity(count);
        let mut normal_sums = [[0.0f64; 4]; 4];
        let mut target_sums = [0.0f64; 4];
        let mut good = 0usize;
        for (n, &c) in s.normals.iter().zip(&s.colors) {
            let n = n.map(f64::from);
            let length = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt();
            if length <= 1e-6 {
                rows.push(None);
                continue;
            }
            let x = [1.0, n[0] / length, n[1] / length, n[2] / length];
            let y = luminance(c);
            for i in 0..4 {
                for j in 0..4 {
                    normal_sums[i][j] += x[i] * x[j];
                }
                target_sums[i] += x[i] * y;
            }
            rows.push(Some((x, y)));
            good += 1;
        }
        if good < 16 {
            return;
        }
        let Some(fit) = solve4(normal_sums, target_sums) else { return };
        let mut residual = vec![0.0f64; count];
        let (mut sum, mut sum2) = (0.0f64, 0.0f64);
        for (r, row) in residual.iter_mut().zip(&rows) {
            if let Some((x, y)) = row {
                *r = y - (0..4).map(|k| fit[k] * x[k]).sum::<f64>();
                sum += *r;
                sum2 += *r * *r;
            }
        }
        let good_variance = sum2 / good as f64 - (sum / good as f64).powi(2);
        let mut smooth = 0.0f64;
        if s.indices.len() >= 3 && good_variance > 1e-9 {
            let all_mean = residual.iter().sum::<f64>() / count as f64;
            let all_variance = residual.iter().map(|r| (r - all_mean).powi(2)).sum::<f64>() / count as f64;
            let means: Vec<f64> = s.indices.chunks_exact(3)
                .map(|t| t.iter().map(|&i| residual.get(i as usize).copied().unwrap_or(0.0)).sum::<f64>() / 3.0)
                .collect();
            let means_mean = means.iter().sum::<f64>() / means.len() as f64;
            let means_variance = means.iter().map(|m| (m - means_mean).powi(2)).sum::<f64>() / means.len() as f64;
            if all_variance > 0.0 {
                smooth = (means_variance / all_variance).min(1.0);
            }
        }
        // How much of the vertex light the fit explains; where it is most of it, the light it found (a and the
        // directional part g = b d): one placed by a single matrix, as the tracer turns d by the item's.
        let (mut y_sum, mut y_sum2) = (0.0f64, 0.0f64);
        for (_, y) in rows.iter().flatten() {
            y_sum += y;
            y_sum2 += y * y;
        }
        let total = y_sum2 - y_sum * y_sum / good as f64;
        let explained = if total > 1e-9 { 1.0 - sum2 / total } else { 0.0 };
        let gain = (fit[1] * fit[1] + fit[2] * fit[2] + fit[3] * fit[3]).sqrt();
        if explained >= PAINT_LIGHT_FIT && fit[0] > 0.0 && gain > 1e-6 && s.single_matrix != NONE {
            self.last = Some(((gain / (fit[0] + gain)) as f32, [(fit[1] / gain) as f32, (fit[2] / gain) as f32, (fit[3] / gain) as f32]));
        }
        self.weight += good as f64;
        self.smooth += smooth * good as f64;
    }

    /// Whether the model's paint holds detail shadows of its own: a tracer shadows its painted materials by lit
    /// things only (NEBULA_HIT_PAINTED_SHADOW).
    fn holds_shadows(&self) -> bool {
        self.weight > 0.0 && self.smooth / self.weight < PAINTED_SHADOW_SMOOTH
    }
}

/// De-bake v2 (c79): the gain's byte, log2 x this (a signed byte: 2^(127/42) = 8.1 holds DEBAKE_CAP).
pub const GAIN_STEPS: f64 = 42.0;
/// The most a vertex colour is lifted, and the region's reference: this share of its area lies under it.
const DEBAKE_CAP: f64 = 8.0;
const DEBAKE_REFERENCE: f64 = 0.9;
/// How far the hue that follows the painted light moves to the lit hue (kDebakeChroma).
const DEBAKE_CHROMA: f64 = 0.5;
/// A luminance under this (8 / 255) is painted black, not shade.
const DEBAKE_NEAR_BLACK: f64 = 8.0 / 255.0;

/// c72 (the owner's verdict A5): whether a material's vertex colour is de-lit by the cooker's gain (c37). Its vertex
/// colour is read and it is no emitter; a lit colour channel that takes its colour from a register keeps it (the game's
/// live colour holds no painted light), while one whose material or ambient colour is the vertex colour is de-lit as a
/// painted one (the gain multiplies the vertex colour before the lights). Blended materials by the same rule.
pub fn debakes(m: &MaterialIr) -> bool {
    let r = super::hit_record::read(m);
    if !r.signals.vertex_colour || r.class == super::hit_record::Class::Emissive {
        return false;
    }
    !(0..(m.channel_count as usize).min(2)).any(|ch| {
        let c = &m.channels[2 * ch];
        c.enable != 0 && c.light_mask != 0 && c.material_source == 0 && c.ambient_source == 0
    })
}

/// c93 (3.3; c65's kind 2): whether a material's meshes may hold painted light in their first map alone: no colour
/// channel's rgb lit, opaque, not emissive, its vertex colour unread, and a stage with a map and a coordinate. Which
/// of its meshes do is map_light's.
pub fn debakes_map(m: &MaterialIr) -> bool {
    let rgb_lit = (0..(m.channel_count as usize).min(2)).any(|ch| m.channels[2 * ch].enable != 0 && m.channels[2 * ch].light_mask != 0);
    let r = super::hit_record::read(m);
    !rgb_lit
        && r.signals.blend == 0
        && r.class != super::hit_record::Class::Emissive
        && !r.signals.vertex_colour
        && m.stages.iter().any(|s| s.map != 0xFF && s.coord != 0xFF)
}

/// c93: how far a map's coordinate may run over its mesh and still be one tile (an unwrapped map: its brightness can be
/// light painted for that mesh; a tiled one's is its pattern).
const MAP_LIGHT_TILE: f64 = 1.05;
/// c93: the least directional part of a map's light, against its mean (1): under it the map lies even to the facing.
const MAP_LIGHT_LEAST: f64 = 0.05;

/// c93: a mesh's first map at its vertices (first_map_at_vertices) where it holds light: the map one tile of the mesh's
/// coordinate (MAP_LIGHT_TILE a side) and its brightness following the facing (map_follows_facing). None otherwise.
fn map_light(ir: &ModelIr, material: usize, s: &MeshStreams) -> Option<Vec<f64>> {
    let ratios = first_map_at_vertices(ir, material, s)?;
    let m = ir.materials.get(material)?;
    let stage = m.stages.iter().find(|stage| stage.map != 0xFF && stage.coord != 0xFF)?;
    let k = usize::from(m.coords.get(usize::from(stage.coord))?.source.checked_sub(4)?);
    let (mut low, mut high) = ([f64::MAX; 2], [f64::MIN; 2]);
    for i in 0..s.positions.len() {
        let uv = s.coord(i, k);
        for a in 0..2 {
            low[a] = low[a].min(f64::from(uv[a]));
            high[a] = high[a].max(f64::from(uv[a]));
        }
    }
    if (0..2).any(|a| !(high[a] - low[a] <= MAP_LIGHT_TILE)) || !map_follows_facing(s, &ratios) {
        return None;
    }
    Some(ratios)
}

/// c93: whether light painted into a map follows the facing: the ratios fitted by an ambient and one directional light
/// over the vertex normals (least squares, as PaintedLight) explain PAINT_LIGHT_FIT of their variance, and the
/// directional part is at least MAP_LIGHT_LEAST.
fn map_follows_facing(s: &MeshStreams, ratios: &[f64]) -> bool {
    if s.normals.len() != ratios.len() || ratios.len() < 16 {
        return false;
    }
    let (mut a, mut b) = ([[0.0f64; 4]; 4], [0.0f64; 4]);
    let mut rows = Vec::with_capacity(ratios.len());
    for (n, &y) in s.normals.iter().zip(ratios) {
        let n = n.map(f64::from);
        let length = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt();
        if length <= 1e-6 {
            continue;
        }
        let x = [1.0, n[0] / length, n[1] / length, n[2] / length];
        for i in 0..4 {
            for j in 0..4 {
                a[i][j] += x[i] * x[j];
            }
            b[i] += x[i] * y;
        }
        rows.push((x, y));
    }
    if rows.len() < 16 {
        return false;
    }
    let Some(fit) = solve4(a, b) else { return false };
    let mean = rows.iter().map(|r| r.1).sum::<f64>() / rows.len() as f64;
    let (mut total, mut left) = (0.0f64, 0.0f64);
    for (x, y) in &rows {
        total += (y - mean).powi(2);
        left += (y - (0..4).map(|k| fit[k] * x[k]).sum::<f64>()).powi(2);
    }
    let directional = (fit[1] * fit[1] + fit[2] * fit[2] + fit[3] * fit[3]).sqrt();
    total > 1e-9 && 1.0 - left / total >= PAINT_LIGHT_FIT && directional >= MAP_LIGHT_LEAST
}

/// c55 (census; d07's question 3): a model's de-light with c37 alone and with d07's map ratios; c93: its meshes painted
/// by a map alone (debakes_map), those whose map holds light (map_light), and their mean gain.
pub struct DebakeReport {
    pub debaked: usize,
    pub mapped: usize,
    pub vertices: usize,
    pub gain_c37: f64,
    pub gain_d07: f64,
    /// c79: the chosen meshes painted near black all over (no light to take out).
    pub near_black: usize,
    pub map_painted: usize,
    pub map_lit: usize,
    pub gain_map: f64,
}

/// The de-light of a model as write() makes it (the same meshes chosen), with and without the maps: None for a sky
/// model, one with no painted mesh, or one whose meshes do not flatten.
/// c85 (3.5): what a near model gives its far twins: each material's area (its meshes' triangles, square units) and
/// its de-baked materials' Lref (debake_gains).
pub fn twin_measures(ir: &ModelIr, stem: &str) -> (Vec<f64>, std::collections::BTreeMap<u32, f64>) {
    let mut area = vec![0.0f64; ir.materials.len()];
    let mut references = std::collections::BTreeMap::new();
    let curved = model_curves(ir);
    let Ok(mut meshes) = ir.shapes.iter().map(|s| flatten(&ir.arrays, s, curved.as_ref())).collect::<Result<Vec<MeshStreams>>>() else {
        return (area, references);
    };
    for (shape, s) in ir.shapes.iter().zip(&meshes) {
        let Some(sum) = area.get_mut(shape.material as usize) else { continue };
        for t in s.indices.chunks_exact(3) {
            let [a, b, c] = [t[0] as usize, t[1] as usize, t[2] as usize];
            let (Some(pa), Some(pb), Some(pc)) = (s.positions.get(a), s.positions.get(b), s.positions.get(c)) else { continue };
            let u = [f64::from(pb[0] - pa[0]), f64::from(pb[1] - pa[1]), f64::from(pb[2] - pa[2])];
            let v = [f64::from(pc[0] - pa[0]), f64::from(pc[1] - pa[1]), f64::from(pc[2] - pa[2])];
            let n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
            *sum += (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt() / 2.0;
        }
    }
    let lower = stem.split('.').next().unwrap_or(stem).to_ascii_lowercase();
    if lower.ends_with("sky") || lower.starts_with("vrorbit") {
        return (area, references);
    }
    let which: Vec<bool> = ir.shapes.iter().map(|s| ir.materials.get(s.material as usize).is_some_and(debakes)).collect();
    if which.iter().any(|&w| w) {
        let maps: Vec<Option<Vec<f64>>> = ir
            .shapes
            .iter()
            .zip(&meshes)
            .zip(&which)
            .map(|((shape, streams), &w)| if w { first_map_at_vertices(ir, shape.material as usize, streams) } else { None })
            .collect();
        let materials: Vec<u32> = ir.shapes.iter().map(|s| s.material).collect();
        debake_gains(&mut meshes, &which, &maps, &materials, &std::collections::BTreeMap::new(), Some(&mut references));
    }
    (area, references)
}

/// c86 (PLAN-LIGHTS L2): a piece of a glowing mesh no longer than this (units: 6 m), and at most this many a mesh.
const GLOW_PIECE: f64 = 600.0;
const GLOW_PIECES_MOST: usize = 32;
/// nebula_model.h NEBULA_GLOW_*.
pub const GLOW_SPHERE: u8 = 0;
pub const GLOW_CAPSULE: u8 = 1;
pub const GLOW_DISC: u8 = 2;

/// c86: one piece of a glowing mesh as a light's shape (nebula_model.h NebulaModelGlowShape), in the model's space.
#[derive(Clone, Debug, PartialEq)]
pub struct GlowShape {
    pub centre: [f32; 3],
    pub axis: [f32; 3],
    pub normal: [f32; 3],
    pub extent: [f32; 3],
    pub area: f32,
    pub kind: u8,
    pub two_sided: bool,
}

/// c86: a symmetric 3 x 3 matrix's eigenvectors (Jacobi rotations; Press et al., Numerical Recipes, 11.1), largest
/// eigenvalue first.
fn principal_axes(mut a: [[f64; 3]; 3]) -> [[f64; 3]; 3] {
    let mut v = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]];
    for _ in 0..50 {
        let (p, q) = [(0usize, 1usize), (0, 2), (1, 2)]
            .into_iter()
            .max_by(|x, y| a[x.0][x.1].abs().total_cmp(&a[y.0][y.1].abs()))
            .unwrap_or((0, 1));
        let scale = a[0][0].abs() + a[1][1].abs() + a[2][2].abs();
        if !(a[p][q].abs() > 1e-12 * scale) {
            break;
        }
        let theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
        let t = theta.signum() / (theta.abs() + (theta * theta + 1.0).sqrt());
        let c = 1.0 / (t * t + 1.0).sqrt();
        let s = t * c;
        for k in 0..3 {
            let (kp, kq) = (a[k][p], a[k][q]);
            a[k][p] = c * kp - s * kq;
            a[k][q] = s * kp + c * kq;
        }
        for k in 0..3 {
            let (pk, qk) = (a[p][k], a[q][k]);
            a[p][k] = c * pk - s * qk;
            a[q][k] = s * pk + c * qk;
        }
        for k in 0..3 {
            let (kp, kq) = (v[k][p], v[k][q]);
            v[k][p] = c * kp - s * kq;
            v[k][q] = s * kp + c * kq;
        }
    }
    let mut order = [0usize, 1, 2];
    order.sort_by(|&x, &y| a[y][y].total_cmp(&a[x][x]));
    order.map(|i| [v[0][i], v[1][i], v[2][i]])
}

/// c86 (PLAN-LIGHTS L2): a glowing mesh's light as shapes (see GLOW_PIECE; capsule, disc or sphere).
pub fn glow_shapes(s: &MeshStreams, two_sided: bool) -> Vec<GlowShape> {
    let indices = if s.indices.is_empty() { &s.refined_indices } else { &s.indices };
    let p = |i: u32| s.positions.get(i as usize).map(|v| v.map(f64::from));
    // Each triangle: its corners and area.
    let mut tris: Vec<([[f64; 3]; 3], f64, [f64; 3])> = Vec::new();
    for t in indices.chunks_exact(3) {
        let (Some(a), Some(b), Some(c)) = (p(t[0]), p(t[1]), p(t[2])) else { continue };
        let u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
        let w = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
        let n = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];
        let area = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt() / 2.0;
        if area > 0.0 && area.is_finite() {
            tris.push(([a, b, c], area, n.map(|x| x / (2.0 * area))));
        }
    }
    if tris.is_empty() {
        return Vec::new();
    }
    let (mut lo, mut hi) = ([f64::MAX; 3], [f64::MIN; 3]);
    for (corners, _, _) in &tris {
        for c in corners {
            for k in 0..3 {
                lo[k] = lo[k].min(c[k]);
                hi[k] = hi[k].max(c[k]);
            }
        }
    }
    let across = (0..3).map(|k| hi[k] - lo[k]).fold(0.0, f64::max);
    // A piece's frame: (centre, axes longest first, half extents, area).
    let frame = |piece: &[usize]| {
        let total: f64 = piece.iter().map(|&t| tris[t].1).sum();
        let mut mean = [0.0f64; 3];
        for &t in piece {
            for c in &tris[t].0 {
                for k in 0..3 {
                    mean[k] += tris[t].1 / 3.0 * c[k];
                }
            }
        }
        mean = mean.map(|x| x / total);
        let mut cov = [[0.0f64; 3]; 3];
        for &t in piece {
            for c in &tris[t].0 {
                let d = [c[0] - mean[0], c[1] - mean[1], c[2] - mean[2]];
                for i in 0..3 {
                    for j in 0..3 {
                        cov[i][j] += tris[t].1 / 3.0 * d[i] * d[j];
                    }
                }
            }
        }
        let axes = principal_axes(cov);
        let mut span = [(f64::MAX, f64::MIN); 3];
        for &t in piece {
            for c in &tris[t].0 {
                for (k, axis) in axes.iter().enumerate() {
                    let x = (c[0] - mean[0]) * axis[0] + (c[1] - mean[1]) * axis[1] + (c[2] - mean[2]) * axis[2];
                    span[k] = (span[k].0.min(x), span[k].1.max(x));
                }
            }
        }
        let mut order = [0usize, 1, 2];
        order.sort_by(|&x, &y| (span[y].1 - span[y].0).total_cmp(&(span[x].1 - span[x].0)));
        let mut centre = mean;
        for k in 0..3 {
            let mid = (span[k].0 + span[k].1) / 2.0;
            for i in 0..3 {
                centre[i] += axes[k][i] * mid;
            }
        }
        (centre, order.map(|k| axes[k]), order.map(|k| (span[k].1 - span[k].0) / 2.0), total)
    };
    let mut pieces: Vec<Vec<usize>> = vec![(0..tris.len()).collect()];
    let mut whole = vec![false];
    while pieces.len() < GLOW_PIECES_MOST {
        // The longest piece that can still be split.
        let longest = (0..pieces.len())
            .filter(|&i| !whole[i])
            .map(|i| (i, frame(&pieces[i])))
            .filter(|(_, f)| 2.0 * f.2[0] > GLOW_PIECE)
            .max_by(|x, y| x.1 .2[0].total_cmp(&y.1 .2[0]));
        let Some((i, (centre, axes, _, total))) = longest else { break };
        let along = |t: usize| {
            let c = tris[t].0;
            (0..3).map(|k| ((c[0][k] + c[1][k] + c[2][k]) / 3.0 - centre[k]) * axes[0][k]).sum::<f64>()
        };
        let mut piece = std::mem::take(&mut pieces[i]);
        piece.sort_by(|&x, &y| along(x).total_cmp(&along(y)).then(x.cmp(&y)));
        let mut sum = 0.0;
        let cut = piece
            .iter()
            .position(|&t| {
                sum += tris[t].1;
                sum >= total / 2.0
            })
            .map_or(piece.len(), |n| n + 1);
        if cut == 0 || cut >= piece.len() {
            pieces[i] = piece;
            whole[i] = true;
            continue;
        }
        let rest = piece.split_off(cut);
        pieces[i] = piece;
        pieces.push(rest);
        whole.push(false);
    }
    pieces
        .iter()
        .map(|piece| {
            let (centre, axes, extent, area) = frame(piece);
            let kind = if extent[0] > 3.0 * extent[1] {
                GLOW_CAPSULE
            } else if across > GLOW_PIECE && extent[2] < 0.1 * extent[1] {
                GLOW_DISC
            } else {
                GLOW_SPHERE
            };
            let mut normal = axes[2];
            if kind == GLOW_DISC {
                let mut n = [0.0f64; 3];
                for &t in piece {
                    for k in 0..3 {
                        n[k] += tris[t].1 * tris[t].2[k];
                    }
                }
                let length = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt();
                if length > 0.5 * area {
                    normal = n.map(|x| x / length);
                }
            }
            GlowShape {
                centre: centre.map(|x| x as f32),
                axis: axes[0].map(|x| x as f32),
                normal: normal.map(|x| x as f32),
                extent: extent.map(|x| x as f32),
                area: area as f32,
                kind,
                two_sided,
            }
        })
        .collect()
}

pub fn debake_report(ir: &ModelIr, stem: &str) -> Option<DebakeReport> {
    let lower = stem.split('.').next().unwrap_or(stem).to_ascii_lowercase();
    if lower.ends_with("sky") || lower.starts_with("vrorbit") {
        return None;
    }
    let debake_material: Vec<bool> = ir
        .materials
        .iter()
        .map(|m| {
            debakes(m)  // c72
        })
        .collect();
    let which: Vec<bool> = ir.shapes.iter().map(|s| debake_material.get(s.material as usize).copied().unwrap_or(false)).collect();
    let map_painted: Vec<bool> = ir.shapes.iter().map(|s| ir.materials.get(s.material as usize).is_some_and(debakes_map)).collect();
    if !which.iter().chain(&map_painted).any(|&w| w) {
        return None;
    }
    let curved = model_curves(ir);
    let mut meshes = Vec::with_capacity(ir.shapes.len());
    for shape in &ir.shapes {
        meshes.push(flatten(&ir.arrays, shape, curved.as_ref()).ok()?);
    }
    let maps: Vec<Option<Vec<f64>>> = ir
        .shapes
        .iter()
        .zip(&meshes)
        .zip(which.iter().zip(&map_painted))
        .map(|((shape, streams), (&w, &w2))| {
            if w {
                first_map_at_vertices(ir, shape.material as usize, streams)
            } else if w2 {
                map_light(ir, shape.material as usize, streams)
            } else {
                None
            }
        })
        .collect();
    // c93: the map-painted meshes whose map holds light.
    let map_only: Vec<bool> = map_painted.iter().zip(&maps).map(|(&w, m)| w && m.is_some()).collect();
    let chosen: Vec<bool> = which.iter().zip(&map_only).map(|(&a, &b)| a || b).collect();
    let mut plain = meshes.clone();
    let no_maps: Vec<Option<Vec<f64>>> = vec![None; meshes.len()];
    let materials: Vec<u32> = ir.shapes.iter().map(|s| s.material).collect();
    let none = std::collections::BTreeMap::new();
    debake_gains(&mut plain, &which, &no_maps, &materials, &none, None);
    let near_black = debake_gains_with(&mut meshes, &chosen, &map_only, &maps, &materials, &none, None).len();
    let mean = |ms: &[MeshStreams], which: &[bool]| -> (f64, usize) {
        let (mut sum, mut count) = (0.0f64, 0usize);
        for (s, &w) in ms.iter().zip(which) {
            if !w {
                continue;
            }
            for i in 0..s.positions.len() {
                let g = s.gains.get(i).copied().unwrap_or(0);
                let channel = |shift: u32| 2f64.powf(f64::from((g >> shift & 255) as u8 as i8) / GAIN_STEPS);
                sum += (channel(0) + channel(8) + channel(16)) / 3.0;
                count += 1;
            }
        }
        (if count > 0 { sum / count as f64 } else { 1.0 }, count)
    };
    let (gain_c37, vertices) = mean(&plain, &which);
    let (gain_d07, _) = mean(&meshes, &which);
    let (gain_map, _) = mean(&meshes, &map_only);
    Some(DebakeReport {
        debaked: which.iter().filter(|&&w| w).count(),
        mapped: maps.iter().zip(&which).filter(|&(m, &w)| w && m.is_some()).count(),
        vertices,
        gain_c37,
        gain_d07,
        near_black,
        map_painted: map_painted.iter().filter(|&&w| w).count(),
        map_lit: map_only.iter().filter(|&&w| w).count(),
        gain_map,
    })
}

/// M5 (director d07; LIGHT-MODEL.md 10): the first map's brightness at each vertex against the mesh's own mean, read
/// from a coarse copy of the map (at most 16 texels a side, box-filtered by alpha: its broad shading, not its detail),
/// where the material's first stage with a map takes one of the mesh's own coordinates through no matrix. None where
/// there is no such map (a generated or moving coordinate, no texels) or the map lies even over the mesh: nothing to
/// add to the fit. A ratio, so one mesh's darker map than another's is not taken for light: only how a map's
/// brightness lies over its own mesh enters, and of that debake_gains keeps what follows the facing.
fn first_map_at_vertices(ir: &ModelIr, material: usize, s: &MeshStreams) -> Option<Vec<f64>> {
    let m = ir.materials.get(material)?;
    let stage = m.stages.iter().find(|stage| stage.map != 0xFF && stage.coord != 0xFF)?;
    let coord = m.coords.get(usize::from(stage.coord))?;
    // A plain coordinate of the mesh (sources 4 to 11: coordinates 0 to 7) through no matrix, of the plain kinds (0
    // and 1: 3x4 and 2x4; a bump kind's light-offset coordinate or kind 10 is not the surface's own uv: FC's review).
    if coord.matrix != NONE || !(4..=11).contains(&coord.source) || coord.kind > 1 {
        return None;
    }
    let k = usize::from(coord.source - 4);
    if !s.has_coord(k) || s.coords.len() != s.positions.len() * s.coordinates.len() {
        return None;
    }
    let texture = ir.textures.get(*m.textures.get(usize::from(stage.map))? as usize)?;
    let (width, height, texels) = super::texture::record_level0(ir.records.get(texture.record)?)?;
    let (width, height) = (width as usize, height as usize);
    if width == 0 || height == 0 || texels.len() < width * height {
        return None;
    }
    const SIDE: usize = 16;
    let (across, down) = (width.min(SIDE), height.min(SIDE));
    let mut cells = vec![[0.0f64; 2]; across * down]; // a cell's luminance by alpha, and its alpha
    for y in 0..height {
        for x in 0..width {
            let t = texels[y * width + x];
            let alpha = f64::from(t[3]) / 255.0;
            let luminance = (0.2126 * f64::from(t[0]) + 0.7152 * f64::from(t[1]) + 0.0722 * f64::from(t[2])) / 255.0;
            let cell = &mut cells[(y * down / height) * across + x * across / width];
            cell[0] += luminance * alpha;
            cell[1] += alpha;
        }
    }
    let covered: f64 = cells.iter().map(|c| c[1]).sum();
    if covered <= 0.0 {
        return None;
    }
    let all = cells.iter().map(|c| c[0]).sum::<f64>() / covered;
    let grid: Vec<f64> = cells.iter().map(|c| if c[1] > 1e-6 { c[0] / c[1] } else { all }).collect();
    // Where a coordinate falls among `n` cells by the map's wrap (0 clamp, 1 repeat, 2 mirror): two cells and the share
    // of the second.
    let place = |u: f64, mode: u8, n: usize| -> (usize, usize, f64) {
        let u = match mode {
            1 => u - u.floor(),
            2 => {
                let f = u - 2.0 * (u * 0.5).floor();
                if f > 1.0 {
                    2.0 - f
                } else {
                    f
                }
            }
            _ => u.clamp(0.0, 1.0),
        };
        let at = u * n as f64 - 0.5;
        let low = at.floor();
        let cell = |i: i64| -> usize {
            if mode == 1 {
                i.rem_euclid(n as i64) as usize
            } else {
                i.clamp(0, n as i64 - 1) as usize
            }
        };
        (cell(low as i64), cell(low as i64 + 1), at - low)
    };
    let seen: Vec<f64> = (0..s.positions.len())
        .map(|i| {
            let uv = s.coord(i, k);
            let (u, v) = (f64::from(uv[0]), f64::from(uv[1]));
            if !u.is_finite() || !v.is_finite() || u.abs() > 1e6 || v.abs() > 1e6 {
                return all;
            }
            let (x0, x1, px) = place(u, texture.wrap_s, across);
            let (y0, y1, py) = place(v, texture.wrap_t, down);
            let top = grid[y0 * across + x0] * (1.0 - px) + grid[y0 * across + x1] * px;
            let bottom = grid[y1 * across + x0] * (1.0 - px) + grid[y1 * across + x1] * px;
            top * (1.0 - py) + bottom * py
        })
        .collect();
    if seen.is_empty() {
        return None;
    }
    let mean = seen.iter().sum::<f64>() / seen.len() as f64;
    if mean <= 1e-3 {
        return None;
    }
    // An even map says nothing: under 2 % between its darkest and brightest place on the mesh.
    let (least, most) = seen.iter().fold((f64::MAX, 0.0f64), |(a, b), &x| (a.min(x), b.max(x)));
    if most - least < 0.02 * mean {
        return None;
    }
    Some(seen.iter().map(|&x| (x / mean).clamp(0.25, 4.0)).collect())
}

/// De-bake v2 (the owner's verdict A5, the director 11-12; LIGHT-MODEL.md 10): the light painted into the vertex
/// colours of the meshes `which` names, taken out by a gain a vertex and channel (r g b signed bytes of log2 x
/// GAIN_STEPS). Per vertex the light L is the luminance of its vertex colour times its first map's brightness there
/// (`maps`, first_map_at_vertices; 1 where none). Vertices of one material (`materials`, by mesh) at the same position
/// are one point; a point's L is its vertices' area-weighted mean, smoothed over its one ring. A region is the points of
/// one material joined by triangles; its Lref is the area-weighted DEBAKE_REFERENCE percentile of L. The gain Lref / L
/// lifts only, within [1, DEBAKE_CAP]. The hue that follows L in its region (each chroma channel's area-weighted
/// regression on L) moves DEBAKE_CHROMA of the way to its value at Lref; the rest of the hue stays. A region whose
/// Lref is under DEBAKE_NEAR_BLACK keeps its colours. A mesh whose every gain is 1 takes none. Returns the chosen
/// meshes painted near black all over (to list).
/// c85: `fixed` gives a material's Lref to take in place of each of its regions' own (a far twin's: its near model's;
/// a region whose own Lref is near black keeps its colours still); `references`, when given, receives each material's
/// own Lref (its points' area-weighted DEBAKE_REFERENCE percentile).
fn debake_gains(
    meshes: &mut [MeshStreams],
    which: &[bool],
    maps: &[Option<Vec<f64>>],
    materials: &[u32],
    fixed: &std::collections::BTreeMap<u32, f64>,
    references: Option<&mut std::collections::BTreeMap<u32, f64>>,
) -> Vec<usize> {
    debake_gains_with(meshes, which, &[], maps, materials, fixed, references)
}

/// debake_gains with c93's meshes painted by their map alone (`map_only`, by mesh): no vertex colour, the colour white,
/// L their map's ratio; one enters only with a ratio a vertex (map_light). Their gain multiplies what they show.
fn debake_gains_with(
    meshes: &mut [MeshStreams],
    which: &[bool],
    map_only: &[bool],
    maps: &[Option<Vec<f64>>],
    materials: &[u32],
    fixed: &std::collections::BTreeMap<u32, f64>,
    references: Option<&mut std::collections::BTreeMap<u32, f64>>,
) -> Vec<usize> {
    let painted_by_map = |mesh: usize| map_only.get(mesh).copied().unwrap_or(false);
    let colour = |c: u32| [f64::from(c >> 24), f64::from(c >> 16 & 255), f64::from(c >> 8 & 255)].map(|x| x / 255.0);
    let luma = |c: [f64; 3]| 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
    let mut near_black = Vec::new();
    // Points: the chosen meshes' vertices, welded by material and exact position.
    let mut points: std::collections::HashMap<(u32, [u32; 3]), usize> = std::collections::HashMap::new();
    let mut point_of: Vec<Vec<usize>> = Vec::with_capacity(meshes.len());
    let mut point_material: Vec<u32> = Vec::new();  // c85
    for (mesh, s) in meshes.iter().enumerate() {
        let chosen = which.get(mesh).copied().unwrap_or(false)
            && !s.positions.is_empty()
            && if painted_by_map(mesh) {
                maps.get(mesh).and_then(|m| m.as_ref()).is_some_and(|m| m.len() == s.positions.len())
            } else {
                s.attributes & MESH_COLOR0 != 0 && s.colors.len() == s.positions.len()
            };
        if !chosen {
            point_of.push(Vec::new());
            continue;
        }
        if !painted_by_map(mesh) && s.colors.iter().all(|&c| luma(colour(c)) < DEBAKE_NEAR_BLACK) {
            near_black.push(mesh);
        }
        let material = materials.get(mesh).copied().unwrap_or(u32::MAX);
        let ids = s
            .positions
            .iter()
            .map(|&p| {
                let next = points.len();
                let id = *points.entry((material, p.map(f32::to_bits))).or_insert(next);
                if id == next {
                    point_material.push(material);
                }
                id
            })
            .collect();
        point_of.push(ids);
    }
    let count = points.len();
    if count == 0 {
        return near_black;
    }
    // Each point's weight (a third of its triangles' area, its vertices'), light and colour (weighted means), and the
    // edges between points.
    let mut weight = vec![0.0f64; count];
    let mut light = vec![0.0f64; count];
    let mut rgb = vec![[0.0f64; 3]; count];
    let mut edges: Vec<(usize, usize)> = Vec::new();
    for (mesh, s) in meshes.iter().enumerate() {
        let ids = &point_of[mesh];
        if ids.is_empty() {
            continue;
        }
        let map = maps.get(mesh).and_then(|m| m.as_ref()).filter(|m| m.len() == s.positions.len());
        let by_map = painted_by_map(mesh);
        let mut area = vec![0.0f64; ids.len()];
        for t in s.indices.chunks_exact(3).chain(s.refined_indices.chunks_exact(3)) {
            let [a, b, c] = [t[0] as usize, t[1] as usize, t[2] as usize];
            if a >= ids.len() || b >= ids.len() || c >= ids.len() {
                continue;
            }
            let p = |i: usize| s.positions[i].map(f64::from);
            let (pa, pb, pc) = (p(a), p(b), p(c));
            let u = [pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]];
            let v = [pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]];
            let n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
            let third = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt() / 6.0;
            for i in [a, b, c] {
                area[i] += third;
            }
            for (x, y) in [(a, b), (b, c), (c, a)] {
                let (x, y) = (ids[x], ids[y]);
                if x != y {
                    edges.push((x.min(y), x.max(y)));
                }
            }
        }
        for (v, &p) in ids.iter().enumerate() {
            let w = area[v] + 1e-12;
            let c = (if by_map { [1.0; 3] } else { colour(s.colors[v]) }).map(|x| x * map.map_or(1.0, |m| m[v]));
            weight[p] += w;
            light[p] += w * luma(c);
            for k in 0..3 {
                rgb[p][k] += w * c[k];
            }
        }
    }
    for p in 0..count {
        light[p] /= weight[p];
        rgb[p] = rgb[p].map(|x| x / weight[p]);
    }
    edges.sort_unstable();
    edges.dedup();
    // The one ring's area-weighted light, and the regions (union-find over the edges).
    fn root(parent: &mut [usize], mut x: usize) -> usize {
        while parent[x] != x {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        x
    }
    let mut ring_weight = weight.clone();
    let mut ring_light: Vec<f64> = (0..count).map(|p| weight[p] * light[p]).collect();
    let mut parent: Vec<usize> = (0..count).collect();
    for &(a, b) in &edges {
        ring_weight[a] += weight[b];
        ring_light[a] += weight[b] * light[b];
        ring_weight[b] += weight[a];
        ring_light[b] += weight[a] * light[a];
        let (ra, rb) = (root(&mut parent, a), root(&mut parent, b));
        if ra != rb {
            parent[ra] = rb;
        }
    }
    let smooth: Vec<f64> = (0..count).map(|p| ring_light[p] / ring_weight[p]).collect();
    let region: Vec<usize> = (0..count).map(|p| root(&mut parent, p)).collect();
    let mut members: Vec<Vec<usize>> = vec![Vec::new(); count];
    for p in 0..count {
        members[region[p]].push(p);
    }
    // c85: each material's own Lref, for its far twins.
    if let Some(out) = references {
        let mut by_material: std::collections::BTreeMap<u32, Vec<usize>> = std::collections::BTreeMap::new();
        for p in 0..count {
            by_material.entry(point_material[p]).or_default().push(p);
        }
        for (material, mut m) in by_material {
            m.sort_by(|&a, &b| smooth[a].total_cmp(&smooth[b]));
            let total: f64 = m.iter().map(|&p| weight[p]).sum();
            let mut level = smooth[m[m.len() - 1]];
            let mut sum = 0.0;
            for &p in &m {
                sum += weight[p];
                if sum >= DEBAKE_REFERENCE * total {
                    level = smooth[p];
                    break;
                }
            }
            out.insert(material, level);
        }
    }
    // Each region's Lref and its hue's slope against L.
    let mut fits = vec![(0.0f64, [0.0f64; 3]); count];
    for (r, m) in members.iter_mut().enumerate() {
        if m.is_empty() {
            continue;
        }
        m.sort_by(|&a, &b| smooth[a].total_cmp(&smooth[b]));
        let total: f64 = m.iter().map(|&p| weight[p]).sum();
        let mut reference = smooth[m[m.len() - 1]];
        let mut acc = 0.0;
        for &p in m.iter() {
            acc += weight[p];
            if acc >= DEBAKE_REFERENCE * total {
                reference = smooth[p];
                break;
            }
        }
        // c85: a far twin's region takes its near material's Lref (the swap keeps the brightness).
        if reference >= DEBAKE_NEAR_BLACK {
            if let Some(&shared) = fixed.get(&point_material[m[0]]) {
                reference = shared;
            }
        }
        let (mut sw, mut sx, mut sxx) = (0.0f64, 0.0f64, 0.0f64);
        let (mut sy, mut sxy) = ([0.0f64; 3], [0.0f64; 3]);
        for &p in m.iter() {
            if light[p] < DEBAKE_NEAR_BLACK {
                continue;
            }
            let (w, x) = (weight[p], smooth[p]);
            let hue = rgb[p].map(|c| c / light[p]);
            sw += w;
            sx += w * x;
            sxx += w * x * x;
            for k in 0..3 {
                sy[k] += w * hue[k];
                sxy[k] += w * x * hue[k];
            }
        }
        let mut slope = [0.0f64; 3];
        if sw > 0.0 {
            let mean = sx / sw;
            let spread = sxx / sw - mean * mean;
            if spread > 1e-8 {
                for k in 0..3 {
                    slope[k] = (sxy[k] / sw - mean * sy[k] / sw) / spread;
                }
            }
        }
        fits[r] = (reference, slope);
    }
    for (mesh, s) in meshes.iter_mut().enumerate() {
        let ids = &point_of[mesh];
        if ids.is_empty() {
            continue;
        }
        let by_map = painted_by_map(mesh);
        let gains: Vec<u32> = ids
            .iter()
            .enumerate()
            .map(|(v, &p)| {
                let (reference, slope) = fits[region[p]];
                if reference < DEBAKE_NEAR_BLACK {
                    return 0;
                }
                let g = (reference / smooth[p].max(1e-6)).clamp(1.0, DEBAKE_CAP);
                let c = if by_map { [1.0; 3] } else { colour(s.colors[v]) };
                let l = luma(c);
                let mut gain = [g; 3];
                if l > 1e-4 {
                    let hue = c.map(|x| x / l);
                    let mut moved = [0.0f64; 3];
                    for k in 0..3 {
                        moved[k] = (hue[k] + DEBAKE_CHROMA * slope[k] * (reference - smooth[p])).max(0.0);
                    }
                    let moved_l = luma(moved);
                    if moved_l > 1e-6 {
                        for k in 0..3 {
                            if hue[k] > 1e-4 {
                                gain[k] = g * (moved[k] / moved_l) / hue[k];
                            }
                        }
                    }
                }
                let mut word = 0u32;
                for k in 0..3 {
                    let step = (gain[k].log2() * GAIN_STEPS).round().clamp(-128.0, 127.0) as i32;
                    word |= ((step as u32) & 255) << (8 * k);
                }
                word
            })
            .collect();
        if gains.iter().any(|&g| g != 0) {
            s.gains = gains;
            s.attributes |= MESH_GAIN;
        }
    }
    near_black
}

/// The least squares solution of four normal equations (Gaussian elimination, partial pivoting); a whisker added to the
/// diagonal keeps a flat mesh (every normal alike: the columns dependent) solvable. None when it still is not.
fn solve4(mut a: [[f64; 4]; 4], mut b: [f64; 4]) -> Option<[f64; 4]> {
    let trace = (0..4).map(|i| a[i][i]).sum::<f64>();
    for (i, row) in a.iter_mut().enumerate() {
        row[i] += 1e-9 * trace.max(1e-12);
    }
    for col in 0..4 {
        let pivot = (col..4).max_by(|&p, &q| a[p][col].abs().total_cmp(&a[q][col].abs()))?;
        if a[pivot][col].abs() < 1e-15 {
            return None;
        }
        a.swap(col, pivot);
        b.swap(col, pivot);
        for row in col + 1..4 {
            let f = a[row][col] / a[col][col];
            for k in col..4 {
                a[row][k] -= f * a[col][k];
            }
            b[row] -= f * b[col];
        }
    }
    let mut x = [0.0f64; 4];
    for row in (0..4).rev() {
        let rest = (row + 1..4).map(|k| a[row][k] * x[k]).sum::<f64>();
        x[row] = (b[row] - rest) / a[row][row];
    }
    Some(x)
}

/// Each material's thickness (nebula_model.h NebulaModelHit.thickness), by its meshes (the audit's; the cook measures
/// as it writes).
pub fn material_measures(ir: &ModelIr, stem: &str) -> Vec<f32> {
    let mut measures = vec![Measure::new(); ir.materials.len()];
    for shape in &ir.shapes {
        if let (Ok(streams), Some(m)) = (flatten(&ir.arrays, shape, None), measures.get_mut(shape.material as usize)) {
            m.add(&streams.positions, &streams.indices);
        }
    }
    let _ = stem;
    measures.iter().map(Measure::thickness).collect()
}

/// One NebulaModelHit (nebula_model.h), whichever kind of material the reading came from; its stages follow later,
/// at the place this returns (the record's stages_at, 0 until they are written).
fn write_hit(b: &mut Blob, r: &super::hit_record::Reading, flags: u8, stages: usize, swaps: &[[u8; 4]; 4], glow: [f32; 4]) -> usize {
    let start = b.0.len();
    b.u8(r.class as u8);
    b.u8(stages as u8);
    b.u8(flags);
    b.u8(r.glow_source);
    b.f32(r.roughness);
    b.f32(r.metal);
    b.f32(r.ior);
    b.f32(r.transmission);
    b.fs(&r.base);
    b.fs(&r.emission);
    b.f32(r.signals.environment_share);
    let stages_slot = b.0.len();
    b.u32(0);
    for table in swaps {
        for &c in table {
            b.u8(c);
        }
    }
    b.f32(r.thickness);
    b.fs(&glow);
    b.fs(&r.env_tint);
    b.u8(r.env_map);
    b.u8(0);
    b.u8(0);
    b.u8(0);
    b.f32(r.body);
    b.f32(r.shine);
    b.f32(r.tint_keep);
    b.f32(0.0);
    debug_assert_eq!(b.0.len() - start, HIT_BYTES);
    stages_slot
}

/// A model texture's level 0, as texels.
fn level0(ir: &ModelIr, texture: u32) -> Option<(u32, u32, Vec<[u8; 4]>)> {
    ir.textures.get(texture as usize).and_then(|t| ir.records.get(t.record)).and_then(|bytes| super::texture::record_level0(bytes))
}

/// What a glTF material's maps and mesh add to its factors: the metal-roughness map's mean blue and green (linear), the
/// emissive map's mean colour (sRGB texels, as linear light), and whether its mesh's colours multiply the base.
fn pbr_means(ir: &ModelIr, m: &MaterialIr, p: &super::model_ir::PbrMaterial) -> super::hit_record::PbrMeans {
    use super::model_ir::{PBR_EMISSIVE, PBR_METAL_ROUGH};
    let mut means = super::hit_record::PbrMeans { vertex_colour: m.channels[0].material_source != 0, ..Default::default() };
    let mean = |texels: &[[u8; 4]], c: usize, decode: fn(f32) -> f32| -> f32 {
        texels.iter().map(|t| f64::from(decode(f32::from(t[c]) / 255.0))).sum::<f64>() as f32 / texels.len().max(1) as f32
    };
    let linear = |v: f32| v;
    let srgb = |v: f32| if v <= 0.04045 { v / 12.92 } else { ((v + 0.055) / 1.055).powf(2.4) };
    if let Some((_, _, texels)) = level0(ir, p.maps[PBR_METAL_ROUGH]).filter(|(_, _, t)| !t.is_empty()) {
        means.roughness = mean(&texels, 1, linear);
        means.metal = mean(&texels, 2, linear);
    }
    if let Some((_, _, texels)) = level0(ir, p.maps[PBR_EMISSIVE]).filter(|(_, _, t)| !t.is_empty()) {
        means.emissive = [0, 1, 2].map(|c| mean(&texels, c, srgb));
    }
    means
}

/// A reading as the cook finishes it (hit_record::finish, then the art's numbers): the painted reflection's map gives
/// its colour and, for metal and gloss, the roughness.
pub fn finish_reading(ir: &ModelIr, stem: &str, material: usize, thickness: f32, r: &mut super::hit_record::Reading) {
    use super::hit_record::{mean_colour, roughness_of_map, Class};
    let Some(m) = ir.materials.get(material) else { return };
    if r.env_map != 0xFF {
        let record = m
            .textures
            .get(r.env_map as usize)
            .and_then(|&t| ir.textures.get(t as usize))
            .and_then(|t| ir.records.get(t.record))
            .and_then(|bytes| super::texture::record_level0(bytes));
        if let Some((w, h, texels)) = record {
            if let Some(mean) = mean_colour(&texels) {
                let tint = [r.env_tint[0] * mean[0], r.env_tint[1] * mean[1], r.env_tint[2] * mean[2]];
                let top = tint[0].max(tint[1]).max(tint[2]);
                if top > 1e-4 {
                    r.env_tint = tint.map(|v| v / top);
                }
            }
            if matches!(r.class, Class::Metal | Class::Gloss) {
                if let Some(roughness) = roughness_of_map(w, h, &texels) {
                    r.roughness = roughness;
                }
            }
        }
    }
    // c99: what it shows with its own maps (each map's level 0 mean, RGBA): a glow's colour.
    let means: Vec<Option<[f32; 4]>> = m
        .textures
        .iter()
        .map(|&t| {
            let (_, _, texels) = super::texture::record_level0(ir.records.get(ir.textures.get(t as usize)?.record)?)?;
            if texels.is_empty() {
                return None;
            }
            let mut sum = [0.0f64; 4];
            for texel in &texels {
                for c in 0..4 {
                    sum[c] += f64::from(texel[c]);
                }
            }
            let n = texels.len() as f64 * 255.0;
            Some(sum.map(|v| (v / n) as f32))
        })
        .collect();
    r.shown = super::hit_record::shown_with_maps(m, &|n| means.get(usize::from(n)).copied().flatten());
    super::hit_record::finish(stem, &m.name, thickness, r);
    // c83 (3.5): a far twin looks as its near model does.
    super::hit_record::follow_twin(stem, &m.name, r);
    // A glTF material is read from its own numbers (pbr_reading): no painted sheen, face rule or metal kind by name.
    if !ir.pbr.is_empty() {
        return;
    }
    // c77 (7.6): a character's face part (a skinned mesh's, by its name) is a plain surface and never a sheen body.
    let face = super::programs::mesh_skinned(ir, m) && super::hit_record::face_kind(&m.name) != 0;
    // c49: the body under a painted sheen mirrors for real (the sheen is not drawn while tracing).
    if let Some(super::hit_record::SheenRole::Body { kind, .. }) = super::hit_record::sheen_roles_kept(ir, stem).get(material).copied() {
        if !face {
            super::hit_record::promote_sheen_body(kind, r);
        }
    }
    // c80: a review row (an ice being's: OWNER-VERDICT G) is kept on its face too.
    if face && r.rule != "the review (rt_materials.tsv)" {
        super::hit_record::face_plain(r);
    }
    // c88: a metal's F0 by its kind, the art a tint.
    super::hit_record::metal_finish(stem, &m.name, r);
}

/// Which mesh corners the program reads comes with the generator; here a mesh holds what its source held.
fn mesh_bounds(positions: &[[f32; 3]]) -> ([f32; 3], [f32; 3]) {
    if positions.is_empty() {
        return ([0.0; 3], [0.0; 3]);
    }
    let (mut min, mut max) = ([f32::MAX; 3], [f32::MIN; 3]);
    for p in positions {
        for k in 0..3 {
            min[k] = min[k].min(p[k]);
            max[k] = max[k].max(p[k]);
        }
    }
    (min, max)
}

pub fn write(ir: &ModelIr, stem: &str, written: &Written) -> Result<Vec<u8>> {
    // The game binds a clip's tracks by these hashes (Clip::bind, ModelData::findMaterial): two names a clip could
    // tell apart must not share one, or the tracks land on the wrong joint or material with nothing said.
    for (kind, names) in [
        ("joint", ir.joints.iter().map(|j| j.name.as_str()).collect::<Vec<_>>()),
        ("material", ir.materials.iter().map(|m| m.name.as_str()).collect::<Vec<_>>()),
    ] {
        let mut seen: HashMap<u32, &str> = HashMap::new();
        for name in names {
            match seen.insert(name_hash(name), name) {
                Some(other) if other != name => {
                    bail!("{stem}: the {kind}s \"{other}\" and \"{name}\" share a name hash: a clip bound to one would drive the other")
                }
                _ => {}
            }
        }
    }
    let curved = model_curves(ir);
    let mut b = Blob(vec![0u8; HEADER_WORDS * 4]);
    let mut strings: Vec<(usize, String)> = Vec::new();
    // Joints.
    let joints_at = b.at();
    let mut first_mesh = 0u32;
    for j in &ir.joints {
        b.fs(&j.scale);
        for r in j.rotation {
            b.f32(r);
        }
        b.u32(j.parent);
        b.fs(&j.translation);
        b.f32(j.radius);
        b.fs(&j.min);
        b.fs(&j.max);
        b.u32(j.child);
        b.u32(j.younger);
        b.u32(first_mesh);
        b.u32(j.meshes.len() as u32);
        first_mesh += j.meshes.len() as u32;
        b.u8(j.kind);
        b.u8(j.scale_compensate);
        b.u8(j.billboard);
        b.u8(0);
        strings.push((b.0.len(), j.name.clone()));
        b.u32(0);
        b.u32(name_hash(&j.name));
    }
    // The walk: depth first through children, each joint left once its children are done, then its younger.
    let steps_at = b.at();
    let mut steps = 0u32;
    if !ir.joints.is_empty() {
        // Roots are the parentless joints linked as they came.
        let mut stack: Vec<(u32, bool)> = Vec::new();
        for (i, j) in ir.joints.iter().enumerate().rev() {
            if j.parent == NONE {
                stack.push((i as u32, false));
            }
        }
        while let Some((j, leaving)) = stack.pop() {
            let joint = &ir.joints[j as usize];
            if leaving {
                b.u32(j);
                b.u32(1);
                steps += 1;
                if joint.younger != NONE {
                    stack.push((joint.younger, false));
                }
            } else {
                b.u32(j);
                b.u32(0);
                steps += 1;
                stack.push((j, true));
                if joint.child != NONE {
                    stack.push((joint.child, false));
                }
            }
        }
    }
    let joint_meshes_at = b.at();
    for j in &ir.joints {
        for &m in &j.meshes {
            b.u32(m);
        }
    }
    b.align(4);
    let draw_matrices_at = b.at();
    for &(flag, index) in &ir.draw_matrices {
        b.u32(u32::from(flag));
        b.u32(index);
    }
    let envelopes_at = b.at();
    let mut first_mix = 0u32;
    for e in &ir.envelopes {
        b.u32(first_mix);
        b.u32(e.len() as u32);
        first_mix += e.len() as u32;
    }
    let mixes_at = b.at();
    for e in &ir.envelopes {
        for &(joint, weight) in e {
            b.u32(u32::from(joint));
            b.f32(weight);
        }
    }
    b.align(16);
    let inverse_binds_at = b.at();
    for m in &ir.inverse_binds {
        b.fs(m);
    }
    let important_at = b.at();
    for &i in &ir.important {
        b.u32(i);
    }
    b.align(4);
    // Meshes: records now, streams after the materials, offsets patched.
    let meshes_at = b.at();
    let mut measures = vec![Measure::new(); ir.materials.len()];
    // The painted light of the model's vertex-coloured meshes (14.6): whether its paint holds shadows of its own.
    let vertex_coloured: Vec<bool> = ir.materials.iter().map(|m| super::hit_record::read(m).signals.vertex_colour).collect();
    let mut painted = PaintedLight::default();
    // Each mesh's paint light (22.19): painted materials only (vertex colour, not lit).
    let painted_unlit: Vec<bool> = ir.materials.iter().map(|m| {
        let s = super::hit_record::read(m).signals;
        s.vertex_colour && !s.lit
    }).collect();
    let mut paint_lights: Vec<Option<(f32, [f32; 3])>> = Vec::with_capacity(ir.shapes.len());
    let mut mesh_slots = Vec::with_capacity(ir.shapes.len());
    let mut meshes = Vec::with_capacity(ir.shapes.len());
    for shape in &ir.shapes {
        let mut streams = flatten(&ir.arrays, shape, curved.as_ref())?;
        // Opaque meshes in the vertex cache's order; what blends keeps its order, which shows.
        // Fur keeps its order too: the last corner to name a position gives that position's growth.
        if ir.materials.get(shape.material as usize).is_some_and(|m| !m.name.contains("Fur") && m.blend_type == 0 && m.depth_test != 0 && m.depth_write != 0) {
            let vertices = streams.positions.len() as u32;
            mesh_order::cache_order(&mut streams.indices, vertices);
            mesh_order::cache_order(&mut streams.refined_indices, vertices);
        }
        mesh_slots.push(b.0.len());
        if let Some(m) = measures.get_mut(shape.material as usize) {
            m.add(&streams.positions, &streams.indices);
        }
        let mut paint_light = None;
        if vertex_coloured.get(shape.material as usize).copied().unwrap_or(false) {
            painted.add(&streams);
            if painted_unlit.get(shape.material as usize).copied().unwrap_or(false) {
                paint_light = painted.last;
            }
        }
        paint_lights.push(paint_light);
        let (min, max) = mesh_bounds(&streams.positions);
        b.fs(&min);
        b.fs(&max);
        b.f32(shape.radius);
        b.u32(streams.base_vertices);
        b.u32(streams.indices.len() as u32);
        for _ in 0..5 {
            b.u32(0);
        }
        b.u32(streams.attributes);
        b.u32(further_stride(&streams));
        b.u32(streams.single_matrix);
        b.u32(shape.material);
        b.u8(if streams.positions.len() > 0xFFFF { 4 } else { 2 });
        b.u8(if shape.matrix_type == 1 || shape.matrix_type == 2 { shape.matrix_type } else { 0 });
        b.u16(0);
        b.u32(streams.positions.len() as u32);
        b.u32(streams.refined_indices.len() as u32);
        b.u32(0);
        b.u32(0);
        b.u32(streams.coordinates.len() as u32);
        b.u32(0);
        meshes.push(streams);
    }
    // D2: the painted light per facing taken out of the painted meshes' vertex colours by a gain (debake_gains):
    // vertex-coloured, its colour's rgb not lit, opaque and not emissive; never a sky's. The records learn of it here.
    let sky_model = {
        let lower = stem.to_ascii_lowercase();
        lower.ends_with("sky") || lower.starts_with("vrorbit")
    };
    if !sky_model {
        let debake_material: Vec<bool> = ir
            .materials
            .iter()
            .map(|m| {
                // c72: painted colour, or a lit colour taken from the vertex colour (debakes).
                debakes(m)
            })
            .collect();
        let which: Vec<bool> = ir.shapes.iter().map(|s| debake_material.get(s.material as usize).copied().unwrap_or(false)).collect();
        // c93 (3.3): the meshes painted by a map alone whose map holds light (map_light).
        let map_painted: Vec<bool> = ir.shapes.iter().map(|s| ir.materials.get(s.material as usize).is_some_and(debakes_map)).collect();
        // M5 (director d07): each such mesh's first map as its vertices see it, for the same fit.
        let maps: Vec<Option<Vec<f64>>> = ir
            .shapes
            .iter()
            .zip(&meshes)
            .zip(which.iter().zip(&map_painted))
            .map(|((shape, streams), (&w, &w2))| {
                if w {
                    first_map_at_vertices(ir, shape.material as usize, streams)
                } else if w2 {
                    map_light(ir, shape.material as usize, streams)
                } else {
                    None
                }
            })
            .collect();
        let map_only: Vec<bool> = map_painted.iter().zip(&maps).map(|(&w, m)| w && m.is_some()).collect();
        let chosen: Vec<bool> = which.iter().zip(&map_only).map(|(&a, &b)| a || b).collect();
        let materials: Vec<u32> = ir.shapes.iter().map(|s| s.material).collect();
        // c85: a far twin's de-baked materials take their near model's Lref (a vertex colour's: c93 keeps the map
        // ratios, each mesh's own, out).
        let fixed: std::collections::BTreeMap<u32, f64> = ir
            .materials
            .iter()
            .enumerate()
            .filter(|(_, m)| debakes(m))
            .filter_map(|(i, m)| super::hit_record::twin_reference(stem, &m.name).map(|r| (i as u32, r)))
            .collect();
        debake_gains_with(&mut meshes, &chosen, &map_only, &maps, &materials, &fixed, None);  // c79: de-bake v2; c93
        for (streams, &slot) in meshes.iter().zip(&mesh_slots) {
            if streams.attributes & MESH_GAIN != 0 {
                b.patch32(slot + 56, streams.attributes);
                b.patch32(slot + 60, further_stride(streams));
            }
        }
    }
    // Each mesh's coordinates by number: coordinate 0 in stream 1, the others packed in stream 3 in ascending order.
    // Their place counts from the mesh's record, so a mesh record alone gives its layout.
    b.align(4);
    for (streams, &slot) in meshes.iter().zip(&mesh_slots) {
        b.patch32(slot + 96, b.at() - slot as u32);
        let mut further = 0u32;
        for k in 0..streams.coordinates.len() {
            let (stream, offset) = if !streams.has_coord(k) {
                (MESH_NO_COORD, 0)
            } else if k == 0 {
                (1, 16)
            } else {
                further += 8;
                (3, further - 8)
            };
            b.u8(stream);
            b.u8(0);
            b.u16(offset as u16);
        }
    }
    // Materials: fixed part, then their variable tables.
    let materials_at = b.at();
    let mut slots = Vec::with_capacity(ir.materials.len());
    for m in &ir.materials {
        slots.push(b.0.len());
        strings.push((b.0.len(), m.name.clone()));
        b.u32(0);
        b.u32(name_hash(&m.name));
        b.u32(u32::from(m.same_as));
        b.u32(m.mesh);
        b.u32(m.joint);
        b.u8(m.mode);
        b.u8(m.cull);
        let flags = u8::from(m.on_billboard) << 4 | u8::from(m.has_ambient) | u8::from(m.has_stage_colors) << 1 | u8::from(m.has_indirect) << 2 | u8::from(m.has_fog) << 3;
        b.u8(flags);
        b.u8(m.early_depth);
        b.u32(m.channel_count as u32);
        b.u32(m.coords.len() as u32);
        b.u32(m.warps.len() as u32);
        b.u32(material_slots(m).len() as u32);
        b.u32(m.matrices.len() as u32);
        b.u32(0); // its forms' count
        for c in m.material_color.iter().chain(m.ambient_color.iter()) {
            b.0.extend_from_slice(c);
        }
        for r in &m.registers {
            for &v in r {
                b.i16(v);
            }
        }
        for c in &m.constants {
            b.0.extend_from_slice(c);
        }
        for c in &m.channels {
            b.0.extend_from_slice(&[c.enable, c.material_source, c.ambient_source, c.light_mask, c.diffuse, c.attenuation]);
        }
        b.0.extend_from_slice(&[m.alpha_comp0, m.alpha_op, m.alpha_comp1, m.alpha_ref0, m.alpha_ref1, m.blend_type, m.blend_src, m.blend_dst, m.depth_test, m.depth_func, m.depth_write, m.has_scale]);
        b.fs(&m.normal_scale);
        let f = &m.fog;
        b.0.extend_from_slice(&[f.kind, 0, 0, 0]);
        b.fs(&[f.start, f.end, f.near, f.far]);
        b.0.extend_from_slice(&f.color);
        b.u32(pipeline_state(m));
        for _ in 0..5 {
            b.u32(0);
        }
    }
    let mut material_forms: Vec<Vec<(u64, u64, Vec<u8>)>> = vec![Vec::new(); ir.materials.len()];
    for (index, (m, &slot)) in ir.materials.iter().zip(&slots).enumerate() {
        let tail = slot + MATERIAL_BYTES - 20;
        b.align(4);
        let coords_at = b.at();
        for c in &m.coords {
            b.0.extend_from_slice(&[c.kind, c.source, 0, 0]);
            b.u32(c.matrix);
        }
        let slots_at = b.at();
        for (texture, scale_map, uses) in material_slots(m) {
            b.u32(texture);
            b.u16(scale_map);
            b.u8(uses);
            b.u8(0);
        }
        let matrices_at = b.at();
        for t in &m.matrices {
            b.u32(u32::from(t.slot));
            b.0.extend_from_slice(&[t.projection, t.info, 0, 0]);
            b.fs(&t.center);
            b.fs(&t.scale);
            b.f32(t.rotation);
            b.fs(&t.translation);
            for row in &t.effect {
                b.fs(row);
            }
        }
        let warps_at = b.at();
        for w in &m.warps {
            b.fs(&w.rows[0]);
            b.fs(&w.rows[1]);
        }
        // Its forms: the records, then what they name.
        let skinned = programs::mesh_skinned(ir, m);
        let registers = written.registers(m);
        let forms = programs::forms_of(stem, m, index)?;
        // A form starts with its two u64 program hashes (NebulaModelForm): 8-aligned in the file, which the store
        // places 32-aligned (cook ALIGN), so 8-aligned wherever the game reads it.
        b.align(8);
        let forms_at = b.at();
        let form_start = b.0.len();
        b.0.resize(form_start + forms.len() * FORM_BYTES, 0);
        for (n, form) in forms.iter().enumerate() {
            let mut t = programs::generate(m, *form, skinned, &registers)?;
            programs::finish_text(&m.name, *form, &mut t);
            let vertex_rows_at = b.at();
            for &r in &t.vertex_rows {
                b.u32(r);
            }
            let pixel_rows_at = b.at();
            for &r in &t.pixel_rows {
                b.u32(r);
            }
            let textures_at = b.at();
            b.0.extend_from_slice(&t.textures);
            let inputs_at = b.at();
            for input in &t.inputs {
                b.0.push(programs::input_code(input));
            }
            b.align(4);
            let at = form_start + n * FORM_BYTES;
            let (vertex, pixel) = (programs::text_hash(&t.vertex), programs::text_hash(&t.pixel));
            material_forms[index].push((vertex, pixel, t.inputs.iter().map(programs::input_code).collect()));
            b.0[at..at + 8].copy_from_slice(&vertex.to_le_bytes());
            b.0[at + 8..at + 16].copy_from_slice(&pixel.to_le_bytes());
            b.patch32(at + 16, vertex_rows_at);
            b.patch32(at + 20, pixel_rows_at);
            b.patch32(at + 24, textures_at);
            b.patch32(at + 28, inputs_at);
            b.patch32(at + 32, t.vertex_rows.len() as u32);
            b.patch32(at + 36, t.pixel_rows.len() as u32);
            b.patch32(at + 40, t.textures.len() as u32);
            b.patch32(at + 44, t.inputs.len() as u32);
            let flags = u8::from(form.normal_coord) | u8::from(form.lit) << 1 | u8::from(t.alpha_test) << 2 | u8::from(form.fog_linear) << 3;
            b.0[at + 48] = form.replace;
            b.0[at + 49] = flags;
            b.0[at + 50] = form.pass;
        }
        b.patch32(slot + 44, forms.len() as u32);
        b.patch32(tail, coords_at);
        b.patch32(tail + 4, slots_at);
        b.patch32(tail + 8, matrices_at);
        b.patch32(tail + 12, warps_at);
        b.patch32(tail + 16, forms_at);
    }
    b.align(4);
    let textures_at = b.at();
    let mut texture_slots = Vec::with_capacity(ir.textures.len());
    for t in &ir.textures {
        texture_slots.push(b.0.len());
        strings.push((b.0.len(), t.name.clone()));
        b.u32(0);
        b.u32(0);
        b.u32(t.width);
        b.u32(t.height);
        b.0.extend_from_slice(&[t.format, t.wrap_s, t.wrap_t, t.mipmap, t.anisotropy, t.min_filter, t.mag_filter, 0]);
        b.u32(t.levels);
        b.fs(&[t.min_lod, t.max_lod, t.lod_bias]);
    }
    // Names.
    let mut placed: HashMap<String, u32> = HashMap::new();
    for (slot, text) in &strings {
        let at = match placed.get(text) {
            Some(&at) => at,
            None => {
                let at = b.at();
                b.0.extend(text.chars().map(|c| c as u32 as u8));
                b.u8(0);
                placed.insert(text.clone(), at);
                at
            }
        };
        b.patch32(*slot, at);
    }
    for (shape, streams) in ir.shapes.iter().zip(&meshes) {
        let Some(forms) = material_forms.get(shape.material as usize) else {
            continue;
        };
        let state = pipeline_state(&ir.materials[shape.material as usize]);
        for (vertex, pixel, inputs) in forms {
            pipeline_list::add_form(*vertex, *pixel, state, inputs, streams);
        }
    }
    // Geometry, each stream on a 32-byte boundary.
    b.align(32);
    let geometry_at = b.at();
    for (streams, &slot) in meshes.iter().zip(&mesh_slots) {
        b.align(32);
        let positions_at = b.at();
        for p in &streams.positions {
            b.fs(p);
        }
        b.align(32);
        let attributes_at = b.at();
        for i in 0..streams.positions.len() {
            b.fs(&streams.normals[i]);
            b.u32(streams.colors[i].swap_bytes());
            b.fs(&if streams.has_coord(0) { streams.coord(i, 0) } else { [0.0; 2] });
        }
        let mut matrices_at = 0;
        if streams.single_matrix == NONE {
            b.align(32);
            matrices_at = b.at();
            for &m in &streams.matrices {
                b.u32(m);
            }
        }
        let mut further_at = 0;
        if further_stride(streams) != 0 {
            b.align(32);
            further_at = b.at();
            for i in 0..streams.positions.len() {
                for k in 1..streams.coordinates.len() {
                    if streams.has_coord(k) {
                        b.fs(&streams.coord(i, k));
                    }
                }
                if streams.attributes & MESH_COLOR1 != 0 {
                    b.u32(streams.colors1[i].swap_bytes());
                }
                if streams.attributes & MESH_TANGENTS != 0 {
                    b.fs(&streams.tangents[i][0]);
                    b.fs(&streams.tangents[i][1]);
                }
                if streams.attributes & MESH_GAIN != 0 {
                    b.u32(streams.gains[i]);
                }
            }
        }
        b.align(32);
        let indices_at = b.at();
        let wide = streams.positions.len() > 0xFFFF;
        let write_indices = |b: &mut Blob, indices: &[u32]| {
            for &i in indices {
                if wide {
                    b.u32(i);
                } else {
                    b.u16(i as u16);
                }
            }
        };
        write_indices(&mut b, &streams.indices);
        let mut refined_at = 0;
        if !streams.refined_indices.is_empty() {
            b.align(32);
            refined_at = b.at();
            write_indices(&mut b, &streams.refined_indices);
        }
        let mut flats_at = 0;
        if streams.flats.iter().any(|&f| f) {
            b.align(4);
            flats_at = b.at();
            let mut word = 0u32;
            for (i, &f) in streams.flats.iter().enumerate() {
                word |= u32::from(f) << (i % 32);
                if i % 32 == 31 {
                    b.u32(word);
                    word = 0;
                }
            }
            if streams.flats.len() % 32 != 0 {
                b.u32(word);
            }
        }
        b.patch32(slot + 84, refined_at);
        b.patch32(slot + 88, flats_at);
        let fields = slot + 36;
        b.patch32(fields, positions_at);
        b.patch32(fields + 4, attributes_at);
        b.patch32(fields + 8, matrices_at);
        b.patch32(fields + 12, further_at);
        b.patch32(fields + 16, indices_at);
    }
    b.align(32);
    let geometry_size = b.at() - geometry_at;
    // Texture records.
    // Each named for the first texture of the model that shows it, `<model stem>/<texture>` (texture::name_record).
    let mut record_at = Vec::with_capacity(ir.records.len());
    for (i, r) in ir.records.iter().enumerate() {
        b.align(32);
        record_at.push(b.at());
        let start = b.0.len();
        b.0.extend_from_slice(r);
        if let (Some(&palette), Some(t)) = (ir.record_palettes.get(i), ir.textures.iter().find(|t| t.record == i)) {
            super::texture::name_record(&mut b.0[start..], &format!("{stem}/{}", t.name), palette);
        }
    }
    for (t, &slot) in ir.textures.iter().zip(&texture_slots) {
        let at = record_at[t.record];
        b.patch32(slot + 4, at);
    }
    // What a ray reads of each material (nebula_model.h NebulaModelHit), then every material's stages.
    b.align(4);
    let hits_at = b.at();
    let mut stage_slots = Vec::new();
    let paint_holds_shadows = painted.holds_shadows();
    for (i, m) in ir.materials.iter().enumerate() {
        // A glTF material says its numbers itself (MOD-MATERIALS-FC.md 2); the game's are read from their stages.
        let (mut r, stages) = match ir.pbr.get(i) {
            Some(p) => (super::hit_record::pbr_reading(p, &pbr_means(ir, m, p)), 0),
            None => (super::hit_record::read(m), (m.pixel_stage_count as usize).min(m.stages.len())),
        };
        finish_reading(ir, stem, i, measures[i].thickness(), &mut r);
        let s = &r.signals;
        let flags = u8::from(s.lit)
            | u8::from(s.two_sided) << 1
            | u8::from(s.alpha_test) << 2
            | u8::from(s.vertex_colour) << 3
            | u8::from(s.environment) << 4
            | u8::from(super::hit_record::drawn_shadow(stem, &m.name, r.class)) << 5
            | u8::from(paint_holds_shadows && s.vertex_colour && !s.lit) << 6
            | u8::from(ir.pbr.is_empty() && matches!(super::hit_record::sheen_roles_kept(ir, stem).get(i),
                                Some(super::hit_record::SheenRole::Sheen { body: Some(_) }))) << 7;
        let glow = super::hit_record::glow_of(stem, &m.name, &r);
        stage_slots.push((write_hit(&mut b, &r, flags, stages, &m.swaps, glow), stages));
    }
    for (m, &(slot, stages)) in ir.materials.iter().zip(&stage_slots) {
        if stages == 0 {
            continue;
        }
        let at = b.at();
        b.patch32(slot, at);
        for s in &m.stages[..stages] {
            b.u8(s.coord);
            b.u8(s.map);
            b.u8(s.channel);
            for &v in &s.color_in {
                b.u8(v);
            }
            b.u8(s.color_op);
            b.u8(s.color_bias);
            b.u8(s.color_scale);
            b.u8(s.color_clamp);
            b.u8(s.color_out);
            for &v in &s.alpha_in {
                b.u8(v);
            }
            b.u8(s.alpha_op);
            b.u8(s.alpha_bias);
            b.u8(s.alpha_scale);
            b.u8(s.alpha_clamp);
            b.u8(s.alpha_out);
            b.u8(s.raster_swap);
            b.u8(s.texture_swap);
            b.u8(s.color_constant);
            b.u8(s.alpha_constant);
            for &v in &s.warp {
                b.u8(v);
            }
            b.u8(0);
            b.u8(0);
        }
    }
    // Each mesh's paint light (nebula_model.h NebulaModelPaintLight), where any mesh's paint is one light.
    b.align(4);
    let paint_lights_at = if paint_lights.iter().any(Option::is_some) {
        let at = b.at();
        for light in &paint_lights {
            let (share, d) = light.unwrap_or((0.0, [0.0, 0.0, 1.0]));
            let [x, y] = octahedral(d);
            b.f32(share);
            b.i16(x);
            b.i16(y);
        }
        at
    } else {
        0
    };
    // c86 (PLAN-LIGHTS L2): each glowing mesh's light as shapes (nebula_model.h NebulaModelGlowSpan, NebulaModelGlowShape).
    let glowing: Vec<bool> = ir
        .materials
        .iter()
        .enumerate()
        .map(|(i, m)| {
            let mut r = super::hit_record::read(m);
            finish_reading(ir, stem, i, 0.0, &mut r);
            super::hit_record::glow_of(stem, &m.name, &r)[3] != 0.0
        })
        .collect();
    let shapes: Vec<Vec<GlowShape>> = ir
        .shapes
        .iter()
        .zip(&meshes)
        .map(|(shape, streams)| {
            let material = shape.material as usize;
            if !glowing.get(material).copied().unwrap_or(false) {
                return Vec::new();
            }
            let two_sided = ir.materials.get(material).is_some_and(|m| m.cull == 0);
            glow_shapes(streams, two_sided)
        })
        .collect();
    b.align(4);
    let glow_shapes_at = if shapes.iter().any(|v| !v.is_empty()) {
        let at = b.at();
        let mut first = 0u32;
        for v in &shapes {
            b.u32(if v.is_empty() { 0 } else { first });
            b.u32(v.len() as u32);
            first += v.len() as u32;
        }
        for g in shapes.iter().flatten() {
            b.fs(&g.centre);
            b.fs(&g.axis);
            b.fs(&g.normal);
            b.fs(&g.extent);
            b.f32(g.area);
            b.u8(g.kind);
            b.u8(u8::from(g.two_sided));
            b.u16(0);
        }
        at
    } else {
        0
    };
    b.align(32);
    let size = b.at();
    let mut flags = u32::from(ir.scale_mode);
    if ir.has_billboard {
        flags |= 1 << 4;
    }
    if ir.materials_only {
        flags |= 1 << 5;
    }
    let header: [u32; HEADER_WORDS] = [
        MAGIC,
        VERSION,
        size,
        flags,
        ir.joints.len() as u32,
        steps,
        first_mesh,
        ir.draw_matrices.len() as u32,
        u32::from(ir.full_weight_count),
        ir.envelopes.len() as u32,
        first_mix,
        ir.shapes.len() as u32,
        ir.materials.len() as u32,
        ir.textures.len() as u32,
        ir.source_vertex_count,
        joints_at,
        steps_at,
        joint_meshes_at,
        draw_matrices_at,
        envelopes_at,
        mixes_at,
        inverse_binds_at,
        important_at,
        meshes_at,
        materials_at,
        textures_at,
        geometry_at,
        geometry_size,
        hits_at,
        paint_lights_at,
        glow_shapes_at,
    ];
    for (i, &v) in header.iter().enumerate() {
        b.patch32(i * 4, v);
    }
    Ok(b.0)
}

/// A cooked model's names, in its order: what a mod's model made like it, or in its place, is matched by.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct ModelNames {
    pub joints: Vec<String>,
    pub materials: Vec<String>,
    pub textures: Vec<String>,
}

/// The names `write` placed in a cooked model: each record's name word is the offset of its text (a byte a character).
pub fn names(bytes: &[u8]) -> Result<ModelNames> {
    let word = |at: usize| -> Result<u32> {
        let four = bytes.get(at..at + 4).context("a model's word past its end")?;
        Ok(u32::from_le_bytes([four[0], four[1], four[2], four[3]]))
    };
    if word(0)? != MAGIC {
        bail!("not a cooked model");
    }
    if word(4)? != VERSION {
        bail!("a cooked model of version {}, not {VERSION}", word(4)?);
    }
    let list = |count: usize, at: usize, stride: usize, name_word: usize| -> Result<Vec<String>> {
        (0..count)
            .map(|i| {
                let text = word(at + i * stride + name_word)? as usize;
                let rest = bytes.get(text..).context("a name past the model's end")?;
                let end = rest.iter().position(|&c| c == 0).context("a name with no end")?;
                Ok(rest[..end].iter().map(|&c| char::from(c)).collect())
            })
            .collect()
    };
    let header = |i: usize| -> Result<usize> { Ok(word(i * 4)? as usize) };
    Ok(ModelNames {
        joints: list(header(4)?, header(15)?, JOINT_BYTES, JOINT_BYTES - 8)?,
        materials: list(header(12)?, header(24)?, MATERIAL_BYTES, 0)?,
        textures: list(header(13)?, header(25)?, TEXTURE_BYTES, 0)?,
    })
}

pub fn further_stride(s: &MeshStreams) -> u32 {
    let mut stride = 0u32;
    for k in 1..s.coordinates.len() {
        if s.has_coord(k) {
            stride += 8;
        }
    }
    if s.attributes & MESH_COLOR1 != 0 {
        stride += 4;
    }
    if s.attributes & MESH_TANGENTS != 0 {
        stride += 24;
    }
    if s.attributes & MESH_GAIN != 0 {
        stride += 4;
    }
    stride
}

#[cfg(test)]
mod tests {
    use super::*;
    /// A ball of 24 x 12 vertices joined by triangles, each vertex coloured by `paint` of its normal.
    fn ball(paint: impl Fn([f32; 3]) -> [f32; 3]) -> MeshStreams {
        let mut s = MeshStreams { attributes: MESH_NORMAL | MESH_COLOR0, ..Default::default() };
        for i in 0..24 {
            for j in 0..12 {
                let (around, down) = (i as f32 / 24.0 * std::f32::consts::TAU, (j as f32 + 0.5) / 12.0 * std::f32::consts::PI);
                let n = [down.sin() * around.cos(), down.cos(), down.sin() * around.sin()];
                let q = paint(n).map(|x| (x * 255.0).round().clamp(0.0, 255.0) as u32);
                s.positions.push(n);
                s.normals.push(n);
                s.colors.push(q[0] << 24 | q[1] << 16 | q[2] << 8 | 255);
            }
        }
        for i in 0..24u32 {
            for j in 0..11u32 {
                let (a, b) = (i * 12 + j, (i + 1) % 24 * 12 + j);
                s.indices.extend([a, b, a + 1, b, b + 1, a + 1]);
            }
        }
        s
    }
    fn gained(c: u32, g: u32, k: u32) -> f64 {
        f64::from(c >> (24 - 8 * k) & 255) / 255.0 * 2f64.powf(f64::from((g >> (8 * k) & 255) as u8 as i8) / GAIN_STEPS)
    }
    fn spread(v: &[f64]) -> f64 {
        v.iter().cloned().fold(0.0, f64::max) / v.iter().cloned().fold(f64::MAX, f64::min)
    }
    #[test]
    fn debake_gains_lift_the_painted_shade_to_the_region_light() {
        // A grey ball painted 0.3 + 0.6 max(n.y, 0): the shade lifts to the region's light (its 90th percentile), the
        // lit top keeps its colour (lifts only), alike in every channel; a mesh not chosen takes none.
        let s = ball(|n| [0.3 + 0.6 * n[1].max(0.0); 3]);
        let mut meshes = vec![s, MeshStreams::default()];
        let black = debake_gains(&mut meshes, &[true, false], &[None, None], &[0, 1], &Default::default(), None);
        assert!(black.is_empty());
        let s = &meshes[0];
        assert!(s.attributes & MESH_GAIN != 0 && s.gains.len() == s.positions.len());
        assert!(meshes[1].attributes & MESH_GAIN == 0 && meshes[1].gains.is_empty());
        assert!(s.gains.iter().all(|&g| g & 255 == g >> 8 & 255 && g & 255 == g >> 16 & 255));
        assert!(s.gains.iter().all(|&g| (g & 255) as u8 as i8 >= 0), "lifts only");
        let before: Vec<f64> = s.colors.iter().map(|&c| f64::from(c >> 24) / 255.0).collect();
        let after: Vec<f64> = s.colors.iter().zip(&s.gains).map(|(&c, &g)| gained(c, g, 0)).collect();
        assert!(spread(&before) > 2.5 && spread(&after) < 1.4, "{} {}", spread(&before), spread(&after));
    }
    #[test]
    fn debake_gains_take_out_a_light_painted_into_the_map() {
        // The same ball, its vertex colours an even grey and the light in its first map's brightness at each vertex
        // (M5): the gains take it out as they take a vertex colour's.
        let s = ball(|_| [0.6; 3]);
        let map: Vec<f64> = s.normals.iter().map(|n| f64::from(0.3 + 0.6 * n[1].max(0.0)) / 0.6).collect();
        let mut meshes = vec![s];
        debake_gains(&mut meshes, &[true], &[Some(map.clone())], &[0], &Default::default(), None);
        let s = &meshes[0];
        assert!(s.attributes & MESH_GAIN != 0 && s.gains.len() == s.positions.len());
        let after: Vec<f64> = map.iter().zip(&s.gains).map(|(&m, &g)| m * 2f64.powf(f64::from(g as u8 as i8) / GAIN_STEPS)).collect();
        assert!(spread(&map) > 2.5 && spread(&after) < 1.4, "{} {}", spread(&map), spread(&after));
    }
    #[test]
    fn debake_gains_take_out_a_light_in_the_map_alone() {
        // c93: the ball with no vertex colour, the light 0.3 + 0.6 max(n.y, 0) in its first map alone: it follows the
        // facing (map_follows_facing), and fitted with the colour white the gains take it out, grey; a map-only mesh
        // with no ratio takes none, and a pattern that does not follow the facing is not light.
        let mut s = ball(|_| [0.5; 3]);
        s.attributes = MESH_NORMAL;
        s.colors.clear();
        let map: Vec<f64> = s.normals.iter().map(|n| f64::from(0.3 + 0.6 * n[1].max(0.0)) / 0.6).collect();
        assert!(map_follows_facing(&s, &map));
        let checker: Vec<f64> = (0..map.len()).map(|i| if (i / 12 + i % 12) % 2 == 0 { 0.5 } else { 1.5 }).collect();
        assert!(!map_follows_facing(&s, &checker));
        let even = vec![1.0; map.len()];
        assert!(!map_follows_facing(&s, &even));
        let mut meshes = vec![s.clone(), s];
        debake_gains_with(&mut meshes, &[true, true], &[true, true], &[Some(map.clone()), None], &[0, 1], &Default::default(), None);
        assert!(meshes[0].attributes & MESH_GAIN != 0 && meshes[0].gains.len() == meshes[0].positions.len());
        assert!(meshes[1].attributes & MESH_GAIN == 0 && meshes[1].gains.is_empty());
        let g = &meshes[0].gains;
        assert!(g.iter().all(|&g| g & 255 == g >> 8 & 255 && g & 255 == g >> 16 & 255), "grey");
        let after: Vec<f64> = map.iter().zip(g).map(|(&m, &g)| m * 2f64.powf(f64::from(g as u8 as i8) / GAIN_STEPS)).collect();
        assert!(spread(&map) > 2.5 && spread(&after) < 1.4, "{} {}", spread(&map), spread(&after));
    }
    #[test]
    fn debake_gains_move_the_shade_hue_halfway_to_the_lit_hue() {
        // Painted shade bluer than the lit side (sky in the shade): its blue over red moves about half way to the lit
        // side's, its light to the region's.
        let s = ball(|n| {
            let t = n[1].max(0.0);
            [0.2 + 0.7 * t, 0.2 + 0.7 * t, 0.4 + 0.5 * t]
        });
        let mut meshes = vec![s];
        debake_gains(&mut meshes, &[true], &[None], &[0], &Default::default(), None);
        let s = &meshes[0];
        let ratio = |c: u32, g: u32| gained(c, g, 2) / gained(c, g, 0);
        let (dark, lit) = (s.positions.iter().position(|p| p[1] < -0.9).unwrap(), s.positions.iter().position(|p| p[1] > 0.95).unwrap());
        let (before, lit_ratio) = (ratio(s.colors[dark], 0), ratio(s.colors[lit], 0));
        let after = ratio(s.colors[dark], s.gains[dark]);
        assert!(before > 1.8 && lit_ratio < 1.1, "{before} {lit_ratio}");
        assert!(after < before - 0.25 * (before - lit_ratio) && after > lit_ratio, "{before} {after} {lit_ratio}");
    }
    #[test]
    fn debake_gains_keep_each_region_and_list_near_black() {
        // Two balls of one material, apart: the dark one is its own region (its own light), so it is not lifted to the
        // bright one's; a mesh painted black all over is listed and keeps its colours.
        let bright = ball(|_| [0.8; 3]);
        let mut dark = ball(|_| [0.3; 3]);
        for p in dark.positions.iter_mut() {
            p[0] += 10.0;
        }
        let mut black = ball(|_| [0.01; 3]);
        for p in black.positions.iter_mut() {
            p[0] += 20.0;
        }
        let mut meshes = vec![bright, dark, black];
        let listed = debake_gains(&mut meshes, &[true, true, true], &[None, None, None], &[0, 0, 0], &Default::default(), None);
        assert_eq!(listed, vec![2]);
        assert!(meshes[1].gains.iter().all(|&g| g == 0), "the dark ball keeps its own light");
        assert!(meshes[2].attributes & MESH_GAIN == 0);
    }
    #[test]
    fn a_glowing_mesh_is_cut_into_capsules_discs_and_spheres() {
        // c86 (L2): a cable 20 m long and 0.2 m thick is capsules along it, each at most 6 m; a lit floor 12 m square
        // is discs facing up; a ball of 1 m is one sphere.
        let mut cable = MeshStreams::default();
        for i in 0..=40u32 {
            let x = i as f32 * 50.0;
            for (y, z) in [(0.0, 0.0), (20.0, 0.0), (0.0, 20.0)] {
                cable.positions.push([x, y, z]);
            }
            if i < 40 {
                let a = i * 3;
                for (p, q) in [(0, 1), (1, 2), (2, 0)] {
                    cable.indices.extend([a + p, a + 3 + p, a + q, a + q, a + 3 + p, a + 3 + q]);
                }
            }
        }
        let pieces = glow_shapes(&cable, false);
        assert!(pieces.len() >= 4 && pieces.iter().all(|g| g.kind == GLOW_CAPSULE && 2.0 * g.extent[0] <= 600.5), "{pieces:?}");
        assert!(pieces.iter().all(|g| g.axis[0].abs() > 0.99));
        let mut floor = MeshStreams::default();
        for i in 0..=12u32 {
            for j in 0..=12u32 {
                floor.positions.push([i as f32 * 100.0, 0.0, j as f32 * 100.0]);
            }
        }
        for i in 0..12u32 {
            for j in 0..12u32 {
                let (a, b) = (i * 13 + j, (i + 1) * 13 + j);
                floor.indices.extend([a, a + 1, b, b, a + 1, b + 1]);
            }
        }
        let discs = glow_shapes(&floor, true);
        assert!(discs.len() >= 4 && discs.iter().all(|g| g.kind == GLOW_DISC && g.normal[1].abs() > 0.99 && g.two_sided), "{discs:?}");
        let area: f32 = discs.iter().map(|g| g.area).sum();
        assert!((area - 1200.0 * 1200.0).abs() < 1.0, "{area}");
        let mut ball = ball(|_| [1.0; 3]);
        for p in ball.positions.iter_mut() {
            *p = p.map(|x| x * 50.0);
        }
        let one = glow_shapes(&ball, false);
        assert!(one.len() == 1 && one[0].kind == GLOW_SPHERE && (one[0].extent[0] - 50.0).abs() < 2.0, "{one:?}");
    }

    #[test]
    fn a_far_twin_takes_its_near_models_light_level() {
        // c85 (3.5): an even ball painted 0.3 keeps its colour on its own (Lref 0.3); given its near model's Lref 0.6 it
        // lifts to it; a material's own Lref is reported.
        let mut meshes = vec![ball(|_| [0.3; 3])];
        let mut own = std::collections::BTreeMap::new();
        debake_gains(&mut meshes, &[true], &[None], &[0], &Default::default(), Some(&mut own));
        assert!(meshes[0].gains.iter().all(|&g| g == 0));
        assert!((own[&0] - 76.0 / 255.0).abs() < 0.01, "{own:?}");
        let mut far = vec![ball(|_| [0.3; 3])];
        let fixed: std::collections::BTreeMap<u32, f64> = [(0u32, 0.6f64)].into_iter().collect();
        debake_gains(&mut far, &[true], &[None], &[0], &fixed, None);
        let lifted: Vec<f64> = far[0].colors.iter().zip(&far[0].gains).map(|(&c, &g)| gained(c, g, 0)).collect();
        assert!(far[0].gains.len() == far[0].colors.len() && lifted.iter().all(|&x| (x - 0.6).abs() < 0.03), "{:?}", &lifted[..4]);
    }
    #[test]
    fn mesh_index_width_changes_above_65535_and_wide_matrix_ids_survive() {
        for count in [65535usize, 65536] {
            let mut ir = ModelIr::default();
            ir.arrays.positions = (0..count).map(|n| [n as f32, 0.0, 0.0]).collect();
            ir.source_vertex_count = count as u32;
            let corners = (0..count).map(|n| Corner { position: n as u32, normal: 0, tangent: 0, binormal: 0, color: [0;2], coord: Vec::new(), matrix: 0, texture_matrix: Vec::new() }).collect();
            ir.shapes.push(ShapeIr {
                groups: vec![GroupIr { attributes: 0, coordinates: Vec::new(), corners, indices: (0..(count as u32 + (3-count as u32%3)%3)).map(|i| i % count as u32).collect(), use_table: vec![70000], places: count as u32 }],
                material: NONE, matrix_type: 0, radius: 0.0, min: [0.0;3], max: [0.0;3]
            });
            let bytes = write(&ir, "wide", &Written::unknown()).unwrap();
            let word = |at| u32::from_le_bytes(bytes[at..at+4].try_into().unwrap());
            let mesh = word(23 * 4) as usize;
            assert_eq!(bytes[mesh + 72], if count > 65535 {4} else {2});
            assert_eq!(word(mesh + 64), 70000);
            let indices = word(mesh + 52) as usize;
            if count > 65535 { assert_eq!((0..word(mesh+32) as usize).map(|i| word(indices+i*4)).max(), Some((count-1) as u32)); }
        }
    }
}
