//! Ghost recordings (.gst) in host order: each packet's update flags say which fields follow.

use anyhow::{bail, Result};

fn swap(out: &mut [u8], at: usize, size: usize, end: usize) -> Result<usize> {
    if at + size > end {
        bail!("a field past its packet at {at}");
    }
    out[at..at + size].reverse();
    Ok(at + size)
}

/// `bytes` with every packet's 16-bit and 32-bit fields in host order. A packet is its frame (u8), its size (u8) and its
/// flags (u16), then by flag: position (3 s16), velocity and scale (3 s8 each), rotation x, y, z (s8), an animation's
/// name (text), its hash (u32), its frame (s16), four track weights and a rate (s8). A size too small to be a packet ends
/// the recording.
pub fn host_order(bytes: &[u8]) -> Result<Vec<u8>> {
    let mut out = bytes.to_vec();
    let mut at = 0usize;
    while at + 4 <= out.len() {
        let end = at + out[at + 1] as usize;
        if end < at + 4 {
            break;
        }
        if end > out.len() {
            bail!("a packet of {} bytes past the end at {at}", end - at);
        }
        let flags = u16::from_be_bytes([out[at + 2], out[at + 3]]);
        out[at + 2..at + 4].copy_from_slice(&flags.to_le_bytes());
        let mut p = at + 4;
        if flags & 0x0001 != 0 {
            for _ in 0..3 {
                p = swap(&mut out, p, 2, end)?;
            }
        }
        for (flag, size) in [(0x0800u16, 3usize), (0x0400, 3), (0x0002, 1), (0x0004, 1), (0x0008, 1)] {
            if flags & flag != 0 {
                p += size;
            }
        }
        if flags & 0x0010 != 0 {
            let Some(length) = out.get(p..end).and_then(|text| text.iter().position(|&b| b == 0)) else {
                bail!("an animation's name past its packet at {at}");
            };
            p += length + 1;
        }
        if flags & 0x2000 != 0 {
            p = swap(&mut out, p, 4, end)?;
        }
        if flags & 0x0020 != 0 {
            p = swap(&mut out, p, 2, end)?;
        }
        for flag in [0x0040u16, 0x0080, 0x0100, 0x0200, 0x1000] {
            if flags & flag != 0 {
                p += 1;
            }
        }
        if p > end {
            bail!("fields past their packet at {at}");
        }
        at = end;
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fields_turn_where_the_flags_say() {
        let mut packet = vec![7u8, 0, 0x20, 0x23];
        packet.extend_from_slice(&[0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC]); // position
        packet.push(5); // rotation x
        packet.extend_from_slice(&[0xAA, 0xBB, 0xCC, 0xDD]); // hash
        packet.extend_from_slice(&[0x01, 0x02]); // animation frame
        packet[1] = packet.len() as u8;
        let out = host_order(&packet).unwrap();
        assert_eq!(&out[2..4], &[0x23, 0x20]);
        assert_eq!(&out[4..10], &[0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A]);
        assert_eq!(out[10], 5);
        assert_eq!(&out[11..15], &[0xDD, 0xCC, 0xBB, 0xAA]);
        assert_eq!(&out[15..17], &[0x02, 0x01]);
    }

    #[test]
    fn a_packet_past_the_end_is_refused() {
        assert!(host_order(&[0, 200, 0, 0]).is_err());
    }
}
