//! Movies: a THP file rewritten for the native game, in host order. The video stays JPEG (the decoder's input);
//! the sound becomes PCM16; the frame sizes and the file header become plain numbers.
//!
//! The file: `NMOV`, the frame count, frame rate, largest frame, first frame's size, width, height, audio tracks, the
//! picture's shape (f32, width / height as shown), then zeros to `HEADER` bytes; then the frames, each `next frame's size, video size, one track's audio size, 0`, the
//! video, each track's audio record, padded to 32 bytes. An audio record: its stereo sample count (u32), then the samples,
//! left and right in turn, PCM16 (movie_audio.h).

use anyhow::{bail, Context, Result};

pub const MAGIC: &[u8; 4] = b"NMOV";
const HEADER: usize = 0x40;
const FRAME_HEADER: usize = 0x10;
/// A disc audio record's header: the second channel's offset, the samples, each channel's eight predictor pairs and its two
/// history samples.
const AUDIO_HEADER: usize = 0x50;
/// The game draws a THP across the whole width of a 16:9 picture this many lines tall, its height in those lines: its
/// pixels are wider than tall (the disc's 640 x 368 shows at 2.2 to 1).
const PICTURE_LINES: f32 = 456.0;

/// A THP's shape as the game shows it.
fn display_aspect(height: u32) -> f32 {
    16.0 / 9.0 * PICTURE_LINES / height as f32
}

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(bytes.get(at..at + 4).context("a field past the movie's end")?.try_into().unwrap()))
}

fn align32(n: usize) -> usize {
    (n + 31) & !31
}

fn be16(bytes: &[u8], at: usize) -> Result<i64> {
    Ok(i16::from_be_bytes(bytes.get(at..at + 2).context("a field past the movie's end")?.try_into().unwrap()) as i64)
}

/// One channel of a disc audio record: 8-byte frames of a header byte (predictor in bits 4-6, scale in bits 0-3) and 14
/// four-bit samples, into every other sample of `out` from `first` (data past the record's end stays silent).
fn decode_channel(data: &[u8], predictors: &[u8], mut history: [i64; 2], samples: usize, out: &mut [i16], first: usize) -> Result<()> {
    let (mut c1, mut c2, mut scale) = (0i64, 0i64, 0u32);
    for i in 0..samples {
        let nibble_index = i % 14;
        let Some(frame) = data.get(i / 14 * 8..i / 14 * 8 + 8) else {
            break;
        };
        if nibble_index == 0 {
            let predictor = ((frame[0] >> 4) & 7) as usize;
            scale = (frame[0] & 15) as u32;
            c1 = be16(predictors, predictor * 4)?;
            c2 = be16(predictors, predictor * 4 + 2)?;
        }
        let byte = frame[1 + nibble_index / 2];
        let bits = if nibble_index & 1 != 0 { byte << 4 } else { byte };
        let nibble = (bits as i8 >> 4) as i64;
        let value = c1 * history[0] + c2 * history[1] + ((nibble << scale) << 11);
        let sample = (((value << 5) + 0x8000).clamp(-2147483648, 2147483647) >> 16) as i16;
        out[i * 2 + first] = sample;
        history = [sample as i64, history[0]];
    }
    Ok(())
}

/// The samples a disc audio record holds.
fn record_samples(record: &[u8]) -> Result<usize> {
    if record.len() < AUDIO_HEADER {
        bail!("an audio record shorter than its header");
    }
    let samples = be32(record, 4)? as usize;
    if samples > 0x10000 {
        bail!("{samples} samples in an audio record");
    }
    Ok(samples)
}

/// A disc audio record as the native one (a mono record plays on both sides), `samples` from the frame's largest track.
fn audio_record(record: &[u8], samples: usize, out: &mut [u8]) -> Result<()> {
    let second = be32(record, 0)? as usize;
    let own = record_samples(record)?;
    let data = &record[AUDIO_HEADER..];
    let mut pcm = vec![0i16; samples * 2];
    let left = [be16(record, 0x48)?, be16(record, 0x4A)?];
    decode_channel(data, &record[8..0x28], left, own, &mut pcm, 0)?;
    if second == 0 {
        for pair in pcm.chunks_exact_mut(2) {
            pair[1] = pair[0];
        }
    } else {
        let right = [be16(record, 0x4C)?, be16(record, 0x4E)?];
        decode_channel(data.get(second..).context("a second channel past the record")?, &record[0x28..0x48], right, own, &mut pcm, 1)?;
    }
    out[..4].copy_from_slice(&(samples as u32).to_le_bytes());
    for (sample, target) in pcm.iter().zip(out[4..].chunks_exact_mut(2)) {
        target.copy_from_slice(&sample.to_le_bytes());
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
    if width == 0 || height == 0 || width > 0x4000 || height > 0x4000 {
        bail!("a {width} x {height} picture");
    }
    let tracks = if components > 1 { be32(bytes, info + 32 + 12)? as usize } else { 0 };
    if components > 2 {
        bail!("{} audio components in a frame", components - 1);
    }
    if tracks == 0 && components > 1 {
        bail!("an audio component with no tracks");
    }
    // Each is at least a frame's header in the file; a file's tracks are few.
    if tracks > 16 || frames > bytes.len() / (8 + components * 4) {
        bail!("{frames} frames of {tracks} tracks in a movie of {} bytes", bytes.len());
    }

    let mut out = vec![0u8; HEADER];
    out.reserve(bytes.len());
    let mut sizes = Vec::with_capacity(frames);
    for frame in 0..frames {
        let body = bytes.get(at..at + size).with_context(|| format!("frame {frame} past the movie's end"))?;
        let header = 8 + components * 4;
        let video = be32(body, 8)? as usize;
        let audio = if components > 1 { be32(body, 12)? as usize } else { 0 };
        let payload = body.get(header..header + video + audio * tracks).with_context(|| format!("frame {frame}: sizes past its end"))?;
        let records: Vec<&[u8]> = (0..tracks).map(|track| &payload[video + audio * track..video + audio * track + audio]).collect();
        let mut samples = 0usize;
        for record in &records {
            samples = samples.max(record_samples(record).with_context(|| format!("frame {frame}"))?);
        }
        let cooked_audio = if tracks != 0 { 4 + samples * 4 } else { 0 };
        let total = FRAME_HEADER + video + cooked_audio * tracks;
        let start = out.len();
        // A mono record's 8 bytes give 56; past that its header's sample counts are wrong.
        if start + align32(total) > bytes.len().saturating_mul(16) + (1 << 20) {
            bail!("frame {frame}: audio sample counts past what the movie holds");
        }
        out.resize(start + align32(total), 0);
        sizes.push(align32(total));
        out[start + 4..start + 8].copy_from_slice(&(video as u32).to_le_bytes());
        out[start + 8..start + 12].copy_from_slice(&(cooked_audio as u32).to_le_bytes());
        out[start + FRAME_HEADER..start + FRAME_HEADER + video].copy_from_slice(&payload[..video]);
        for (track, record) in records.iter().enumerate() {
            let at = start + FRAME_HEADER + video + cooked_audio * track;
            audio_record(record, samples, &mut out[at..at + cooked_audio]).with_context(|| format!("frame {frame}"))?;
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
    out[32..36].copy_from_slice(&display_aspect(height).to_le_bytes());
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
            put(&mut thp, at + 12, AUDIO_HEADER as u32 + 8);
            thp[at + 16] = 0xFF;
            // One sample, one channel: the predictor 0 pair is 1.0 and 0; history 0; scale 0, nibble 1.
            let record = at + 24;
            put(&mut thp, record + 4, 1);
            thp[record + 8] = 0x08;
            thp[record + AUDIO_HEADER + 1] = 0x10;
        }
        let cooked = movie(&thp).unwrap();
        assert_eq!(&cooked[..4], MAGIC);
        assert_eq!(u32::from_le_bytes(cooked[4..8].try_into().unwrap()), 2);
        assert_eq!(u32::from_le_bytes(cooked[20..24].try_into().unwrap()), 640);
        let aspect = f32::from_le_bytes(cooked[32..36].try_into().unwrap());
        assert!((aspect - 16.0 / 9.0 * 456.0 / 368.0).abs() < 1e-5);
        let frame = &cooked[HEADER..];
        assert_eq!(u32::from_le_bytes(frame[4..8].try_into().unwrap()), 8);
        assert_eq!(frame[FRAME_HEADER], 0xFF);
        // The audio record: one stereo sample, the mono channel on both sides.
        let audio = &frame[FRAME_HEADER + 8..];
        assert_eq!(u32::from_le_bytes(frame[8..12].try_into().unwrap()), 8);
        assert_eq!(u32::from_le_bytes(audio[..4].try_into().unwrap()), 1);
        assert_eq!(i16::from_le_bytes([audio[4], audio[5]]), 1);
        assert_eq!(i16::from_le_bytes([audio[6], audio[7]]), 1);
    }

    fn header_only(frames: u32, tracks: u32) -> Vec<u8> {
        let mut thp = vec![0u8; 0x60];
        thp[..4].copy_from_slice(b"THP\0");
        put(&mut thp, 0x14, frames);
        put(&mut thp, 0x20, 0x30);
        put(&mut thp, 0x30, 2);
        thp[0x35] = 1;
        put(&mut thp, 0x44, 640);
        put(&mut thp, 0x48, 368);
        put(&mut thp, 0x50 + 12, tracks);
        thp
    }

    #[test]
    fn header_counts_are_refused_before_their_allocations() {
        assert!(movie(&header_only(u32::MAX, 1)).unwrap_err().to_string().contains("frames of"));
        assert!(movie(&header_only(1, u32::MAX)).unwrap_err().to_string().contains("frames of"));
    }

    #[test]
    fn a_picture_of_no_size_is_refused() {
        let mut thp = header_only(1, 1);
        put(&mut thp, 0x48, 0);
        assert!(movie(&thp).unwrap_err().to_string().contains("picture"));
    }

    #[test]
    fn a_short_file_is_refused() {
        assert!(movie(b"THP\0").is_err());
        assert!(movie(b"nope").is_err());
    }
}
