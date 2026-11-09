//! Curved surfaces: the game's models are polygons made for a 456-line picture, and drawn at many times that their outlines
//! show the polygon. Each triangle that bends is subdivided here, once, and each new point moved towards the tangent planes
//! of its corners (Phong tessellation, Boubekeur and Alexa 2008). That move is a sum over the triangle's edges, each term set
//! by the edge's two ends and the normal each end bends it by. A model's shading normals are drawn for the look as often as
//! for the shape, so a normal bends nothing unless the geometry shows the curve it implies:
//!  - around a position, the faces that meet through edges agreeing on the normal form a group, and the group bends by its
//!    normal only where it is a sampled surface, every test geometric: its faces within 35 degrees of the normal and 62 of
//!    each other; the normal inside the convex cone of its faces' normals; the same curvature on opposite sides of the
//!    position; one quadratic form of direction placing every neighbour within 5 % of its edge; one shape operator explaining
//!    every face's tilt; and no more curvature along a sliver than the position's short edges show;
//!  - an edge where groups meet (a crease) bends as the crease line does: by the circle through the position and its two
//!    crease neighbours where the crease is a gentle evenly sampled curve at the edge of a curved group whose normal, in the
//!    crease's plane, is the crease's own; any other crease edge stays straight, and a position with more than one group and
//!    other than two crease edges bends nothing;
//!  - a T-junction and a zero-area triangle's corners stay put where any edge along the line would bend.
//! Every edge two triangles share bends alike in both, so no cracks open.

use std::collections::HashMap;

pub const NO_PLACE: u32 = u32::MAX;

type D3 = [f64; 3];
pub type Vec3 = [f32; 3];

const PI: f64 = std::f64::consts::PI;

fn cos_deg(degrees: f64) -> f64 {
    (degrees * PI / 180.0).cos()
}
fn sin_deg(degrees: f64) -> f64 {
    (degrees * PI / 180.0).sin()
}
fn widen(v: &Vec3) -> D3 {
    [f64::from(v[0]), f64::from(v[1]), f64::from(v[2])]
}
fn narrow(v: &D3) -> Vec3 {
    [v[0] as f32, v[1] as f32, v[2] as f32]
}
fn dot(a: &D3, b: &D3) -> f64 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}
fn sub(a: &D3, b: &D3) -> D3 {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}
fn add(a: &D3, b: &D3) -> D3 {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}
fn mul(a: &D3, s: f64) -> D3 {
    [a[0] * s, a[1] * s, a[2] * s]
}
fn cross(a: &D3, b: &D3) -> D3 {
    [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
}
fn length(a: &D3) -> f64 {
    dot(a, a).sqrt()
}
fn normalized(v: &D3) -> D3 {
    let l = length(v);
    if l > 1e-20 {
        mul(v, 1.0 / l)
    } else {
        [0.0; 3]
    }
}
fn is_zero(v: &D3) -> bool {
    v[0] == 0.0 && v[1] == 0.0 && v[2] == 0.0
}
fn angle_between(a: &D3, b: &D3) -> f64 {
    dot(a, b).clamp(-1.0, 1.0).acos()
}
/// The circumcentre of the triangle (q, q + ea, q + eb), from q (zero for a degenerate triangle).
fn circumcentre(ea: &D3, eb: &D3) -> D3 {
    let w = cross(ea, eb);
    let ww = dot(&w, &w);
    if ww.partial_cmp(&0.0) != Some(std::cmp::Ordering::Greater) {
        return [0.0; 3];
    }
    mul(&add(&mul(&cross(&w, ea), dot(eb, eb)), &mul(&cross(eb, &w), dot(ea, ea))), 0.5 / ww)
}

struct Limits {
    parallel: f64,
    normal_to_face: f64,
    face_to_face: f64,
    opposite: f64,
    cone: f64,
    flat: f64,
    straight: f64,
    turn: f64,
    agree: f64,
    solid_crease: f64,
    splayed: f64,
}

impl Limits {
    fn new() -> Limits {
        Limits {
            parallel: cos_deg(2.0),
            normal_to_face: cos_deg(35.0),
            face_to_face: cos_deg(62.0),
            opposite: -cos_deg(15.0),
            cone: 2.0 * PI / 180.0,
            flat: sin_deg(2.0),
            straight: cos_deg(0.5),
            turn: cos_deg(62.0),
            agree: cos_deg(10.0),
            solid_crease: cos_deg(40.0),
            splayed: cos_deg(10.0),
        }
    }
}

const SLOPE: f64 = 0.05;
const CURVE_RATIO: f64 = 2.5;
const SLIVER: f64 = 4.0;
const FIT: f64 = 0.35;
const IN_PLANE: f64 = 0.5;
const BOW: f64 = 1e-5;

fn pair_key(a: u32, b: u32) -> u64 {
    (u64::from(a) << 32) | u64::from(b)
}

/// A triangle's corner at a place.
#[derive(Clone)]
struct At {
    normal: D3,
    normal_index: u32,
    face: D3,
    other: [u32; 2],
}

/// Solves the 3x3 symmetric positive semi-definite system m x = r; an unknown the system does not determine is taken as zero.
fn solve3(mut m: [[f64; 3]; 3], mut r: [f64; 3]) -> [f64; 3] {
    let scale = (m[0][0] + m[1][1] + m[2][2]).max(1e-300);
    for c in 0..3 {
        let mut pivot = c;
        for k in c + 1..3 {
            if m[k][c].abs() > m[pivot][c].abs() {
                pivot = k;
            }
        }
        if m[pivot][c].abs() < 1e-12 * scale {
            m[c] = [0.0; 3];
            m[c][c] = 1.0;
            r[c] = 0.0;
            pivot = c;
        }
        m.swap(c, pivot);
        r.swap(c, pivot);
        for k in 0..3 {
            if k == c || m[k][c] == 0.0 {
                continue;
            }
            let f = m[k][c] / m[c][c];
            for j in 0..3 {
                m[k][j] -= f * m[c][j];
            }
            r[k] -= f * r[c];
        }
    }
    [r[0] / m[0][0], r[1] / m[1][1], r[2] / m[2][2]]
}

/// The angle from unit n to the convex cone the unit vectors `generators` span (zero inside it).
fn cone_angle(n: &D3, generators: &[D3]) -> f64 {
    let mut best = PI;
    for g in generators {
        best = best.min(angle_between(n, g));
    }
    for i in 0..generators.len() {
        for j in i + 1..generators.len() {
            let (a, b) = (&generators[i], &generators[j]);
            let ab = dot(a, b);
            let det = 1.0 - ab * ab;
            if det >= 1e-12 {
                let (na, nb) = (dot(n, a), dot(n, b));
                let x = (na - ab * nb) / det;
                let y = (nb - ab * na) / det;
                if x >= 0.0 && y >= 0.0 {
                    let projection = add(&mul(a, x), &mul(b, y));
                    let l = length(&projection);
                    if l > 0.0 && dot(n, &projection) > 0.0 {
                        best = best.min(angle_between(n, &mul(&projection, 1.0 / l)));
                    }
                }
            }
            for c in &generators[j + 1..] {
                let det3 = dot(a, &cross(b, c));
                if det3.abs() < 1e-12 {
                    continue;
                }
                if dot(n, &cross(b, c)) / det3 >= 0.0 && dot(n, &cross(c, a)) / det3 >= 0.0 && dot(n, &cross(a, b)) / det3 >= 0.0 {
                    return 0.0;
                }
            }
        }
    }
    best
}

struct Side {
    tangent: D3,
    curvature: f64,
    edge: f64,
    flat: bool,
}

/// Whether a group of corners at place q, with normal n0, is a sampled surface.
fn sampled(lim: &Limits, q: u32, n0: &D3, group: &[&At], places: &[D3]) -> bool {
    for i in 0..group.len() {
        if dot(&group[i].face, n0).abs() < lim.normal_to_face {
            return false;
        }
        for j in i + 1..group.len() {
            if dot(&group[i].face, &group[j].face).abs() < lim.face_to_face {
                return false;
            }
        }
    }
    let oriented: Vec<D3> = group.iter().map(|at| if dot(&at.face, n0) >= 0.0 { at.face } else { mul(&at.face, -1.0) }).collect();
    if oriented.is_empty() || cone_angle(n0, &oriented) > lim.cone {
        return false;
    }

    // Each neighbour's normal curvature 2 h / |d|^2 (h its height over the tangent plane) along its tangent direction.
    let pq = places[q as usize];
    let u = normalized(&cross(n0, &if n0[0].abs() < 0.9 { [1.0, 0.0, 0.0] } else { [0.0, 1.0, 0.0] }));
    let v = cross(n0, &u);
    let mut neighbours: Vec<u32> = group.iter().flat_map(|at| at.other).collect();
    neighbours.sort_unstable();
    neighbours.dedup();
    let mut sides: Vec<Side> = Vec::with_capacity(neighbours.len());
    for &j in &neighbours {
        let d = sub(&places[j as usize], &pq);
        let dd = dot(&d, &d);
        let h = dot(&d, n0);
        let t = sub(&d, &mul(n0, h));
        let tl = length(&t);
        if tl < 1e-6 * dd.sqrt() {
            continue;
        }
        sides.push(Side { tangent: mul(&t, 1.0 / tl), curvature: 2.0 * h / dd, edge: dd.sqrt(), flat: h.abs() <= lim.flat * dd.sqrt() });
    }
    // The same curvature on opposite sides: against the neighbour most nearly opposite.
    for i in 0..sides.len() {
        let mut best = sides.len();
        for j in 0..sides.len() {
            let c = dot(&sides[i].tangent, &sides[j].tangent);
            if j != i && c <= lim.opposite && (best == sides.len() || c < dot(&sides[i].tangent, &sides[best].tangent)) {
                best = j;
            }
        }
        if best == sides.len() {
            continue;
        }
        let (a, b) = (&sides[i], &sides[best]);
        if a.flat && b.flat {
            continue;
        }
        if a.flat || b.flat || a.curvature * b.curvature <= 0.0 {
            return false;
        }
        if a.curvature.abs().max(b.curvature.abs()) > CURVE_RATIO * a.curvature.abs().min(b.curvature.abs()) {
            return false;
        }
    }
    // Euler: one quadratic form of the tangent direction gives every neighbour's normal curvature, by least squares.
    if sides.len() > 3 {
        let mut m = [[0.0f64; 3]; 3];
        let mut rhs = [0.0f64; 3];
        let mut forms: Vec<[f64; 3]> = Vec::with_capacity(sides.len());
        for side in &sides {
            let x = dot(&side.tangent, &u);
            let y = dot(&side.tangent, &v);
            let f = [x * x, 2.0 * x * y, y * y];
            forms.push(f);
            for i in 0..3 {
                rhs[i] += f[i] * side.curvature;
                for j in 0..3 {
                    m[i][j] += f[i] * f[j];
                }
            }
        }
        let e = solve3(m, rhs);
        for i in 0..sides.len() {
            let fitted = e[0] * forms[i][0] + e[1] * forms[i][1] + e[2] * forms[i][2];
            if (sides[i].curvature - fitted).abs() * 0.5 * sides[i].edge > SLOPE {
                return false;
            }
        }
    }

    // One shape operator explains every face: its tilt from n0 is the operator on its circumcentre's offset.
    struct Row {
        eu: f64,
        ev: f64,
        tu: f64,
        tv: f64,
    }
    let mut rows: Vec<Row> = Vec::with_capacity(group.len());
    for i in 0..group.len() {
        let centre = circumcentre(&sub(&places[group[i].other[0] as usize], &pq), &sub(&places[group[i].other[1] as usize], &pq));
        if is_zero(&centre) {
            continue;
        }
        rows.push(Row { eu: dot(&centre, &u), ev: dot(&centre, &v), tu: dot(&oriented[i], &u), tv: dot(&oriented[i], &v) });
    }
    let tilt2: f64 = rows.iter().map(|r| r.tu * r.tu + r.tv * r.tv).sum();
    if tilt2 > lim.flat * lim.flat * rows.len() as f64 {
        let mut m = [[0.0f64; 3]; 3];
        let mut rhs = [0.0f64; 3];
        for r in &rows {
            let coeffs = [[r.eu, r.ev, 0.0], [0.0, r.eu, r.ev]];
            let t = [r.tu, r.tv];
            for e in 0..2 {
                for i in 0..3 {
                    rhs[i] += coeffs[e][i] * t[e];
                    for j in 0..3 {
                        m[i][j] += coeffs[e][i] * coeffs[e][j];
                    }
                }
            }
        }
        let s = solve3(m, rhs);
        let mut residual = 0.0;
        for r in &rows {
            let du = r.tu - s[0] * r.eu - s[1] * r.ev;
            let dv = r.tv - s[1] * r.eu - s[2] * r.ev;
            residual += du * du + dv * dv;
        }
        if residual > FIT * FIT * tilt2 {
            return false;
        }
    }

    // A sliver shows curvature only across itself.
    let mut lengthwise: Vec<u32> = Vec::new();
    let mut across: Vec<u32> = Vec::new();
    for at in group {
        let (pa, pb) = (&places[at.other[0] as usize], &places[at.other[1] as usize]);
        let e = [length(&sub(pa, &pq)), length(&sub(pb, &pq)), length(&sub(pa, pb))];
        let height = length(&cross(&sub(pa, &pq), &sub(pb, &pq))) / e[0].max(e[1]).max(e[2]);
        for k in 0..2 {
            let set = if e[k] > SLIVER * height { &mut lengthwise } else { &mut across };
            if !set.contains(&at.other[k]) {
                set.push(at.other[k]);
            }
        }
    }
    let curvature = |j: u32| {
        let d = sub(&places[j as usize], &pq);
        let dd = dot(&d, &d);
        let h = dot(&d, n0);
        if h.abs() <= lim.flat * dd.sqrt() {
            0.0
        } else {
            (2.0 * h / dd).abs()
        }
    };
    let mut bound = 0.0f64;
    for &j in &across {
        if !lengthwise.contains(&j) {
            bound = bound.max(curvature(j));
        }
    }
    lengthwise.iter().all(|&j| curvature(j) <= bound)
}

/// Whether a group of corners with normal n0 is a flat solid's corner shaded soft: its faces in at most three planes, each 40
/// degrees or more from the others, and the normal 10 degrees or more off one of them.
fn flat_solid(lim: &Limits, n0: &D3, group: &[&At]) -> bool {
    let mut planes: Vec<D3> = Vec::with_capacity(3);
    for c in group {
        if planes.iter().any(|p| dot(p, &c.face) >= lim.parallel) {
            continue;
        }
        if planes.len() == 3 {
            return false;
        }
        planes.push(c.face);
    }
    let mut splayed = false;
    for i in 0..planes.len() {
        for j in i + 1..planes.len() {
            if dot(&planes[i], &planes[j]) > lim.solid_crease {
                return false;
            }
        }
        splayed = splayed || dot(&planes[i], n0).abs() < lim.splayed;
    }
    !planes.is_empty() && splayed
}

/// T-junctions: (inner place, edge end, edge end) where a place lies inside another edge. Places are entered in a grid of the
/// median edge's size; each edge looks in the cells along it.
fn find_junctions(places: &[D3], edges: &[u64], out: &mut Vec<[u32; 3]>) {
    if edges.is_empty() {
        return;
    }
    let mut lengths: Vec<f64> = edges.iter().map(|&e| length(&sub(&places[(e >> 32) as usize], &places[(e & 0xFFFF_FFFF) as usize]))).collect();
    let mid = lengths.len() / 2;
    let (_, median, _) = lengths.select_nth_unstable_by(mid, f64::total_cmp);
    let cell = median.max(1e-3);
    let cell_of = |p: &D3| [(p[0] / cell).floor() as i64, (p[1] / cell).floor() as i64, (p[2] / cell).floor() as i64];
    let key_of = |c: &[i64; 3]| ((c[0] as u64 & 0x1F_FFFF) << 42) | ((c[1] as u64 & 0x1F_FFFF) << 21) | (c[2] as u64 & 0x1F_FFFF);
    let mut grid: HashMap<u64, Vec<u32>> = HashMap::new();
    for (i, p) in places.iter().enumerate() {
        grid.entry(key_of(&cell_of(p))).or_default().push(i as u32);
    }
    for &e in edges {
        let a = (e >> 32) as u32;
        let b = (e & 0xFFFF_FFFF) as u32;
        let pa = places[a as usize];
        let d = sub(&places[b as usize], &pa);
        let dd = dot(&d, &d);
        if dd.partial_cmp(&0.0) != Some(std::cmp::Ordering::Greater) {
            continue;
        }
        let steps = (dd.sqrt() / (0.5 * cell)) as u64 + 1;
        // The samples' cells move monotonically along each axis: each cell of the edge's blocks is visited once.
        let mut previous: Option<[i64; 3]> = None;
        for s in 0..=steps {
            let c = cell_of(&add(&pa, &mul(&d, s as f64 / steps as f64)));
            if previous == Some(c) {
                continue;
            }
            for dx in -1..=1i64 {
                for dy in -1..=1i64 {
                    for dz in -1..=1i64 {
                        let x = [c[0] + dx, c[1] + dy, c[2] + dz];
                        if let Some(prev) = previous {
                            if (x[0] - prev[0]).abs() <= 1 && (x[1] - prev[1]).abs() <= 1 && (x[2] - prev[2]).abs() <= 1 {
                                continue;
                            }
                        }
                        let Some(run) = grid.get(&key_of(&x)) else { continue };
                        for &p in run {
                            if p == a || p == b {
                                continue;
                            }
                            let w = sub(&places[p as usize], &pa);
                            let t = dot(&w, &d) / dd;
                            if t <= 1e-4 || t >= 1.0 - 1e-4 {
                                continue;
                            }
                            let off = sub(&w, &mul(&d, t));
                            if dot(&off, &off) <= 1e-8 * dd {
                                out.push([p, a, b]);
                            }
                        }
                    }
                }
            }
            previous = Some(c);
        }
    }
}

/// A unit normal in 32 bits (octahedral, two 16-bit halves each from 1 to 65535), zero for the zero vector.
pub fn pack_normal(n: &Vec3) -> u32 {
    let sum = n[0].abs() + n[1].abs() + n[2].abs();
    if sum.partial_cmp(&0.0) != Some(std::cmp::Ordering::Greater) {
        return 0;
    }
    let mut x = n[0] / sum;
    let mut y = n[1] / sum;
    if n[2] < 0.0 {
        let fx = (1.0 - y.abs()) * if x >= 0.0 { 1.0 } else { -1.0 };
        let fy = (1.0 - x.abs()) * if y >= 0.0 { 1.0 } else { -1.0 };
        x = fx;
        y = fy;
    }
    let half = |v: f32| ((v.clamp(-1.0, 1.0) * 32767.0).round() as i32 + 32768) as u32;
    (half(x) << 16) | half(y)
}

pub fn unpack_normal(packed: u32) -> Vec3 {
    if packed == 0 {
        return [0.0; 3];
    }
    let x = ((packed >> 16) as f32 - 32768.0) / 32767.0;
    let y = ((packed & 0xFFFF) as f32 - 32768.0) / 32767.0;
    let mut n = [x, y, 1.0 - x.abs() - y.abs()];
    if n[2] < 0.0 {
        n[0] = (1.0 - y.abs()) * if x >= 0.0 { 1.0 } else { -1.0 };
        n[1] = (1.0 - x.abs()) * if y >= 0.0 { 1.0 } else { -1.0 };
    }
    let l = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt();
    [n[0] / l, n[1] / l, n[2] / l]
}

/// What the analysis left for each position of a model.
#[derive(Default, Clone)]
pub struct Curves {
    pub bends: bool,
    pub places: Vec<u32>, // one u32 place ID per source position
    pub neighbour: Vec<u32>, // first crease neighbour, NO_PLACE for none
    pub second: Vec<u32>, // second crease neighbour
    pub crease: Vec<u32>, // packed
    pub first: Vec<u32>,  // positions + 1
    pub bend_normal: Vec<u32>,
    pub bend: Vec<u32>,
    pub bend_flat: Vec<bool>,
}

impl Curves {
    pub fn bend_of(&self, position: u32, normal: u32) -> u32 {
        if position as usize + 1 >= self.first.len() {
            return 0;
        }
        for i in self.first[position as usize]..self.first[position as usize + 1] {
            if u32::from(self.bend_normal[i as usize]) == normal {
                return self.bend[i as usize];
            }
        }
        0
    }
    pub fn flat_of(&self, position: u32, normal: u32) -> bool {
        if position as usize + 1 >= self.first.len() {
            return false;
        }
        for i in self.first[position as usize]..self.first[position as usize + 1] {
            if u32::from(self.bend_normal[i as usize]) == normal {
                return self.bend_flat[i as usize];
            }
        }
        false
    }
}

/// The analysis of a model's triangles (three (position, normal) corners each). None when nothing bends and no corner is flat.
pub fn analyse(positions: &[Vec3], normals: &[Vec3], corners: &[(u32, u32)]) -> Option<Curves> {
    let lim = Limits::new();
    // Places: positions equal by value (-0 as 0; a NaN equals nothing), numbered in the order they first appear.
    let bits = |f: f32| (f + 0.0).to_bits();
    let mut place = vec![0u32; positions.len()];
    let mut places: Vec<D3> = Vec::with_capacity(positions.len());
    {
        let mut keys: Vec<(u32, u32, u32, u32)> = positions.iter().enumerate().map(|(i, p)| (bits(p[0]), bits(p[1]), bits(p[2]), i as u32)).collect();
        keys.sort_unstable();
        let mut first_of = vec![0u32; positions.len()];
        let mut b = 0;
        while b < keys.len() {
            let p = &positions[keys[b].3 as usize];
            let unequal = p[0].is_nan() || p[1].is_nan() || p[2].is_nan();
            let mut e = b + 1;
            while !unequal && e < keys.len() && keys[e].0 == keys[b].0 && keys[e].1 == keys[b].1 && keys[e].2 == keys[b].2 {
                e += 1;
            }
            for k in &keys[b..e] {
                first_of[k.3 as usize] = keys[b].3;
            }
            b = e;
        }
        for i in 0..positions.len() {
            if first_of[i] as usize == i {
                place[i] = places.len() as u32;
                places.push(widen(&positions[i]));
            } else {
                place[i] = place[first_of[i] as usize];
            }
        }
    }
    let place_count = places.len();
    if place_count >= NO_PLACE as usize {
        return None;
    }
    let unit_normals: Vec<D3> = normals.iter().map(|n| normalized(&widen(n))).collect();

    struct PlaceCorner {
        place: u32,
        at: At,
    }
    let mut place_corners: Vec<PlaceCorner> = Vec::with_capacity(corners.len());
    let mut edges: Vec<u64> = Vec::with_capacity(corners.len());
    let mut edge_index: Vec<(u64, u32)> = Vec::with_capacity(2 * corners.len());
    let mut edge_ends: Vec<(u64, D3)> = Vec::with_capacity(2 * corners.len());
    let mut junctions: Vec<[u32; 3]> = Vec::new();
    for tri in corners.chunks_exact(3) {
        if tri.iter().any(|c| c.0 as usize >= positions.len() || c.1 as usize >= normals.len()) {
            continue;
        }
        let p = [place[tri[0].0 as usize], place[tri[1].0 as usize], place[tri[2].0 as usize]];
        let n = [unit_normals[tri[0].1 as usize], unit_normals[tri[1].1 as usize], unit_normals[tri[2].1 as usize]];
        if p[0] == p[1] || p[1] == p[2] || p[0] == p[2] {
            continue;
        }
        for k in 0..3 {
            let (a, b) = (p[k], p[(k + 1) % 3]);
            edges.push(pair_key(a.min(b), a.max(b)));
            for j in [(k + 1) % 3, (k + 2) % 3] {
                edge_index.push((pair_key(p[k], p[j]), tri[k].1));
            }
        }
        let (p0, p1, p2) = (&places[p[0] as usize], &places[p[1] as usize], &places[p[2] as usize]);
        let face = normalized(&cross(&sub(p1, p0), &sub(p2, p0)));
        if is_zero(&face) {
            // Collinear corners: the middle one lies inside the edge of the other two.
            for k in 0..3 {
                let (a, b) = (p[(k + 1) % 3], p[(k + 2) % 3]);
                let d = sub(&places[b as usize], &places[a as usize]);
                let s = dot(&sub(&places[p[k] as usize], &places[a as usize]), &d) / dot(&d, &d).max(1e-300);
                if s > 0.0 && s < 1.0 {
                    junctions.push([p[k], a, b]);
                }
            }
            continue;
        }
        for k in 0..3 {
            let (o0, o1) = (p[(k + 1) % 3], p[(k + 2) % 3]);
            place_corners.push(PlaceCorner { place: p[k], at: At { normal: n[k], normal_index: tri[k].1, face, other: [o0, o1] } });
            edge_ends.push((pair_key(p[k], o0), n[k]));
            edge_ends.push((pair_key(p[k], o1), n[k]));
        }
    }
    edges.sort_unstable();
    edges.dedup();
    edge_index.sort_unstable();
    edge_index.dedup();

    // Each place's corners in triangle order.
    let mut corner_first = vec![0u32; place_count + 1];
    for pc in &place_corners {
        corner_first[pc.place as usize + 1] += 1;
    }
    for q in 0..place_count {
        corner_first[q + 1] += corner_first[q];
    }
    let mut at: Vec<Option<At>> = vec![None; place_corners.len()];
    {
        let mut next = corner_first[..place_count].to_vec();
        for pc in place_corners {
            at[next[pc.place as usize] as usize] = Some(pc.at);
            next[pc.place as usize] += 1;
        }
    }
    let at: Vec<At> = at.into_iter().map(|a| a.expect("every slot of the corner table is filled")).collect();

    // Crease edges: their triangles disagree on the normal at the near end (against the first), or one triangle holds them.
    edge_ends.sort_by_key(|e| e.0);
    let mut crease_keys: Vec<u64> = Vec::new();
    let mut b = 0;
    while b < edge_ends.len() {
        let mut e = b + 1;
        let mut agree = dot(&edge_ends[b].1, &edge_ends[b].1).abs() >= lim.parallel;
        while e < edge_ends.len() && edge_ends[e].0 == edge_ends[b].0 {
            agree = agree && dot(&edge_ends[b].1, &edge_ends[e].1).abs() >= lim.parallel;
            e += 1;
        }
        if e - b == 1 || !agree {
            crease_keys.push(edge_ends[b].0);
        }
        b = e;
    }
    let mut crease_first = vec![0u32; place_count + 1];
    for &key in &crease_keys {
        crease_first[(key >> 32) as usize + 1] += 1;
    }
    for q in 0..place_count {
        crease_first[q + 1] += crease_first[q];
    }
    let is_crease = |q: usize, j: u32| crease_keys[crease_first[q] as usize..crease_first[q + 1] as usize].binary_search(&pair_key(q as u32, j)).is_ok();

    let mut crease_places = vec![[NO_PLACE; 2]; place_count];
    let mut bends: Vec<(u32, D3)> = Vec::new();
    let mut bend_first = vec![0u32; place_count + 1];
    let mut flats: Vec<u32> = Vec::new();
    let mut flat_first = vec![0u32; place_count + 1];
    let mut crease_normal = vec![[0.0f64; 3]; place_count];
    fn root(sets: &mut [u32], mut x: u32) -> u32 {
        while sets[x as usize] != x {
            sets[x as usize] = sets[sets[x as usize] as usize];
            x = sets[x as usize];
        }
        x
    }
    fn join(sets: &mut [u32], a: u32, b: u32) {
        let (ra, rb) = (root(sets, a), root(sets, b));
        sets[ra as usize] = rb;
    }
    #[derive(Clone, Copy, Default)]
    struct SetBend {
        any: bool,
        agree: bool,
        value: D3,
    }
    for q in 0..place_count {
        bend_first[q] = bends.len() as u32;
        flat_first[q] = flats.len() as u32;
        let cs = &at[corner_first[q] as usize..corner_first[q + 1] as usize];
        let count = cs.len();
        if count == 0 {
            continue;
        }
        // Groups: corners joined through their triangles' shared non-crease edges at q; each group's corners in triangle order.
        let mut corner_sets: Vec<u32> = (0..count as u32).collect();
        let mut through: Vec<(u32, u32)> = Vec::new();
        for (i, c) in cs.iter().enumerate() {
            for &j in &c.other {
                if is_crease(q, j) {
                    continue;
                }
                match through.iter().find(|e| e.0 == j) {
                    None => through.push((j, i as u32)),
                    Some(e) => {
                        let other = e.1;
                        join(&mut corner_sets, i as u32, other);
                    }
                }
            }
        }
        let mut roots: Vec<u32> = Vec::new();
        let mut group_of = vec![0u32; count];
        for i in 0..count {
            let r = root(&mut corner_sets, i as u32);
            group_of[i] = match roots.iter().position(|&x| x == r) {
                Some(g) => g as u32,
                None => {
                    roots.push(r);
                    roots.len() as u32 - 1
                }
            };
        }
        let groups = roots.len();
        let mut members: Vec<Vec<usize>> = vec![Vec::new(); groups];
        for i in 0..count {
            members[group_of[i] as usize].push(i);
        }
        let group_refs: Vec<Vec<&At>> = members.iter().map(|m| m.iter().map(|&i| &cs[i]).collect()).collect();
        let multi = groups > 1;
        let crease_count = (crease_first[q + 1] - crease_first[q]) as usize;
        if multi && crease_count != 2 {
            // A corner: nothing bends.
            for c in cs {
                bends.push((c.normal_index, [0.0; 3]));
            }
            for g in &group_refs {
                if flat_solid(&lim, &g[0].normal, g) {
                    flats.extend(g.iter().map(|c| c.normal_index));
                }
            }
            bends[bend_first[q] as usize..].sort_by_key(|b| b.0);
            continue;
        }
        let mut crease_plane = [0.0f64; 3];
        if multi {
            let c0 = (crease_keys[crease_first[q] as usize] & 0xFFFF_FFFF) as u32;
            let c1 = (crease_keys[crease_first[q] as usize + 1] & 0xFFFF_FFFF) as u32;
            crease_places[q] = [c0 as u32, c1 as u32];
            let (a, bb) = (places[c0 as usize], places[c1 as usize]);
            let u = sub(&places[q], &a);
            let v = sub(&bb, &places[q]);
            let (lu, lv) = (length(&u), length(&v));
            let turn = if lu > 0.0 && lv > 0.0 { dot(&u, &v) / (lu * lv) } else { 1.0 };
            if turn <= lim.straight && turn >= lim.turn && lu.max(lv) <= CURVE_RATIO * lu.min(lv) {
                let ea = sub(&a, &places[q]);
                let eb = sub(&bb, &places[q]);
                crease_normal[q] = normalized(&circumcentre(&ea, &eb));
                crease_plane = normalized(&cross(&ea, &eb));
            }
        }
        // Normal indices one set per group, sets joined where groups share an index: one bend a set, the groups' own where they
        // agree, else none.
        let mut slot_index: Vec<u32> = Vec::new();
        let mut slot_of = vec![0u32; count];
        for i in 0..count {
            slot_of[i] = match slot_index.iter().position(|&x| x == cs[i].normal_index) {
                Some(s) => s as u32,
                None => {
                    slot_index.push(cs[i].normal_index);
                    slot_index.len() as u32 - 1
                }
            };
        }
        let mut slot_sets: Vec<u32> = (0..slot_index.len() as u32).collect();
        for m in &members {
            let lead = slot_of[m[0]];
            for &i in m {
                join(&mut slot_sets, slot_of[i], lead);
            }
        }
        let mut set_bends = vec![SetBend { any: false, agree: true, value: [0.0; 3] }; slot_index.len()];
        let mut backed = false;
        for (g, m) in group_refs.iter().enumerate() {
            let n0 = m[0].normal;
            let curved = sampled(&lim, q as u32, &n0, m, &places);
            if !curved && flat_solid(&lim, &n0, m) {
                flats.extend(m.iter().map(|c| c.normal_index));
            }
            let value = if curved { n0 } else { [0.0; 3] };
            let r = root(&mut slot_sets, slot_of[members[g][0]]) as usize;
            let set = &mut set_bends[r];
            if !set.any {
                set.any = true;
                set.value = value;
            }
            set.agree = set.agree && value == set.value;
            if curved && !is_zero(&crease_normal[q]) {
                let in_plane = sub(&n0, &mul(&crease_plane, dot(&n0, &crease_plane)));
                let l = length(&in_plane);
                backed = backed || (l >= IN_PLANE && dot(&in_plane, &crease_normal[q]).abs() >= lim.agree * l);
            }
        }
        if !backed {
            crease_normal[q] = [0.0; 3];
        }
        for slot in 0..slot_index.len() {
            let set = set_bends[root(&mut slot_sets, slot as u32) as usize];
            bends.push((slot_index[slot], if set.any && set.agree { set.value } else { [0.0; 3] }));
        }
        bends[bend_first[q] as usize..].sort_by_key(|b| b.0);
    }
    bend_first[place_count] = bends.len() as u32;
    flat_first[place_count] = flats.len() as u32;

    // T-junctions (and zero-area triangles' corners): where any edge along the line would bend, the three places stay put.
    find_junctions(&places, &edges, &mut junctions);
    let bend_at = |x: u32, normal_index: u32, y: u32| -> D3 {
        let c = crease_places[x as usize];
        if u32::from(c[0]) == y || u32::from(c[1]) == y {
            return crease_normal[x as usize];
        }
        for k in bend_first[x as usize]..bend_first[x as usize + 1] {
            if bends[k as usize].0 == normal_index {
                return bends[k as usize].1;
            }
        }
        [0.0; 3]
    };
    let mut fixed = vec![false; place_count];
    for j in &junctions {
        let line = normalized(&sub(&places[j[2] as usize], &places[j[1] as usize]));
        let mut bows = false;
        for &x in j {
            for &y in j {
                if x == y {
                    continue;
                }
                let key = pair_key(x, y);
                let mut i = edge_index.partition_point(|e| e.0 < key);
                while i < edge_index.len() && edge_index[i].0 == key {
                    bows = bows || dot(&bend_at(x, edge_index[i].1, y), &line).abs() > BOW;
                    i += 1;
                }
            }
        }
        if bows {
            for &x in j {
                fixed[x as usize] = true;
            }
        }
    }

    // Per place, then per position.
    let bends_at = |v: &Vec3| v[0] != 0.0 || v[1] != 0.0 || v[2] != 0.0;
    let mut place_first = vec![0u32; place_count + 1];
    let mut out_bends: Vec<(u32, Vec3, bool)> = Vec::with_capacity(bends.len());
    let mut crease: Vec<Vec3> = vec![[0.0; 3]; place_count];
    for q in 0..place_count {
        place_first[q] = out_bends.len() as u32;
        let flat = &flats[flat_first[q] as usize..flat_first[q + 1] as usize];
        for k in bend_first[q]..bend_first[q + 1] {
            let (index, bend) = bends[k as usize];
            out_bends.push((index, if fixed[q] { [0.0; 3] } else { narrow(&bend) }, flat.contains(&index)));
        }
        crease[q] = if fixed[q] { [0.0; 3] } else { narrow(&crease_normal[q]) };
    }
    place_first[place_count] = out_bends.len() as u32;
    let any_bend = crease.iter().any(bends_at) || out_bends.iter().any(|b| bends_at(&b.1));
    let any_flat = out_bends.iter().any(|b| b.2);
    if !any_bend && !any_flat {
        return None;
    }
    let mut model = Curves {
        bends: any_bend,
        places: vec![0; positions.len()],
        neighbour: vec![NO_PLACE; positions.len()],
        second: vec![0; positions.len()],
        crease: vec![0; positions.len()],
        first: vec![0; positions.len() + 1],
        ..Curves::default()
    };
    for i in 0..positions.len() {
        let q = place[i] as usize;
        model.first[i] = model.bend.len() as u32;
        model.places[i] = q as u32;
        model.neighbour[i] = crease_places[q][0];
        model.second[i] = crease_places[q][1];
        model.crease[i] = pack_normal(&crease[q]);
        for b in &out_bends[place_first[q] as usize..place_first[q + 1] as usize] {
            model.bend_normal.push(b.0);
            model.bend.push(pack_normal(&b.1));
            model.bend_flat.push(b.2);
        }
    }
    model.first[positions.len()] = model.bend.len() as u32;
    Some(model)
}

const MOST_LEVELS: u32 = 3; // eight segments an edge
const OF_EXTENT: f32 = 1.0 / 800.0;
/// How far a refined edge may stand off its arc, in model units.
pub const TOLERANCE: f32 = 0.4;

/// A corner as refinement mixes it.
#[derive(Clone, Default)]
pub struct Vertex {
    pub position: [f32; 3],
    pub normal: [f32; 3],
    pub tangent: [f32; 3],
    pub binormal: [f32; 3],
    /// By coordinate number; refinement mixes corners of one group, which carry as many.
    pub uv: Vec<[f32; 2]>,
    pub color0: u32,
    pub color1: u32,
    pub matrix: u32,
    pub bend: u32,
    pub crease: u32,
    pub place: u32,
    pub neighbour: u32,
    pub second: u32,
    pub flat: bool,
}

fn refine_edge_normal(a: &Vertex, b: &Vertex) -> Vec3 {
    let place = b.place;
    let crease = place != NO_PLACE && (place == a.neighbour || place == u32::from(a.second));
    unpack_normal(if crease { a.crease } else { a.bend })
}

/// How many times the edge halves: the arc of a chord of length L turning by angle t stands L t / 8 off it, and n segments
/// divide that by n squared.
fn refine_level(a: &Vertex, b: &Vertex, tolerance: f32) -> u32 {
    if a.matrix != b.matrix {
        return 0;
    }
    let e = [b.position[0] - a.position[0], b.position[1] - a.position[1], b.position[2] - a.position[2]];
    let length = (e[0] * e[0] + e[1] * e[1] + e[2] * e[2]).sqrt();
    if length.partial_cmp(&0.0) != Some(std::cmp::Ordering::Greater) {
        return 0;
    }
    let (na, nb) = (refine_edge_normal(a, b), refine_edge_normal(b, a));
    let tilt = (e[0] * na[0] + e[1] * na[1] + e[2] * na[2]).abs().max((e[0] * nb[0] + e[1] * nb[1] + e[2] * nb[2]).abs()) / length;
    if tilt.partial_cmp(&0.0) != Some(std::cmp::Ordering::Greater) {
        return 0;
    }
    let off = length * 2.0 * tilt.min(1.0).asin() / (8.0 * tolerance);
    let mut level = 0;
    let mut n2 = 1.0f32;
    while n2 < off && level < MOST_LEVELS {
        level += 1;
        n2 *= 4.0;
    }
    level
}

fn mix_colour(a: u32, b: u32, c: u32, w: &[f32; 3]) -> u32 {
    let mut out = 0u32;
    for shift in (0..32).step_by(8) {
        let v = ((a >> shift) & 0xFF) as f32 * w[0] + ((b >> shift) & 0xFF) as f32 * w[1] + ((c >> shift) & 0xFF) as f32 * w[2];
        out |= ((v + 0.5) as u32) << shift;
    }
    out
}

fn refine_point(t: &[&Vertex; 3], w: &[f32; 3]) -> Vertex {
    let mut v = t[if w[0] > 0.0 { 0 } else { 1 }].clone();
    let mix3 = |a: &[f32; 3], b: &[f32; 3], c: &[f32; 3]| [a[0] * w[0] + b[0] * w[1] + c[0] * w[2], a[1] * w[0] + b[1] * w[1] + c[1] * w[2], a[2] * w[0] + b[2] * w[1] + c[2] * w[2]];
    v.position = mix3(&t[0].position, &t[1].position, &t[2].position);
    v.normal = mix3(&t[0].normal, &t[1].normal, &t[2].normal);
    v.tangent = mix3(&t[0].tangent, &t[1].tangent, &t[2].tangent);
    v.binormal = mix3(&t[0].binormal, &t[1].binormal, &t[2].binormal);
    for k in 0..v.uv.len() {
        let at = |n: usize| t[n].uv.get(k).copied().unwrap_or([0.0; 2]);
        for a in 0..2 {
            v.uv[k][a] = at(0)[a] * w[0] + at(1)[a] * w[1] + at(2)[a] * w[2];
        }
    }
    let length2 = v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2];
    if length2 > 0.0 {
        let scale = 1.0 / length2.sqrt();
        v.normal = [v.normal[0] * scale, v.normal[1] * scale, v.normal[2] * scale];
    }
    v.color0 = mix_colour(t[0].color0, t[1].color0, t[2].color0, w);
    v.color1 = mix_colour(t[0].color1, t[1].color1, t[2].color1, w);
    let mut movement = [0.0f32; 3];
    for i in 0..3 {
        let k = (i + 1) % 3;
        let both = w[i] * w[k];
        if both == 0.0 {
            continue;
        }
        let e = [t[k].position[0] - t[i].position[0], t[k].position[1] - t[i].position[1], t[k].position[2] - t[i].position[2]];
        let (ni, nk) = (refine_edge_normal(t[i], t[k]), refine_edge_normal(t[k], t[i]));
        let di = e[0] * ni[0] + e[1] * ni[1] + e[2] * ni[2];
        let dk = e[0] * nk[0] + e[1] * nk[1] + e[2] * nk[2];
        for a in 0..3 {
            movement[a] -= both * (di * ni[a] - dk * nk[a]);
        }
    }
    for a in 0..3 {
        v.position[a] += 0.5 * movement[a];
    }
    v
}

#[derive(Clone, Copy)]
struct RefineCorner {
    id: u32,
    w: [f32; 3],
}

struct Refiner<'a> {
    corners: &'a [Vertex],
    added: Vec<Vertex>,
    indices: Vec<u32>,
    points: HashMap<u64, u32>,
    triangle: [u32; 3],
}

impl Refiner<'_> {
    /// The point half way along (a, b): the same one for every triangle holding that edge.
    fn middle(&mut self, a: &RefineCorner, b: &RefineCorner) -> RefineCorner {
        let w = [0.5 * (a.w[0] + b.w[0]), 0.5 * (a.w[1] + b.w[1]), 0.5 * (a.w[2] + b.w[2])];
        let key = (u64::from(a.id.min(b.id)) << 32) | u64::from(a.id.max(b.id));
        if let Some(&id) = self.points.get(&key) {
            return RefineCorner { id, w };
        }
        let id = (self.corners.len() + self.added.len()) as u32;
        let t = [&self.corners[self.triangle[0] as usize], &self.corners[self.triangle[1] as usize], &self.corners[self.triangle[2] as usize]];
        let point = refine_point(&t, &w);
        self.added.push(point);
        self.points.insert(key, id);
        RefineCorner { id, w }
    }

    fn split(&mut self, a: RefineCorner, b: RefineCorner, c: RefineCorner, ab: u32, bc: u32, ca: u32, inside: bool) {
        let most = ab.max(bc).max(ca);
        if most == 0 {
            self.indices.extend_from_slice(&[a.id, b.id, c.id]);
            return;
        }
        if ab != 0 && bc != 0 && ca != 0 {
            let mab = self.middle(&a, &b);
            let mbc = self.middle(&b, &c);
            let mca = self.middle(&c, &a);
            let n = most - 1;
            self.split(a, mab, mca, ab - 1, n, ca - 1, inside);
            self.split(mab, b, mbc, ab - 1, bc - 1, n, inside);
            self.split(mca, mbc, c, n, bc - 1, ca - 1, inside);
            self.split(mab, mbc, mca, n, n, n, inside);
            return;
        }
        // Halve the most bent edge, across from the corner it does not touch.
        let n = if inside { most - 1 } else { 0 };
        if ab == most {
            let m = self.middle(&a, &b);
            self.split(a, m, c, ab - 1, n, ca, inside);
            self.split(m, b, c, ab - 1, bc, n, inside);
        } else if bc == most {
            let m = self.middle(&b, &c);
            self.split(a, b, m, ab, bc - 1, n, inside);
            self.split(a, m, c, n, bc - 1, ca, inside);
        } else {
            let m = self.middle(&c, &a);
            self.split(a, b, m, ab, n, ca - 1, inside);
            self.split(m, b, c, n, bc, ca - 1, inside);
        }
    }
}

/// A group's triangles with the bending ones refined: the vertices it adds behind `corners` and the new triangles.
pub fn refine(corners: &[Vertex], indices: &[u32]) -> (Vec<Vertex>, Vec<u32>) {
    // A group the size of the stage is seen from afar or from on it, where its arc's depth does not show: its tolerance grows
    // with its largest extent, and a sky or a planet costs what it shows.
    let (mut low, mut high) = ([1e30f32; 3], [-1e30f32; 3]);
    for v in corners {
        for a in 0..3 {
            low[a] = low[a].min(v.position[a]);
            high[a] = high[a].max(v.position[a]);
        }
    }
    let tolerance = TOLERANCE.max(OF_EXTENT * (high[0] - low[0]).max(high[1] - low[1]).max(high[2] - low[2]));
    let mut r = Refiner { corners, added: Vec::new(), indices: Vec::with_capacity(indices.len()), points: HashMap::with_capacity(indices.len()), triangle: [0; 3] };
    for tri in indices.chunks_exact(3) {
        let (t0, t1, t2) = (&corners[tri[0] as usize], &corners[tri[1] as usize], &corners[tri[2] as usize]);
        r.triangle = [tri[0], tri[1], tri[2]];
        let one_matrix = t0.matrix == t1.matrix && t1.matrix == t2.matrix;
        let a = RefineCorner { id: tri[0], w: [1.0, 0.0, 0.0] };
        let b = RefineCorner { id: tri[1], w: [0.0, 1.0, 0.0] };
        let c = RefineCorner { id: tri[2], w: [0.0, 0.0, 1.0] };
        r.split(a, b, c, refine_level(t0, t1, tolerance), refine_level(t1, t2, tolerance), refine_level(t2, t0, tolerance), one_matrix);
    }
    (r.added, r.indices)
}

#[cfg(test)]
mod wide_identity_tests {
    use super::*;
    #[test]
    fn crease_adjacency_uses_all_place_bits() {
        let a = Vertex { place: 70000, neighbour: 70001, second: 70002, crease: pack_normal(&[1.0,0.0,0.0]), bend: pack_normal(&[0.0,1.0,0.0]), ..Default::default() };
        let b = Vertex { place: 70001, ..Default::default() };
        let alias = Vertex { place: 70001 & 65535, ..Default::default() };
        assert_eq!(refine_edge_normal(&a,&b),unpack_normal(a.crease));
        assert_eq!(refine_edge_normal(&a,&alias),unpack_normal(a.bend));
    }
}
