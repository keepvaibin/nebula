//! ModelIr to the flat block native/include/nebula_model.h describes.

use super::curves::{self, Curves, Vertex};
use super::mesh_order;
use super::model_ir::*;
use super::pipeline_list;
use super::programs::{self, Written};
use anyhow::{bail, Context, Result};
use std::collections::HashMap;

pub const MAGIC: u32 = 0x4C44_4D4E;
pub const VERSION: u32 = 13;
/// The header's room in words: magic, version, size, flags, the section count and the table's place, then zero.
pub const HEADER_WORDS: usize = 28;
/// NebulaModelSection.flags: a reader that does not know the section refuses the file.
pub const SECTION_REQUIRED: u32 = 1;
pub const SECTION_BYTES: usize = 24;
pub const JOINT_BYTES: usize = 96;
pub const MESH_BYTES: usize = 100;
pub const MESH_COORD_BYTES: usize = 4;
pub const MATL_BYTES: usize = 64;
pub const GX_BYTES: usize = 120;
pub const SLOT_BYTES: usize = 8;
pub const FORM_BYTES: usize = 56;
pub const TEXTURE_BYTES: usize = 20;
pub const SAMPLER_BYTES: usize = 20;
pub const COORD_MATRIX_BYTES: usize = 104;
pub const WARP_BYTES: usize = 24;

pub const MESH_NORMAL: u32 = 1 << 0;
pub const MESH_COLOR0: u32 = 1 << 1;
pub const MESH_COLOR1: u32 = 1 << 2;
pub const MESH_TANGENTS: u32 = 1 << 3;
/// NebulaModelMeshCoord.stream for a coordinate the mesh has none of.
pub const MESH_NO_COORD: u8 = 0xFF;
/// NebulaModelSlot.uses
pub const SLOT_USES_MAP: u8 = 1 << 0;
pub const SLOT_USES_COORD: u8 = 1 << 1;
pub const SLOT_NO_MAP: u16 = 0xFFFF;

/// A texture's sampler as the renderer takes it (nebula_model.h NebulaModelSampler, FRAME-MODEL 2.10), decoded from
/// the disc's codes: wrap codes as they are (0 clamp, 1 repeat, 2 mirror); minification codes 1, 3 and 5 linear;
/// 0 and 1 the first level only, 2 and 3 the nearest level, 4 and 5 between levels; magnification linear but for 0.
pub fn sampler_bytes(t: &TextureIr) -> [u8; SAMPLER_BYTES] {
    let mut s = [0u8; SAMPLER_BYTES];
    s[0] = t.wrap_s;
    s[1] = t.wrap_t;
    s[2] = u8::from(matches!(t.min_filter, 1 | 3 | 5));
    s[3] = u8::from(t.mag_filter != 0);
    s[4] = match t.min_filter {
        0 | 1 => 0,
        2 | 3 => 1,
        _ => 2,
    };
    s[5] = t.anisotropy;
    s[8..12].copy_from_slice(&t.min_lod.to_le_bytes());
    s[12..16].copy_from_slice(&t.max_lod.to_le_bytes());
    s[16..20].copy_from_slice(&t.lod_bias.to_le_bytes());
    s
}

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
    pub matrices: Vec<u32>,
    pub indices: Vec<u32>,
    pub single_matrix: u32,
    /// The plain mesh's vertices; those past it are refinement's.
    pub base_vertices: u32,
    pub refined_indices: Vec<u32>,
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
            }
            mesh_of[i as usize] = index;
            out.indices.push(index);
        }
        // Refinement mixes a group's corners; its vertices follow the whole plain mesh.
        if curved.is_some() {
            let corners: Vec<Vertex> = group.corners.iter().map(|c| corner_vertex(a, attr, &group.coordinates, c, curve_data)).collect();
            let (added, indices) = curves::refine(&corners, &group.indices);
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

pub fn write(ir: &ModelIr, stem: &str, written: &Written, pipelines: &mut pipeline_list::Pipelines) -> Result<Vec<u8>> {
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
    // Materials (FRAME-MODEL 2.9): each section's entries gathered, then the sections one after another. A record names
    // its parts by index and count in their section; names stay offsets of the block.
    let mut matl = Blob(Vec::new());
    let mut gx = Blob(Vec::new());
    let mut form_entries = Blob(Vec::new());
    let mut slot_entries = Blob(Vec::new());
    let mut coord_entries = Blob(Vec::new());
    let mut matrix_entries = Blob(Vec::new());
    let mut warp_entries = Blob(Vec::new());
    let mut row_entries = Blob(Vec::new());
    let mut map_entries = Blob(Vec::new());
    let mut input_entries = Blob(Vec::new());
    let mut matl_names = Vec::with_capacity(ir.materials.len());
    let mut material_forms: Vec<Vec<(u64, u64, Vec<u8>)>> = vec![Vec::new(); ir.materials.len()];
    let count = |blob: &Blob, stride: usize| (blob.0.len() / stride) as u32;
    for (index, m) in ir.materials.iter().enumerate() {
        // GXMT: the console's material, read until step 3's last landing.
        let gx_index = count(&gx, GX_BYTES);
        gx.u32(u32::from(m.has_ambient) | u32::from(m.has_stage_colors) << 1 | u32::from(m.has_indirect) << 2 | u32::from(m.has_fog) << 3);
        for c in m.material_color.iter().chain(m.ambient_color.iter()) {
            gx.0.extend_from_slice(c);
        }
        for r in &m.registers {
            for &v in r {
                gx.i16(v);
            }
        }
        for c in &m.constants {
            gx.0.extend_from_slice(c);
        }
        gx.0.extend_from_slice(&[m.alpha_ref0, m.alpha_ref1, 0, 0]);
        let f = &m.fog;
        gx.0.extend_from_slice(&[f.kind, 0, 0, 0]);
        gx.fs(&[f.start, f.end, f.near, f.far]);
        gx.0.extend_from_slice(&f.color);
        gx.u32(count(&coord_entries, 8));
        gx.u32(m.coords.len() as u32);
        gx.u32(count(&matrix_entries, COORD_MATRIX_BYTES));
        gx.u32(m.matrices.len() as u32);
        gx.u32(count(&warp_entries, WARP_BYTES));
        gx.u32(m.warps.len() as u32);
        for c in &m.coords {
            coord_entries.0.extend_from_slice(&[c.kind, c.source, 0, 0]);
            coord_entries.u32(c.matrix);
        }
        for t in &m.matrices {
            matrix_entries.u32(u32::from(t.slot));
            matrix_entries.0.extend_from_slice(&[t.projection, t.info, 0, 0]);
            matrix_entries.fs(&t.center);
            matrix_entries.fs(&t.scale);
            matrix_entries.f32(t.rotation);
            matrix_entries.fs(&t.translation);
            for row in &t.effect {
                matrix_entries.fs(row);
            }
        }
        for w in &m.warps {
            warp_entries.fs(&w.rows[0]);
            warp_entries.fs(&w.rows[1]);
        }
        let first_slot = count(&slot_entries, SLOT_BYTES);
        let material_slot_list = material_slots(m);
        for &(texture, scale_map, uses) in &material_slot_list {
            slot_entries.u32(texture);
            slot_entries.u16(scale_map);
            slot_entries.u8(uses);
            slot_entries.u8(0);
        }
        // Its forms: the programs, their rows, map numbers and inputs.
        let skinned = programs::mesh_skinned(ir, m);
        let registers = written.registers(m);
        let forms = programs::forms_of(stem, m, index)?;
        let first_form = count(&form_entries, FORM_BYTES);
        for form in &forms {
            let mut t = programs::generate(m, *form, skinned, &registers)?;
            programs::finish_text(&m.name, *form, &mut t);
            let (vertex, pixel) = (programs::text_hash(&t.vertex), programs::text_hash(&t.pixel));
            material_forms[index].push((vertex, pixel, t.inputs.iter().map(programs::input_code).collect()));
            form_entries.0.extend_from_slice(&vertex.to_le_bytes());
            form_entries.0.extend_from_slice(&pixel.to_le_bytes());
            form_entries.u32(index as u32);
            form_entries.u32(count(&row_entries, 4));
            form_entries.u32(t.vertex_rows.len() as u32);
            form_entries.u32(count(&row_entries, 4) + t.vertex_rows.len() as u32);
            form_entries.u32(t.pixel_rows.len() as u32);
            for &r in t.vertex_rows.iter().chain(&t.pixel_rows) {
                row_entries.u32(r);
            }
            form_entries.u32(map_entries.0.len() as u32);
            form_entries.u32(t.textures.len() as u32);
            map_entries.0.extend_from_slice(&t.textures);
            form_entries.u32(input_entries.0.len() as u32);
            form_entries.u32(t.inputs.len() as u32);
            for input in &t.inputs {
                input_entries.0.push(programs::input_code(input));
            }
            let flags = u8::from(form.normal_coord) | u8::from(form.lit) << 1 | u8::from(t.alpha_test) << 2 | u8::from(form.fog_linear) << 3;
            form_entries.0.extend_from_slice(&[form.replace, flags, form.pass, 0]);
        }
        // MATL.
        matl_names.push((matl.0.len(), m.name.clone()));
        matl.u32(0);
        matl.u32(name_hash(&m.name));
        matl.u32(u32::from(m.same_as));
        matl.u32(m.mesh);
        matl.u32(m.joint);
        matl.u32(u32::from(m.mode & 3 == 0) | u32::from(m.has_scale != 0) << 1 | u32::from(m.on_billboard) << 2);
        matl.u32(pipeline_state(m));
        let mut light_masks = [0u8; 4];
        for (mask, c) in light_masks.iter_mut().zip(&m.channels) {
            *mask = if c.enable != 0 { c.light_mask } else { 0 };
        }
        matl.0.extend_from_slice(&light_masks);
        matl.u32(gx_index);
        matl.u32(first_slot);
        matl.u32(material_slot_list.len() as u32);
        matl.u32(first_form);
        matl.u32(forms.len() as u32);
        matl.fs(&m.normal_scale);
    }
    // FORM starts with two u64 program hashes: 8-aligned in the file, which the store places 32-aligned (cook ALIGN).
    let materials_at = b.at();
    b.0.extend_from_slice(&matl.0);
    for (offset, name) in matl_names {
        strings.push((materials_at as usize + offset, name));
    }
    let gx_at = b.at();
    b.0.extend_from_slice(&gx.0);
    b.align(8);
    let forms_at = b.at();
    b.0.extend_from_slice(&form_entries.0);
    let slots_at = b.at();
    b.0.extend_from_slice(&slot_entries.0);
    let coords_at = b.at();
    b.0.extend_from_slice(&coord_entries.0);
    let coord_matrices_at = b.at();
    b.0.extend_from_slice(&matrix_entries.0);
    let warps_at = b.at();
    b.0.extend_from_slice(&warp_entries.0);
    let rows_at = b.at();
    b.0.extend_from_slice(&row_entries.0);
    let maps_at = b.at();
    b.0.extend_from_slice(&map_entries.0);
    let inputs_at = b.at();
    b.0.extend_from_slice(&input_entries.0);
    let material_sections = [
        Section::records(*b"MATL", materials_at, count(&matl, MATL_BYTES), MATL_BYTES as u32),
        Section::records(*b"GXMT", gx_at, count(&gx, GX_BYTES), GX_BYTES as u32),
        Section::records(*b"FORM", forms_at, count(&form_entries, FORM_BYTES), FORM_BYTES as u32),
        Section::records(*b"SLOT", slots_at, count(&slot_entries, SLOT_BYTES), SLOT_BYTES as u32),
        Section::records(*b"GXTG", coords_at, count(&coord_entries, 8), 8),
        Section::records(*b"GXTM", coord_matrices_at, count(&matrix_entries, COORD_MATRIX_BYTES), COORD_MATRIX_BYTES as u32),
        Section::records(*b"GXWP", warps_at, count(&warp_entries, WARP_BYTES), WARP_BYTES as u32),
        Section::records(*b"FROW", rows_at, count(&row_entries, 4), 4),
        Section::bytes(*b"FTEX", maps_at, map_entries.0.len() as u32),
        Section::bytes(*b"FINP", inputs_at, input_entries.0.len() as u32),
    ];
    b.align(4);
    let textures_at = b.at();
    // Each texture names its sampler, one of the model's distinct samplers in SAMP (FRAME-MODEL 2.10).
    let mut samplers: Vec<[u8; SAMPLER_BYTES]> = Vec::new();
    let mut texture_slots = Vec::with_capacity(ir.textures.len());
    for t in &ir.textures {
        let sampler = sampler_bytes(t);
        let index = match samplers.iter().position(|s| *s == sampler) {
            Some(index) => index,
            None => {
                samplers.push(sampler);
                samplers.len() - 1
            }
        };
        texture_slots.push(b.0.len());
        strings.push((b.0.len(), t.name.clone()));
        b.u32(0);
        b.u32(0);
        b.u32(t.width);
        b.u32(t.height);
        b.u32(index as u32);
    }
    let samplers_at = b.at();
    for sampler in &samplers {
        b.0.extend_from_slice(sampler);
    }
    // Names.
    let names_at = b.at();
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
    let names_size = b.at() - names_at;
    for (shape, streams) in ir.shapes.iter().zip(&meshes) {
        let Some(forms) = material_forms.get(shape.material as usize) else {
            continue;
        };
        let state = pipeline_state(&ir.materials[shape.material as usize]);
        for (vertex, pixel, inputs) in forms {
            pipelines.add_form(*vertex, *pixel, state, inputs, streams);
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
        b.patch32(slot + 84, refined_at);
        let fields = slot + 36;
        b.patch32(fields, positions_at);
        b.patch32(fields + 4, attributes_at);
        b.patch32(fields + 8, matrices_at);
        b.patch32(fields + 12, further_at);
        b.patch32(fields + 16, indices_at);
    }
    b.align(32);
    let geometry_size = b.at() - geometry_at;
    let images_at = b.at();
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
    b.align(32);
    let images_size = b.at() - images_at;
    // The model's scalars, then the section table, after 12's arrays (FRAME-MODEL 2.8): every array lies where 12
    // put it.
    let scalars_at = b.at();
    b.u32(u32::from(ir.full_weight_count));
    b.u32(ir.source_vertex_count);
    let count = |n: usize| n as u32;
    let mut table: Vec<Section> = vec![
        Section::records(*b"MODL", scalars_at, 1, 8),
        Section::records(*b"JNTS", joints_at, count(ir.joints.len()), JOINT_BYTES as u32),
        Section::records(*b"STEP", steps_at, steps, 8),
        Section::records(*b"JMSH", joint_meshes_at, first_mesh, 4),
        Section::records(*b"DMTX", draw_matrices_at, count(ir.draw_matrices.len()), 8),
        Section::records(*b"ENVL", envelopes_at, count(ir.envelopes.len()), 8),
        Section::records(*b"MIXS", mixes_at, first_mix, 8),
        Section::records(*b"IBND", inverse_binds_at, count(ir.inverse_binds.len()), 48),
        Section::records(*b"IMPT", important_at, count(ir.important.len()), 4),
        // A mesh's coordinate table and a material's parts lie inside their section's span.
        Section::span(*b"MESH", meshes_at, materials_at - meshes_at, count(ir.shapes.len()), MESH_BYTES as u32),
        Section::records(*b"TEXR", textures_at, count(ir.textures.len()), TEXTURE_BYTES as u32),
        Section::bytes(*b"NAME", names_at, names_size),
        Section::bytes(*b"GEOM", geometry_at, geometry_size),
        Section::bytes(*b"TIMG", images_at, images_size),
    ];
    table.extend(material_sections);
    table.push(Section::records(*b"SAMP", samplers_at, samplers.len() as u32, SAMPLER_BYTES as u32));
    let sections_at = b.at();
    for section in &table {
        b.u32(u32::from_le_bytes(section.name));
        b.u32(section.flags);
        b.u32(section.at);
        b.u32(section.size);
        b.u32(section.count);
        b.u32(section.stride);
    }
    let size = b.at();
    let mut flags = u32::from(ir.scale_mode);
    if ir.has_billboard {
        flags |= 1 << 4;
    }
    if ir.materials_only {
        flags |= 1 << 5;
    }
    let mut header = [0u32; HEADER_WORDS];
    header[..6].copy_from_slice(&[MAGIC, VERSION, size, flags, table.len() as u32, sections_at]);
    for (i, &v) in header.iter().enumerate() {
        b.patch32(i * 4, v);
    }
    Ok(b.0)
}

/// One entry of a model's section table (nebula_model.h NebulaModelSection).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Section {
    pub name: [u8; 4],
    pub flags: u32,
    pub at: u32,
    pub size: u32,
    pub count: u32,
    pub stride: u32,
}

impl Section {
    fn records(name: [u8; 4], at: u32, count: u32, stride: u32) -> Section {
        Section { name, flags: SECTION_REQUIRED, at, size: count * stride, count, stride }
    }
    fn span(name: [u8; 4], at: u32, size: u32, count: u32, stride: u32) -> Section {
        Section { name, flags: SECTION_REQUIRED, at, size, count, stride }
    }
    fn bytes(name: [u8; 4], at: u32, size: u32) -> Section {
        Section { name, flags: SECTION_REQUIRED, at, size, count: size, stride: 1 }
    }
}

/// A cooked model's section table: this version's, each section inside the block. Errors name what is wrong.
pub fn sections(bytes: &[u8]) -> Result<Vec<Section>> {
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
    let size = (word(8)? as usize).min(bytes.len());
    let (count, table) = (word(16)? as usize, word(20)? as usize);
    if count > size.saturating_sub(table) / SECTION_BYTES {
        bail!("a model's section table past its end");
    }
    (0..count)
        .map(|i| {
            let e = table + i * SECTION_BYTES;
            let section = Section {
                name: word(e)?.to_le_bytes(),
                flags: word(e + 4)?,
                at: word(e + 8)?,
                size: word(e + 12)?,
                count: word(e + 16)?,
                stride: word(e + 20)?,
            };
            if section.at as usize > size || section.size as usize > size - section.at as usize {
                bail!("a model's section {} past its end", String::from_utf8_lossy(&section.name));
            }
            Ok(section)
        })
        .collect()
}

/// The section named `name` in `list`.
pub fn section<'a>(list: &'a [Section], name: &[u8; 4]) -> Result<&'a Section> {
    list.iter().find(|s| &s.name == name).with_context(|| format!("a cooked model without its section {}", String::from_utf8_lossy(name)))
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
    let table = sections(bytes)?;
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
    let place = |name: &[u8; 4]| -> Result<(usize, usize, usize)> {
        let s = section(&table, name)?;
        Ok((s.count as usize, s.at as usize, s.stride as usize))
    };
    let ((joints, joints_at, joint_stride), (materials, materials_at, material_stride), (textures, textures_at, texture_stride)) =
        (place(b"JNTS")?, place(b"MATL")?, place(b"TEXR")?);
    Ok(ModelNames {
        joints: list(joints, joints_at, joint_stride, JOINT_BYTES - 8)?,
        materials: list(materials, materials_at, material_stride, 0)?,
        textures: list(textures, textures_at, texture_stride, 0)?,
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
    stride
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_sampler_is_the_decode_of_the_disc_codes() {
        let mut t = TextureIr {
            name: String::new(), format: 0, width: 1, height: 1, wrap_s: 2, wrap_t: 1, mipmap: 1, anisotropy: 2,
            min_filter: 0, mag_filter: 0, min_lod: -1.5, max_lod: 10.0, levels: 1, lod_bias: 0.25, record: 0,
        };
        // (minification code, linear, mip mode) for the six codes, by Material.cpp's rule before 2.10.
        for (code, linear, mip) in [(0u8, 0u8, 0u8), (1, 1, 0), (2, 0, 1), (3, 1, 1), (4, 0, 2), (5, 1, 2)] {
            t.min_filter = code;
            for mag in [0u8, 1] {
                t.mag_filter = mag;
                let s = sampler_bytes(&t);
                assert_eq!(&s[..8], &[2, 1, linear, u8::from(mag != 0), mip, 2, 0, 0], "code {code}, mag {mag}");
                assert_eq!(f32::from_le_bytes(s[8..12].try_into().unwrap()), -1.5);
                assert_eq!(f32::from_le_bytes(s[12..16].try_into().unwrap()), 10.0);
                assert_eq!(f32::from_le_bytes(s[16..20].try_into().unwrap()), 0.25);
            }
        }
    }

    #[test]
    fn the_section_table_names_every_array_where_12_put_it() {
        let mut ir = ModelIr::default();
        ir.arrays.positions = vec![[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]];
        ir.source_vertex_count = 3;
        ir.full_weight_count = 2;
        let corners = (0..3).map(|n| Corner { position: n, normal: 0, tangent: 0, binormal: 0, color: [0; 2], coord: Vec::new(), matrix: 0, texture_matrix: Vec::new() }).collect();
        ir.shapes.push(ShapeIr {
            groups: vec![GroupIr { attributes: 0, coordinates: Vec::new(), corners, indices: vec![0, 1, 2], use_table: vec![0], places: 3 }],
            material: NONE, matrix_type: 0, radius: 0.0, min: [0.0; 3], max: [0.0; 3],
        });
        let bytes = write(&ir, "sections", &Written::unknown(), &mut Default::default()).unwrap();
        let word = |at: usize| u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap());
        assert_eq!((word(0), word(4), word(8) as usize), (MAGIC, VERSION, bytes.len()));
        assert!((6..HEADER_WORDS).all(|k| word(k * 4) == 0), "the header's room past the table's place is zero");
        let table = sections(&bytes).unwrap();
        let names: Vec<&[u8; 4]> = table.iter().map(|s| &s.name).collect();
        assert_eq!(names, [b"MODL", b"JNTS", b"STEP", b"JMSH", b"DMTX", b"ENVL", b"MIXS", b"IBND", b"IMPT", b"MESH", b"TEXR", b"NAME", b"GEOM", b"TIMG",
            b"MATL", b"GXMT", b"FORM", b"SLOT", b"GXTG", b"GXTM", b"GXWP", b"FROW", b"FTEX", b"FINP", b"SAMP"]);
        assert!(table.iter().all(|s| s.flags == SECTION_REQUIRED && u64::from(s.count) * u64::from(s.stride) <= u64::from(s.size)));
        // The arrays in file order, each after the one before, all past the header; the scalars after the images.
        let mut placed: Vec<Section> = table[1..].to_vec();
        placed.sort_by_key(|s| (s.at, s.size));
        for pair in placed.windows(2) {
            assert!(pair[0].at + pair[0].size <= pair[1].at, "{:?} then {:?}", pair[0], pair[1]);
        }
        assert!(table[1].at as usize >= HEADER_WORDS * 4);
        let modl = section(&table, b"MODL").unwrap();
        let images = section(&table, b"TIMG").unwrap();
        assert_eq!(modl.at, images.at + images.size);
        assert_eq!((word(modl.at as usize), word(modl.at as usize + 4)), (2, 3));
        assert_eq!(section(&table, b"MESH").unwrap().count, 1);
        assert_eq!(section(&table, b"JNTS").unwrap().count, 0);
        let geom = section(&table, b"GEOM").unwrap();
        assert!(geom.size > 0 && geom.at % 32 == 0);
        // A reader refuses another version and a table past the end.
        let mut other = bytes.clone();
        other[4..8].copy_from_slice(&12u32.to_le_bytes());
        assert!(sections(&other).unwrap_err().to_string().contains("version 12"));
        let mut past = bytes.clone();
        past[16..20].copy_from_slice(&1000u32.to_le_bytes());
        assert!(sections(&past).is_err());
        assert!(section(&table, b"NONE").is_err(), "a name the table lacks is refused");
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
            let bytes = write(&ir, "wide", &Written::unknown(), &mut Default::default()).unwrap();
            let word = |at| u32::from_le_bytes(bytes[at..at+4].try_into().unwrap());
            let table = sections(&bytes).unwrap();
            let mesh = section(&table, b"MESH").unwrap().at as usize;
            assert_eq!(bytes[mesh + 72], if count > 65535 {4} else {2});
            assert_eq!(word(mesh + 64), 70000);
            let indices = word(mesh + 52) as usize;
            if count > 65535 { assert_eq!((0..word(mesh+32) as usize).map(|i| word(indices+i*4)).max(), Some((count-1) as u32)); }
        }
    }
}
