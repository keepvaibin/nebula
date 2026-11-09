//! Camera animations (NCAM): a disc's CANM (a value per frame) or CKAN (keys) as eight tracks over one array of floats,
//! the form CameraAnim reads.
//!
//! Layout, little-endian:
//! - header (12 bytes): "NCAM", frames, the value count;
//! - eight tracks (12 bytes): count, first value, stride. Stride 1: a value per frame, linear between frames; 3: keys
//!   (frame, value, slope); 4: keys (frame, value, slope in, slope out); a track of one entry is its first value;
//! - the values;
//! - the cuts: a count, then each cut's step s, ascending (the shot changes between frames s and s + 1).
//!
//! The tracks: position x, y, z, watch position x, y, z, twist, field of view.
//!
//! A cut is the clip's own shot change, so the frames on either side are not blended (CameraAnim signals it as it plays
//! across). The disc marks one as two keys a frame apart with a hold on each side; dense baked motion also has keys a
//! frame apart, so a step is a cut when it moves or turns far more than the steps around it.

use anyhow::{bail, Context, Result};

pub const MAGIC: &[u8; 4] = b"NCAM";
const HEADER: usize = 12;
pub const TRACKS: usize = 8;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Track {
    pub count: u32,
    pub first: u32,
    pub stride: u32,
}

#[derive(Debug, Default, PartialEq)]
pub struct Ncam {
    pub frames: u32,
    pub tracks: [Track; TRACKS],
    pub values: Vec<f32>,
    pub cuts: Vec<u32>,
}

/// A step is a cut when the eye moves more than this many units and this many times the median of the steps around it,
/// or the look direction or the twist turns more than this many degrees and that many times their steps' median.
const CUT_MOVE: f64 = 100.0;
const CUT_TURN_DEGREES: f64 = 20.0;
const CUT_RATIO: f64 = 8.0;
/// The steps on each side a step is measured against.
const CUT_AROUND: usize = 4;

/// The game's hermiteInterpolation (nebula_geometry.hpp), operation for operation.
fn hermite(key: f32, t0: f32, p0: f32, m0: f32, t1: f32, p1: f32, m1: f32) -> f32 {
    let from = key - t0;
    let t = from / (t1 - t0);
    let squared = t * t;
    let twice = t + t;
    let a = squared - t;
    let rise = p0 - p1;
    let mut b = twice * a - squared;
    let mut c = m0 * a + m0;
    b = b * rise + p0;
    c = m1 * a + c;
    c = t * m0 - c;
    b - from * c
}

fn search(values: &[f32], key: f32, first: usize, count: usize, stride: usize) -> usize {
    let (mut low, mut high) = (0usize, count);
    while low < high {
        let middle = (low + high) / 2;
        if values[first + middle * stride] <= key {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    low.wrapping_sub(1)
}

/// A track at `key` as CameraAnim::getTrack reads it.
pub fn read(ncam: &Ncam, track: usize, key: f32) -> Option<f32> {
    let t = ncam.tracks[track];
    let (count, first, stride) = (t.count as usize, t.first as usize, t.stride as usize);
    let values = &ncam.values;
    if count == 1 {
        return values.get(first).copied();
    }
    if stride == 1 {
        let floor = key as u32 as usize;
        let rate = (key - floor as f32).clamp(0.0, 1.0);
        return if floor < count.wrapping_sub(1) {
            Some((1.0 - rate) * *values.get(first + floor)? + rate * *values.get(first + floor + 1)?)
        } else {
            values.get((first + count).wrapping_sub(1)).copied()
        };
    }
    let index = search(values, key, first, count, stride);
    // A key's slope out is its last word; the next key's slope in is its third.
    let v = values.get(first + index.wrapping_mul(stride)..)?;
    Some(hermite(key, *v.first()?, *v.get(1)?, *v.get(stride - 1)? / 30.0, *v.get(stride)?, *v.get(stride + 1)?, *v.get(stride + 2)? / 30.0))
}

fn median_around(steps: &[f64], at: usize) -> f64 {
    let mut around: Vec<f64> = steps[at.saturating_sub(CUT_AROUND)..at].iter().chain(steps.iter().skip(at + 1).take(CUT_AROUND)).copied().collect();
    if around.is_empty() {
        return 0.0;
    }
    around.sort_by(f64::total_cmp);
    around[around.len() / 2]
}

/// The clip's cuts: each step (frame s to s + 1) that jumps against the steps around it.
pub fn find_cuts(ncam: &Ncam) -> Vec<u32> {
    let at = |track: usize, frame: u32| read(ncam, track, frame as f32).map_or(0.0, f64::from);
    let frames = ncam.frames;
    if frames < 2 || ncam.tracks.iter().take(6).any(|t| t.count == 0) {
        return Vec::new();
    }
    let eye = |f: u32| [at(0, f), at(1, f), at(2, f)];
    let look = |f: u32| {
        let (e, w) = (eye(f), [at(3, f), at(4, f), at(5, f)]);
        let d = [w[0] - e[0], w[1] - e[1], w[2] - e[2]];
        let length = (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]).sqrt();
        if length > 0.0 { [d[0] / length, d[1] / length, d[2] / length] } else { [0.0, 0.0, 1.0] }
    };
    let twisted = ncam.tracks[6].count != 0;
    let (mut moves, mut turns, mut twists) = (Vec::new(), Vec::new(), Vec::new());
    for f in 0..frames - 1 {
        let (a, b) = (eye(f), eye(f + 1));
        moves.push(((b[0] - a[0]).powi(2) + (b[1] - a[1]).powi(2) + (b[2] - a[2]).powi(2)).sqrt());
        let (la, lb) = (look(f), look(f + 1));
        turns.push((la[0] * lb[0] + la[1] * lb[1] + la[2] * lb[2]).clamp(-1.0, 1.0).acos().to_degrees());
        twists.push(if twisted { (at(6, f + 1) - at(6, f)).abs() } else { 0.0 });
    }
    let turned = |steps: &[f64], s: usize| steps[s] > CUT_TURN_DEGREES.max(CUT_RATIO * median_around(steps, s));
    (0..moves.len())
        .filter(|&s| {
            (moves[s] > CUT_MOVE && moves[s] > CUT_RATIO * median_around(&moves, s).max(1.0)) || turned(&turns, s) || turned(&twists, s)
        })
        .map(|s| s as u32)
        .collect()
}

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 4).context("past the camera animation's end")?;
    Ok(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
}

fn le32(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 4).context("past the camera animation's end")?;
    Ok(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
}

impl Ncam {
    /// A disc file: "ANDO", then "CANM" or "CKAN", big-endian.
    pub fn from_disc(bytes: &[u8]) -> Result<Ncam> {
        if bytes.len() < 0x20 || &bytes[0..4] != b"ANDO" {
            bail!("not a camera animation");
        }
        let keys = match &bytes[4..8] {
            b"CANM" => false,
            b"CKAN" => true,
            other => bail!("not a camera animation ({:?})", String::from_utf8_lossy(other)),
        };
        // Every disc clip has 1 here; the game refuses a clip with 0, so one is not cooked.
        if be32(bytes, 0x8)? == 0 {
            bail!("a camera animation the game refuses (word 0x8 is 0)");
        }
        let entry = 0x20;
        let words_per_track = if keys { 3 } else { 2 };
        let mut tracks = [Track::default(); TRACKS];
        for (i, track) in tracks.iter_mut().enumerate() {
            let at = entry + 4 * words_per_track * i;
            let stride = if !keys {
                1
            } else if be32(bytes, at + 8)? == 0 {
                3
            } else {
                4
            };
            *track = Track { count: be32(bytes, at)?, first: be32(bytes, at + 4)?, stride };
        }
        let values_at = entry + be32(bytes, 0x1C)? as usize;
        let size = be32(bytes, values_at)? as usize;
        if size % 4 != 0 || values_at + 4 + size > bytes.len() {
            bail!("camera animation values ({size:#x} bytes at {values_at:#x}) outside the file");
        }
        let values: Vec<f32> = (0..size / 4).map(|v| be32(bytes, values_at + 4 + 4 * v).map(f32::from_bits)).collect::<Result<_>>()?;
        for (i, track) in tracks.iter().enumerate() {
            // The last entry read: a key's last word, or the frame value after the last (read only between frames).
            let span = if track.count <= 1 { 1 } else { track.count * track.stride };
            if track.count != 0 && track.first as usize + span as usize > values.len() {
                bail!("camera animation track {i} ({track:?}) outside its {} values", values.len());
            }
        }
        let mut ncam = Ncam { frames: be32(bytes, 0x18)?, tracks, values, cuts: Vec::new() };
        ncam.cuts = find_cuts(&ncam);
        Ok(ncam)
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(HEADER + 12 * TRACKS + 4 * (self.values.len() + 1 + self.cuts.len()));
        out.extend_from_slice(MAGIC);
        for word in [self.frames, self.values.len() as u32] {
            out.extend_from_slice(&word.to_le_bytes());
        }
        for track in &self.tracks {
            for word in [track.count, track.first, track.stride] {
                out.extend_from_slice(&word.to_le_bytes());
            }
        }
        for value in &self.values {
            out.extend_from_slice(&value.to_le_bytes());
        }
        out.extend_from_slice(&(self.cuts.len() as u32).to_le_bytes());
        for step in &self.cuts {
            out.extend_from_slice(&step.to_le_bytes());
        }
        out
    }

    pub fn from_ncam(bytes: &[u8]) -> Result<Ncam> {
        if !bytes.starts_with(MAGIC) {
            bail!("not a cooked camera animation");
        }
        let w = |i: usize| le32(bytes, 4 * i);
        let mut tracks = [Track::default(); TRACKS];
        for (i, track) in tracks.iter_mut().enumerate() {
            let at = HEADER + 12 * i;
            *track = Track { count: le32(bytes, at)?, first: le32(bytes, at + 4)?, stride: le32(bytes, at + 8)? };
        }
        let values_at = HEADER + 12 * TRACKS;
        let count = w(2)? as usize;
        let values = (0..count).map(|v| le32(bytes, values_at + 4 * v).map(f32::from_bits)).collect::<Result<_>>()?;
        let cuts_at = values_at + 4 * count;
        let cuts = (0..le32(bytes, cuts_at)? as usize).map(|c| le32(bytes, cuts_at + 4 + 4 * c)).collect::<Result<_>>()?;
        Ok(Ncam { frames: w(1)?, tracks, values, cuts })
    }
}

/// A camera animation cooked: a disc's to NCAM; a cooked one as it is, once it reads whole (with its cuts).
pub fn cook(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.starts_with(MAGIC) {
        if Ncam::from_ncam(bytes)?.to_bytes() != bytes {
            bail!("a cooked camera animation of another layout");
        }
        return Ok(bytes.to_vec());
    }
    let ncam = Ncam::from_disc(bytes)?;
    let out = ncam.to_bytes();
    #[cfg(feature = "proof-tools")]
    proof::reads_agree(bytes, &out)?;
    Ok(out)
}

/// The old reader on the disc file against NCAM: every track at every half frame from -1 to frames + 1 gives the same
/// bits (proof-tools: the cook stops on a difference).
#[cfg(feature = "proof-tools")]
pub mod proof {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    pub static CLIPS: AtomicU64 = AtomicU64::new(0);
    pub static READS: AtomicU64 = AtomicU64::new(0);

    /// CameraAnim before NCAM: frame tracks linear, key tracks by type word.
    fn old(values: &[f32], key: f32, count: u32, offset: u32, key_type: Option<u32>) -> Option<f32> {
        let (count, offset) = (count as usize, offset as usize);
        match key_type {
            None => {
                let floor = key as u32 as usize;
                let rate = (key - floor as f32).clamp(0.0, 1.0);
                if floor < count.wrapping_sub(1) {
                    Some((1.0 - rate) * *values.get(offset + floor)? + rate * *values.get(offset + floor + 1)?)
                } else {
                    values.get((offset + count).wrapping_sub(1)).copied()
                }
            }
            Some(_) if count == 1 => values.get(offset).copied(),
            Some(kind) => {
                let stride = if kind == 0 { 3 } else { 4 };
                let index = search(values, key, offset, count, stride);
                let v = values.get(offset + index.wrapping_mul(stride)..)?;
                if kind == 0 {
                    Some(hermite(key, *v.first()?, *v.get(1)?, *v.get(2)? / 30.0, *v.get(3)?, *v.get(4)?, *v.get(5)? / 30.0))
                } else {
                    Some(hermite(key, *v.first()?, *v.get(1)?, *v.get(3)? / 30.0, *v.get(4)?, *v.get(5)?, *v.get(6)? / 30.0))
                }
            }
        }
    }

    pub fn reads_agree(bytes: &[u8], cooked: &[u8]) -> Result<()> {
        let ncam = Ncam::from_ncam(cooked)?;
        if ncam.to_bytes() != cooked {
            bail!("NCAM reads back other than it was written");
        }
        let keys = &bytes[4..8] == b"CKAN";
        let words_per_track = if keys { 3 } else { 2 };
        let values_at = 0x20 + be32(bytes, 0x1C)? as usize;
        let disc: Vec<f32> = (0..be32(bytes, values_at)? as usize / 4).map(|v| be32(bytes, values_at + 4 + 4 * v).map(f32::from_bits)).collect::<Result<_>>()?;
        let mut reads = 0;
        for track in 0..TRACKS {
            let at = 0x20 + 4 * words_per_track * track;
            let key_type = if keys { Some(be32(bytes, at + 8)?) } else { None };
            let (count, offset) = (be32(bytes, at)?, be32(bytes, at + 4)?);
            for half in -2..=(2 * ncam.frames as i64 + 2) {
                let key = half as f32 * 0.5;
                let (a, b) = (old(&disc, key, count, offset, key_type), read(&ncam, track, key));
                if a.map(f32::to_bits) != b.map(f32::to_bits) {
                    bail!("track {track} at {key}: the disc reads {a:?}, NCAM {b:?}");
                }
                reads += 1;
            }
        }
        CLIPS.fetch_add(1, Ordering::Relaxed);
        READS.fetch_add(reads, Ordering::Relaxed);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn disc(keys: bool) -> Vec<u8> {
        let mut out = Vec::new();
        out.extend_from_slice(b"ANDO");
        out.extend_from_slice(if keys { b"CKAN" } else { b"CANM" });
        let words_per_track = if keys { 3 } else { 2 };
        let info = 4 * words_per_track * TRACKS;
        for word in [1u32, 2, 3, 4, 3, info as u32] {
            out.extend_from_slice(&word.to_be_bytes());
        }
        let values: Vec<f32> = if keys { vec![0.0, 10.0, 0.0, 2.0, 30.0, 0.0] } else { vec![1.0, 2.0, 4.0] };
        for _ in 0..TRACKS {
            let count = if keys { 2u32 } else { 3 };
            out.extend_from_slice(&count.to_be_bytes());
            out.extend_from_slice(&0u32.to_be_bytes());
            if keys {
                out.extend_from_slice(&0u32.to_be_bytes());
            }
        }
        out.extend_from_slice(&((values.len() * 4) as u32).to_be_bytes());
        for v in values {
            out.extend_from_slice(&v.to_bits().to_be_bytes());
        }
        out
    }

    #[test]
    fn frames_and_keys_cook_to_tracks() {
        let frames = Ncam::from_disc(&disc(false)).unwrap();
        assert_eq!(frames.frames, 3);
        assert_eq!(frames.tracks[7], Track { count: 3, first: 0, stride: 1 });
        assert_eq!(frames.values, [1.0, 2.0, 4.0]);
        let keys = Ncam::from_disc(&disc(true)).unwrap();
        assert_eq!(keys.tracks[0], Track { count: 2, first: 0, stride: 3 });
        let cooked = cook(&disc(true)).unwrap();
        assert!(cooked.starts_with(MAGIC));
        assert_eq!(Ncam::from_ncam(&cooked).unwrap(), keys);
        assert_eq!(cook(&cooked).unwrap(), cooked);
        assert_eq!(cooked.len(), 12 + 12 * TRACKS + 4 * 6 + 4);
    }

    /// A clip of a value per frame: the eye's x from `xs`, the watch 100 ahead in z, the rest still.
    fn per_frame(xs: &[f32]) -> Ncam {
        let n = xs.len() as u32;
        let mut values = Vec::new();
        let mut tracks = [Track::default(); TRACKS];
        for (i, track) in tracks.iter_mut().enumerate() {
            *track = Track { count: n, first: values.len() as u32, stride: 1 };
            for &x in xs {
                values.push(match i {
                    0 | 3 => x,
                    5 => 100.0,
                    7 => 45.0,
                    _ => 0.0,
                });
            }
        }
        Ncam { frames: n, tracks, values, cuts: Vec::new() }
    }

    #[test]
    fn a_jump_against_its_neighbours_is_a_cut() {
        let mut xs: Vec<f32> = (0..10).map(|f| f as f32).collect();
        xs.extend((0..10).map(|f| 500.0 + f as f32));
        assert_eq!(find_cuts(&per_frame(&xs)), [9]);
        // Fast steady motion is no cut, nor is a clip that holds still.
        let fast: Vec<f32> = (0..20).map(|f| 300.0 * f as f32).collect();
        assert!(find_cuts(&per_frame(&fast)).is_empty());
        assert!(find_cuts(&per_frame(&[7.0; 20])).is_empty());
        let mut ncam = per_frame(&xs);
        ncam.cuts = find_cuts(&ncam);
        assert_eq!(Ncam::from_ncam(&ncam.to_bytes()).unwrap(), ncam);
    }

    #[test]
    fn a_turn_is_a_cut() {
        let mut ncam = per_frame(&[0.0; 20]);
        // The watch swings from ahead (z) to the side (x) between frames 11 and 12.
        let watch_x = ncam.tracks[3].first as usize;
        let watch_z = ncam.tracks[5].first as usize;
        for f in 12..20 {
            ncam.values[watch_x + f] = 100.0;
            ncam.values[watch_z + f] = 0.0;
        }
        assert_eq!(find_cuts(&ncam), [11]);
    }

    #[test]
    fn a_refused_clip_is_not_cooked() {
        let mut bytes = disc(false);
        bytes[0x8..0xC].copy_from_slice(&0u32.to_be_bytes());
        assert!(cook(&bytes).is_err());
    }
}
