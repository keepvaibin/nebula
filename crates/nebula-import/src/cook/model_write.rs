//! ModelIr to the flat block native/include/nebula_model.h describes.

use super::curves::{self, Curves, Vertex};
use super::mesh_order;
use super::model_ir::*;
use super::pipeline_list;
use super::programs::{self, Written};
use anyhow::{bail, Context, Result};
use std::collections::HashMap;

pub const MAGIC: u32 = 0x4C44_4D4E;
pub const VERSION: u32 = 8;
pub const HEADER_WORDS: usize = 30;
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
    for order in &m.warp_orders {
        if order[0] != 255 {
            out[usize::from(order[0])].2 |= SLOT_USES_COORD;
        }
    }
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
    /// With MESH_GAIN: each vertex's de-light gain, r g b signed bytes of log2 x 64 (debake_gains).
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

/// The least squares solution of four normal equations (Gaussian elimination, partial pivoting); a whisker added to the
/// diagonal keeps a flat mesh (every normal alike: the columns dependent) solvable. None when it still is not.
/// The nine real spherical harmonics (bands 0-2) at unit direction n (the basis of October's de-baking).
fn sh9(n: [f64; 3]) -> [f64; 9] {
    [
        0.282095,
        0.488603 * n[1],
        0.488603 * n[2],
        0.488603 * n[0],
        1.092548 * n[0] * n[1],
        1.092548 * n[1] * n[2],
        0.315392 * (3.0 * n[2] * n[2] - 1.0),
        1.092548 * n[0] * n[2],
        0.546274 * (n[0] * n[0] - n[1] * n[1]),
    ]
}

/// A mesh's unit vertex normals: its own where it has them, else its triangles' (area-weighted); None where neither.
fn unit_normals(s: &MeshStreams) -> Vec<Option<[f64; 3]>> {
    let count = s.positions.len();
    let mut sums = vec![[0.0f64; 3]; count];
    if s.attributes & MESH_NORMAL != 0 && s.normals.len() == count {
        for (sum, n) in sums.iter_mut().zip(&s.normals) {
            *sum = n.map(f64::from);
        }
    } else {
        for t in s.indices.chunks_exact(3) {
            let [a, b, c] = [t[0] as usize, t[1] as usize, t[2] as usize];
            if a >= count || b >= count || c >= count {
                continue;
            }
            let p = |i: usize| s.positions[i].map(f64::from);
            let (pa, pb, pc) = (p(a), p(b), p(c));
            let u = [pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]];
            let v = [pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]];
            let n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
            for i in [a, b, c] {
                for k in 0..3 {
                    sums[i][k] += n[k];
                }
            }
        }
    }
    sums.into_iter()
        .map(|n| {
            let length = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt();
            (length > 1e-6).then(|| [n[0] / length, n[1] / length, n[2] / length])
        })
        .collect()
}

/// c55 (census; d07's question 3): a model's de-light with c37 alone and with d07's map ratios.
pub struct DebakeReport {
    pub debaked: usize,
    pub mapped: usize,
    pub vertices: usize,
    pub gain_c37: f64,
    pub gain_d07: f64,
}

/// The de-light of a model as write() makes it (the same meshes chosen), with and without the maps: None for a sky
/// model, one with no painted mesh, or one whose meshes do not flatten.
pub fn debake_report(ir: &ModelIr, stem: &str) -> Option<DebakeReport> {
    let lower = stem.split('.').next().unwrap_or(stem).to_ascii_lowercase();
    if lower.ends_with("sky") || lower.starts_with("vrorbit") {
        return None;
    }
    let debake_material: Vec<bool> = ir
        .materials
        .iter()
        .map(|m| {
            let rgb_lit = (0..(m.channel_count as usize).min(2)).any(|ch| m.channels[2 * ch].enable != 0 && m.channels[2 * ch].light_mask != 0);
            let r = super::hit_record::read(m);
            r.signals.vertex_colour && !rgb_lit && r.signals.blend == 0 && r.class != super::hit_record::Class::Emissive
        })
        .collect();
    let which: Vec<bool> = ir.shapes.iter().map(|s| debake_material.get(s.material as usize).copied().unwrap_or(false)).collect();
    if !which.iter().any(|&w| w) {
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
        .zip(&which)
        .map(|((shape, streams), &w)| if w { first_map_at_vertices(ir, shape.material as usize, streams) } else { None })
        .collect();
    let mut plain = meshes.clone();
    let no_maps: Vec<Option<Vec<f64>>> = vec![None; meshes.len()];
    debake_gains(&mut plain, &which, &no_maps);
    debake_gains(&mut meshes, &which, &maps);
    let mean = |ms: &[MeshStreams]| -> (f64, usize) {
        let (mut sum, mut count) = (0.0f64, 0usize);
        for (s, &w) in ms.iter().zip(&which) {
            if !w {
                continue;
            }
            for i in 0..s.positions.len() {
                let g = s.gains.get(i).copied().unwrap_or(0);
                let channel = |shift: u32| 2f64.powf(f64::from((g >> shift & 255) as u8 as i8) / 64.0);
                sum += (channel(0) + channel(8) + channel(16)) / 3.0;
                count += 1;
            }
        }
        (if count > 0 { sum / count as f64 } else { 1.0 }, count)
    };
    let (gain_c37, vertices) = mean(&plain);
    let (gain_d07, _) = mean(&meshes);
    Some(DebakeReport {
        debaked: which.iter().filter(|&&w| w).count(),
        mapped: maps.iter().filter(|m| m.is_some()).count(),
        vertices,
        gain_c37,
        gain_d07,
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

/// D2 (DIRECTOR.md section 12 M4; October's engine v2, render_core.cpp fit_debake_light at 191ab083a^): the light
/// painted into a model's vertex colours as a function of the surface's facing, fitted per channel by nine spherical
/// harmonics of the vertex normal over the meshes `which` names (least squares, ridge 1e-3 of the mean diagonal), and
/// taken out by a gain a vertex and channel, peak / max(fit, peak / 4) in [0.25, 4] (peak: the fit's most lit
/// vertex). What the facing does not explain stays (paint, occlusion). A mesh whose every gain rounds to 1 takes none.
/// `maps` (M5, director d07): by mesh, its first map's brightness at each vertex against the mesh's own mean
/// (first_map_at_vertices), or None: what the surface shows is its vertex colour times that, so the light painted into
/// the map by facing is found, and taken out, with the vertex colour's.
fn debake_gains(meshes: &mut [MeshStreams], which: &[bool], maps: &[Option<Vec<f64>>]) {
    let colour = |c: u32| [f64::from(c >> 24), f64::from(c >> 16 & 255), f64::from(c >> 8 & 255)].map(|x| x / 255.0);
    let normals: Vec<Vec<Option<[f64; 3]>>> = meshes
        .iter()
        .zip(which)
        .map(|(s, &w)| if w && s.attributes & MESH_COLOR0 != 0 && s.colors.len() == s.positions.len() { unit_normals(s) } else { Vec::new() })
        .collect();
    let mut a = [[0.0f64; 9]; 9];
    let mut b = [[0.0f64; 9]; 3];
    let mut samples = 0usize;
    for (mesh, (s, ns)) in meshes.iter().zip(&normals).enumerate() {
        let map = maps.get(mesh).and_then(|m| m.as_ref()).filter(|m| m.len() == s.colors.len());
        for (i, (n, &c)) in ns.iter().zip(&s.colors).enumerate() {
            let Some(n) = n else { continue };
            let y = sh9(*n);
            let mut rgb = colour(c);
            if let Some(map) = map {
                rgb = rgb.map(|x| x * map[i]);
            }
            for i in 0..9 {
                for j in 0..9 {
                    a[i][j] += y[i] * y[j];
                }
                for k in 0..3 {
                    b[k][i] += y[i] * rgb[k];
                }
            }
            samples += 1;
        }
    }
    if samples < 32 {
        return;
    }
    let ridge = 1e-3 * (0..9).map(|i| a[i][i]).sum::<f64>() / 9.0;
    for (i, row) in a.iter_mut().enumerate() {
        row[i] += ridge;
    }
    // Cholesky: a = l l^T, then each channel's coefficients.
    let mut l = [[0.0f64; 9]; 9];
    for i in 0..9 {
        for j in 0..=i {
            let mut sum = a[i][j];
            for k in 0..j {
                sum -= l[i][k] * l[j][k];
            }
            if i == j {
                if sum <= 0.0 {
                    return;
                }
                l[i][i] = sum.sqrt();
            } else {
                l[i][j] = sum / l[j][j];
            }
        }
    }
    let mut coef = [[0.0f64; 9]; 3];
    for k in 0..3 {
        let mut z = [0.0f64; 9];
        for i in 0..9 {
            let mut sum = b[k][i];
            for m in 0..i {
                sum -= l[i][m] * z[m];
            }
            z[i] = sum / l[i][i];
        }
        for i in (0..9).rev() {
            let mut sum = z[i];
            for m in i + 1..9 {
                sum -= l[m][i] * coef[k][m];
            }
            coef[k][i] = sum / l[i][i];
        }
    }
    let fit = |n: [f64; 3]| {
        let y = sh9(n);
        [0, 1, 2].map(|k| (0..9).map(|i| coef[k][i] * y[i]).sum::<f64>())
    };
    let mut peak = [1e-3f64; 3];
    for ns in &normals {
        for n in ns.iter().flatten() {
            let f = fit(*n);
            for k in 0..3 {
                peak[k] = peak[k].max(f[k]);
            }
        }
    }
    // A model painted dark all over (Lref under 0.05): no light to take out.
    if 0.2126 * peak[0] + 0.7152 * peak[1] + 0.0722 * peak[2] < 0.05 {
        return;
    }
    for (s, ns) in meshes.iter_mut().zip(&normals) {
        if ns.is_empty() {
            continue;
        }
        let gains: Vec<u32> = ns
            .iter()
            .map(|n| {
                let Some(n) = n else { return 0 };
                let f = fit(*n);
                let mut word = 0u32;
                for k in 0..3 {
                    let gain = (peak[k] / f[k].max(peak[k] * 0.25)).clamp(0.25, 4.0);
                    let step = (gain.log2() * 64.0).round().clamp(-128.0, 127.0) as i32;
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
}

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
    super::hit_record::finish(stem, &m.name, thickness, r);
    // c49: the body under a painted sheen mirrors for real (the sheen is not drawn while tracing).
    if let Some(super::hit_record::SheenRole::Body { kind, .. }) = super::hit_record::sheen_roles_kept(ir, stem).get(material).copied() {
        super::hit_record::promote_sheen_body(kind, r);
    }
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
                // Painted colour: no colour channel's rgb lit (a channel that lights only its alpha keeps painted rgb).
                let rgb_lit = (0..(m.channel_count as usize).min(2)).any(|ch| m.channels[2 * ch].enable != 0 && m.channels[2 * ch].light_mask != 0);
                let r = super::hit_record::read(m);
                r.signals.vertex_colour && !rgb_lit && r.signals.blend == 0 && r.class != super::hit_record::Class::Emissive
            })
            .collect();
        let which: Vec<bool> = ir.shapes.iter().map(|s| debake_material.get(s.material as usize).copied().unwrap_or(false)).collect();
        // M5 (director d07): each such mesh's first map as its vertices see it, for the same fit.
        let maps: Vec<Option<Vec<f64>>> = ir
            .shapes
            .iter()
            .zip(&meshes)
            .zip(&which)
            .map(|((shape, streams), &w)| if w { first_map_at_vertices(ir, shape.material as usize, streams) } else { None })
            .collect();
        debake_gains(&mut meshes, &which, &maps);
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
        let mut r = super::hit_record::read(m);
        finish_reading(ir, stem, i, measures[i].thickness(), &mut r);
        let s = &r.signals;
        let stages = (m.pixel_stage_count as usize).min(m.stages.len());
        b.u8(r.class as u8);
        b.u8(stages as u8);
        b.u8(u8::from(s.lit)
            | u8::from(s.two_sided) << 1
            | u8::from(s.alpha_test) << 2
            | u8::from(s.vertex_colour) << 3
            | u8::from(s.environment) << 4
            | u8::from(super::hit_record::drawn_shadow(stem, &m.name, r.class)) << 5
            | u8::from(paint_holds_shadows && s.vertex_colour && !s.lit) << 6
            | u8::from(matches!(super::hit_record::sheen_roles_kept(ir, stem).get(i),
                                Some(super::hit_record::SheenRole::Sheen { body: Some(_) }))) << 7);
        b.u8(r.glow_source);
        b.f32(r.roughness);
        b.f32(r.metal);
        b.f32(r.ior);
        b.f32(r.transmission);
        b.fs(&r.base);
        b.fs(&r.emission);
        b.f32(s.environment_share);
        stage_slots.push(b.0.len());
        b.u32(0);
        for table in &m.swaps {
            for &c in table {
                b.u8(c);
            }
        }
        b.f32(r.thickness);
        b.fs(&super::hit_record::glow_of(stem, &m.name, &r));
        b.fs(&r.env_tint);
        b.u8(r.env_map);
        b.u8(0);
        b.u8(0);
        b.u8(0);
        b.f32(r.body);
        b.f32(r.shine);
        b.f32(r.tint_keep);
        b.f32(0.0);
    }
    for (m, &slot) in ir.materials.iter().zip(&stage_slots) {
        let stages = (m.pixel_stage_count as usize).min(m.stages.len());
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
    #[test]
    fn debake_gains_take_out_a_painted_light_by_facing() {
        // A grey ball painted 0.3 + 0.6 max(n.y, 0) (sky and a sun overhead): the gains bring every facing near the
        // peak (what nine harmonics of the clamped cosine leave, and the byte's 1/64 octave), alike in every channel.
        let mut s = MeshStreams { attributes: MESH_NORMAL | MESH_COLOR0, ..Default::default() };
        for i in 0..24 {
            for j in 0..12 {
                let (around, down) = (i as f32 / 24.0 * std::f32::consts::TAU, (j as f32 + 0.5) / 12.0 * std::f32::consts::PI);
                let n = [down.sin() * around.cos(), down.cos(), down.sin() * around.sin()];
                let q = ((0.3 + 0.6 * n[1].max(0.0)) * 255.0).round() as u32;
                s.positions.push(n);
                s.normals.push(n);
                s.colors.push(q << 24 | q << 16 | q << 8 | 255);
            }
        }
        let mut meshes = vec![s, MeshStreams::default()];
        debake_gains(&mut meshes, &[true, false], &[None, None]);
        let s = &meshes[0];
        assert!(s.attributes & MESH_GAIN != 0 && s.gains.len() == s.positions.len());
        assert!(meshes[1].attributes & MESH_GAIN == 0 && meshes[1].gains.is_empty());
        assert!(s.gains.iter().all(|&g| g & 255 == g >> 8 & 255 && g & 255 == g >> 16 & 255));
        let before: Vec<f64> = s.colors.iter().map(|&c| f64::from(c >> 24) / 255.0).collect();
        let after: Vec<f64> = before.iter().zip(&s.gains).map(|(&c, &g)| c * 2f64.powf(f64::from(g as u8 as i8) / 64.0)).collect();
        let spread = |v: &[f64]| v.iter().cloned().fold(0.0, f64::max) / v.iter().cloned().fold(f64::MAX, f64::min);
        assert!(spread(&before) > 2.5 && spread(&after) < 1.4, "{} {}", spread(&before), spread(&after));
    }
    #[test]
    fn debake_gains_take_out_a_light_painted_into_the_map() {
        // The same ball, its vertex colours an even grey and the light 0.3 + 0.6 max(n.y, 0) in its first map's
        // brightness at each vertex (M5): the gains take it out as they take a vertex colour's.
        let mut s = MeshStreams { attributes: MESH_NORMAL | MESH_COLOR0, ..Default::default() };
        let mut map = Vec::new();
        for i in 0..24 {
            for j in 0..12 {
                let (around, down) = (i as f32 / 24.0 * std::f32::consts::TAU, (j as f32 + 0.5) / 12.0 * std::f32::consts::PI);
                let n = [down.sin() * around.cos(), down.cos(), down.sin() * around.sin()];
                s.positions.push(n);
                s.normals.push(n);
                s.colors.push(153 << 24 | 153 << 16 | 153 << 8 | 255);
                map.push(f64::from(0.3 + 0.6 * n[1].max(0.0)) / 0.6);
            }
        }
        let mut meshes = vec![s];
        debake_gains(&mut meshes, &[true], &[Some(map.clone())]);
        let s = &meshes[0];
        assert!(s.attributes & MESH_GAIN != 0 && s.gains.len() == s.positions.len());
        let after: Vec<f64> = map.iter().zip(&s.gains).map(|(&m, &g)| m * 2f64.powf(f64::from(g as u8 as i8) / 64.0)).collect();
        let spread = |v: &[f64]| v.iter().cloned().fold(0.0, f64::max) / v.iter().cloned().fold(f64::MAX, f64::min);
        assert!(spread(&map) > 2.5 && spread(&after) < 1.4, "{} {}", spread(&map), spread(&after));
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
