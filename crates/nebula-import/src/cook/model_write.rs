//! ModelIr to the flat block native/include/nebula_model.h describes.

use super::curves::{self, Curves, Vertex};
use super::mesh_order;
use super::model_ir::*;
use super::pipeline_list;
use super::programs::{self, Written};
use anyhow::{bail, Context, Result};
use std::collections::HashMap;

pub const MAGIC: u32 = 0x4C44_4D4E;
pub const VERSION: u32 = 4;
pub const HEADER_WORDS: usize = 28;
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
    stride
}

#[cfg(test)]
mod tests {
    use super::*;
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
