//! A mod's sound effect (plan 3.7 layer 1, contract board/contracts/sound.md): `<name>.sound.json` in a layer's
//! assets (the store's `Luma/<id>/<path>`) names a WAV, FLAC or Ogg Vorbis file beside it; the cook writes
//! `Luma/<id>/<name>.nsfx`, the sound in the one form the game's mixer plays a mod's effect from (48 kHz, 16-bit,
//! interleaved, its loop sample-exact in that rate), with how it plays. Loudness (EBU R 128) is measured and said in
//! the cook's fact, never changed.
//!
//! NSFX, little-endian, read in place:
//!
//! | at | what |
//! |---|---|
//! | 0 | `NSFX` |
//! | 4 | version (1) |
//! | 8 | rate (48000) |
//! | 12 | channels (1 or 2) |
//! | 16 | frames |
//! | 20 | loop start (frame), 0xFFFFFFFF none |
//! | 24 | loop end (frame, not played), 0 none |
//! | 28 | volume (f32, linear) |
//! | 32 | lowest pitch (f32), 36 highest pitch (f32): each play takes one between, evenly |
//! | 40 | near (f32, game units: full volume within), 44 far (f32: silent beyond) |
//! | 48 | priority (u8, 0 lowest), 49 bus (u8: 0 effects, 1 ui, 2 voice, 3 music), 50 flags (u8: 1 positioned), 51 0 |
//! | 52 | loudness (f32, LUFS; NaN when shorter than one 400 ms block), 56 peak (f32, dBFS) |
//! | 60 | the game sound it replaces: its name's offset from the file's start, 0 none (layer 2) |
//! | 64 | samples (i16, interleaved), then the replaced name (UTF-8, 0-ended) |

use super::flac::{self, FlacError};
use super::particle_json::{as_f32, as_int, at_key, choice, floats_of, missing, object, only, text};
use super::refuse::refuse;
use anyhow::Result;
use serde_json::Value;

pub const MAGIC: &[u8; 4] = b"NSFX";
pub const VERSION: u32 = 1;
pub const HEADER: usize = 64;
pub const RATE: u32 = 48_000;
/// An effect's longest: it is held in memory while the mod runs (a stream is 3.7 layer 3).
pub const MOST_SECONDS: f64 = 60.0;
pub const LOWEST_RATE: u32 = 8_000;
pub const HIGHEST_RATE: u32 = 384_000;
pub const BUSES: [&str; 4] = ["effects", "ui", "voice", "music"];
/// The kinds of `.sound.json`: an effect (layer 1, with `replace` layer 2), a stream (layer 3, sound_stream.rs), a voice
/// line (layer 4: an effect on the voice bus standing in for a game voice line), a sequence (layer 5, sound_sequence.rs).
pub const KINDS: [&str; 4] = ["effect", "stream", "voice", "sequence"];
const PARTS: [&str; 10] = ["kind", "file", "volume", "pitch", "distance", "priority", "bus", "loop", "positioned", "replace"];

/// Where a file a `.sound.json` makes goes: beside the source with this ending (`Luma/<id>/<path><ending>`), or at a
/// game path, in place of the game's file.
#[derive(Clone, Debug, PartialEq)]
pub enum Made {
    Own(&'static str),
    Game(String),
}

/// What `cook.sound` says of a sound.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct SoundFacts {
    /// Its kind (KINDS).
    pub kind: String,
    /// A sequence's notes, and the MIDI events it left out (`name x count`, comma between).
    pub notes: u32,
    pub dropped: String,
    pub frames: u32,
    pub channels: u32,
    /// The source file's rate.
    pub rate_in: u32,
    pub looped: bool,
    pub loudness: f32,
    pub peak: f32,
    pub replaces: String,
}

/// Decoded sound: channels, rate, interleaved samples in -1..1, the file's loop (frames).
pub struct Pcm {
    pub channels: usize,
    pub rate: u32,
    pub samples: Vec<f32>,
    pub loop_points: Option<(u64, u64)>,
}

fn le16(b: &[u8], at: usize) -> Option<u16> {
    Some(u16::from_le_bytes(b.get(at..at + 2)?.try_into().ok()?))
}
fn le32(b: &[u8], at: usize) -> Option<u32> {
    Some(u32::from_le_bytes(b.get(at..at + 4)?.try_into().ok()?))
}

/// Not a sound file the cook reads.
fn unreadable(file: &str) -> anyhow::Error {
    refuse("Cook.Sound.Unreadable", &[("uri", file.to_string())])
}

/// Damaged at byte `at`.
fn broken(file: &str, at: usize) -> anyhow::Error {
    refuse("Cook.Sound.Broken", &[("uri", file.to_string()), ("value", at.to_string())])
}

fn cut(file: &str) -> anyhow::Error {
    refuse("Cook.Sound.Cut", &[("uri", file.to_string())])
}

/// A WAV: PCM 8 (unsigned), 16, 24 or 32 bits, or float 32 or 64, plain or extensible; its `smpl` loop.
fn read_wav(b: &[u8], file: &str) -> Result<Pcm> {
    let mut at = 12;
    let mut format: Option<(u16, usize, u32, u16)> = None;
    let mut data: Option<&[u8]> = None;
    let mut loop_points = None;
    while at + 8 <= b.len() {
        let kind = &b[at..at + 4];
        let size = le32(b, at + 4).ok_or_else(|| cut(file))? as usize;
        let body = b.get(at + 8..(at + 8).checked_add(size).ok_or_else(|| cut(file))?).ok_or_else(|| cut(file))?;
        match kind {
            b"fmt " => {
                let mut tag = le16(body, 0).ok_or_else(|| broken(file, at))?;
                let channels = le16(body, 2).ok_or_else(|| broken(file, at))? as usize;
                let rate = le32(body, 4).ok_or_else(|| broken(file, at))?;
                let bits = le16(body, 14).ok_or_else(|| broken(file, at))?;
                if tag == 0xFFFE {
                    // WAVE_FORMAT_EXTENSIBLE: the sub-format GUID's first two bytes are the tag.
                    tag = le16(body, 24).ok_or_else(|| broken(file, at))?;
                }
                format = Some((tag, channels, rate, bits));
            }
            b"data" => data = Some(body),
            b"smpl" => {
                if le32(body, 28).unwrap_or(0) > 0 {
                    let (s, e) = (le32(body, 44).ok_or_else(|| broken(file, at))?, le32(body, 48).ok_or_else(|| broken(file, at))?);
                    loop_points = Some((u64::from(s), u64::from(e) + 1));
                }
            }
            _ => {}
        }
        at += 8 + size + (size & 1);
    }
    let (tag, channels, rate, bits) = format.ok_or_else(|| broken(file, 12))?;
    let data = data.ok_or_else(|| broken(file, 12))?;
    let samples: Vec<f32> = match (tag, bits) {
        (1, 8) => data.iter().map(|&v| (f32::from(v) - 128.0) / 128.0).collect(),
        (1, 16) => data.chunks_exact(2).map(|s| f32::from(i16::from_le_bytes([s[0], s[1]])) / 32768.0).collect(),
        (1, 24) => data.chunks_exact(3).map(|s| (i32::from_le_bytes([0, s[0], s[1], s[2]]) >> 8) as f32 / 8_388_608.0).collect(),
        (1, 32) => data.chunks_exact(4).map(|s| i32::from_le_bytes([s[0], s[1], s[2], s[3]]) as f32 / 2_147_483_648.0).collect(),
        (3, 32) => data.chunks_exact(4).map(|s| f32::from_le_bytes([s[0], s[1], s[2], s[3]])).collect(),
        (3, 64) => data.chunks_exact(8).map(|s| f64::from_le_bytes(s.try_into().unwrap_or([0; 8])) as f32).collect(),
        _ => return Err(refuse("Cook.Sound.WavKind", &[("uri", file.to_string()), ("kind", tag.to_string()), ("width", bits.to_string())])),
    };
    Ok(Pcm { channels, rate, samples, loop_points })
}

fn read_ogg(b: &[u8], file: &str) -> Result<Pcm> {
    // An Ogg Opus stream starts with "OpusHead" in its first page.
    if b.windows(8).take(200).any(|w| w == b"OpusHead") {
        return Err(refuse("Cook.Sound.Opus", &[("uri", file.to_string())]));
    }
    let mut reader = lewton::inside_ogg::OggStreamReader::new(std::io::Cursor::new(b)).map_err(|_| broken(file, 0))?;
    let channels = usize::from(reader.ident_hdr.audio_channels);
    let rate = reader.ident_hdr.audio_sample_rate;
    let comment = |name: &str| reader.comment_hdr.comment_list.iter().find(|(k, _)| k.eq_ignore_ascii_case(name)).and_then(|(_, v)| v.trim().parse::<u64>().ok());
    let loop_points = match (comment("LOOPSTART"), comment("LOOPEND"), comment("LOOPLENGTH")) {
        (Some(s), Some(e), _) => Some((s, e)),
        (Some(s), None, Some(n)) => Some((s, s.saturating_add(n))),
        _ => None,
    };
    let most = (MOST_SECONDS * f64::from(HIGHEST_RATE)) as usize * 2;
    let mut samples = Vec::new();
    loop {
        match reader.read_dec_packet_itl() {
            Ok(Some(packet)) => samples.extend(packet.iter().map(|&v| f32::from(v) / 32768.0)),
            Ok(None) => break,
            // A stream that ends inside a page or packet: cut off.
            Err(_) => return Err(cut(file)),
        }
        if samples.len() > most {
            return Err(too_long(samples.len() as f64 / channels.max(1) as f64 / f64::from(rate.max(1))));
        }
    }
    Ok(Pcm { channels, rate, samples, loop_points })
}

fn too_long(seconds: f64) -> anyhow::Error {
    refuse("Cook.Sound.TooLong", &[("seconds", format!("{seconds:.1}")), ("max", format!("{MOST_SECONDS:.0}"))])
}

/// A sound file by its bytes (not its name): WAV, FLAC or Ogg Vorbis.
pub fn decode(b: &[u8], file: &str) -> Result<Pcm> {
    if b.is_empty() {
        return Err(refuse("Cook.Sound.Empty", &[("uri", file.to_string())]));
    }
    if b.starts_with(b"RIFF") && b.get(8..12) == Some(b"WAVE") {
        return read_wav(b, file);
    }
    if b.starts_with(b"fLaC") {
        return match flac::decode(b) {
            Ok(f) => {
                let scale = (1u64 << (f.bits - 1)) as f32;
                Ok(Pcm { channels: f.channels, rate: f.rate, samples: f.samples.iter().map(|&v| v as f32 / scale).collect(), loop_points: f.loop_points })
            }
            Err(FlacError::Cut) => Err(cut(file)),
            Err(FlacError::Broken(at)) => Err(broken(file, at)),
            Err(FlacError::NotFlac) => Err(unreadable(file)),
        };
    }
    if b.starts_with(b"OggS") {
        return read_ogg(b, file);
    }
    Err(unreadable(file))
}

/// Windowed-sinc resampling to `RATE` (Blackman, 32 zero crossings a side, the cutoff at the lower Nyquist), from a
/// table of 4,096 phases read between its two nearest. With a loop, what is read past its end is read from its start,
/// so the loop's seam is the source's.
pub(super) fn resample_to_rate(pcm: &Pcm, loop_points: Option<(u64, u64)>) -> (Vec<f32>, Option<(u64, u64)>) {
    resample(pcm, loop_points)
}

fn resample(pcm: &Pcm, loop_points: Option<(u64, u64)>) -> (Vec<f32>, Option<(u64, u64)>) {
    let ch = pcm.channels;
    let frames = pcm.samples.len() / ch;
    if pcm.rate == RATE {
        return (pcm.samples.clone(), loop_points);
    }
    let ratio = f64::from(RATE) / f64::from(pcm.rate);
    let out_frames = ((frames as f64) * ratio).round() as usize;
    let out_loop = loop_points.map(|(s, e)| ((s as f64 * ratio).round() as u64, ((e as f64 * ratio).round() as u64).min(out_frames as u64)));
    let cutoff = ratio.min(1.0);
    const ZEROS: f64 = 32.0;
    const PHASES: usize = 4096;
    let half = (ZEROS / cutoff).ceil() as i64;
    let taps = (2 * half) as usize;
    // table[p][j]: the weight of the sample at offset j - half + 1 from the one before the output point, for the
    // output point p / PHASES past that sample.
    let mut table = vec![0f32; (PHASES + 1) * taps];
    for p in 0..=PHASES {
        let frac = p as f64 / PHASES as f64;
        for j in 0..taps {
            let off = j as i64 - half + 1;
            let x = (frac - off as f64) * cutoff;
            let w = x / ZEROS;
            if w.abs() >= 1.0 {
                continue;
            }
            let sinc = if x.abs() < 1e-12 { 1.0 } else { (std::f64::consts::PI * x).sin() / (std::f64::consts::PI * x) };
            let q = std::f64::consts::PI * (w + 1.0);
            let window = 0.42 - 0.5 * q.cos() + 0.08 * (2.0 * q).cos();
            table[p * taps + j] = (sinc * window * cutoff) as f32;
        }
    }
    let at = |i: i64, c: usize| -> f32 {
        let i = match loop_points {
            Some((s, e)) if i >= e as i64 && e > s => s as i64 + (i - e as i64) % (e - s) as i64,
            _ => i,
        };
        if i < 0 || i >= frames as i64 { 0.0 } else { pcm.samples[i as usize * ch + c] }
    };
    let mut out = vec![0f32; out_frames * ch];
    for n in 0..out_frames {
        let t = n as f64 / ratio;
        let centre = t.floor();
        let place = (t - centre) * PHASES as f64;
        let p = (place.floor() as usize).min(PHASES - 1);
        let between = (place - p as f64) as f32;
        let (a, b) = (&table[p * taps..(p + 1) * taps], &table[(p + 1) * taps..(p + 2) * taps]);
        let first = centre as i64 - half + 1;
        for c in 0..ch {
            let mut sum = 0f32;
            for j in 0..taps {
                let w = a[j] + (b[j] - a[j]) * between;
                if w != 0.0 {
                    sum += at(first + j as i64, c) * w;
                }
            }
            out[n * ch + c] = sum;
        }
    }
    (out, out_loop)
}

/// A biquad over one channel.
fn biquad(x: &[f64], b: [f64; 3], a: [f64; 2]) -> Vec<f64> {
    let (mut x1, mut x2, mut y1, mut y2) = (0.0, 0.0, 0.0, 0.0);
    x.iter()
        .map(|&x0| {
            let y0 = b[0] * x0 + b[1] * x1 + b[2] * x2 - a[0] * y1 - a[1] * y2;
            (x2, x1, y2, y1) = (x1, x0, y1, y0);
            y0
        })
        .collect()
}

/// Integrated loudness (ITU-R BS.1770-4, EBU R 128) at 48 kHz: K-weighting, 400 ms blocks overlapping 75 %, the
/// absolute gate at -70 LUFS and the relative one 10 LU under. NaN when the sound is shorter than one block or
/// silent.
pub fn loudness(samples: &[f32], channels: usize) -> f32 {
    let frames = samples.len() / channels;
    let block = (RATE as usize) * 4 / 10;
    if frames < block {
        return f32::NAN;
    }
    // The K filter's two stages at 48 kHz (BS.1770's coefficients).
    let shelf = ([1.535_124_859_586_97, -2.691_696_189_406_38, 1.198_392_810_852_85], [-1.690_659_293_182_41, 0.732_480_774_215_85]);
    let high = ([1.0, -2.0, 1.0], [-1.990_047_454_833_98, 0.990_072_250_366_21]);
    let weighted: Vec<Vec<f64>> = (0..channels)
        .map(|c| {
            let x: Vec<f64> = (0..frames).map(|i| f64::from(samples[i * channels + c])).collect();
            biquad(&biquad(&x, shelf.0, shelf.1), high.0, high.1)
        })
        .collect();
    let step = block / 4;
    let blocks: Vec<f64> = (0..=(frames - block) / step)
        .map(|j| weighted.iter().map(|ch| ch[j * step..j * step + block].iter().map(|v| v * v).sum::<f64>() / block as f64).sum::<f64>())
        .collect();
    let lufs = |z: f64| -0.691 + 10.0 * z.log10();
    let gated: Vec<f64> = blocks.iter().copied().filter(|&z| z > 0.0 && lufs(z) > -70.0).collect();
    if gated.is_empty() {
        return f32::NAN;
    }
    let relative = lufs(gated.iter().sum::<f64>() / gated.len() as f64) - 10.0;
    let kept: Vec<f64> = gated.into_iter().filter(|&z| lufs(z) > relative).collect();
    if kept.is_empty() {
        return f32::NAN;
    }
    lufs(kept.iter().sum::<f64>() / kept.len() as f64) as f32
}

fn number_pair(v: &Value, at: &str) -> Result<[f32; 2]> {
    if v.is_number() {
        let x = as_f32(v, at)?;
        return Ok([x, x]);
    }
    let f = floats_of(v, 2, at)?;
    Ok([f[0], f[1]])
}

pub(super) fn in_range(at: &str, v: f32, min: f32, max: f32) -> Result<f32> {
    if !(min..=max).contains(&v) {
        return Err(super::particle_json::range(at, v, min, max));
    }
    Ok(v)
}

/// What the cook needs from around the source: the bytes of a file named relative to it (None: the mod has none),
/// and whether a game sound has a name (None: the game's names are not known here).
pub struct Around<'a> {
    pub file: &'a dyn Fn(&str) -> Option<Vec<u8>>,
    pub game_sound: &'a dyn Fn(&str) -> Option<bool>,
    /// A game stream by name (`AudioRes/Stream/<name>.ast`).
    pub game_stream: &'a dyn Fn(&str) -> Option<bool>,
    /// A game sequence by its path in the sequence archive (`AudioRes/Seqs/JaiSeq.arc/<path>`).
    pub game_sequence: &'a dyn Fn(&str) -> Option<bool>,
}

/// The file a source names, relative to it and inside its folder.
pub(super) fn file_of(map: &serde_json::Map<String, Value>) -> Result<&str> {
    let file = text(map.get("file").ok_or_else(|| missing("/", "file"))?, "/file")?;
    if file.is_empty() || file.contains("..") || file.contains(':') || file.contains('\\') || file.starts_with('/') {
        return Err(refuse("Cook.Effect.FilePath", &[("where", "/file".into()), ("uri", file.to_string())]));
    }
    Ok(file)
}

/// `<name>.sound.json`'s bytes, of any kind, as the files it makes.
/// The files a `.sound.json` makes, each with its place, and its fact.
pub type Cooked = (Vec<(Made, Vec<u8>)>, SoundFacts);

pub fn cook_sound(json: &[u8], around: &Around) -> Result<Cooked> {
    let root: Value = serde_json::from_slice(json).map_err(|e| refuse("Cook.Model.Json", &[("line", e.line().to_string()), ("column", e.column().to_string())]))?;
    let map = object(&root, "/")?;
    match choice(&KINDS, map.get("kind").ok_or_else(|| missing("/", "kind"))?, "/kind", 0)? {
        1 => super::sound_stream::cook(map, around),
        3 => super::sound_sequence::cook(map, around),
        kind => {
            let (nsfx, facts) = cook_effect(map, around, kind == 2)?;
            Ok((vec![(Made::Own(".nsfx"), nsfx)], facts))
        }
    }
}

/// `<name>.sound.json`'s bytes as NSFX: an effect or a voice line.
pub fn cook(json: &[u8], around: &Around) -> Result<(Vec<u8>, SoundFacts)> {
    let (mut made, facts) = cook_sound(json, around)?;
    match made.pop() {
        Some((Made::Own(".nsfx"), nsfx)) if made.is_empty() => Ok((nsfx, facts)),
        _ => Err(refuse("Cook.Effect.Choice", &[("where", "/kind".into()), ("value", facts.kind.clone()), ("expected", "effect, voice".into())])),
    }
}

/// An effect (layer 1, `replace` layer 2) or a voice line (layer 4: `replace` names the game's voice line it stands in
/// for, and it plays on the voice bus unless it says).
fn cook_effect(map: &serde_json::Map<String, Value>, around: &Around, voice: bool) -> Result<(Vec<u8>, SoundFacts)> {
    only(map, "/", &PARTS)?;
    let file = file_of(map)?;
    if voice && !map.contains_key("replace") {
        return Err(missing("/", "replace"));
    }
    let volume = match map.get("volume") {
        Some(v) => in_range("/volume", as_f32(v, "/volume")?, 0.0, 4.0)?,
        None => 1.0,
    };
    let pitch = match map.get("pitch") {
        Some(v) => {
            let p = number_pair(v, "/pitch")?;
            for (i, x) in p.iter().enumerate() {
                in_range(&format!("/pitch/{i}"), *x, 0.25, 4.0)?;
            }
            if p[0] > p[1] {
                return Err(refuse("Cook.Sound.Order", &[("where", "/pitch".into())]));
            }
            p
        }
        None => [1.0, 1.0],
    };
    let distance = match map.get("distance") {
        Some(v) => {
            let d = floats_of(v, 2, "/distance")?;
            in_range("/distance/0", d[0], 0.0, 1.0e6)?;
            in_range("/distance/1", d[1], 0.0, 1.0e6)?;
            if d[0] >= d[1] {
                return Err(refuse("Cook.Sound.Order", &[("where", "/distance".into())]));
            }
            [d[0], d[1]]
        }
        None => [300.0, 3000.0],
    };
    let priority = match map.get("priority") {
        Some(v) => as_int(v, "/priority", 0, 255)? as u8,
        None => 128,
    };
    let bus = match map.get("bus") {
        Some(v) => choice(&BUSES, v, "/bus", 3)? as u8,
        None if voice => 2,
        None => 0,
    };
    let positioned = match map.get("positioned") {
        Some(v) => v.as_bool().ok_or_else(|| super::particle_json::not_kind("/positioned", "Cook.Effect.Is.Bool"))?,
        None => true,
    };
    let replaces = match map.get("replace") {
        Some(v) => {
            let name = text(v, "/replace")?;
            if (around.game_sound)(name) == Some(false) {
                return Err(refuse("Cook.Sound.NoGameSound", &[("where", "/replace".into()), ("name", name.to_string())]));
            }
            name.to_string()
        }
        None => String::new(),
    };
    let bytes = (around.file)(file).ok_or_else(|| refuse("Cook.Effect.FileMissing", &[("where", "/file".into()), ("uri", file.to_string())]))?;
    let pcm = decode(&bytes, file)?;
    if pcm.channels == 0 || pcm.channels > 2 {
        return Err(refuse("Cook.Sound.Channels", &[("uri", file.to_string()), ("count", pcm.channels.to_string()), ("max", "2".into())]));
    }
    if pcm.rate < LOWEST_RATE || pcm.rate > HIGHEST_RATE {
        return Err(refuse("Cook.Sound.Rate", &[("uri", file.to_string()), ("value", pcm.rate.to_string()), ("min", LOWEST_RATE.to_string()), ("max", HIGHEST_RATE.to_string())]));
    }
    let frames = (pcm.samples.len() / pcm.channels) as u64;
    if frames == 0 {
        return Err(refuse("Cook.Sound.Empty", &[("uri", file.to_string())]));
    }
    let seconds = frames as f64 / f64::from(pcm.rate);
    if seconds > MOST_SECONDS {
        return Err(too_long(seconds));
    }
    if pcm.samples.iter().any(|s| !s.is_finite()) {
        return Err(refuse("Cook.Sound.NotANumber", &[("uri", file.to_string())]));
    }
    // The loop: the file's unless the source says; `true` is the file's or the whole sound; frames of the file.
    let loop_points = match map.get("loop") {
        None => pcm.loop_points,
        Some(Value::Bool(false)) => None,
        Some(Value::Bool(true)) => Some(pcm.loop_points.unwrap_or((0, frames))),
        Some(v) => {
            let l = object(v, "/loop")?;
            only(l, "/loop", &["start", "end"])?;
            let start = as_int(l.get("start").ok_or_else(|| missing("/loop", "start"))?, &at_key("/loop", "start"), 0, i64::MAX)? as u64;
            let end = match l.get("end") {
                Some(e) => as_int(e, &at_key("/loop", "end"), 0, i64::MAX)? as u64,
                None => frames,
            };
            Some((start, end))
        }
    };
    if let Some((s, e)) = loop_points {
        if s >= e || e > frames {
            return Err(refuse("Cook.Sound.LoopOutside", &[("uri", file.to_string()), ("value", format!("{s}-{e}")), ("count", frames.to_string())]));
        }
    }
    let (samples, out_loop) = resample(&pcm, loop_points);
    let out_frames = samples.len() / pcm.channels;
    let peak = samples.iter().fold(0f32, |m, s| m.max(s.abs()));
    let facts = SoundFacts {
        kind: if voice { "voice" } else { "effect" }.to_string(),
        notes: 0,
        dropped: String::new(),
        frames: out_frames as u32,
        channels: pcm.channels as u32,
        rate_in: pcm.rate,
        looped: out_loop.is_some(),
        loudness: loudness(&samples, pcm.channels),
        peak: if peak > 0.0 { 20.0 * peak.log10() } else { f32::NEG_INFINITY },
        replaces: replaces.clone(),
    };

    let mut out = Vec::with_capacity(HEADER + samples.len() * 2 + replaces.len() + 1);
    let mut put = |v: u32| out.extend_from_slice(&v.to_le_bytes());
    put(u32::from_le_bytes(*MAGIC));
    put(VERSION);
    put(RATE);
    put(pcm.channels as u32);
    put(out_frames as u32);
    let (ls, le) = out_loop.map_or((u32::MAX, 0), |(s, e)| (s as u32, e as u32));
    put(ls);
    put(le);
    for f in [volume, pitch[0], pitch[1], distance[0], distance[1]] {
        put(f.to_bits());
    }
    out.extend_from_slice(&[priority, bus, u8::from(positioned), 0]);
    for f in [facts.loudness, facts.peak] {
        out.extend_from_slice(&f.to_le_bytes());
    }
    let name_at = if replaces.is_empty() { 0 } else { (HEADER + samples.len() * 2) as u32 };
    out.extend_from_slice(&name_at.to_le_bytes());
    debug_assert_eq!(out.len(), HEADER);
    for s in &samples {
        // TPDF dither of ±1 LSB would change the bytes run to run; rounding is exact and repeatable.
        let v = (s * 32768.0).round().clamp(-32768.0, 32767.0) as i16;
        out.extend_from_slice(&v.to_le_bytes());
    }
    if !replaces.is_empty() {
        out.extend_from_slice(replaces.as_bytes());
        out.push(0);
    }
    Ok((out, facts))
}

/// The sound's store path and its name for `audio.play`: `Luma/<id>/<path>.sound.json` makes
/// `Luma/<id>/<path>.nsfx`, played by `<path>`.
pub fn cooked_path(path: &str) -> Option<String> {
    let lower = path.to_ascii_lowercase();
    lower.ends_with(".sound.json").then(|| format!("{}.nsfx", &path[..path.len() - ".sound.json".len()]))
}

/// A sound file in a mod's assets (`Luma/<id>/...`): the source a `.sound.json` names, not a game file. WAV, FLAC and
/// Ogg for every kind, BRSTM, BCSTM and BFSTM for a stream, MIDI for a sequence.
pub fn is_asset_source(path: &str) -> bool {
    let lower = path.trim_start_matches('/').to_ascii_lowercase();
    lower.starts_with("luma/") && [".wav", ".flac", ".ogg", ".brstm", ".bcstm", ".bfstm", ".mid", ".midi"].iter().any(|e| lower.ends_with(e))
}

#[cfg(test)]
#[path = "sound_effect_tests.rs"]
mod tests;
