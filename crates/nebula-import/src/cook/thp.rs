//! Movies (B-60): a THP file rewritten for the native game, in host order. The video stays JPEG (the decoder's input)
//! and the sound DSP-ADPCM (the mixer's input); their headers, frame sizes and the file header become plain numbers.
//!
//! The file: `NMOV`, the frame count, frame rate, largest frame, first frame's size, width, height, audio tracks, then
//! zeros to `HEADER` bytes; then the frames, each `next frame's size, video size, one track's audio size, 0`, the
//! video, each track's audio record, padded to 32 bytes.

use anyhow::{bail, Context, Result};

pub const MAGIC: &[u8; 4] = b"NMOV";
const HEADER: usize = 0x40;
const FRAME_HEADER: usize = 0x10;
/// A track's audio record header (movie_audio.cpp): the second channel's offset, the samples, 32 predictor words and
/// 4 history samples.
const AUDIO_HEADER: usize = 0x50;

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(bytes.get(at..at + 4).context("a field past the movie's end")?.try_into().unwrap()))
}

fn align32(n: usize) -> usize {
    (n + 31) & !31
}

/// One frame's audio record in host order.
fn audio_record(record: &mut [u8]) -> Result<()> {
    if record.len() < AUDIO_HEADER {
        bail!("an audio record shorter than its header");
    }
    for at in [0usize, 4] {
        record[at..at + 4].reverse();
    }
    for at in (8..AUDIO_HEADER).step_by(2) {
        record[at..at + 2].reverse();
    }
    Ok(())
}

/// A THP file as the native movie.
pub fn movie(bytes: &[u8]) -> Result<Vec<u8>> {
    if !bytes.starts_with(b"THP\0") {
        bail!("not a THP file");
    }
    let frame_rate = be32(bytes, 0x10)?;
    let frames = be32(bytes, 0x14)? as usize;
    let mut size = be32(bytes, 0x18)? as usize;
    let info = be32(bytes, 0x20)? as usize;
    let mut at = be32(bytes, 0x28)? as usize;
    let components = be32(bytes, info)? as usize;
    if components == 0 || components > 16 {
        bail!("{components} components in a frame");
    }
    let kinds = bytes.get(info + 4..info + 20).context("the component table")?;
    if kinds[0] != 0 || kinds[1..components].iter().any(|k| *k != 1) {
        bail!("a frame other than video, then audio");
    }
    let (width, height) = (be32(bytes, info + 20)?, be32(bytes, info + 24)?);
    let tracks = if components > 1 { be32(bytes, info + 32 + 12)? as usize } else { 0 };
    if components > 2 {
        bail!("{} audio components in a frame", components - 1);
    }
    if tracks == 0 && components > 1 {
        bail!("an audio component with no tracks");
    }

    let mut out = vec![0u8; HEADER];
    out.reserve(bytes.len());
    let mut sizes = Vec::with_capacity(frames);
    for frame in 0..frames {
        let body = bytes.get(at..at + size).with_context(|| format!("frame {frame} past the movie's end"))?;
        let header = 8 + components * 4;
        let video = be32(body, 8)? as usize;
        let audio = if components > 1 { be32(body, 12)? as usize } else { 0 };
        let total = FRAME_HEADER + video + audio * tracks;
        let payload = body.get(header..header + video + audio * tracks).with_context(|| format!("frame {frame}: sizes past its end"))?;
        let start = out.len();
        out.resize(start + align32(total), 0);
        sizes.push(align32(total));
        out[start + 4..start + 8].copy_from_slice(&(video as u32).to_le_bytes());
        out[start + 8..start + 12].copy_from_slice(&(audio as u32).to_le_bytes());
        out[start + FRAME_HEADER..start + total].copy_from_slice(payload);
        for track in 0..tracks {
            let record = start + FRAME_HEADER + video + audio * track;
            audio_record(&mut out[record..record + audio]).with_context(|| format!("frame {frame}"))?;
        }
        at += size;
        size = be32(body, 0)? as usize;
    }

    // Each frame names the next one's size; the last, the first's (a loop starts over).
    let first = sizes.first().copied().unwrap_or(0);
    let mut start = HEADER;
    for i in 0..frames {
        let next = if i + 1 < frames { sizes[i + 1] } else { first };
        out[start..start + 4].copy_from_slice(&(next as u32).to_le_bytes());
        start += sizes[i];
    }
    let largest = sizes.iter().copied().max().unwrap_or(0);
    out[0..4].copy_from_slice(MAGIC);
    out[4..8].copy_from_slice(&(frames as u32).to_le_bytes());
    out[8..12].copy_from_slice(&frame_rate.to_le_bytes());
    out[12..16].copy_from_slice(&(largest as u32).to_le_bytes());
    out[16..20].copy_from_slice(&(first as u32).to_le_bytes());
    out[20..24].copy_from_slice(&width.to_le_bytes());
    out[24..28].copy_from_slice(&height.to_le_bytes());
    out[28..32].copy_from_slice(&(tracks as u32).to_le_bytes());
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn put(bytes: &mut Vec<u8>, at: usize, value: u32) {
        bytes[at..at + 4].copy_from_slice(&value.to_be_bytes());
    }

    #[test]
    fn two_frames_in_host_order() {
        let frame_size = 0x80usize;
        let mut thp = vec![0u8; 0x60 + frame_size * 2];
        thp[..4].copy_from_slice(b"THP\0");
        put(&mut thp, 0x10, 59.94f32.to_bits());
        put(&mut thp, 0x14, 2);
        put(&mut thp, 0x18, frame_size as u32);
        put(&mut thp, 0x20, 0x30);
        put(&mut thp, 0x28, 0x60);
        put(&mut thp, 0x30, 2);
        thp[0x34] = 0;
        thp[0x35] = 1;
        put(&mut thp, 0x44, 640);
        put(&mut thp, 0x48, 368);
        put(&mut thp, 0x50 + 12, 1);
        for frame in 0..2 {
            let at = 0x60 + frame * frame_size;
            put(&mut thp, at, frame_size as u32);
            put(&mut thp, at + 8, 8);
            put(&mut thp, at + 12, AUDIO_HEADER as u32);
            thp[at + 16] = 0xFF;
            put(&mut thp, at + 24, 0x1234);
        }
        let cooked = movie(&thp).unwrap();
        assert_eq!(&cooked[..4], MAGIC);
        assert_eq!(u32::from_le_bytes(cooked[4..8].try_into().unwrap()), 2);
        assert_eq!(u32::from_le_bytes(cooked[20..24].try_into().unwrap()), 640);
        let frame = &cooked[HEADER..];
        assert_eq!(u32::from_le_bytes(frame[4..8].try_into().unwrap()), 8);
        assert_eq!(frame[FRAME_HEADER], 0xFF);
        // The audio record's first word, host order.
        assert_eq!(u32::from_le_bytes(frame[FRAME_HEADER + 8..FRAME_HEADER + 12].try_into().unwrap()), 0x1234);
    }

    #[test]
    fn a_short_file_is_refused() {
        assert!(movie(b"THP\0").is_err());
        assert!(movie(b"nope").is_err());
    }
}
