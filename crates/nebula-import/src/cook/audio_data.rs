//! Sound data in host order (C-05): AST streams (header, block headers, PCM16 samples) and the PCM16 waves of AW wave
//! archives. An AW file has no header: which ranges hold PCM16 comes from the wave systems of the audio archive.

use super::archive;
use anyhow::{bail, Context, Result};
use std::collections::BTreeMap;

const AST_HEADER: usize = 0x40;
const AST_BLOCK_HEADER: usize = 0x20;
const STREAM_HEADER: usize = 32;
const FORMAT_PCM16: u8 = 1;
/// A wave's format in a wave system.
const WAVE_PCM16: u8 = 3;

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(bytes.get(at..at + 4).context("a field past the data's end")?.try_into().unwrap()))
}

fn turn(bytes: &mut [u8], at: usize, width: usize) -> Result<()> {
    bytes.get_mut(at..at + width).context("a field past the data's end")?.reverse();
    Ok(())
}

/// 16-bit samples turned in place.
fn turn_samples(samples: &mut [u8]) {
    for s in samples.chunks_exact_mut(2) {
        s.swap(0, 1);
    }
}

/// A stream as the game plays it (JASStream.hpp): the numbers it needs as host numbers, then each channel's PCM16 samples in
/// turn. The disc's blocks and their hold-over samples are gone: the whole file is in memory while it plays.
pub fn ast(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() < AST_HEADER || &bytes[..4] != b"STRM" {
        bail!("not an AST stream");
    }
    let format = bytes[9];
    if format != FORMAT_PCM16 {
        bail!("stream format {format}");
    }
    let channels = u16::from_be_bytes([bytes[0xC], bytes[0xD]]) as usize;
    if channels == 0 || channels > 6 {
        bail!("{channels} channels in a stream");
    }
    let loop_flag = u16::from_be_bytes([bytes[0xE], bytes[0xF]]) as u32;
    let rate = be32(bytes, 0x10)?;
    let samples = be32(bytes, 0x14)? as usize;
    let loop_start = be32(bytes, 0x18)?;
    let loop_end = be32(bytes, 0x1C)?;
    if loop_end as usize > samples || loop_start > loop_end {
        bail!("a loop past the stream's samples");
    }
    let header: [u32; 8] = [
        u32::from_be_bytes(*b"NSTR"),
        channels as u32,
        rate,
        samples as u32,
        loop_flag,
        loop_start,
        loop_end,
        bytes[0x28] as u32,
    ];
    let mut out = vec![0u8; STREAM_HEADER + samples * 2 * channels];
    for (i, value) in header.iter().enumerate() {
        out[i * 4..i * 4 + 4].copy_from_slice(&value.to_le_bytes());
    }
    let mut written = 0usize; // bytes of each channel so far
    let mut at = AST_HEADER;
    while at < bytes.len() {
        if bytes.get(at..at + 4) != Some(b"BLCK".as_slice()) {
            bail!("no block at {at:#x}");
        }
        let size = be32(bytes, at + 4)? as usize;
        let data = at + AST_BLOCK_HEADER;
        // On the disc a last block's size can overrun the file by up to a 32-byte unit per channel. Anything more is not a stream.
        let mut per_channel = size;
        if data + size * channels > bytes.len() {
            if data + size * channels - bytes.len() > 32 * channels {
                bail!("a block at {at:#x} past the stream's end");
            }
            per_channel = bytes.len().saturating_sub(data + size * (channels - 1));
        }
        for channel in 0..channels {
            let source = &bytes[data + size * channel..data + size * channel + per_channel];
            let take = per_channel.min((samples * 2).saturating_sub(written));
            let target = STREAM_HEADER + samples * 2 * channel + written;
            out[target..target + take].copy_from_slice(&source[..take]);
            turn_samples(&mut out[target..target + take]);
        }
        written = (written + per_channel).min(samples * 2);
        at = data + size * (channels - 1) + per_channel;
    }
    Ok(out)
}

/// Each AW file's PCM16 ranges (offset, bytes) from an audio archive's wave systems, the disc's or a cooked one.
pub fn pcm16_ranges(archive_file: &[u8]) -> Result<BTreeMap<String, Vec<(usize, usize)>>> {
    let expanded;
    let bytes = if archive::is_compressed(archive_file) {
        expanded = archive::expand(archive_file)?;
        &expanded[..]
    } else {
        archive_file
    };
    let mut ranges: BTreeMap<String, Vec<(usize, usize)>> = BTreeMap::new();
    let mut search = 0usize;
    while let Some(found) = bytes[search..].windows(4).position(|w| w == b"WSYS") {
        let base = search + found;
        search = base + 4;
        // The size word tells the order: it fits the file only as the order it was written in.
        let size_be = be32(bytes, base + 4)? as usize;
        let host = size_be > bytes.len();
        let word = |at: usize| -> Result<usize> {
            let raw: [u8; 4] = bytes.get(at..at + 4).context("a wave system field past the end")?.try_into().unwrap();
            Ok(if host { u32::from_le_bytes(raw) } else { u32::from_be_bytes(raw) } as usize)
        };
        let bank = word(base + 0x10)?;
        if bank == 0 {
            continue;
        }
        for a in 0..word(base + bank + 4)? {
            let entry = word(base + bank + 8 + 4 * a)?;
            if entry == 0 {
                continue;
            }
            let name_at = base + entry;
            let name_bytes = bytes.get(name_at..name_at + 0x70).context("a wave archive's name")?;
            let name_end = name_bytes.iter().position(|b| *b == 0).unwrap_or(name_bytes.len());
            let name = String::from_utf8_lossy(&name_bytes[..name_end]).into_owned();
            for w in 0..word(name_at + 0x70)? {
                let wave = word(name_at + 0x74 + 4 * w)?;
                if wave != 0 && bytes.get(base + wave + 1).copied() == Some(WAVE_PCM16) {
                    ranges.entry(name.to_ascii_lowercase()).or_default().push((word(base + wave + 8)?, word(base + wave + 0xC)?));
                }
            }
        }
    }
    for list in ranges.values_mut() {
        list.sort_unstable();
        list.dedup();
    }
    Ok(ranges)
}

/// An AW file with the given PCM16 ranges in host order (ranges past its end are refused).
pub fn wave_archive(bytes: &[u8], ranges: &[(usize, usize)]) -> Result<Vec<u8>> {
    let mut out = bytes.to_vec();
    for &(start, length) in ranges {
        let samples = out.get_mut(start..start + length).with_context(|| format!("PCM16 wave at {start:#x} past the file's end"))?;
        turn_samples(samples);
    }
    Ok(out)
}

/// The file name a wave system uses for a path.
pub fn wave_archive_key(path: &str) -> String {
    path.rsplit('/').next().unwrap_or(path).to_ascii_lowercase()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_stream_cooked() {
        let mut s = vec![0u8; AST_HEADER + AST_BLOCK_HEADER + 8];
        s[..4].copy_from_slice(b"STRM");
        s[9] = FORMAT_PCM16;
        s[0xC..0xE].copy_from_slice(&2u16.to_be_bytes());
        s[0x10..0x14].copy_from_slice(&32000u32.to_be_bytes());
        s[0x14..0x18].copy_from_slice(&2u32.to_be_bytes());
        s[0x1C..0x20].copy_from_slice(&2u32.to_be_bytes());
        s[AST_HEADER..AST_HEADER + 4].copy_from_slice(b"BLCK");
        s[AST_HEADER + 4..AST_HEADER + 8].copy_from_slice(&4u32.to_be_bytes());
        let data = AST_HEADER + AST_BLOCK_HEADER;
        s[data..data + 2].copy_from_slice(&0x1234u16.to_be_bytes());
        s[data + 4..data + 6].copy_from_slice(&0x5678u16.to_be_bytes());
        let cooked = ast(&s).unwrap();
        assert_eq!(&cooked[..4], b"RTSN");
        assert_eq!(u32::from_le_bytes(cooked[4..8].try_into().unwrap()), 2);
        assert_eq!(u32::from_le_bytes(cooked[12..16].try_into().unwrap()), 2);
        assert_eq!(cooked.len(), STREAM_HEADER + 8);
        assert_eq!(i16::from_le_bytes([cooked[STREAM_HEADER], cooked[STREAM_HEADER + 1]]), 0x1234);
        assert_eq!(i16::from_le_bytes([cooked[STREAM_HEADER + 4], cooked[STREAM_HEADER + 5]]), 0x5678);
    }

    #[test]
    fn a_truncated_stream_is_refused() {
        let mut s = vec![0u8; AST_HEADER + 4];
        s[..4].copy_from_slice(b"STRM");
        s[9] = FORMAT_PCM16;
        s[0xC..0xE].copy_from_slice(&1u16.to_be_bytes());
        assert!(ast(&s).is_err());
    }

    #[test]
    fn wave_ranges_turn_only_their_bytes() {
        let cooked = wave_archive(&[1, 2, 3, 4, 5, 6], &[(2, 2)]).unwrap();
        assert_eq!(cooked, vec![1, 2, 4, 3, 5, 6]);
        assert!(wave_archive(&[1, 2], &[(2, 2)]).is_err());
    }
}
