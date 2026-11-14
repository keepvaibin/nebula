//! Collision models (NCOL): a disc's KCL, or a model from a mod's triangles, in the one form the game reads
//! (KCollisionServer::setData). Indices are 32-bit: no limit of 65,536 points or prisms.
//!
//! Layout, little-endian:
//! - header (80 bytes): "NCOL", positions_at, positions, normals_at, normals, prisms_at, prisms, octree_at, lists_at, lists
//!   (words), thickness, the octree's low corner (3 floats), the x/y/z masks, the block width shift, the x and xy shifts;
//! - positions and normals: 3 floats each;
//! - prisms (28 bytes): height, then the position, face normal, three edge normals and attribute row (u32 each). Entry
//!   0 is zeros: lists name prisms from 1, a 0 ends a list;
//! - octree: u32 words. A word below 2^31 is the offset in words, from its block's first word, of a block of eight; a word
//!   with the top bit set is the index in the lists of a list's first word, which is not read (a list runs from the
//!   word after it to a 0).
//! - lists: u32 prism numbers.
//!
//! A prism is a triangle by one corner, its face normal, the outward normals of its three edges and its height from the
//! first corner to the far edge.

use anyhow::{bail, Context, Result};
use std::collections::{BTreeMap, BTreeSet};

pub const MAGIC: &[u8; 4] = b"NCOL";
const HEADER: usize = 80;

pub struct Triangle {
    pub corners: [[f32; 3]; 3],
    pub attribute: u16,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Prism {
    pub height: f32,
    pub position: u32,
    pub normal: u32,
    pub edges: [u32; 3],
    pub attribute: u32,
}

/// A collision model between its sources and NCOL. `prisms` are the real ones (the file's entries 1..).
#[derive(Debug, Default, PartialEq)]
pub struct Ncol {
    pub positions: Vec<[f32; 3]>,
    pub normals: Vec<[f32; 3]>,
    pub prisms: Vec<Prism>,
    pub octree: Vec<u32>,
    pub lists: Vec<u32>,
    pub thickness: f32,
    pub low: [f32; 3],
    pub masks: [u32; 3],
    pub shift: u32,
    pub x_shift: u32,
    pub xy_shift: u32,
}

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 4).context("past the collision file's end")?;
    Ok(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
}

fn be16(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 2).context("past the collision file's end")?;
    Ok(u32::from(u16::from_be_bytes([b[0], b[1]])))
}

fn bef(bytes: &[u8], at: usize) -> Result<f32> {
    Ok(f32::from_bits(be32(bytes, at)?))
}

fn le32(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 4).context("past the collision file's end")?;
    Ok(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
}

/// The disc's octree walked (blocks and lists once each; a word may point at a block or list another reaches too).
struct DiscOctree {
    /// Block offset (bytes from the octree's start) -> its words' count.
    blocks: BTreeMap<usize, usize>,
    /// List offsets: each list's first unit, which is not read.
    lists: BTreeSet<usize>,
}

impl DiscOctree {
    fn walk(&mut self, bytes: &[u8], base: usize, block: usize, count: usize, depth: u32) -> Result<()> {
        if depth > 64 {
            bail!("an octree deeper than 64 levels at {block:#x}");
        }
        if self.blocks.insert(block, count).is_some() {
            return Ok(());
        }
        for i in 0..count {
            let word = be32(bytes, base + block + i * 4)?;
            if word & 0x8000_0000 == 0 {
                self.walk(bytes, base, block + word as usize, 8, depth + 1)?;
            } else {
                self.lists.insert(block + (word & 0x7FFF_FFFF) as usize);
            }
        }
        Ok(())
    }
}

impl Ncol {
    /// A disc's collision file (big-endian KCL).
    pub fn from_disc(bytes: &[u8]) -> Result<Ncol> {
        if bytes.len() < 0x38 {
            bail!("not a collision file");
        }
        let positions_at = be32(bytes, 0)? as usize;
        let normals_at = be32(bytes, 4)? as usize;
        let prisms_at = be32(bytes, 8)? as usize;
        let octree_at = be32(bytes, 0xC)? as usize;
        if !(positions_at <= normals_at && normals_at <= prisms_at + 0x10 && prisms_at + 0x10 <= octree_at && octree_at <= bytes.len()) {
            bail!("a collision file whose parts are out of order ({positions_at:#x} {normals_at:#x} {prisms_at:#x} {octree_at:#x})");
        }
        let vectors = |from: usize, to: usize| -> Result<Vec<[f32; 3]>> {
            (0..(to - from) / 12).map(|i| Ok([bef(bytes, from + i * 12)?, bef(bytes, from + i * 12 + 4)?, bef(bytes, from + i * 12 + 8)?])).collect()
        };
        // Prism 0 is not read: its 16 bytes are the normals' end.
        let positions = vectors(positions_at, normals_at)?;
        let normals = vectors(normals_at, prisms_at + 0x10)?;
        let mut prisms = Vec::with_capacity((octree_at - prisms_at - 0x10) / 0x10);
        let mut at = prisms_at + 0x10;
        while at + 0x10 <= octree_at {
            prisms.push(Prism {
                height: bef(bytes, at)?,
                position: be16(bytes, at + 4)?,
                normal: be16(bytes, at + 6)?,
                edges: [be16(bytes, at + 8)?, be16(bytes, at + 10)?, be16(bytes, at + 12)?],
                attribute: be16(bytes, at + 14)?,
            });
            at += 0x10;
        }
        let masks = [be32(bytes, 0x20)?, be32(bytes, 0x24)?, be32(bytes, 0x28)?];
        let shift = be32(bytes, 0x2C)?;
        let (x_shift, xy_shift) = (be32(bytes, 0x30)?, be32(bytes, 0x34)?);
        let roots = if x_shift as i32 == -1 && xy_shift as i32 == -1 {
            1usize
        } else {
            let side = |mask: u32| -> usize { (!mask).checked_shr(shift).unwrap_or(0) as usize + 1 };
            side(masks[0]).saturating_mul(side(masks[1])).saturating_mul(side(masks[2]))
        };
        let mut disc = DiscOctree { blocks: BTreeMap::new(), lists: BTreeSet::new() };
        disc.walk(bytes, octree_at, 0, roots, 0)?;

        // Every walked word keeps its order: a block's children lie after it, so offsets stay above 0.
        let mut place: BTreeMap<usize, usize> = BTreeMap::new();
        for (&block, &count) in &disc.blocks {
            for i in 0..count {
                place.insert(block + i * 4, 0);
            }
        }
        for (rank, slot) in place.values_mut().enumerate() {
            *slot = rank;
        }
        let mut lists = Vec::new();
        let mut list_at: BTreeMap<usize, u32> = BTreeMap::new();
        for &list in &disc.lists {
            list_at.insert(list, lists.len() as u32);
            lists.push(0);
            let mut unit = list + 2;
            loop {
                let prism = be16(bytes, octree_at + unit)?;
                lists.push(prism);
                if prism == 0 {
                    break;
                }
                if prism as usize > prisms.len() {
                    bail!("an octree list names prism {prism} of {}", prisms.len());
                }
                unit += 2;
            }
        }
        let mut octree = vec![0u32; place.len()];
        for (&block, &count) in &disc.blocks {
            for i in 0..count {
                let word = be32(bytes, octree_at + block + i * 4)?;
                octree[place[&(block + i * 4)]] = if word & 0x8000_0000 == 0 {
                    (place[&(block + word as usize)] - place[&block]) as u32
                } else {
                    0x8000_0000 | list_at[&(block + (word & 0x7FFF_FFFF) as usize)]
                };
            }
        }
        Ok(Ncol {
            positions,
            normals,
            prisms,
            octree,
            lists,
            thickness: bef(bytes, 0x10)?,
            low: [bef(bytes, 0x14)?, bef(bytes, 0x18)?, bef(bytes, 0x1C)?],
            masks,
            shift,
            x_shift,
            xy_shift,
        })
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let positions_at = HEADER;
        let normals_at = positions_at + 12 * self.positions.len();
        let prisms_at = normals_at + 12 * self.normals.len();
        let octree_at = prisms_at + 28 * (self.prisms.len() + 1);
        let lists_at = octree_at + 4 * self.octree.len();
        let mut out = Vec::with_capacity(lists_at + 4 * self.lists.len() + 32);
        out.extend_from_slice(MAGIC);
        for word in [
            positions_at,
            self.positions.len(),
            normals_at,
            self.normals.len(),
            prisms_at,
            self.prisms.len(),
            octree_at,
            lists_at,
            self.lists.len(),
        ] {
            out.extend_from_slice(&(word as u32).to_le_bytes());
        }
        for word in [self.thickness.to_bits(), self.low[0].to_bits(), self.low[1].to_bits(), self.low[2].to_bits()] {
            out.extend_from_slice(&word.to_le_bytes());
        }
        for word in [self.masks[0], self.masks[1], self.masks[2], self.shift, self.x_shift, self.xy_shift] {
            out.extend_from_slice(&word.to_le_bytes());
        }
        for v in self.positions.iter().chain(&self.normals) {
            for c in v {
                out.extend_from_slice(&c.to_le_bytes());
            }
        }
        for prism in std::iter::once(&Prism::default()).chain(&self.prisms) {
            out.extend_from_slice(&prism.height.to_le_bytes());
            for word in [prism.position, prism.normal, prism.edges[0], prism.edges[1], prism.edges[2], prism.attribute] {
                out.extend_from_slice(&word.to_le_bytes());
            }
        }
        for word in self.octree.iter().chain(&self.lists) {
            out.extend_from_slice(&word.to_le_bytes());
        }
        while out.len() % 32 != 0 {
            out.push(0);
        }
        out
    }

    /// A cooked model, read back (tests and proofs).
    pub fn from_ncol(bytes: &[u8]) -> Result<Ncol> {
        if !bytes.starts_with(MAGIC) {
            bail!("not a cooked collision model");
        }
        let w = |i: usize| le32(bytes, 4 * i);
        let f = |i: usize| -> Result<f32> { Ok(f32::from_bits(w(i)?)) };
        let vectors = |at: usize, count: usize| -> Result<Vec<[f32; 3]>> {
            (0..count)
                .map(|i| {
                    let v = |k: usize| -> Result<f32> { Ok(f32::from_bits(le32(bytes, at + i * 12 + k * 4)?)) };
                    Ok([v(0)?, v(1)?, v(2)?])
                })
                .collect()
        };
        let words = |at: usize, count: usize| -> Result<Vec<u32>> { (0..count).map(|i| le32(bytes, at + 4 * i)).collect() };
        let (prisms_at, prism_count, octree_at, lists_at) = (w(5)? as usize, w(6)? as usize, w(7)? as usize, w(8)? as usize);
        let prisms = (1..=prism_count)
            .map(|p| {
                let at = prisms_at + 28 * p;
                Ok(Prism {
                    height: f32::from_bits(le32(bytes, at)?),
                    position: le32(bytes, at + 4)?,
                    normal: le32(bytes, at + 8)?,
                    edges: [le32(bytes, at + 12)?, le32(bytes, at + 16)?, le32(bytes, at + 20)?],
                    attribute: le32(bytes, at + 24)?,
                })
            })
            .collect::<Result<Vec<_>>>()?;
        Ok(Ncol {
            positions: vectors(w(1)? as usize, w(2)? as usize)?,
            normals: vectors(w(3)? as usize, w(4)? as usize)?,
            prisms,
            octree: words(octree_at, (lists_at - octree_at) / 4)?,
            lists: words(lists_at, w(9)? as usize)?,
            thickness: f(10)?,
            low: [f(11)?, f(12)?, f(13)?],
            masks: [w(14)?, w(15)?, w(16)?],
            shift: w(17)?,
            x_shift: w(18)?,
            xy_shift: w(19)?,
        })
    }
}

/// A collision file cooked: a disc's KCL to NCOL; a cooked one as it is.
pub fn cook(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.starts_with(MAGIC) {
        return Ok(bytes.to_vec());
    }
    let ncol = Ncol::from_disc(bytes)?.to_bytes();
    #[cfg(feature = "proof-tools")]
    proof::walks_agree(bytes, &ncol)?;
    Ok(ncol)
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
    seen: BTreeMap<[u32; 3], u32>,
}

impl Pool {
    fn add(&mut self, v: [f32; 3]) -> u32 {
        let key = [v[0].to_bits(), v[1].to_bits(), v[2].to_bits()];
        if let Some(found) = self.seen.get(&key) {
            return *found;
        }
        let index = self.items.len() as u32;
        self.items.push(v);
        self.seen.insert(key, index);
        index
    }
}

/// The block of the octree a point falls in, and what's in it.
enum Node {
    Leaf(Vec<u32>),
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

fn build(shapes: &[Shape], inside: &[u32], low: [f32; 3], shift: u32, margin: f32) -> Node {
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
        let held: Vec<u32> = inside.iter().copied().filter(|p| touches(&shapes[*p as usize - 1], at, width, margin)).collect();
        build(shapes, &held, at, shift - 1, margin)
    };
    Node::Split(Box::new([child(0), child(1), child(2), child(3), child(4), child(5), child(6), child(7)]))
}

/// A node's word, its blocks after the octree's end and its list after the lists'.
fn put_node(octree: &mut Vec<u32>, lists: &mut Vec<u32>, node: &Node, block: usize, slot: usize) {
    octree[slot] = match node {
        Node::Leaf(list) => {
            let at = lists.len() as u32;
            lists.push(0);
            lists.extend_from_slice(list);
            lists.push(0);
            at | 0x8000_0000
        }
        Node::Split(children) => {
            let at = octree.len();
            octree.resize(at + 8, 0);
            for (i, child) in children.iter().enumerate() {
                put_node(octree, lists, child, at, at + i);
            }
            (at - block) as u32
        }
    };
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

/// A mod's collision model from its triangles.
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
        prisms.push(Prism {
            height,
            position: positions.add(a),
            normal: normals.add(face),
            edges: [normals.add(edge1), normals.add(edge2), normals.add(edge3)],
            attribute: u32::from(triangle.attribute),
        });
        let low = [a[0].min(b[0]).min(c[0]), a[1].min(b[1]).min(c[1]), a[2].min(b[2]).min(c[2])];
        let high = [a[0].max(b[0]).max(c[0]), a[1].max(b[1]).max(c[1]), a[2].max(b[2]).max(c[2])];
        shapes.push(Shape { corners: triangle.corners, normal: face, low, high });
    }
    if prisms.is_empty() {
        return Err(super::refuse::refuse("Cook.Model.NoTriangles", &[]));
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
    let every: Vec<u32> = (1..=prisms.len() as u32).collect();

    let roots = side(0) * side(1) * side(2);
    let mut octree = vec![0u32; roots];
    let mut lists = Vec::new();
    let mut slot = 0;
    for z in 0..side(2) {
        for y in 0..side(1) {
            for x in 0..side(0) {
                let width = (1u32 << shift) as f32;
                let at = [origin[0] + x as f32 * width, origin[1] + y as f32 * width, origin[2] + z as f32 * width];
                let held: Vec<u32> = every.iter().copied().filter(|p| touches(&shapes[*p as usize - 1], at, width, margin)).collect();
                let node = build(&shapes, &held, at, shift, margin);
                put_node(&mut octree, &mut lists, &node, 0, slot);
                slot += 1;
            }
        }
    }
    let mask = |bit_count: u32| !((1u32 << bit_count) - 1);
    Ok(Ncol {
        positions: positions.items,
        normals: normals.items,
        prisms,
        octree,
        lists,
        thickness: THICKNESS,
        low: origin,
        masks: [mask(bits[0]), mask(bits[1]), mask(bits[2])],
        shift,
        x_shift: bits[0].saturating_sub(shift),
        xy_shift: bits[0].saturating_sub(shift) + bits[1].saturating_sub(shift),
    }
    .to_bytes())
}

/// A walk of the octree from a root slot down a path of child slots: the prism numbers of the list it reaches.
#[cfg(any(test, feature = "proof-tools"))]
fn ncol_list(ncol: &Ncol, root: usize, path: &[usize]) -> Option<Vec<u32>> {
    let mut block = 0usize;
    let mut word = *ncol.octree.get(root)?;
    for &child in path {
        if word & 0x8000_0000 != 0 {
            return None;
        }
        block += word as usize;
        word = *ncol.octree.get(block + child)?;
    }
    if word & 0x8000_0000 == 0 {
        return None;
    }
    let start = (word & 0x7FFF_FFFF) as usize + 1;
    let end = start + ncol.lists.get(start..)?.iter().position(|p| *p == 0)?;
    Some(ncol.lists[start..end].to_vec())
}

/// The disc's octree compared with the cooked one: every root and every path down reaches the same prisms, and the
/// arrays and scalars read the same (proof-tools: the cook stops on a difference).
#[cfg(feature = "proof-tools")]
pub mod proof {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    pub static MODELS: AtomicU64 = AtomicU64::new(0);
    pub static LEAVES: AtomicU64 = AtomicU64::new(0);

    fn disc_list(bytes: &[u8], base: usize, root: usize, path: &[usize]) -> Result<Option<Vec<u32>>> {
        let mut block = 0usize;
        let mut word = be32(bytes, base + root * 4)?;
        for &child in path {
            if word & 0x8000_0000 != 0 {
                return Ok(None);
            }
            block += word as usize;
            word = be32(bytes, base + block + child * 4)?;
        }
        if word & 0x8000_0000 == 0 {
            return Ok(None);
        }
        let mut at = base + block + (word & 0x7FFF_FFFF) as usize + 2;
        let mut out = Vec::new();
        loop {
            let prism = be16(bytes, at)?;
            if prism == 0 {
                return Ok(Some(out));
            }
            out.push(prism);
            at += 2;
        }
    }

    fn descend(bytes: &[u8], base: usize, ncol: &Ncol, root: usize, path: &mut Vec<usize>, leaves: &mut u64) -> Result<()> {
        let old = disc_list(bytes, base, root, path)?;
        let new = ncol_list(ncol, root, path);
        if old != new {
            bail!("octree root {root} path {path:?}: KCL reaches {old:?}, NCOL {new:?}");
        }
        if old.is_some() {
            *leaves += 1;
            return Ok(());
        }
        if path.len() > 64 {
            bail!("octree deeper than 64 levels");
        }
        for child in 0..8 {
            path.push(child);
            descend(bytes, base, ncol, root, path, leaves)?;
            path.pop();
        }
        Ok(())
    }

    pub fn walks_agree(bytes: &[u8], cooked: &[u8]) -> Result<()> {
        let ncol = Ncol::from_ncol(cooked)?;
        let disc = Ncol::from_disc(bytes)?;
        // By bytes: floats compared by their bits.
        if ncol.to_bytes() != cooked || disc.to_bytes() != cooked {
            bail!("NCOL reads back other than it was written");
        }
        let base = be32(bytes, 0xC)? as usize;
        let roots = ncol.octree.len().min(if ncol.x_shift as i32 == -1 && ncol.xy_shift as i32 == -1 {
            1
        } else {
            let side = |mask: u32| -> usize { (!mask).checked_shr(ncol.shift).unwrap_or(0) as usize + 1 };
            side(ncol.masks[0]) * side(ncol.masks[1]) * side(ncol.masks[2])
        });
        let mut leaves = 0;
        for root in 0..roots {
            descend(bytes, base, &ncol, root, &mut Vec::new(), &mut leaves)?;
        }
        MODELS.fetch_add(1, Ordering::Relaxed);
        LEAVES.fetch_add(leaves, Ordering::Relaxed);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_square_makes_a_model_the_game_can_walk() {
        let triangles = [
            Triangle { corners: [[0.0, 0.0, 0.0], [0.0, 0.0, 1000.0], [1000.0, 0.0, 0.0]], attribute: 0 },
            Triangle { corners: [[1000.0, 0.0, 0.0], [0.0, 0.0, 1000.0], [1000.0, 0.0, 1000.0]], attribute: 0 },
        ];
        let file = from_triangles(&triangles).unwrap();
        assert!(file.starts_with(MAGIC));
        let ncol = Ncol::from_ncol(&file).unwrap();
        assert_eq!(ncol.prisms.len(), 2);
        // A point above the middle: the walk reaches a list holding both.
        let (x, y, z) = (500u32 + 80, 80u32, 500u32 + 80);
        assert_eq!((x & ncol.masks[0], y & ncol.masks[1], z & ncol.masks[2]), (0, 0, 0));
        let root = (((z >> ncol.shift) << ncol.xy_shift) | ((y >> ncol.shift) << ncol.x_shift) | (x >> ncol.shift)) as usize;
        let mut path = Vec::new();
        let mut level = ncol.shift;
        while ncol_list(&ncol, root, &path).is_none() {
            level -= 1;
            path.push((((z >> level) & 1) << 2 | ((y >> level) & 1) << 1 | ((x >> level) & 1)) as usize);
        }
        let list = ncol_list(&ncol, root, &path).unwrap();
        assert!(list.contains(&1) || list.contains(&2));
    }

    /// A disc KCL: two positions, normals, one prism, one root leaf list naming it.
    fn disc() -> Vec<u8> {
        let mut out = vec![0u8; 0x3C];
        let floats = |out: &mut Vec<u8>, v: &[f32]| v.iter().for_each(|f| out.extend_from_slice(&f.to_bits().to_be_bytes()));
        let positions_at = out.len();
        floats(&mut out, &[0.0, 0.0, 0.0, 1.0, 2.0, 3.0]);
        let normals_at = out.len();
        floats(&mut out, &[0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -1.0, 0.0, 0.0]);
        let prisms_at = out.len() - 0x10;
        floats(&mut out, &[50.0]);
        for unit in [1u16, 0, 1, 2, 3, 7] {
            out.extend_from_slice(&unit.to_be_bytes());
        }
        let octree_at = out.len();
        // The root leaf: its list's unread first unit at byte 6, then prism 1 and the end.
        out.extend_from_slice(&0x8000_0006u32.to_be_bytes());
        for unit in [0xFFFFu16, 0, 1, 0] {
            out.extend_from_slice(&unit.to_be_bytes());
        }
        for (at, word) in [(0usize, positions_at as u32), (4, normals_at as u32), (8, prisms_at as u32), (0xC, octree_at as u32)] {
            out[at..at + 4].copy_from_slice(&word.to_be_bytes());
        }
        out[0x10..0x14].copy_from_slice(&40.0f32.to_bits().to_be_bytes());
        for at in [0x30, 0x34] {
            out[at..at + 4].copy_from_slice(&u32::MAX.to_be_bytes());
        }
        out
    }

    #[test]
    fn a_disc_model_cooks_to_32_bits() {
        let ncol = Ncol::from_disc(&disc()).unwrap();
        assert_eq!(ncol.positions, [[0.0, 0.0, 0.0], [1.0, 2.0, 3.0]]);
        assert_eq!(ncol.normals.len(), 4);
        assert_eq!(ncol.prisms, [Prism { height: 50.0, position: 1, normal: 0, edges: [1, 2, 3], attribute: 7 }]);
        assert_eq!(ncol.octree, [0x8000_0000]);
        assert_eq!(ncol.lists, [0, 1, 0]);
        assert_eq!(ncol_list(&ncol, 0, &[]), Some(vec![1]));
        let cooked = cook(&disc()).unwrap();
        assert_eq!(Ncol::from_ncol(&cooked).unwrap(), ncol);
        assert_eq!(cook(&cooked).unwrap(), cooked);
    }
}
