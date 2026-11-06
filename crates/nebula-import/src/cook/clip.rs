//! J3D animation files to the store's clip block (native/include/nebula_clip.h).

use super::model_write::name_hash;
use anyhow::{bail, Context, Result};

pub const MAGIC: u32 = 0x504C_434E;
pub const VERSION: u32 = 2;
pub const HEADER_WORDS: usize = 11;
pub const TRACK_BYTES: usize = 12;
pub const TARGET_BYTES: usize = 28;

pub const KIND_JOINTS: u8 = 0;
pub const KIND_TEXTURE_MATRICES: u8 = 1;
pub const KIND_COLORS: u8 = 2;
pub const KIND_REGISTERS: u8 = 3;
pub const KIND_PATTERN: u8 = 4;
pub const KIND_VISIBILITY: u8 = 5;
pub const KIND_UNPLAYED: u8 = 6;

const STEP: u8 = 1;
const STEP_NEAREST: u8 = 2;

#[derive(Clone, Copy, Default)]
pub struct Track {
    pub count: u32,
    pub stride: u8,
    pub integer_keys: bool,
    pub at: u32,
}

pub struct Target {
    pub name: Vec<u8>,
    pub material: u32,
    pub slot: u8,
    pub center: [f32; 3],
}

#[derive(Default)]
pub struct ClipIr {
    pub kind: u8,
    pub loop_mode: u8,
    pub coord_mode: u8,
    pub frames: i32,
    pub first_count: u32,
    pub second_count: u32,
    pub tracks: Vec<Track>,
    pub targets: Vec<Target>,
    pub values: Vec<f32>,
    pub indices: Vec<u32>, // texture-pattern samples never pass through floating point
}

/// One animation block, big-endian, offsets from its start.
struct Block<'a>(&'a [u8]);

impl Block<'_> {
    fn u8(&self, at: usize) -> Result<u8> {
        self.0.get(at).copied().context("animation field past the block")
    }
    fn u16(&self, at: usize) -> Result<u16> {
        let b = self.0.get(at..at + 2).context("animation field past the block")?;
        Ok(u16::from_be_bytes([b[0], b[1]]))
    }
    fn texture_index(&self, at: usize) -> Result<u32> {
        let index = self.u16(at)?;
        Ok(if index == u16::MAX { super::model_ir::NONE } else { u32::from(index) })
    }
    fn u32(&self, at: usize) -> Result<usize> {
        let b = self.0.get(at..at + 4).context("animation field past the block")?;
        Ok(u32::from_be_bytes([b[0], b[1], b[2], b[3]]) as usize)
    }
    fn floats(&self, at: usize, count: usize, out: &mut Vec<f32>) -> Result<u32> {
        let base = out.len() as u32;
        for i in 0..count {
            let w = self.u32(at + i * 4)?;
            out.push(f32::from_bits(w as u32));
        }
        Ok(base)
    }
    fn shorts(&self, at: usize, count: usize, out: &mut Vec<f32>) -> Result<u32> {
        let base = out.len() as u32;
        for i in 0..count {
            out.push(self.u16(at + i * 2)? as i16 as f32);
        }
        Ok(base)
    }
    fn bytes(&self, at: usize, count: usize, out: &mut Vec<f32>) -> Result<u32> {
        let base = out.len() as u32;
        for i in 0..count {
            out.push(self.u8(at + i)? as f32);
        }
        Ok(base)
    }
    fn key_track(&self, at: usize, base: u32, integer_keys: bool) -> Result<Track> {
        let stride = if self.u16(at + 4)? == 0 { 3 } else { 4 };
        Ok(Track { count: self.u16(at)? as u32, stride, integer_keys, at: base + self.u16(at + 2)? as u32 })
    }
    fn step_track(&self, at: usize, base: u32, stride: u8) -> Result<Track> {
        Ok(Track { count: self.u16(at)? as u32, stride, integer_keys: false, at: base + self.u16(at + 2)? as u32 })
    }
    fn names(&self, at: usize, count: usize) -> Result<Vec<Vec<u8>>> {
        let have = self.u16(at)? as usize;
        if have < count {
            bail!("name table holds {have} names, the clip updates {count}");
        }
        let mut out = Vec::with_capacity(count);
        for i in 0..count {
            let from = at + self.u16(at + 6 + i * 4)? as usize;
            let rest = self.0.get(from..).context("name past the block")?;
            let end = rest.iter().position(|&c| c == 0).context("name without an end")?;
            out.push(rest[..end].to_vec());
        }
        Ok(out)
    }
}

fn targets(b: &Block, ids: usize, names: usize, count: usize, slot: impl Fn(usize) -> Result<u8>, out: &mut Vec<Target>) -> Result<()> {
    let names = b.names(names, count)?;
    for (i, name) in names.into_iter().enumerate() {
        out.push(Target { name, material: b.u16(ids + i * 2)? as u32, slot: slot(i)?, center: [0.0; 3] });
    }
    Ok(())
}

pub fn read(bytes: &[u8]) -> Result<ClipIr> {
    let block = bytes.get(0x20..).context("an animation with no block")?;
    let tag: [u8; 4] = block.get(0..4).context("an animation with no block")?.try_into().unwrap();
    let b = Block(block);
    let mut c = ClipIr { loop_mode: b.u8(8)?, ..ClipIr::default() };
    let mut rotation_shift = 0u8;
    match &tag {
        b"ANK1" | b"ANF1" => {
            let keys = &tag == b"ANK1";
            c.kind = KIND_JOINTS;
            c.frames = b.u16(0xA)? as i16 as i32;
            c.first_count = b.u16(0xC)? as u32;
            let (scale, rot, trans) = (b.u16(0xE)? as usize, b.u16(0x10)? as usize, b.u16(0x12)? as usize);
            let table = b.u32(0x14)?;
            let s = b.floats(b.u32(0x18)?, scale, &mut c.values)?;
            let r = b.shorts(b.u32(0x1C)?, rot, &mut c.values)?;
            let t = b.floats(b.u32(0x20)?, trans, &mut c.values)?;
            for axis in 0..c.first_count as usize * 3 {
                if keys {
                    let at = table + axis * 18;
                    c.tracks.push(b.key_track(at, s, false)?);
                    c.tracks.push(b.key_track(at + 6, r, true)?);
                    c.tracks.push(b.key_track(at + 12, t, false)?);
                } else {
                    let at = table + axis * 12;
                    c.tracks.push(b.step_track(at, s, STEP_NEAREST)?);
                    c.tracks.push(b.step_track(at + 4, r, STEP_NEAREST)?);
                    c.tracks.push(b.step_track(at + 8, t, STEP_NEAREST)?);
                }
            }
            if keys {
                rotation_shift = b.u8(9)?;
            }
        }
        b"TTK1" => {
            c.kind = KIND_TEXTURE_MATRICES;
            c.frames = b.u16(0xA)? as i16 as i32;
            rotation_shift = b.u8(9)?;
            c.coord_mode = b.u32(0x5C)? as u8;
            let count = b.u16(0xC)? as usize / 3;
            c.first_count = count as u32;
            let (scale, rot, trans) = (b.u16(0xE)? as usize, b.u16(0x10)? as usize, b.u16(0x12)? as usize);
            let table = b.u32(0x14)?;
            let s = b.floats(b.u32(0x28)?, scale, &mut c.values)?;
            let r = b.shorts(b.u32(0x2C)?, rot, &mut c.values)?;
            let t = b.floats(b.u32(0x30)?, trans, &mut c.values)?;
            for m in 0..count {
                let x = table + m * 3 * 18;
                let (y, angle) = (x + 18, x + 36);
                c.tracks.push(b.key_track(x, s, false)?);
                c.tracks.push(b.key_track(y, s, false)?);
                c.tracks.push(b.key_track(angle + 6, r, true)?);
                c.tracks.push(b.key_track(x + 12, t, false)?);
                c.tracks.push(b.key_track(y + 12, t, false)?);
            }
            let slots = b.u32(0x20)?;
            let start = c.targets.len();
            targets(&b, b.u32(0x18)?, b.u32(0x1C)?, count, |i| b.u8(slots + i), &mut c.targets)?;
            let centers = b.u32(0x24)?;
            for i in 0..count {
                let mut center = Vec::with_capacity(3);
                b.floats(centers + i * 12, 3, &mut center)?;
                c.targets[start + i].center.copy_from_slice(&center);
            }
        }
        b"PAK1" | b"PAF1" => {
            let keys = &tag == b"PAK1";
            c.kind = KIND_COLORS;
            c.frames = b.u16(0xC)? as i16 as i32;
            let count = b.u16(0xE)? as usize;
            c.first_count = count as u32;
            let table = b.u32(0x18)?;
            let mut base = [0u32; 4];
            for k in 0..4 {
                let (n, at) = (b.u16(0x10 + k * 2)? as usize, b.u32(0x24 + k * 4)?);
                base[k] = if keys { b.shorts(at, n, &mut c.values)? } else { b.bytes(at, n, &mut c.values)? };
            }
            for e in 0..count {
                for (k, &v) in base.iter().enumerate() {
                    c.tracks.push(if keys {
                        b.key_track(table + e * 24 + k * 6, v, true)?
                    } else {
                        b.step_track(table + e * 16 + k * 4, v, STEP_NEAREST)?
                    });
                }
            }
            targets(&b, b.u32(0x1C)?, b.u32(0x20)?, count, |_| Ok(0), &mut c.targets)?;
        }
        b"TRK1" => {
            c.kind = KIND_REGISTERS;
            c.frames = b.u16(0xA)? as i16 as i32;
            for set in 0..2 {
                let count = b.u16(0xC + set * 2)? as usize;
                if set == 0 {
                    c.first_count = count as u32;
                } else {
                    c.second_count = count as u32;
                }
                let table = b.u32(0x20 + set * 4)?;
                let mut base = [0u32; 4];
                for k in 0..4 {
                    let n = b.u16(0x10 + set * 8 + k * 2)? as usize;
                    base[k] = b.shorts(b.u32(0x38 + set * 16 + k * 4)?, n, &mut c.values)?;
                }
                for e in 0..count {
                    for (k, &v) in base.iter().enumerate() {
                        c.tracks.push(b.key_track(table + e * 28 + k * 6, v, true)?);
                    }
                }
                targets(&b, b.u32(0x28 + set * 4)?, b.u32(0x30 + set * 4)?, count, |i| b.u8(table + i * 28 + 24), &mut c.targets)?;
            }
        }
        b"TPT1" => {
            c.kind = KIND_PATTERN;
            c.frames = b.u16(0xA)? as i16 as i32;
            let count = b.u16(0xC)? as usize;
            c.first_count = count as u32;
            let table = b.u32(0x10)?;
            let index_at = b.u32(0x14)?;
            for i in 0..b.u16(0xE)? as usize {
                c.indices.push(b.texture_index(index_at + i * 2)?);
            }
            for e in 0..count {
                c.tracks.push(b.step_track(table + e * 8, 0, STEP)?);
            }
            targets(&b, b.u32(0x18)?, b.u32(0x1C)?, count, |i| b.u8(table + i * 8 + 4), &mut c.targets)?;
        }
        b"VAF1" => {
            c.kind = KIND_VISIBILITY;
            c.frames = b.u16(0xA)? as i16 as i32;
            let count = b.u16(0xC)? as usize;
            c.first_count = count as u32;
            let table = b.u32(0x10)?;
            let v = b.bytes(b.u32(0x14)?, b.u16(0xE)? as usize, &mut c.values)?;
            for e in 0..count {
                c.tracks.push(b.step_track(table + e * 4, v, STEP_NEAREST)?);
            }
        }
        b"CLK1" | b"CLF1" | b"VCK1" | b"VCF1" => {
            c.kind = KIND_UNPLAYED;
            c.frames = b.u16(0xA)? as i16 as i32;
        }
        _ => bail!("unknown animation block {}", String::from_utf8_lossy(&tag)),
    }
    let value_count = if c.kind == KIND_PATTERN { c.indices.len() } else { c.values.len() };
    for (i, t) in c.tracks.iter().enumerate() {
        let need = match t.count {
            0 => 0,
            1 => 1,
            n if t.stride <= STEP_NEAREST => n as usize,
            n => n as usize * t.stride as usize,
        };
        if t.at as usize + need > value_count {
            bail!("track {i}: {} values at {} past the {} values", need, t.at, value_count);
        }
    }
    convert_rotations(&mut c, rotation_shift);
    Ok(c)
}

/// Source rotation keys may share their value pool. Each converted track owns
/// its copy, so times and other tracks are never scaled through an alias.
fn convert_rotations(c: &mut ClipIr, shift: u8) {
    if c.kind == KIND_PATTERN { return; }
    let factor = 2.0f32.powi(i32::from(shift)) * super::model_ir::ANGLE_UNIT;
    let source = std::mem::take(&mut c.values);
    for (i, track) in c.tracks.iter_mut().enumerate() {
        let rotation = (c.kind == KIND_JOINTS && i % 3 == 1)
            || (c.kind == KIND_TEXTURE_MATRICES && i % 5 == 2);
        let size = match track.count {
            0 => 0, 1 => 1,
            n if track.stride <= STEP_NEAREST => n as usize,
            n => n as usize * track.stride as usize,
        };
        let mut values = source[track.at as usize..track.at as usize + size].to_vec();
        if rotation {
            for (k, v) in values.iter_mut().enumerate() {
                if track.count == 1 || track.stride <= STEP_NEAREST || k % track.stride as usize != 0 {
                    *v *= factor;
                }
            }
        }
        track.at = c.values.len() as u32;
        c.values.extend(values);
    }
}

pub fn write(c: &ClipIr) -> Vec<u8> {
    let tracks_at = HEADER_WORDS * 4;
    let targets_at = tracks_at + c.tracks.len() * TRACK_BYTES;
    let values_at = targets_at + c.targets.len() * TARGET_BYTES;
    let value_count = if c.kind == KIND_PATTERN { c.indices.len() } else { c.values.len() };
    let mut names_at = values_at + value_count * 4;
    let mut out = Vec::with_capacity(names_at + c.targets.len() * 16);
    let word = |out: &mut Vec<u8>, v: u32| out.extend_from_slice(&v.to_le_bytes());
    word(&mut out, MAGIC);
    word(&mut out, VERSION);
    word(&mut out, 0);
    out.extend_from_slice(&[c.kind, c.loop_mode, 0, c.coord_mode]);
    word(&mut out, c.frames as u32);
    word(&mut out, c.first_count);
    word(&mut out, c.second_count);
    word(&mut out, tracks_at as u32);
    word(&mut out, targets_at as u32);
    word(&mut out, values_at as u32);
    word(&mut out, value_count as u32);
    for t in &c.tracks {
        out.extend_from_slice(&t.count.to_le_bytes());
        out.extend_from_slice(&[t.stride, t.integer_keys as u8, 0, 0]);
        word(&mut out, t.at);
    }
    for t in &c.targets {
        word(&mut out, names_at as u32);
        word(&mut out, name_hash_bytes(&t.name));
        out.extend_from_slice(&t.material.to_le_bytes());
        out.extend_from_slice(&[t.slot, 0, 0, 0]);
        for v in t.center {
            word(&mut out, v.to_bits());
        }
        names_at += t.name.len() + 1;
    }
    if c.kind == KIND_PATTERN {
        for &v in &c.indices { word(&mut out, v); }
    } else {
        for v in &c.values { word(&mut out, v.to_bits()); }
    }
    for t in &c.targets {
        out.extend_from_slice(&t.name);
        out.push(0);
    }
    while out.len() % 4 != 0 {
        out.push(0);
    }
    let size = out.len() as u32;
    out[8..12].copy_from_slice(&size.to_le_bytes());
    out
}

fn name_hash_bytes(name: &[u8]) -> u32 {
    name_hash(&name.iter().map(|&c| c as char).collect::<String>())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn word(bytes: &[u8], at: usize) -> u32 {
        u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap())
    }

    #[test]
    fn shared_rotation_keys_convert_once_and_keep_times() {
        let track = Track { count: 2, stride: 3, integer_keys: true, at: 0 };
        let mut c = ClipIr {
            kind: KIND_JOINTS,
            tracks: vec![track, track, track, track, track, track],
            values: vec![0.0, 16384.0, 4.0, 10.0, 8192.0, -2.0],
            ..ClipIr::default()
        };
        convert_rotations(&mut c, 1);
        assert_eq!(c.values.len(), 36);
        let values = |i: usize| &c.values[c.tracks[i].at as usize..c.tracks[i].at as usize + 6];
        assert_eq!(values(0), &[0.0, 16384.0, 4.0, 10.0, 8192.0, -2.0]);
        assert_eq!(values(1), values(4));
        assert_eq!(values(1)[0], 0.0);
        assert_eq!(values(1)[3], 10.0);
        assert_eq!(values(1)[1], std::f32::consts::PI);
        assert_eq!(values(1)[2], 8.0 * super::super::model_ir::ANGLE_UNIT);
        assert_eq!(write(&c)[14], 0);
    }

    #[test]
    fn long_tracks_and_large_material_indices_survive_the_written_layout() {
        let clip = ClipIr {
            kind: KIND_PATTERN,
            frames: 100_000,
            first_count: 1,
            tracks: vec![Track { count: 70_000, stride: STEP, integer_keys: false, at: 0 }],
            targets: vec![Target { name: b"material".to_vec(), material: 80_000, slot: 12, center: [1.0, 2.0, 3.0] }],
            indices: (0..70_000).collect(),
            ..ClipIr::default()
        };
        let bytes = write(&clip);
        assert_eq!(word(&bytes, 4), VERSION);
        assert_eq!(word(&bytes, 16), 100_000);
        let track = word(&bytes, 28) as usize;
        let target = word(&bytes, 32) as usize;
        let values = word(&bytes, 36) as usize;
        assert_eq!(target - track, TRACK_BYTES);
        assert_eq!(values - target, TARGET_BYTES);
        assert_eq!(word(&bytes, track), 70_000);
        assert_eq!(&bytes[track + 4..track + 8], &[STEP, 0, 0, 0]);
        assert_eq!(word(&bytes, target + 8), 80_000);
        assert_eq!(&bytes[target + 12..target + 16], &[12, 0, 0, 0]);
        assert_eq!(word(&bytes, target + 16), 1.0f32.to_bits());
        assert_eq!(word(&bytes, values + 69_999 * 4), 69_999);
        let name = word(&bytes, target) as usize;
        assert_eq!(&bytes[name..name + 9], b"material\0");
    }

    #[test]
    fn texture_pattern_samples_keep_all_index_bits() {
        assert_eq!(Block(&[0xFF, 0xFF]).texture_index(0).unwrap(), super::super::model_ir::NONE);
        assert_eq!(Block(&[0xFF, 0xFE]).texture_index(0).unwrap(), 65_534);
        let mut clip = ClipIr { kind: KIND_PATTERN,
            tracks: vec![Track { count: 4, stride: STEP, ..Track::default() }],
            indices: vec![65_535, 0x01000001, 1 << 31 | 1, u32::MAX], ..ClipIr::default() };
        convert_rotations(&mut clip, 0);
        let bytes = write(&clip);
        let at = word(&bytes, 36) as usize;
        assert_eq!(word(&bytes, 40), 4);
        assert_eq!([word(&bytes, at), word(&bytes, at + 4), word(&bytes, at + 8), word(&bytes, at + 12)], [65_535, 0x01000001, 1 << 31 | 1, u32::MAX]);
    }
}
