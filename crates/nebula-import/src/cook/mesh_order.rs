//! Triangles in the vertex cache's order (Forsyth, "Linear-Speed Vertex Cache Optimisation"): the same triangles, wound as they
//! were, in an order that reuses recent vertices.

const CACHE: usize = 32;
const DECAY_POWER: f32 = 1.5;
const LAST_TRIANGLE: f32 = 0.75;
const VALENCE_SCALE: f32 = 2.0;
const VALENCE_POWER: f32 = 0.5;

fn score(position: i32, active: u32) -> f32 {
    if active == 0 {
        return -1.0;
    }
    let mut s = 0.0;
    if position >= 0 {
        s = if position < 3 { LAST_TRIANGLE } else { (1.0 - (position - 3) as f32 / (CACHE - 3) as f32).powf(DECAY_POWER) };
    }
    s + VALENCE_SCALE * (active as f32).powf(-VALENCE_POWER)
}

pub fn cache_order(indices: &mut [u32], vertices: u32) {
    let triangles = indices.len() / 3;
    if triangles < 2 {
        return;
    }
    let vertices = vertices as usize;
    let mut active = vec![0u32; vertices];
    for &i in indices.iter() {
        active[i as usize] += 1;
    }
    let mut first = vec![0u32; vertices + 1];
    for v in 0..vertices {
        first[v + 1] = first[v] + active[v];
    }
    let mut holding = vec![0u32; indices.len()];
    let mut next = first.clone();
    for (t, tri) in indices.chunks_exact(3).enumerate() {
        for &i in tri {
            holding[next[i as usize] as usize] = t as u32;
            next[i as usize] += 1;
        }
    }
    let mut remaining = active.clone();
    let mut position = vec![-1i32; vertices];
    let mut vertex_score: Vec<f32> = (0..vertices).map(|v| score(-1, active[v])).collect();
    let mut triangle_score = vec![0.0f32; triangles];
    for (t, tri) in indices.chunks_exact(3).enumerate() {
        triangle_score[t] = tri.iter().map(|&i| vertex_score[i as usize]).sum();
    }
    let mut done = vec![false; triangles];
    let mut out = Vec::with_capacity(indices.len());
    let mut cache: Vec<u32> = Vec::with_capacity(CACHE + 3);
    let mut scan = 0usize;
    let mut best: Option<usize> = (0..triangles).max_by(|&a, &b| triangle_score[a].total_cmp(&triangle_score[b]));
    for _ in 0..triangles {
        let t = match best {
            Some(t) => t,
            None => {
                while done[scan] {
                    scan += 1;
                }
                let mut pick = scan;
                for k in scan..triangles {
                    if !done[k] && triangle_score[k] > triangle_score[pick] {
                        pick = k;
                    }
                }
                pick
            }
        };
        done[t] = true;
        let tri = [indices[t * 3], indices[t * 3 + 1], indices[t * 3 + 2]];
        out.extend_from_slice(&tri);
        for &v in &tri {
            remaining[v as usize] -= 1;
            cache.retain(|&c| c != v);
        }
        let mut grown: Vec<u32> = tri.to_vec();
        grown.extend_from_slice(&cache);
        let touched = grown.clone();
        grown.truncate(CACHE);
        for &gone in touched.iter().skip(CACHE) {
            position[gone as usize] = -1;
        }
        cache = grown;
        for (k, &v) in cache.iter().enumerate() {
            position[v as usize] = k as i32;
        }
        best = None;
        let mut best_score = -1.0f32;
        for &v in &touched {
            let v = v as usize;
            let new_score = score(position[v], remaining[v]);
            let delta = new_score - vertex_score[v];
            vertex_score[v] = new_score;
            for &h in &holding[first[v] as usize..first[v + 1] as usize] {
                let h = h as usize;
                if done[h] {
                    continue;
                }
                triangle_score[h] += delta;
                if triangle_score[h] > best_score {
                    best_score = triangle_score[h];
                    best = Some(h);
                }
            }
        }
    }
    indices.copy_from_slice(&out);
}
