//! A collision model from triangles, in the form the game keeps one (host order, as `native_data::kcl` leaves a disc's).
//!
//! The file: a header, positions, normals, prisms (1-based: entry 0 is the normals' end), then an octree over the model's bounds.
//! A prism is a triangle by one corner, its face normal, the outward normals of its three edges and its height from the first corner
//! to the far edge. KCollision.cpp reads it; a word of the octree at or above 0 is the offset of a block of eight from this block, a negative one
//! the offset from this block of a list of u16 prism numbers, read from its second place to a 0.

use anyhow::{bail, Result};
use std::collections::BTreeMap;

pub struct Triangle {
    pub corners: [[f32; 3]; 3],
    pub attribute: u16,
}

fn sub(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
}

fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn unit(a: [f32; 3]) -> Option<[f32; 3]> {
    let length = dot(a, a).sqrt();
    (length > 1e-6).then(|| [a[0] / length, a[1] / length, a[2] / length])
}

/// A table of unique vectors.
#[derive(Default)]
struct Pool {
    items: Vec<[f32; 3]>,
    seen: BTreeMap<[u32; 3], u16>,
}

impl Pool {
    fn add(&mut self, v: [f32; 3]) -> Result<u16> {
        let key = [v[0].to_bits(), v[1].to_bits(), v[2].to_bits()];
        if let Some(found) = self.seen.get(&key) {
            return Ok(*found);
        }
        let Ok(index) = u16::try_from(self.items.len()) else {
            bail!("a collision model holds at most 65,536 different points: split it into parts");
        };
        self.items.push(v);
        self.seen.insert(key, index);
        Ok(index)
    }
}

struct Prism {
    height: f32,
    position: u16,
    normal: u16,
    edges: [u16; 3],
    attribute: u16,
}

/// The block of the octree a point falls in, and what's in it.
enum Node {
    Leaf(Vec<u16>),
    Split(Box<[Node; 8]>),
}

// A block splits while it holds more than this and is wider than the smallest.
const MOST_IN_BLOCK: usize = 24;
const SMALLEST_SHIFT: u32 = 4;

struct Shape {
    corners: [[f32; 3]; 3],
    normal: [f32; 3],
    low: [f32; 3],
    high: [f32; 3],
}

fn touches(shape: &Shape, low: [f32; 3], width: f32, margin: f32) -> bool {
    for axis in 0..3 {
        if shape.high[axis] < low[axis] - margin || shape.low[axis] > low[axis] + width + margin {
            return false;
        }
    }
    // The box against the triangle's plane: its centre's distance, against how far its corners reach along the normal.
    let half = width * 0.5;
    let centre = [low[0] + half, low[1] + half, low[2] + half];
    let reach = half * (shape.normal[0].abs() + shape.normal[1].abs() + shape.normal[2].abs());
    dot(sub(centre, shape.corners[0]), shape.normal).abs() <= reach + margin
}

fn build(shapes: &[Shape], inside: &[u16], low: [f32; 3], shift: u32, margin: f32) -> Node {
    if inside.len() <= MOST_IN_BLOCK || shift <= SMALLEST_SHIFT {
        return Node::Leaf(inside.to_vec());
    }
    let width = (1u32 << (shift - 1)) as f32;
    let child = |index: usize| {
        let at = [
            low[0] + if index & 1 != 0 { width } else { 0.0 },
            low[1] + if index & 2 != 0 { width } else { 0.0 },
            low[2] + if index & 4 != 0 { width } else { 0.0 },
        ];
        let held: Vec<u16> = inside.iter().copied().filter(|p| touches(&shapes[*p as usize - 1], at, width, margin)).collect();
        build(shapes, &held, at, shift - 1, margin)
    };
    Node::Split(Box::new([child(0), child(1), child(2), child(3), child(4), child(5), child(6), child(7)]))
}

fn put_node(octree: &mut Vec<u8>, node: &Node, block: usize, slot: usize) {
    let word = match node {
        Node::Leaf(list) => {
            let at = octree.len();
            octree.extend_from_slice(&0u16.to_le_bytes());
            for prism in list {
                octree.extend_from_slice(&prism.to_le_bytes());
            }
            octree.extend_from_slice(&0u16.to_le_bytes());
            while octree.len() % 4 != 0 {
                octree.push(0);
            }
            ((at - block) as u32) | 0x8000_0000
        }
        Node::Split(children) => {
            let at = octree.len();
            octree.resize(at + 8 * 4, 0);
            for (i, child) in children.iter().enumerate() {
                put_node(octree, child, at, at + i * 4);
            }
            (at - block) as u32
        }
    };
    octree[slot..slot + 4].copy_from_slice(&word.to_le_bytes());
}

fn bits_for(extent: f32) -> u32 {
    let mut bits = 1;
    while ((1u64 << bits) as f32) < extent {
        bits += 1;
    }
    bits
}

/// How far behind a face the player still counts as hitting it.
const THICKNESS: f32 = 40.0;

pub fn from_triangles(triangles: &[Triangle]) -> Result<Vec<u8>> {
    let mut positions = Pool::default();
    let mut normals = Pool::default();
    let mut prisms: Vec<Prism> = Vec::new();
    let mut shapes: Vec<Shape> = Vec::new();
    for triangle in triangles {
        let [a, b, c] = triangle.corners;
        let Some(face) = unit(cross(sub(b, a), sub(c, a))) else {
            continue; // no area
        };
        // Outward from each edge: a to c, a to b, b to c.
        let (Some(edge1), Some(edge2), Some(edge3)) = (unit(cross(face, sub(c, a))), unit(cross(sub(b, a), face)), unit(cross(sub(c, b), face))) else {
            continue;
        };
        let height = dot(sub(b, a), edge3);
        if height <= 0.0 {
            continue;
        }
        if prisms.len() >= 65534 {
            bail!("a collision model holds at most 65,534 triangles: split it into parts");
        }
        prisms.push(Prism {
            height,
            position: positions.add(a)?,
            normal: normals.add(face)?,
            edges: [normals.add(edge1)?, normals.add(edge2)?, normals.add(edge3)?],
            attribute: triangle.attribute,
        });
        let low = [a[0].min(b[0]).min(c[0]), a[1].min(b[1]).min(c[1]), a[2].min(b[2]).min(c[2])];
        let high = [a[0].max(b[0]).max(c[0]), a[1].max(b[1]).max(c[1]), a[2].max(b[2]).max(c[2])];
        shapes.push(Shape { corners: triangle.corners, normal: face, low, high });
    }
    if prisms.is_empty() {
        bail!("the collision model has no triangles");
    }

    let mut low = [f32::MAX; 3];
    let mut high = [f32::MIN; 3];
    for shape in &shapes {
        for axis in 0..3 {
            low[axis] = low[axis].min(shape.low[axis]);
            high[axis] = high[axis].max(shape.high[axis]);
        }
    }
    let margin = THICKNESS;
    let origin = [(low[0] - margin * 2.0).floor(), (low[1] - margin * 2.0).floor(), (low[2] - margin * 2.0).floor()];
    let bits = [
        bits_for(high[0] + margin * 2.0 - origin[0] + 1.0),
        bits_for(high[1] + margin * 2.0 - origin[1] + 1.0),
        bits_for(high[2] + margin * 2.0 - origin[2] + 1.0),
    ];
    let shift = bits[0].min(bits[1]).min(bits[2]).max(SMALLEST_SHIFT);
    let side = |axis: usize| 1usize << bits[axis].saturating_sub(shift);
    let every: Vec<u16> = (1..=prisms.len() as u32).map(|p| p as u16).collect();

    let mut octree = vec![0u8; side(0) * side(1) * side(2) * 4];
    let mut slot = 0;
    for z in 0..side(2) {
        for y in 0..side(1) {
            for x in 0..side(0) {
                let width = (1u32 << shift) as f32;
                let at = [origin[0] + x as f32 * width, origin[1] + y as f32 * width, origin[2] + z as f32 * width];
                let held: Vec<u16> = every.iter().copied().filter(|p| touches(&shapes[*p as usize - 1], at, width, margin)).collect();
                let node = build(&shapes, &held, at, shift, margin);
                put_node(&mut octree, &node, 0, slot);
                slot += 4;
            }
        }
    }

    // Laid out as a disc's is.
    const HEADER: usize = 0x3C;
    let mut out = vec![0u8; HEADER];
    let positions_at = out.len();
    for p in &positions.items {
        for v in p {
            out.extend_from_slice(&v.to_le_bytes());
        }
    }
    let normals_at = out.len();
    for n in &normals.items {
        for v in n {
            out.extend_from_slice(&v.to_le_bytes());
        }
    }
    // Entry 0 of the prisms isn't read: the offset points 16 bytes before the first.
    let prisms_at = out.len() - 16;
    for prism in &prisms {
        out.extend_from_slice(&prism.height.to_le_bytes());
        out.extend_from_slice(&prism.position.to_le_bytes());
        out.extend_from_slice(&prism.normal.to_le_bytes());
        for edge in prism.edges {
            out.extend_from_slice(&edge.to_le_bytes());
        }
        out.extend_from_slice(&prism.attribute.to_le_bytes());
    }
    let octree_at = out.len();
    out.extend_from_slice(&octree);
    while out.len() % 32 != 0 {
        out.push(0);
    }
    let mask = |bit_count: u32| !((1u32 << bit_count) - 1);
    let mut word = |at: usize, value: u32| out[at..at + 4].copy_from_slice(&value.to_le_bytes());
    word(0x00, positions_at as u32);
    word(0x04, normals_at as u32);
    word(0x08, prisms_at as u32);
    word(0x0C, octree_at as u32);
    word(0x10, THICKNESS.to_bits());
    word(0x14, origin[0].to_bits());
    word(0x18, origin[1].to_bits());
    word(0x1C, origin[2].to_bits());
    word(0x20, mask(bits[0]));
    word(0x24, mask(bits[1]));
    word(0x28, mask(bits[2]));
    word(0x2C, shift);
    word(0x30, bits[0].saturating_sub(shift));
    word(0x34, bits[0].saturating_sub(shift) + bits[1].saturating_sub(shift));
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_square_makes_a_file_the_game_can_walk() {
        let triangles = [
            Triangle { corners: [[0.0, 0.0, 0.0], [0.0, 0.0, 1000.0], [1000.0, 0.0, 0.0]], attribute: 0 },
            Triangle { corners: [[1000.0, 0.0, 0.0], [0.0, 0.0, 1000.0], [1000.0, 0.0, 1000.0]], attribute: 0 },
        ];
        let file = from_triangles(&triangles).unwrap();
        let word = |at: usize| u32::from_le_bytes(file[at..at + 4].try_into().unwrap());
        assert_eq!(word(0), 0x3C);
        // Two prisms between their offset and the octree, past the unread first.
        assert_eq!((word(0xC) - word(8)) / 16, 3);
        // A point above the middle: the walk reaches a list holding both.
        let (x, y, z) = (500u32 + 80, 0u32 + 80, 500u32 + 80);
        assert_eq!(x & word(0x20), 0);
        assert_eq!(y & word(0x24), 0);
        assert_eq!(z & word(0x28), 0);
        let octree = word(0xC) as usize;
        let shift = word(0x2C);
        let at = (((z >> shift) << word(0x34)) | ((y >> shift) << word(0x30)) | (x >> shift)) as usize * 4;
        let mut base = octree;
        let mut offset = u32::from_le_bytes(file[base + at..base + at + 4].try_into().unwrap());
        let mut level = shift;
        while offset & 0x8000_0000 == 0 {
            base += offset as usize;
            level -= 1;
            let child = (((z >> level) & 1) << 2 | ((y >> level) & 1) << 1 | ((x >> level) & 1)) as usize * 4;
            offset = u32::from_le_bytes(file[base + child..base + child + 4].try_into().unwrap());
        }
        let list = base + (offset & 0x7FFF_FFFF) as usize + 2;
        let first = u16::from_le_bytes(file[list..list + 2].try_into().unwrap());
        assert!(first == 1 || first == 2);
    }

    #[test]
    fn each_prism_rebuilds_its_triangle() {
        let triangle = Triangle { corners: [[0.0, 0.0, 0.0], [0.0, 0.0, 100.0], [100.0, 0.0, 0.0]], attribute: 0 };
        let [a, b, c] = triangle.corners;
        let face = unit(cross(sub(b, a), sub(c, a))).unwrap();
        assert!(face[1] > 0.99, "up: the front of a counter-clockwise triangle seen from above");
        let edge1 = unit(cross(face, sub(c, a))).unwrap();
        let edge2 = unit(cross(sub(b, a), face)).unwrap();
        let edge3 = unit(cross(sub(c, b), face)).unwrap();
        // Outward: the opposite corner lies behind each edge's normal.
        assert!(dot(sub(b, a), edge1) < 0.0);
        assert!(dot(sub(c, a), edge2) < 0.0);
        assert!(dot(sub(a, b), edge3) < 0.0);
    }
}
