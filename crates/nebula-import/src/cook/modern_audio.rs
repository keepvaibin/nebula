//! Common audio files in place of the game's streams: WAV (16-bit PCM), Ogg Vorbis and Nintendo's streams (BRSTM,
//! BCSTM, BFSTM, as custom music sites share them) become the stream form the game plays (audio_data::ast's). A loop
//! comes from the file: a WAV's `smpl` chunk, an Ogg's LOOPSTART and LOOPEND comments, a Nintendo stream's loop start;
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


/// A number of a file in its own byte order.
struct Reader<'a> {
    bytes: &'a [u8],
    big: bool,
}

impl Reader<'_> {
    fn u8(&self, at: usize) -> Result<u8> {
        self.bytes.get(at).copied().context("a cut-off file")
    }

    fn u16(&self, at: usize) -> Result<u16> {
        let b: [u8; 2] = self.bytes.get(at..at + 2).context("a cut-off file")?.try_into()?;
        Ok(if self.big { u16::from_be_bytes(b) } else { u16::from_le_bytes(b) })
    }

    fn u32(&self, at: usize) -> Result<u32> {
        let b: [u8; 4] = self.bytes.get(at..at + 4).context("a cut-off file")?.try_into()?;
        Ok(if self.big { u32::from_be_bytes(b) } else { u32::from_le_bytes(b) })
    }

    /// An offset into the file: a number from it plus where it counts from.
    fn at(&self, base: usize, at: usize) -> Result<usize> {
        let offset = self.u32(at)? as usize;
        let to = base.checked_add(offset).context("an offset past the file")?;
        if to >= self.bytes.len() {
            bail!("an offset past the file");
        }
        Ok(to)
    }
}

/// Nintendo's 4-bit DSP ADPCM: frames of 8 bytes, a predictor and scale then 14 samples, each predicted from the two
/// before it with the channel's coefficients.
fn decode_dsp(data: &[u8], samples: usize, coefs: &[i16; 16], history: &mut (i32, i32), out: &mut Vec<i16>) {
    let (mut h1, mut h2) = *history;
    let mut left = samples;
    for frame in data.chunks(8) {
        if left == 0 {
            break;
        }
        let predictor = usize::from((frame[0] >> 4) & 7);
        let scale = 1i32 << (frame[0] & 15);
        let (c1, c2) = (i32::from(coefs[predictor * 2]), i32::from(coefs[predictor * 2 + 1]));
        for i in 0..14 {
            if left == 0 {
                break;
            }
            let byte = frame.get(1 + i / 2).copied().unwrap_or(0);
            let nibble = i32::from(if i % 2 == 0 { byte >> 4 } else { byte & 15 });
            let nibble = if nibble >= 8 { nibble - 16 } else { nibble };
            let sample = ((((nibble * scale) << 11) + 1024 + c1 * h1 + c2 * h2) >> 11).clamp(-32768, 32767);
            out.push(sample as i16);
            h2 = h1;
            h1 = sample;
            left -= 1;
        }
    }
    *history = (h1, h2);
}

/// What a Nintendo stream's header says of its sound and where it is.
struct StreamInfo {
    codec: u8,
    looped: bool,
    channels: usize,
    rate: u32,
    loop_start: u32,
    frames: u32,
    blocks: usize,
    block_size: usize,
    block_samples: usize,
    last_size: usize,
    last_samples: usize,
    last_padded: usize,
    /// Where the first block starts.
    data: usize,
    /// Each channel's DSP ADPCM coefficients (zeros for PCM).
    coefs: Vec<[i16; 16]>,
}

fn read_coefs(r: &Reader, at: usize) -> Result<[i16; 16]> {
    let mut coefs = [0i16; 16];
    for (i, c) in coefs.iter_mut().enumerate() {
        *c = r.u16(at + i * 2)? as i16;
    }
    Ok(coefs)
}

/// BRSTM (`RSTM`, big-endian; Wii): HEAD's three parts give the stream, its tracks and its channels.
fn brstm_info(r: &Reader) -> Result<StreamInfo> {
    let head = r.u32(0x10)? as usize;
    if r.bytes.get(head..head + 4) != Some(b"HEAD") {
        bail!("a BRSTM with no HEAD");
    }
    let base = head + 8;
    let info = r.at(base, base + 4)?;
    let table = r.at(base, base + 0x14)?;
    let channels = usize::from(r.u8(info + 2)?);
    let mut coefs = Vec::with_capacity(channels);
    for c in 0..channels {
        let channel = r.at(base, table + 4 + c * 8 + 4)?;
        coefs.push(read_coefs(r, r.at(base, channel + 4)?)?);
    }
    Ok(StreamInfo {
        codec: r.u8(info)?,
        looped: r.u8(info + 1)? != 0,
        channels,
        rate: u32::from(r.u16(info + 4)?),
        loop_start: r.u32(info + 8)?,
        frames: r.u32(info + 0xC)?,
        data: r.u32(info + 0x10)? as usize,
        blocks: r.u32(info + 0x14)? as usize,
        block_size: r.u32(info + 0x18)? as usize,
        block_samples: r.u32(info + 0x1C)? as usize,
        last_size: r.u32(info + 0x20)? as usize,
        last_samples: r.u32(info + 0x24)? as usize,
        last_padded: r.u32(info + 0x28)? as usize,
        coefs,
    })
}

/// BCSTM (`CSTM`, 3DS) and BFSTM (`FSTM`, Wii U and Switch): sections by type, INFO's references to the stream and
/// its channels, DATA's samples.
fn cstm_info(r: &Reader) -> Result<StreamInfo> {
    let sections = usize::from(r.u16(0x10)?);
    let (mut info, mut data) = (None, None);
    for s in 0..sections {
        let at = 0x14 + s * 12;
        match r.u16(at)? {
            0x4000 => info = Some(r.u32(at + 4)? as usize),
            0x4002 => data = Some(r.u32(at + 4)? as usize),
            _ => {}
        }
    }
    let (info, data) = (info.context("a stream with no INFO")?, data.context("a stream with no DATA")?);
    let base = info + 8;
    let stream = r.at(base, base + 4)?;
    let table = r.at(base, base + 0x14)?;
    let channels = usize::from(r.u8(stream + 2)?);
    let codec = r.u8(stream)?;
    let mut coefs = Vec::with_capacity(channels);
    for c in 0..channels {
        let channel = r.at(table, table + 4 + c * 8 + 4)?;
        coefs.push(if codec == 2 { read_coefs(r, r.at(channel, channel + 4)?)? } else { [0; 16] });
    }
    Ok(StreamInfo {
        codec,
        looped: r.u8(stream + 1)? != 0,
        channels,
        rate: r.u32(stream + 4)?,
        loop_start: r.u32(stream + 8)?,
        frames: r.u32(stream + 0xC)?,
        blocks: r.u32(stream + 0x10)? as usize,
        block_size: r.u32(stream + 0x14)? as usize,
        block_samples: r.u32(stream + 0x18)? as usize,
        last_size: r.u32(stream + 0x1C)? as usize,
        last_samples: r.u32(stream + 0x20)? as usize,
        last_padded: r.u32(stream + 0x24)? as usize,
        data: r.at(data + 8, stream + 0x34)?,
        coefs,
    })
}

/// A Nintendo stream (BRSTM, BCSTM, BFSTM): its blocks, each channel's block in turn, decoded to PCM16.
fn read_nintendo_stream(bytes: &[u8]) -> Result<Audio> {
    let magic = bytes.get(..4).context("a cut-off file")?;
    let big = match bytes.get(4..6) {
        Some([0xFE, 0xFF]) => true,
        Some([0xFF, 0xFE]) => false,
        _ => bail!("a stream with no byte order mark"),
    };
    let r = Reader { bytes, big };
    let info = match magic {
        b"RSTM" => brstm_info(&r)?,
        b"CSTM" | b"FSTM" => cstm_info(&r)?,
        _ => bail!("not a BRSTM, BCSTM or BFSTM"),
    };
    if info.codec > 2 {
        bail!("codec {}: only PCM8, PCM16 and DSP ADPCM streams are read", info.codec);
    }
    if info.channels == 0 || info.blocks == 0 {
        bail!("a stream with no sound");
    }
    let frames = info.frames as usize;
    // A header may claim any count: no channel holds more samples than two a byte of the file (DSP ADPCM's 14 from 8).
    let mut channels: Vec<Vec<i16>> = vec![Vec::with_capacity(frames.min(bytes.len() * 2)); info.channels];
    let mut history = vec![(0i32, 0i32); info.channels];
    for block in 0..info.blocks {
        let last = block + 1 == info.blocks;
        let (stride, samples) = if last { (info.last_padded, info.last_samples) } else { (info.block_size, info.block_samples) };
        let size = if last { info.last_size } else { info.block_size };
        let start = info.data + block * info.block_size * info.channels;
        for (c, out) in channels.iter_mut().enumerate() {
            let at = start + c * stride;
            let data = bytes.get(at..at + size).with_context(|| format!("block {block} past the file's end"))?;
            match info.codec {
                0 => out.extend(data.iter().take(samples).map(|&b| i16::from(b as i8) << 8)),
                1 => out.extend(data.chunks_exact(2).take(samples).map(|b| if big { i16::from_be_bytes([b[0], b[1]]) } else { i16::from_le_bytes([b[0], b[1]]) })),
                _ => decode_dsp(data, samples, &info.coefs[c], &mut history[c], out),
            }
        }
    }
    let frames = frames.min(channels.iter().map(Vec::len).min().unwrap_or(0));
    let mut samples = Vec::with_capacity(frames * info.channels);
    for f in 0..frames {
        for channel in &channels {
            samples.push(channel[f]);
        }
    }
    let loop_points = (info.looped && (info.loop_start as usize) < frames).then_some((info.loop_start, frames as u32));
    Ok(Audio { channels: info.channels, rate: info.rate, samples, loop_points })
}

/// Channels, rate, interleaved samples, the loop in frames.
pub type Decoded = (usize, u32, Vec<i16>, Option<(u32, u32)>);

/// A Nintendo stream (BRSTM, BCSTM, BFSTM) decoded: channels, rate, interleaved samples, its loop in frames.
pub fn read_nintendo(bytes: &[u8]) -> Result<Decoded> {
    let a = read_nintendo_stream(bytes)?;
    Ok((a.channels, a.rate, a.samples, a.loop_points))
}

/// Whether a file name is one of the sound files `to_stream` reads.
pub fn is_stream_source(name: &str) -> bool {
    let lower = name.to_ascii_lowercase();
    [".wav", ".ogg", ".brstm", ".bcstm", ".bfstm"].iter().any(|e| lower.ends_with(e))
}

/// A file named `.wav`, `.ogg`, `.brstm`, `.bcstm` or `.bfstm`, as the game's stream.
pub fn to_stream(name: &str, bytes: &[u8]) -> Result<Vec<u8>> {
    let lower = name.to_ascii_lowercase();
    let audio = if lower.ends_with(".wav") {
        read_wav(bytes)?
    } else if lower.ends_with(".ogg") {
        read_ogg(bytes)?
    } else if lower.ends_with(".brstm") || lower.ends_with(".bcstm") || lower.ends_with(".bfstm") {
        read_nintendo_stream(bytes)?
    } else {
        bail!("{name} isn't WAV, Ogg, BRSTM, BCSTM or BFSTM")
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
    fn dsp_adpcm_decodes_by_scale_and_prediction() {
        let mut out = Vec::new();
        let mut history = (0, 0);
        // No prediction, scale 1: the nibbles are the samples (1..7, then -8..-2 as their two's complement).
        decode_dsp(&[0x00, 0x12, 0x34, 0x56, 0x7F, 0xED, 0xCB, 0xA9], 14, &[0; 16], &mut history, &mut out);
        assert_eq!(out, [1, 2, 3, 4, 5, 6, 7, -1, -2, -3, -4, -5, -6, -7]);
        // Predictor 0 with coefficient 2048 (1.0) on the last sample: each sample adds to the one before.
        let mut coefs = [0i16; 16];
        coefs[0] = 2048;
        let mut more = Vec::new();
        decode_dsp(&[0x00, 0x11, 0x00, 0, 0, 0, 0, 0], 3, &coefs, &mut history, &mut more);
        assert_eq!(more, [-6, -5, -5]);
    }

    /// A BRSTM as its public description lays it out: header, HEAD with its three parts, DATA; PCM16, one block.
    fn brstm(samples: &[[i16; 2]], loop_start: Option<u32>) -> Vec<u8> {
        let mut f = vec![0u8; 0x40];
        f[..4].copy_from_slice(b"RSTM");
        f[4..6].copy_from_slice(&[0xFE, 0xFF]);
        let head = f.len();
        let put32 = |f: &mut Vec<u8>, at: usize, v: u32| f[at..at + 4].copy_from_slice(&v.to_be_bytes());
        f.resize(head + 0x100, 0);
        f[head..head + 4].copy_from_slice(b"HEAD");
        let base = head + 8;
        put32(&mut f, 0x10, head as u32);
        // Part 1 at base+0x20, part 3 (channels) at base+0x60, channel infos at +0x70/+0x80, ADPCM infos at +0x90/+0xC0.
        put32(&mut f, base, 0x0100_0000);
        put32(&mut f, base + 4, 0x20);
        put32(&mut f, base + 0x10, 0x0100_0000);
        put32(&mut f, base + 0x14, 0x60);
        let info = base + 0x20;
        f[info] = 1;
        f[info + 1] = u8::from(loop_start.is_some());
        f[info + 2] = 2;
        f[info + 4..info + 6].copy_from_slice(&32000u16.to_be_bytes());
        put32(&mut f, info + 8, loop_start.unwrap_or(0));
        put32(&mut f, info + 0xC, samples.len() as u32);
        let data = head + 0x100 + 0x20;
        put32(&mut f, info + 0x10, data as u32);
        put32(&mut f, info + 0x14, 1);
        put32(&mut f, info + 0x18, 0x40);
        put32(&mut f, info + 0x1C, 0x20);
        put32(&mut f, info + 0x20, samples.len() as u32 * 2);
        put32(&mut f, info + 0x24, samples.len() as u32);
        put32(&mut f, info + 0x28, 0x20);
        let table = base + 0x60;
        f[table] = 2;
        for c in 0..2 {
            put32(&mut f, table + 4 + c * 8, 0x0100_0000);
            put32(&mut f, table + 8 + c * 8, 0x70 + c as u32 * 0x10);
            put32(&mut f, base + 0x70 + c * 0x10, 0x0100_0000);
            put32(&mut f, base + 0x74 + c * 0x10, 0x90 + c as u32 * 0x30);
        }
        f.resize(data, 0);
        f[data - 0x20..data - 0x1C].copy_from_slice(b"DATA");
        for c in 0..2 {
            let mut block = vec![0u8; 0x20];
            for (i, s) in samples.iter().enumerate() {
                block[i * 2..i * 2 + 2].copy_from_slice(&s[c].to_be_bytes());
            }
            f.extend_from_slice(&block);
        }
        f
    }

    #[test]
    fn a_brstm_becomes_a_stream_with_its_loop() {
        let s = to_stream("SMG_galaxy01_strm.brstm", &brstm(&[[1, -1], [2, -2], [3, -3]], Some(1))).unwrap();
        assert_eq!((word(&s, 1), word(&s, 2), word(&s, 3)), (2, 32000, 3));
        assert_eq!((word(&s, 5), word(&s, 6)), (1, 3));
        let samples: Vec<i16> = s[32..].chunks_exact(2).map(|b| i16::from_le_bytes([b[0], b[1]])).collect();
        assert_eq!(samples, [1, 2, 3, -1, -2, -3]);
        let whole = to_stream("a.brstm", &brstm(&[[5, 6]], None)).unwrap();
        assert_eq!((word(&whole, 5), word(&whole, 6)), (0, 1));
        assert!(is_stream_source("A.BFSTM") && !is_stream_source("a.ast"));
    }

    /// A BCSTM (little-endian): sections INFO and DATA, references by type; PCM16 mono, one block.
    #[test]
    fn a_bcstm_becomes_a_stream() {
        let mut f = vec![0u8; 0x40];
        f[..4].copy_from_slice(b"CSTM");
        f[4..6].copy_from_slice(&[0xFF, 0xFE]);
        let put32 = |f: &mut Vec<u8>, at: usize, v: u32| f[at..at + 4].copy_from_slice(&v.to_le_bytes());
        f[0x10..0x12].copy_from_slice(&2u16.to_le_bytes());
        let info = 0x40;
        f[0x14..0x16].copy_from_slice(&0x4000u16.to_le_bytes());
        put32(&mut f, 0x18, info as u32);
        f.resize(info + 0x100, 0);
        f[info..info + 4].copy_from_slice(b"INFO");
        let base = info + 8;
        put32(&mut f, base + 4, 0x20);
        put32(&mut f, base + 0x14, 0x80);
        let stream = base + 0x20;
        f[stream] = 1;
        f[stream + 2] = 1;
        put32(&mut f, stream + 4, 48000);
        put32(&mut f, stream + 0xC, 2);
        put32(&mut f, stream + 0x10, 1);
        put32(&mut f, stream + 0x14, 0x20);
        put32(&mut f, stream + 0x18, 0x10);
        put32(&mut f, stream + 0x1C, 4);
        put32(&mut f, stream + 0x20, 2);
        put32(&mut f, stream + 0x24, 0x20);
        put32(&mut f, stream + 0x34, 0x18);
        let table = base + 0x80;
        put32(&mut f, table, 1);
        put32(&mut f, table + 8, 0x10);
        let data = f.len();
        f[0x20..0x22].copy_from_slice(&0x4002u16.to_le_bytes());
        put32(&mut f, 0x24, data as u32);
        f.resize(data + 0x20, 0);
        f[data..data + 4].copy_from_slice(b"DATA");
        f.extend_from_slice(&7i16.to_le_bytes());
        f.extend_from_slice(&(-7i16).to_le_bytes());
        f.resize(data + 0x40, 0);
        let s = to_stream("x.bcstm", &f).unwrap();
        assert_eq!((word(&s, 1), word(&s, 2), word(&s, 3)), (1, 48000, 2));
        let samples: Vec<i16> = s[32..].chunks_exact(2).map(|b| i16::from_le_bytes([b[0], b[1]])).collect();
        assert_eq!(samples, [7, -7]);
    }

    #[test]
    fn what_cant_be_read_says_so() {
        assert!(to_stream("a.wav", b"nope").is_err());
        assert!(to_stream("a.ogg", b"nope").is_err());
        assert!(to_stream("a.mp3", b"nope").is_err());
    }
}
