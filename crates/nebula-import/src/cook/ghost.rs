//! Ghost recordings (NGST): a disc's .gst (a packet a frame, each with flags saying which fields follow) as one fixed
//! record a frame, the form GhostPlayer reads.
//!
//! Layout, little-endian:
//! - header (16 bytes): "NGST", the frame count, the names' offset and size;
//! - frames (32 bytes): flags (u16, as the disc's: 0x1 position, 0x800 velocity, 0x400 scale, 0x2/0x4/0x8 rotation
//!   x/y/z, 0x10 animation by name, 0x2000 animation by hash, 0x20 animation frame, 0x40 << i track weight i, 0x1000
//!   rate), animation frame (s16), position (3 s16), velocity (3 s8), scale (3 s8), rotation (3 s8), track weights
//!   (4 s8), rate (s8), the name's offset in the names (u32), the animation's hash (u32, MR::getHashCode of its UTF-8
//!   name). Fields a frame's flags leave out are 0;
//! - names: UTF-8, each ending in a 0 (offset 0 is "").
//!
//! The disc's names are Shift-JIS and its hashes are of those bytes; the cook turns both to UTF-8, the hashes through
//! the names of Mario's animations (ghost_names.txt, taken from MarioAnimatorData.hpp).

use anyhow::{bail, Context, Result};
use std::collections::HashMap;

pub const MAGIC: &[u8; 4] = b"NGST";
const HEADER: usize = 16;
const FRAME: usize = 32;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Frame {
    pub flags: u16,
    pub anim_frame: i16,
    pub position: [i16; 3],
    pub velocity: [i8; 3],
    pub scale: [i8; 3],
    pub rotation: [i8; 3],
    pub weights: [i8; 4],
    pub rate: i8,
    pub name: u32,
    pub hash: u32,
}

/// MR::getHashCode: x31 over the bytes taken as signed.
pub fn game_hash(bytes: &[u8]) -> u32 {
    bytes.iter().fold(0u32, |h, &b| h.wrapping_mul(31).wrapping_add(b as i8 as i32 as u32))
}

/// Shift-JIS hash -> UTF-8 name, for the names of Mario's animations (a hash two names share maps to none).
fn names_by_disc_hash() -> HashMap<u32, Option<&'static str>> {
    let mut out: HashMap<u32, Option<&'static str>> = HashMap::new();
    for name in include_str!("ghost_names.txt").lines().filter(|l| !l.is_empty()) {
        let (sjis, _, unmappable) = encoding_rs::SHIFT_JIS.encode(name);
        if unmappable {
            continue;
        }
        let entry = out.entry(game_hash(&sjis)).or_insert(Some(name));
        if *entry != Some(name) {
            *entry = None;
        }
    }
    out
}

fn be16(bytes: &[u8], at: usize) -> Result<u16> {
    let b = bytes.get(at..at + 2).context("past the recording's end")?;
    Ok(u16::from_be_bytes([b[0], b[1]]))
}

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 4).context("past the recording's end")?;
    Ok(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
}

fn s8(bytes: &[u8], at: usize, end: usize) -> Result<i8> {
    if at >= end {
        bail!("a field past its packet at {at}");
    }
    Ok(bytes[at] as i8)
}

/// A disc recording: its frames and names (UTF-8, as the names' pool).
pub fn from_disc(bytes: &[u8]) -> Result<(Vec<Frame>, Vec<u8>)> {
    let by_hash = names_by_disc_hash();
    let mut frames = Vec::new();
    let mut names = vec![0u8];
    let mut name_at: HashMap<String, u32> = HashMap::new();
    let mut previous: Option<u8> = None;
    let mut at = 0usize;
    while at + 4 <= bytes.len() {
        let end = at + bytes[at + 1] as usize;
        if end < at + 4 {
            break;
        }
        if end > bytes.len() {
            bail!("a packet of {} bytes past the end at {at}", end - at);
        }
        if let Some(p) = previous {
            if p.wrapping_add(1) != bytes[at] {
                bail!("packet at {at}: frame {} after {p} (a frame a packet is all the format holds)", bytes[at]);
            }
        }
        previous = Some(bytes[at]);
        let mut f = Frame { flags: be16(bytes, at + 2)?, ..Frame::default() };
        let mut p = at + 4;
        if f.flags & 0x0001 != 0 {
            for c in 0..3 {
                if p + 2 > end {
                    bail!("a position past its packet at {at}");
                }
                f.position[c] = be16(bytes, p)? as i16;
                p += 2;
            }
        }
        if f.flags & 0x0800 != 0 {
            for c in 0..3 {
                f.velocity[c] = s8(bytes, p, end)?;
                p += 1;
            }
        }
        if f.flags & 0x0400 != 0 {
            for c in 0..3 {
                f.scale[c] = s8(bytes, p, end)?;
                p += 1;
            }
        }
        for (c, flag) in [0x0002u16, 0x0004, 0x0008].into_iter().enumerate() {
            if f.flags & flag != 0 {
                f.rotation[c] = s8(bytes, p, end)?;
                p += 1;
            }
        }
        if f.flags & 0x0010 != 0 {
            let length = bytes.get(p..end).and_then(|t| t.iter().position(|&b| b == 0)).context("an animation's name past its packet")?;
            let name = String::from_utf8(super::table::decode_text(&bytes[p..p + length]))
                .context("an animation's name that is not Shift-JIS")?;
            p += length + 1;
            f.name = *name_at.entry(name.clone()).or_insert_with(|| {
                let offset = names.len() as u32;
                names.extend_from_slice(name.as_bytes());
                names.push(0);
                offset
            });
        }
        if f.flags & 0x2000 != 0 {
            if p + 4 > end {
                bail!("a hash past its packet at {at}");
            }
            let disc = be32(bytes, p)?;
            p += 4;
            let Some(Some(name)) = by_hash.get(&disc) else {
                bail!("packet at {at}: animation hash {disc:08x} is none of Mario's animations");
            };
            f.hash = game_hash(name.as_bytes());
        }
        if f.flags & 0x0020 != 0 {
            if p + 2 > end {
                bail!("an animation frame past its packet at {at}");
            }
            f.anim_frame = be16(bytes, p)? as i16;
            p += 2;
        }
        for (i, flag) in [0x0040u16, 0x0080, 0x0100, 0x0200].into_iter().enumerate() {
            if f.flags & flag != 0 {
                f.weights[i] = s8(bytes, p, end)?;
                p += 1;
            }
        }
        if f.flags & 0x1000 != 0 {
            f.rate = s8(bytes, p, end)?;
            p += 1;
        }
        if p > end {
            bail!("fields past their packet at {at}");
        }
        frames.push(f);
        at = end;
    }
    Ok((frames, names))
}

pub fn to_bytes(frames: &[Frame], names: &[u8]) -> Vec<u8> {
    let names_at = HEADER + FRAME * frames.len();
    let mut out = Vec::with_capacity(names_at + names.len());
    out.extend_from_slice(MAGIC);
    for word in [frames.len(), names_at, names.len()] {
        out.extend_from_slice(&(word as u32).to_le_bytes());
    }
    for f in frames {
        out.extend_from_slice(&f.flags.to_le_bytes());
        out.extend_from_slice(&f.anim_frame.to_le_bytes());
        for c in f.position {
            out.extend_from_slice(&c.to_le_bytes());
        }
        for b in f.velocity.iter().chain(&f.scale).chain(&f.rotation).chain(&f.weights).chain(std::iter::once(&f.rate)) {
            out.push(*b as u8);
        }
        out.extend_from_slice(&f.name.to_le_bytes());
        out.extend_from_slice(&f.hash.to_le_bytes());
    }
    out.extend_from_slice(names);
    out
}

/// A recording cooked: a disc's to NGST; a cooked one as it is.
pub fn cook(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.starts_with(MAGIC) {
        return Ok(bytes.to_vec());
    }
    let (frames, names) = from_disc(bytes)?;
    Ok(to_bytes(&frames, &names))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_disc_hashes_map_to_utf8_names() {
        // From ghost_census.py on the store: the disc hash of 基本 (Shift-JIS).
        let sjis = encoding_rs::SHIFT_JIS.encode("基本").0;
        assert_eq!(game_hash(&sjis), 0xffca0c49);
        assert_eq!(names_by_disc_hash().get(&0xffca0c49), Some(&Some("基本")));
    }

    #[test]
    fn packets_become_frames() {
        let mut packet = vec![7u8, 0, 0x20, 0x33];
        packet.extend_from_slice(&[0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC]); // position
        packet.push(5); // rotation x
        packet.extend_from_slice(&[b'A', 0x00]); // a name, and no hash where the flags say one is
        packet[1] = packet.len() as u8;
        assert!(from_disc(&packet).is_err());

        let mut packet = vec![7u8, 0, 0x20, 0x33];
        packet.extend_from_slice(&[0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC]);
        packet.push(5);
        packet.extend_from_slice(&encoding_rs::SHIFT_JIS.encode("基本").0);
        packet.push(0);
        packet.extend_from_slice(&0xffca0c49u32.to_be_bytes()); // hash
        packet.extend_from_slice(&[0x01, 0x02]); // animation frame
        packet[1] = packet.len() as u8;
        let mut two = packet.clone();
        let mut next = packet.clone();
        next[0] = 8;
        two.extend_from_slice(&next);
        let (frames, names) = from_disc(&two).unwrap();
        assert_eq!(frames.len(), 2);
        let f = frames[0];
        assert_eq!(f.position, [0x1234, 0x5678, 0x9ABCu16 as i16]);
        assert_eq!(f.rotation, [5, 0, 0]);
        assert_eq!(&names[f.name as usize..f.name as usize + 7], "基本\0".as_bytes());
        assert_eq!(frames[1].name, f.name);
        assert_eq!(f.hash, game_hash("基本".as_bytes()));
        assert_eq!(f.anim_frame, 0x0102);
        let cooked = cook(&two).unwrap();
        assert_eq!(cooked.len(), HEADER + 2 * FRAME + names.len());
        assert_eq!(cook(&cooked).unwrap(), cooked);
        next[0] = 10;
        let mut gap = packet.clone();
        gap.extend_from_slice(&next);
        assert!(from_disc(&gap).is_err());
    }

    #[test]
    fn a_packet_past_the_end_is_refused() {
        assert!(from_disc(&[0, 200, 0, 0]).is_err());
    }
}
