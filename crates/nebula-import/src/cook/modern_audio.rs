//! Common audio files in place of the game's streams: WAV (16-bit PCM) and Ogg Vorbis become the stream form the game
//! plays (audio_data::ast's). A loop comes from the file: a WAV's `smpl` chunk, an Ogg's LOOPSTART and LOOPEND comments;
//! without one the whole track loops.

use anyhow::{bail, Context, Result};

const STREAM_HEADER: usize = 32;
const DEFAULT_VOLUME: u32 = 127;

struct Audio {
    channels: usize,
    rate: u32,
    /// Interleaved samples.
    samples: Vec<i16>,
    loop_points: Option<(u32, u32)>,
}

fn stream(audio: &Audio) -> Result<Vec<u8>> {
    if audio.channels == 0 || audio.channels > 6 {
        bail!("{} channels: a stream has 1 to 6", audio.channels);
    }
    let frames = audio.samples.len() / audio.channels;
    if frames == 0 {
        bail!("it has no sound");
    }
    let (start, end) = audio.loop_points.unwrap_or((0, frames as u32));
    if end as usize > frames || start >= end {
        bail!("its loop is past its end");
    }
    let header: [u32; 8] = [u32::from_be_bytes(*b"NSTR"), audio.channels as u32, audio.rate, frames as u32, 1, start, end, DEFAULT_VOLUME];
    let mut out = vec![0u8; STREAM_HEADER + frames * 2 * audio.channels];
    for (i, value) in header.iter().enumerate() {
        out[i * 4..i * 4 + 4].copy_from_slice(&value.to_le_bytes());
    }
    for channel in 0..audio.channels {
        let target = STREAM_HEADER + frames * 2 * channel;
        for frame in 0..frames {
            let sample = audio.samples[frame * audio.channels + channel];
            out[target + frame * 2..target + frame * 2 + 2].copy_from_slice(&sample.to_le_bytes());
        }
    }
    Ok(out)
}

fn le32(bytes: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(bytes.get(at..at + 4).context("a cut-off file")?.try_into()?))
}

fn le16(bytes: &[u8], at: usize) -> Result<u16> {
    Ok(u16::from_le_bytes(bytes.get(at..at + 2).context("a cut-off file")?.try_into()?))
}

fn read_wav(bytes: &[u8]) -> Result<Audio> {
    if bytes.get(..4) != Some(b"RIFF") || bytes.get(8..12) != Some(b"WAVE") {
        bail!("not a WAV file");
    }
    let mut at = 12;
    let (mut channels, mut rate, mut samples, mut loop_points) = (0usize, 0u32, Vec::new(), None);
    while at + 8 <= bytes.len() {
        let kind = &bytes[at..at + 4];
        let size = le32(bytes, at + 4)? as usize;
        let body = bytes.get(at + 8..(at + 8 + size).min(bytes.len())).context("a cut-off file")?;
        match kind {
            b"fmt " => {
                let format = le16(body, 0)?;
                channels = le16(body, 2)? as usize;
                rate = le32(body, 4)?;
                let bits = le16(body, 14)?;
                // 0xFFFE: the extended form of the same, by its bits.
                if (format != 1 && format != 0xFFFE) || bits != 16 {
                    bail!("WAV has to be 16-bit PCM");
                }
            }
            b"data" => samples = body.chunks_exact(2).map(|s| i16::from_le_bytes([s[0], s[1]])).collect(),
            b"smpl" if le32(body, 28)? > 0 => loop_points = Some((le32(body, 44)?, le32(body, 48)?.saturating_add(1))),
            _ => {}
        }
        at += 8 + size + (size & 1);
    }
    if channels == 0 {
        bail!("a WAV with no format");
    }
    Ok(Audio { channels, rate, samples, loop_points })
}

fn read_ogg(bytes: &[u8]) -> Result<Audio> {
    let mut reader = lewton::inside_ogg::OggStreamReader::new(std::io::Cursor::new(bytes)).context("the Ogg file can't be read")?;
    let channels = usize::from(reader.ident_hdr.audio_channels);
    let rate = reader.ident_hdr.audio_sample_rate;
    let comment = |name: &str| reader.comment_hdr.comment_list.iter().find(|(k, _)| k.eq_ignore_ascii_case(name)).and_then(|(_, v)| v.trim().parse::<u32>().ok());
    let loop_points = match (comment("LOOPSTART"), comment("LOOPEND"), comment("LOOPLENGTH")) {
        (Some(start), Some(end), _) => Some((start, end)),
        (Some(start), None, Some(length)) => Some((start, start.saturating_add(length))),
        _ => None,
    };
    let mut samples = Vec::new();
    while let Some(packet) = reader.read_dec_packet_itl().context("the Ogg file can't be read")? {
        samples.extend_from_slice(&packet);
    }
    Ok(Audio { channels, rate, samples, loop_points })
}

/// A file named `.wav` or `.ogg`, as the game's stream.
pub fn to_stream(name: &str, bytes: &[u8]) -> Result<Vec<u8>> {
    let lower = name.to_ascii_lowercase();
    let audio = if lower.ends_with(".wav") {
        read_wav(bytes)?
    } else if lower.ends_with(".ogg") {
        read_ogg(bytes)?
    } else {
        bail!("{name} isn't WAV or Ogg")
    };
    stream(&audio)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn wav(channels: u16, samples: &[i16], looped: Option<(u32, u32)>) -> Vec<u8> {
        let data: Vec<u8> = samples.iter().flat_map(|s| s.to_le_bytes()).collect();
        let mut chunks = Vec::new();
        chunks.extend_from_slice(b"fmt ");
        chunks.extend_from_slice(&16u32.to_le_bytes());
        chunks.extend_from_slice(&1u16.to_le_bytes());
        chunks.extend_from_slice(&channels.to_le_bytes());
        chunks.extend_from_slice(&32000u32.to_le_bytes());
        chunks.extend_from_slice(&(32000u32 * 2 * u32::from(channels)).to_le_bytes());
        chunks.extend_from_slice(&(2 * channels).to_le_bytes());
        chunks.extend_from_slice(&16u16.to_le_bytes());
        chunks.extend_from_slice(b"data");
        chunks.extend_from_slice(&(data.len() as u32).to_le_bytes());
        chunks.extend_from_slice(&data);
        if let Some((start, end)) = looped {
            let mut smpl = vec![0u8; 36 + 24];
            smpl[28..32].copy_from_slice(&1u32.to_le_bytes());
            smpl[44..48].copy_from_slice(&start.to_le_bytes());
            smpl[48..52].copy_from_slice(&end.to_le_bytes());
            chunks.extend_from_slice(b"smpl");
            chunks.extend_from_slice(&(smpl.len() as u32).to_le_bytes());
            chunks.extend_from_slice(&smpl);
        }
        let mut out = b"RIFF".to_vec();
        out.extend_from_slice(&(chunks.len() as u32 + 4).to_le_bytes());
        out.extend_from_slice(b"WAVE");
        out.extend_from_slice(&chunks);
        out
    }

    fn word(s: &[u8], i: usize) -> u32 {
        u32::from_le_bytes(s[i * 4..i * 4 + 4].try_into().unwrap())
    }

    #[test]
    fn a_wav_becomes_a_stream_with_each_channel_in_turn() {
        let s = to_stream("a.wav", &wav(2, &[1, 2, 3, 4], None)).unwrap();
        assert_eq!(&s[..4], b"RTSN");
        assert_eq!(word(&s, 1), 2);
        assert_eq!(word(&s, 2), 32000);
        assert_eq!(word(&s, 3), 2);
        assert_eq!((word(&s, 4), word(&s, 5), word(&s, 6)), (1, 0, 2));
        let samples: Vec<i16> = s[32..].chunks_exact(2).map(|b| i16::from_le_bytes([b[0], b[1]])).collect();
        assert_eq!(samples, [1, 3, 2, 4]);
    }

    #[test]
    fn a_wavs_loop_is_kept_and_a_bad_one_is_named() {
        let s = to_stream("a.wav", &wav(1, &[1, 2, 3, 4], Some((1, 2)))).unwrap();
        assert_eq!((word(&s, 5), word(&s, 6)), (1, 3));
        assert!(format!("{:#}", to_stream("a.wav", &wav(1, &[1, 2], Some((1, 9)))).unwrap_err()).contains("loop"));
    }

    #[test]
    fn what_cant_be_read_says_so() {
        assert!(to_stream("a.wav", b"nope").is_err());
        assert!(to_stream("a.ogg", b"nope").is_err());
        assert!(to_stream("a.mp3", b"nope").is_err());
    }
}
