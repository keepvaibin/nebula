//! Sound data in host order: AST streams (header, block headers, PCM16 samples) and AW wave archives, every wave
//! PCM16 (the disc's ADPCM and 8-bit waves are decoded here, so the mixer has one source form). An AW file has no
//! header: where its waves lie comes from the wave systems of the audio archive, which name their new places.

use super::archive;
use anyhow::{bail, Context, Result};
use std::collections::BTreeMap;

const AST_HEADER: usize = 0x40;
const AST_BLOCK_HEADER: usize = 0x20;
const STREAM_HEADER: usize = 32;
const FORMAT_PCM16: u8 = 1;
const FORMAT_AFC: u8 = 0;
/// A wave's format in a wave system, on the disc.
const WAVE_ADPCM4: u8 = 0;
const WAVE_ADPCM2: u8 = 1;
const WAVE_PCM8: u8 = 2;
const WAVE_PCM16: u8 = 3;
/// The AFC ADPCM format's predictor pairs, by the block header's low nibble (11-bit fractions).
const ADPCM_PREDICTORS: [[i64; 2]; 16] = [
    [0, 0],
    [2048, 0],
    [0, 2048],
    [1024, 1024],
    [4096, -2048],
    [3584, -1536],
    [3072, -1024],
    [4608, -2560],
    [4200, -2248],
    [4800, -2300],
    [5120, -3072],
    [2048, -2048],
    [1024, -1024],
    [-1024, 1024],
    [-1024, 0],
    [-2048, 0],
];

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(bytes.get(at..at + 4).context("a field past the data's end")?.try_into().unwrap()))
}

/// 16-bit samples turned in place.
fn turn_samples(samples: &mut [u8]) {
    for s in samples.chunks_exact_mut(2) {
        s.swap(0, 1);
    }
}

/// The prediction pairs of the 4-bit ADPCM streams (11 fractional bits).
const AFC_COEFFICIENTS: [(i32, i32); 16] = [
    (0, 0),
    (2048, 0),
    (0, 2048),
    (1024, 1024),
    (4096, -2048),
    (3584, -1536),
    (3072, -1024),
    (4608, -2560),
    (4200, -2248),
    (4800, -2300),
    (5120, -3072),
    (2048, -2048),
    (1024, -1024),
    (-1024, 1024),
    (-1024, 0),
    (-2048, 0),
];
const AFC_FRAME_BYTES: usize = 9;
const AFC_FRAME_SAMPLES: usize = 16;

/// A channel's 4-bit ADPCM frames as little-endian PCM16, up to `room` bytes; `history` carries over between blocks.
fn decode_afc(frames: &[u8], history: &mut (i32, i32), out: &mut [u8]) {
    let mut at = 0usize;
    for frame in frames.chunks_exact(AFC_FRAME_BYTES) {
        let scale = 1i32 << (frame[0] >> 4);
        let (first, second) = AFC_COEFFICIENTS[(frame[0] & 15) as usize];
        for i in 0..AFC_FRAME_SAMPLES {
            let byte = frame[1 + i / 2];
            let nibble = if i & 1 == 0 { byte >> 4 } else { byte & 15 };
            let code = i32::from((nibble as i8) << 4 >> 4);
            let sample = (((code * scale) << 11) + first * history.0 + second * history.1) >> 11;
            let sample = sample.clamp(-32768, 32767);
            history.1 = history.0;
            history.0 = sample;
            if let Some(target) = out.get_mut(at..at + 2) {
                target.copy_from_slice(&(sample as i16).to_le_bytes());
            }
            at += 2;
        }
    }
}

/// A stream as the game plays it (StreamPlayer::Header):the numbers it needs as host numbers, then each channel's PCM16 samples in
/// turn. The disc's blocks and their hold-over samples are gone: the whole file is in memory while it plays.
pub fn ast(bytes: &[u8]) -> Result<Vec<u8>> {
    if bytes.len() < AST_HEADER || &bytes[..4] != b"STRM" {
        bail!("not an AST stream");
    }
    let format = bytes[9];
    if format != FORMAT_PCM16 && format != FORMAT_AFC {
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
    // The blocks hold the samples: a count past twice the file is the header's alone, not worth its allocation.
    let bytes_per_16_samples = if format == FORMAT_AFC { AFC_FRAME_BYTES as u64 } else { 32 };
    if samples as u64 / 16 * bytes_per_16_samples * channels as u64 > bytes.len() as u64 * 2 {
        bail!("{samples} samples in a stream of {} bytes", bytes.len());
    }
    let mut out = vec![0u8; STREAM_HEADER + samples * 2 * channels];
    for (i, value) in header.iter().enumerate() {
        out[i * 4..i * 4 + 4].copy_from_slice(&value.to_le_bytes());
    }
    let mut written = 0usize; // bytes of each channel so far
    let mut histories = vec![(0i32, 0i32); channels];
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
            let source = bytes.get(data + size * channel..data + size * channel + per_channel).with_context(|| format!("a block at {at:#x} past the stream's end"))?;
            let produced = if format == FORMAT_AFC { per_channel / AFC_FRAME_BYTES * AFC_FRAME_SAMPLES * 2 } else { per_channel };
            let take = produced.min((samples * 2).saturating_sub(written));
            let target = STREAM_HEADER + samples * 2 * channel + written;
            if format == FORMAT_AFC {
                decode_afc(source, &mut histories[channel], &mut out[target..target + take]);
            } else {
                out[target..target + take].copy_from_slice(&source[..take]);
                turn_samples(&mut out[target..target + take]);
            }
        }
        let produced = if format == FORMAT_AFC { per_channel / AFC_FRAME_BYTES * AFC_FRAME_SAMPLES * 2 } else { per_channel };
        written = (written + produced).min(samples * 2);
        at = data + size * (channels - 1) + per_channel;
    }
    Ok(out)
}

/// A wave's bytes in an AW file: where the disc has them, and where the cooked file has them as PCM16.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct WavePlace {
    pub offset: usize,
    pub length: usize,
    pub format: u8,
    pub samples: usize,
    pub new_offset: usize,
    pub new_length: usize,
}

/// The samples a wave's bytes hold.
fn samples_in(length: usize, format: u8, sample_count: usize) -> usize {
    match format {
        WAVE_ADPCM4 => (length / 9).max(sample_count.div_ceil(16)) * 16,
        WAVE_ADPCM2 => (length / 5).max(sample_count.div_ceil(16)) * 16,
        WAVE_PCM8 => length,
        _ => length / 2,
    }
}

/// Each AW file's waves from an audio archive's wave systems, the disc's or a cooked one, laid out one after the other.
/// The same data gives the same layout, so the archive's cook and each AW file's agree.
pub fn wave_layout(archive_file: &[u8]) -> Result<BTreeMap<String, Vec<WavePlace>>> {
    let expanded;
    let bytes = if archive::is_compressed(archive_file) {
        expanded = archive::expand(archive_file)?;
        &expanded[..]
    } else {
        archive_file
    };
    let mut layout: BTreeMap<String, Vec<WavePlace>> = BTreeMap::new();
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
            let name = archive_name(bytes.get(name_at..name_at + 0x70).context("a wave archive's name")?);
            for w in 0..word(name_at + 0x70)? {
                let wave = word(name_at + 0x74 + 4 * w)?;
                let format = *bytes.get(base + wave + 1).context("a wave past the end")?;
                if wave != 0 && format <= WAVE_PCM16 {
                    let length = word(base + wave + 0xC)?;
                    layout.entry(name.clone()).or_default().push(WavePlace {
                        offset: word(base + wave + 8)?,
                        length,
                        format,
                        samples: samples_in(length, format, word(base + wave + 0x1C)?),
                        new_offset: 0,
                        new_length: 0,
                    });
                }
            }
        }
    }
    for list in layout.values_mut() {
        list.sort_unstable();
        list.dedup_by_key(|w| (w.offset, w.length, w.format));
        let mut at = 0usize;
        for wave in list.iter_mut() {
            wave.new_offset = at;
            wave.new_length = wave.samples * 2;
            at += wave.new_length;
        }
    }
    Ok(layout)
}

/// A wave archive's name in its wave system, as wave_archive_key gives it.
pub fn archive_name(field: &[u8]) -> String {
    let end = field.iter().position(|b| *b == 0).unwrap_or(field.len());
    String::from_utf8_lossy(&field[..end]).to_ascii_lowercase()
}

/// The place of a wave of a wave system (its disc offset, length and format).
pub fn place_of(layout: &BTreeMap<String, Vec<WavePlace>>, name: &str, offset: usize, length: usize, format: u8) -> Option<WavePlace> {
    layout.get(name)?.iter().find(|w| w.offset == offset && w.length == length && w.format == format).copied()
}

/// ADPCM blocks to host-order PCM16. The decoder's history runs through the wave from zero, so a loop's start holds what
/// the wave's own samples lead to.
fn decode_adpcm(data: &[u8], block_bytes: usize, samples: usize, out: &mut [u8]) {
    let (mut s1, mut s2) = (0i64, 0i64);
    for (b, block) in out[..samples * 2].chunks_exact_mut(32).enumerate() {
        let source = data.get(b * block_bytes..b * block_bytes + block_bytes);
        let header = source.map_or(0, |s| s[0]);
        let scale = (header >> 4) as u32;
        let [c1, c2] = ADPCM_PREDICTORS[(header & 15) as usize];
        for k in 0..16 {
            let delta = match source {
                None => 0,
                Some(s) if block_bytes == 5 => (((s[1 + k / 4] << ((k & 3) * 2)) as i8 >> 6) as i64) << (scale + 2),
                Some(s) => {
                    let byte = s[1 + k / 2];
                    (((if k & 1 != 0 { byte << 4 } else { byte }) as i8 >> 4) as i64) << scale
                }
            };
            let sample = (((delta << 11) + c1 * s1 + c2 * s2) >> 11).clamp(-32768, 32767);
            s2 = s1;
            s1 = sample;
            block[k * 2..k * 2 + 2].copy_from_slice(&(sample as i16).to_le_bytes());
        }
    }
}

/// An AW file with every wave PCM16 in host order, at the places of its layout (a wave's range past the file is refused).
pub fn wave_archive(bytes: &[u8], places: &[WavePlace]) -> Result<Vec<u8>> {
    let size = places.iter().map(|w| w.new_offset + w.new_length).max().unwrap_or(0);
    // Two-bit ADPCM, the most a byte can grow, is 32 bytes from 5; past that the wave systems' counts are wrong.
    if size as u64 > bytes.len() as u64 * 8 + (1 << 20) {
        bail!("wave systems that make {size} bytes from a wave archive of {} bytes", bytes.len());
    }
    let mut out = vec![0u8; size];
    for w in places {
        let data = bytes.get(w.offset..w.offset + w.length).with_context(|| format!("a wave at {:#x} past the file's end", w.offset))?;
        let target = &mut out[w.new_offset..w.new_offset + w.new_length];
        match w.format {
            WAVE_ADPCM4 => decode_adpcm(data, 9, w.samples, target),
            WAVE_ADPCM2 => decode_adpcm(data, 5, w.samples, target),
            WAVE_PCM8 => {
                for (s, d) in data.iter().zip(target.chunks_exact_mut(2)) {
                    d.copy_from_slice(&(((*s as i8) as i16) << 8).to_le_bytes());
                }
            }
            _ => {
                target.copy_from_slice(&data[..w.new_length]);
                turn_samples(target);
            }
        }
    }
    Ok(out)
}

/// A layout as the store keeps it (NebulaData/WaveLayout.txt): a cooked archive no longer says where the disc's waves
/// lie, and a mod's wave archive is laid out as the disc's. One wave a line: file, then its numbers.
pub fn layout_text(layout: &BTreeMap<String, Vec<WavePlace>>) -> String {
    let mut text = String::new();
    for (name, waves) in layout {
        for w in waves {
            text.push_str(&format!(
                "{name}\t{} {} {} {} {} {}\n",
                w.offset, w.length, w.format, w.samples, w.new_offset, w.new_length
            ));
        }
    }
    text
}

pub fn parse_layout_text(text: &str) -> Result<BTreeMap<String, Vec<WavePlace>>> {
    let mut layout: BTreeMap<String, Vec<WavePlace>> = BTreeMap::new();
    for line in text.lines().filter(|line| !line.is_empty()) {
        let (name, numbers) = line.split_once('\t').with_context(|| format!("a wave layout line {line:?}"))?;
        let n: Vec<usize> = numbers.split(' ').map(str::parse).collect::<std::result::Result<_, _>>().with_context(|| format!("a wave layout line {line:?}"))?;
        let [offset, length, format, samples, new_offset, new_length] = n[..] else {
            bail!("a wave layout line {line:?}");
        };
        layout.entry(name.to_string()).or_default().push(WavePlace {
            offset,
            length,
            format: format as u8,
            samples,
            new_offset,
            new_length,
        });
    }
    Ok(layout)
}

/// The file name a wave system uses for a path.
pub fn wave_archive_key(path: &str) -> String {
    path.rsplit('/').next().unwrap_or(path).to_ascii_lowercase()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_layout_survives_its_text() {
        let mut layout = BTreeMap::new();
        layout.insert(
            "a.aw".to_string(),
            vec![WavePlace { offset: 16, length: 90, format: 0, samples: 160, new_offset: 0, new_length: 320 }],
        );
        assert_eq!(parse_layout_text(&layout_text(&layout)).unwrap(), layout);
        assert!(parse_layout_text("a.aw\t1 2\n").is_err());
    }

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
    fn a_4_bit_stream_decodes_across_blocks() {
        // Hand-worked: scale 2, row 1 (first coefficient 1.0). Code +1 then -1, then zeros carry the history.
        let mut frame = [0u8; AFC_FRAME_BYTES];
        frame[0] = 0x11;
        frame[1] = 0x1F;
        let mut out = vec![0u8; 32];
        let mut history = (0, 0);
        decode_afc(&frame, &mut history, &mut out);
        let at = |i: usize| i16::from_le_bytes([out[i * 2], out[i * 2 + 1]]);
        assert_eq!((at(0), at(1), at(2), at(15)), (2, 0, 0, 0));

        let mut s = vec![0u8; AST_HEADER + (AST_BLOCK_HEADER + 18) * 2];
        s[..4].copy_from_slice(b"STRM");
        s[9] = FORMAT_AFC;
        s[0xC..0xE].copy_from_slice(&2u16.to_be_bytes());
        s[0x10..0x14].copy_from_slice(&32000u32.to_be_bytes());
        s[0x14..0x18].copy_from_slice(&32u32.to_be_bytes());
        s[0x1C..0x20].copy_from_slice(&32u32.to_be_bytes());
        let first = AST_HEADER;
        let second = first + AST_BLOCK_HEADER + 18;
        for block in [first, second] {
            s[block..block + 4].copy_from_slice(b"BLCK");
            s[block + 4..block + 8].copy_from_slice(&9u32.to_be_bytes());
        }
        // The left channel's first frame sets 2; the second block's row 1 and zero codes hold it: its history carried.
        s[first + AST_BLOCK_HEADER] = 0x11;
        s[first + AST_BLOCK_HEADER + 1] = 0x10;
        s[second + AST_BLOCK_HEADER] = 0x01;
        let cooked = ast(&s).unwrap();
        assert_eq!(cooked.len(), STREAM_HEADER + 32 * 2 * 2);
        let left = |i: usize| i16::from_le_bytes([cooked[STREAM_HEADER + i * 2], cooked[STREAM_HEADER + i * 2 + 1]]);
        assert_eq!(left(0), 2);
        assert_eq!(left(15), 2);
        assert_eq!(left(16), 2);
        assert_eq!(left(31), 2);
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
    fn a_header_sized_stream_is_refused_before_its_allocation() {
        let mut s = vec![0u8; AST_HEADER];
        s[..4].copy_from_slice(b"STRM");
        s[9] = FORMAT_PCM16;
        s[0xC..0xE].copy_from_slice(&6u16.to_be_bytes());
        s[0x14..0x18].copy_from_slice(&u32::MAX.to_be_bytes());
        assert!(ast(&s).unwrap_err().to_string().contains("samples in a stream"));
    }

    #[test]
    fn a_block_past_the_end_is_an_error() {
        // Two channels of 40 bytes: the file ends inside the first, within the overrun a last block is allowed.
        let mut s = vec![0u8; AST_HEADER + AST_BLOCK_HEADER + 16];
        s[..4].copy_from_slice(b"STRM");
        s[9] = FORMAT_PCM16;
        s[0xC..0xE].copy_from_slice(&2u16.to_be_bytes());
        s[AST_HEADER..AST_HEADER + 4].copy_from_slice(b"BLCK");
        s[AST_HEADER + 4..AST_HEADER + 8].copy_from_slice(&40u32.to_be_bytes());
        assert!(ast(&s).is_err());
        // The block's header cut short.
        let mut cut = vec![0u8; AST_HEADER + 8];
        cut[..AST_HEADER].copy_from_slice(&s[..AST_HEADER]);
        cut[AST_HEADER..AST_HEADER + 4].copy_from_slice(b"BLCK");
        assert!(ast(&cut).is_err());
    }

    #[test]
    fn wave_systems_cannot_ask_for_more_than_the_file_makes() {
        let place = WavePlace { offset: 0, length: 2, format: WAVE_PCM16, samples: 1 << 40, new_offset: 0, new_length: 1 << 41 };
        assert!(wave_archive(&[1, 2], &[place]).is_err());
    }

    fn place(offset: usize, length: usize, format: u8, samples: usize, new_offset: usize) -> WavePlace {
        WavePlace { offset, length, format, samples, new_offset, new_length: samples * 2 }
    }

    #[test]
    fn pcm16_waves_turn() {
        let cooked = wave_archive(&[1, 2, 3, 4, 5, 6], &[place(2, 2, WAVE_PCM16, 1, 0)]).unwrap();
        assert_eq!(cooked, vec![4, 3]);
        assert!(wave_archive(&[1, 2], &[place(2, 2, WAVE_PCM16, 1, 0)]).is_err());
    }

    #[test]
    fn pcm8_waves_widen() {
        let cooked = wave_archive(&[0x7F, 0x80], &[place(0, 2, WAVE_PCM8, 2, 0)]).unwrap();
        assert_eq!(cooked, vec![0x00, 0x7F, 0x00, 0x80]);
    }

    #[test]
    fn adpcm_blocks_decode() {
        // Predictor 0 at scale 11: a nibble of 1 is 2048, then -1 is -2048, the rest 0.
        let mut block = [0u8; 9];
        block[0] = 0xB0;
        block[1] = 0x1F;
        let cooked = wave_archive(&block, &[place(0, 9, WAVE_ADPCM4, 16, 0)]).unwrap();
        assert_eq!(i16::from_le_bytes([cooked[0], cooked[1]]), 2048);
        assert_eq!(i16::from_le_bytes([cooked[2], cooked[3]]), -2048);
        assert_eq!(cooked.len(), 32);
        assert_eq!(cooked[4..], [0u8; 28]);
    }

    #[test]
    fn a_two_bit_block_decodes() {
        // Predictor 0 at scale 9 (9 + 2 = 11 bits): 01 is 2048.
        let mut block = [0u8; 5];
        block[0] = 0x90;
        block[1] = 0b0100_0000;
        let cooked = wave_archive(&block, &[place(0, 5, WAVE_ADPCM2, 16, 0)]).unwrap();
        assert_eq!(i16::from_le_bytes([cooked[0], cooked[1]]), 2048);
        assert_eq!(cooked[2..], [0u8; 30]);
    }
}
