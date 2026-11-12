//! A mod's movie (plan 3.10, contract board/contracts/movies.md): `<name>.movie.json` in a layer's assets names an AV1
//! video beside it (WebM or IVF) and, if it has sound, a WAV, FLAC or Ogg Vorbis file. The cook writes
//! `Luma/<id>/<name>.nmov`, the game's cooked movie (thp.rs's NMOV) with codec 1 (AV1, J's run time decodes it with
//! dav1d): each frame's video one temporal unit, the first a key frame carrying the sequence header; the sound as the
//! game's movie records (stereo PCM16 at 32 kHz, a frame's share with each frame). With `replace`, the same file takes
//! a game movie's place (`MovieData/<name>.thp`). Subtitles go beside it as `<name>.subs` (a table: text key, from,
//! to, in seconds).
//!
//! Nothing is transcoded: the cook carries no video encoder. VP9, H.264 and the rest are refused by name
//! (Cook.Movie.Codec) with what to give instead; a WebM's own sound track (Opus or Vorbis) is not read (a Note).
//!
//! NMOV's header as the game reads it (little-endian): 0 `NMOV`, 4 frames, 8 frame rate (f32), 12 largest frame,
//! 16 first frame's size, 20 width, 24 height, 28 audio tracks, 32 shape (f32, width / height), 0x24 codec (1 AV1),
//! 0x28 colour (0 BT.601 full range, 1 BT.709 limited), 0x2C flags (1 loops, 2 may be skipped), zeros to 0x40.

use super::particle_json::{as_f32, at_key, missing, object, only, text};
use super::refuse::refuse;
use anyhow::Result;
use serde_json::Value;

pub const MOST_WIDTH: u32 = 1920;
pub const MOST_PIXELS: u64 = 1920 * 1080;
pub const MOST_RATE: f64 = 60.0;
/// A movie's longest: an hour.
pub const MOST_SECONDS: f64 = 3600.0;
const AUDIO_RATE: u32 = 32_000;
const HEADER: usize = 0x40;
const FRAME_HEADER: usize = 0x10;
const PARTS: [&str; 6] = ["file", "audio", "replace", "loop", "skip", "subtitles"];

/// What `cook.movie` says of a movie.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct MovieFacts {
    pub frames: u32,
    pub width: u32,
    pub height: u32,
    pub rate: f32,
    pub seconds: f32,
    /// The sound file's seconds, 0 without one.
    pub audio_seconds: f32,
    pub colour: u32,
    pub subtitles: u32,
    pub replaces: String,
}

/// The video as read: its frames (temporal units, in order), its rate, its sequence header OBU, and the container's
/// size of the picture.
struct Video {
    frames: Vec<Vec<u8>>,
    rate: f64,
    /// From the container's codec data (WebM's av1C), when it has one.
    sequence: Option<Vec<u8>>,
    size: Option<(u32, u32)>,
}

fn bad(file: &str, detail: impl Into<String>) -> anyhow::Error {
    refuse("Cook.Movie.File", &[("uri", file.to_string()), ("detail", detail.into())])
}

// ---- IVF

fn read_ivf(b: &[u8], file: &str) -> Result<Video> {
    let le16 = |at: usize| b.get(at..at + 2).map(|s| u16::from_le_bytes([s[0], s[1]]));
    let le32 = |at: usize| b.get(at..at + 4).map(|s| u32::from_le_bytes(s.try_into().unwrap()));
    let head = le16(6).ok_or_else(|| bad(file, "a cut-off IVF header"))? as usize;
    let fourcc = b.get(8..12).ok_or_else(|| bad(file, "a cut-off IVF header"))?;
    if fourcc != b"AV01" {
        return Err(codec(file, &String::from_utf8_lossy(fourcc)));
    }
    let (w, h) = (le16(12).unwrap_or(0) as u32, le16(14).unwrap_or(0) as u32);
    let (rate, scale) = (le32(16).unwrap_or(0), le32(20).unwrap_or(0));
    let mut at = head.max(32);
    let mut frames = Vec::new();
    let mut stamps = Vec::new();
    while at + 12 <= b.len() {
        let size = le32(at).unwrap() as usize;
        let pts = u64::from_le_bytes(b[at + 4..at + 12].try_into().unwrap());
        let data = b.get(at + 12..at + 12 + size).ok_or_else(|| refuse("Cook.Sound.Cut", &[("uri", file.to_string())]))?;
        frames.push(data.to_vec());
        stamps.push(pts);
        at += 12 + size;
    }
    if at != b.len() {
        return Err(refuse("Cook.Sound.Cut", &[("uri", file.to_string())]));
    }
    // The time base is scale / rate seconds; a frame lasts the stamps' step (1 when they count frames).
    let step = if stamps.len() > 1 { stamps[1].saturating_sub(stamps[0]).max(1) } else { 1 };
    let fps = if scale == 0 { 0.0 } else { f64::from(rate) / f64::from(scale) / step as f64 };
    Ok(Video { frames, rate: fps, sequence: None, size: Some((w, h)) })
}

// ---- WebM (Matroska)

struct Ebml<'a> {
    b: &'a [u8],
    p: usize,
}

impl Ebml<'_> {
    /// An element's ID, its marker bits kept.
    fn id(&mut self) -> Option<u32> {
        let first = *self.b.get(self.p)?;
        let len = first.leading_zeros() as usize + 1;
        if len > 4 {
            return None;
        }
        let bytes = self.b.get(self.p..self.p + len)?;
        self.p += len;
        Some(bytes.iter().fold(0u32, |v, x| v << 8 | u32::from(*x)))
    }
    /// A size: None for "unknown" (all ones).
    fn size(&mut self) -> Option<Option<u64>> {
        let first = *self.b.get(self.p)?;
        let len = first.leading_zeros() as usize + 1;
        if len > 8 {
            return None;
        }
        let bytes = self.b.get(self.p..self.p + len)?;
        self.p += len;
        let mut v = u64::from(first) & ((1u64 << (8 - len)) - 1);
        for x in &bytes[1..] {
            v = v << 8 | u64::from(*x);
        }
        let all = (1u64 << (7 * len)) - 1;
        Some(if v == all { None } else { Some(v) })
    }
}

fn uint(b: &[u8]) -> u64 {
    b.iter().take(8).fold(0u64, |v, x| v << 8 | u64::from(*x))
}

const SEGMENT: u32 = 0x1853_8067;
const CLUSTER: u32 = 0x1F43_B675;
const TRACKS: u32 = 0x1654_AE6B;
const TRACK_ENTRY: u32 = 0xAE;
const VIDEO: u32 = 0xE0;
const BLOCK_GROUP: u32 = 0xA0;
const INFO: u32 = 0x1549_A966;
/// The elements stepped into: their children are read in turn.
const MASTERS: [u32; 7] = [SEGMENT, CLUSTER, TRACKS, TRACK_ENTRY, VIDEO, BLOCK_GROUP, INFO];

#[derive(Default)]
struct Track {
    number: u64,
    kind: u64,
    codec: String,
    duration: u64,
    private: Vec<u8>,
    width: u32,
    height: u32,
}

fn read_webm(b: &[u8], file: &str) -> Result<(Video, Vec<String>)> {
    let mut r = Ebml { b, p: 0 };
    let mut tracks: Vec<Track> = Vec::new();
    let mut scale = 1_000_000u64;
    let mut cluster_time = 0u64;
    // Blocks before the tracks are known: (track, time, key, data).
    let mut blocks: Vec<(u64, u64, bool, &[u8])> = Vec::new();
    let mut in_entry = false;
    while r.p < b.len() {
        let id = r.id().ok_or_else(|| bad(file, format!("a damaged element at byte {}", r.p)))?;
        let size = r.size().ok_or_else(|| bad(file, format!("a damaged size at byte {}", r.p)))?;
        if MASTERS.contains(&id) {
            if id == TRACK_ENTRY {
                tracks.push(Track::default());
                in_entry = true;
            } else if id == CLUSTER || id == TRACKS {
                in_entry = false;
            }
            continue; // its children follow
        }
        let size = size.ok_or_else(|| bad(file, format!("an element of unknown size at byte {}", r.p)))? as usize;
        let body = b.get(r.p..r.p.checked_add(size).unwrap_or(usize::MAX)).ok_or_else(|| refuse("Cook.Sound.Cut", &[("uri", file.to_string())]))?;
        r.p += size;
        let track = if in_entry { tracks.last_mut() } else { None };
        match (id, track) {
            (0xD7, Some(t)) => t.number = uint(body),
            (0x83, Some(t)) => t.kind = uint(body),
            (0x86, Some(t)) => t.codec = String::from_utf8_lossy(body).trim_end_matches('\0').to_string(),
            (0x23E383, Some(t)) => t.duration = uint(body),
            (0x63A2, Some(t)) => t.private = body.to_vec(),
            (0xB0, Some(t)) => t.width = uint(body) as u32,
            (0xBA, Some(t)) => t.height = uint(body) as u32,
            (0x2AD7B1, _) => scale = uint(body).max(1),
            (0xE7, _) => cluster_time = uint(body),
            (0xA3 | 0xA1, _) => {
                let mut k = Ebml { b: body, p: 0 };
                let number = k.size().flatten().ok_or_else(|| bad(file, "a block with no track"))?;
                let rest = body.get(k.p..).unwrap_or(&[]);
                if rest.len() < 3 {
                    return Err(bad(file, "a cut-off block"));
                }
                let offset = i16::from_be_bytes([rest[0], rest[1]]);
                let flags = rest[2];
                if flags & 0x06 != 0 {
                    blocks.push((number, 0, false, &[]));
                    // Laced video is not AV1's mapping; kept to be refused if it is the video's.
                    continue;
                }
                let time = (cluster_time as i64 + i64::from(offset)).max(0) as u64;
                blocks.push((number, time, (id == 0xA3 && flags & 0x80 != 0) || id == 0xA1, &rest[3..]));
            }
            _ => {}
        }
    }
    let video = tracks.iter().find(|t| t.kind == 1).ok_or_else(|| bad(file, "no video track"))?;
    if video.codec != "V_AV1" {
        return Err(codec(file, &video.codec));
    }
    let mut notes = Vec::new();
    for t in tracks.iter().filter(|t| t.kind == 2) {
        notes.push(t.codec.clone());
    }
    let mine: Vec<&(u64, u64, bool, &[u8])> = blocks.iter().filter(|(n, _, _, _)| *n == video.number).collect();
    if mine.iter().any(|(_, _, _, d)| d.is_empty()) {
        return Err(bad(file, "laced video blocks (AV1 in WebM is one frame a block)"));
    }
    let rate = if video.duration > 0 {
        1.0e9 / video.duration as f64
    } else if mine.len() > 1 {
        let span = (mine[mine.len() - 1].1 - mine[0].1) as f64 * scale as f64 / 1.0e9;
        if span > 0.0 { (mine.len() - 1) as f64 / span } else { 0.0 }
    } else {
        0.0
    };
    if let Some((_, _, key, _)) = mine.first() {
        if !key {
            return Err(refuse("Cook.Movie.FirstKey", &[("uri", file.to_string())]));
        }
    }
    // av1C: 4 bytes, then the configuration OBUs (the sequence header).
    let sequence = video.private.get(4..).filter(|s| !s.is_empty()).map(<[u8]>::to_vec);
    let size = (video.width > 0 && video.height > 0).then_some((video.width, video.height));
    Ok((Video { frames: mine.iter().map(|(_, _, _, d)| d.to_vec()).collect(), rate, sequence, size }, notes))
}

fn codec(file: &str, found: &str) -> anyhow::Error {
    refuse("Cook.Movie.Codec", &[("uri", file.to_string()), ("found", found.to_string())])
}

// ---- AV1

/// One OBU of a temporal unit: its type and its bytes (header included).
fn obus<'a>(tu: &'a [u8], file: &str, frame: usize) -> Result<Vec<(u8, &'a [u8], &'a [u8])>> {
    let mut out = Vec::new();
    let mut p = 0;
    while p < tu.len() {
        let start = p;
        let h = tu[p];
        if h & 0x80 != 0 {
            return Err(bad(file, format!("frame {frame}: an OBU with its forbidden bit set")));
        }
        let kind = (h >> 3) & 15;
        let extension = h & 4 != 0;
        let has_size = h & 2 != 0;
        p += 1 + usize::from(extension);
        let size = if has_size {
            let mut v = 0u64;
            let mut i = 0;
            loop {
                let byte = *tu.get(p).ok_or_else(|| bad(file, format!("frame {frame}: a cut-off OBU size")))?;
                p += 1;
                v |= u64::from(byte & 0x7F) << (7 * i);
                i += 1;
                if byte & 0x80 == 0 {
                    break;
                }
                if i == 8 {
                    return Err(bad(file, format!("frame {frame}: an OBU size of more than 8 bytes")));
                }
            }
            usize::try_from(v).unwrap_or(usize::MAX)
        } else {
            tu.len() - p.min(tu.len())
        };
        let end = p.checked_add(size).filter(|e| *e <= tu.len()).ok_or_else(|| bad(file, format!("frame {frame}: an OBU past the frame's end")))?;
        out.push((kind, &tu[start..end], &tu[p..end]));
        p = end;
    }
    Ok(out)
}

struct Bits<'a> {
    b: &'a [u8],
    p: usize,
}

impl Bits<'_> {
    fn f(&mut self, n: u32) -> Option<u64> {
        let mut v = 0u64;
        for _ in 0..n {
            let byte = *self.b.get(self.p / 8)?;
            v = v << 1 | u64::from(byte >> (7 - self.p % 8) & 1);
            self.p += 1;
        }
        Some(v)
    }
    fn uvlc(&mut self) -> Option<u64> {
        let mut zeros = 0;
        while self.f(1)? == 0 {
            zeros += 1;
            if zeros >= 32 {
                return Some(u64::from(u32::MAX));
            }
        }
        Some(self.f(zeros)? + (1u64 << zeros) - 1)
    }
}

/// What the game's decoder needs of a sequence header.
#[derive(Debug, PartialEq)]
struct Sequence {
    profile: u64,
    reduced: bool,
    width: u32,
    height: u32,
    high_bitdepth: bool,
    mono: bool,
    matrix: u64,
    full_range: bool,
}

fn sequence_header(b: &[u8]) -> Option<Sequence> {
    let mut r = Bits { b, p: 0 };
    let profile = r.f(3)?;
    let _still = r.f(1)?;
    let reduced = r.f(1)? == 1;
    let mut decoder_model = false;
    let mut buffer_delay_bits = 0;
    if !reduced {
        if r.f(1)? == 1 {
            // timing_info
            r.f(32)?;
            r.f(32)?;
            if r.f(1)? == 1 {
                r.uvlc()?;
            }
            decoder_model = r.f(1)? == 1;
            if decoder_model {
                buffer_delay_bits = r.f(5)? as u32 + 1;
                r.f(32)?;
                r.f(5)?;
                r.f(5)?;
            }
        }
        let display_delay = r.f(1)? == 1;
        let points = r.f(5)? + 1;
        for _ in 0..points {
            r.f(12)?;
            if r.f(5)? > 7 {
                r.f(1)?;
            }
            if decoder_model && r.f(1)? == 1 {
                r.f(buffer_delay_bits)?;
                r.f(buffer_delay_bits)?;
                r.f(1)?;
            }
            if display_delay && r.f(1)? == 1 {
                r.f(4)?;
            }
        }
    } else {
        r.f(5)?;
    }
    let wbits = r.f(4)? as u32 + 1;
    let hbits = r.f(4)? as u32 + 1;
    let width = r.f(wbits)? as u32 + 1;
    let height = r.f(hbits)? as u32 + 1;
    if !reduced && r.f(1)? == 1 {
        r.f(4)?;
        r.f(3)?;
    }
    r.f(3)?; // 128x128, filter intra, intra edge
    if !reduced {
        r.f(4)?; // interintra, masked, warped, dual filter
        let order_hint = r.f(1)? == 1;
        if order_hint {
            r.f(2)?;
        }
        let force_screen = if r.f(1)? == 1 { 2 } else { r.f(1)? };
        if force_screen > 0 && r.f(1)? == 0 {
            r.f(1)?;
        }
        if order_hint {
            r.f(3)?;
        }
    }
    r.f(3)?; // superres, cdef, restoration
    let high_bitdepth = r.f(1)? == 1;
    if profile == 2 && high_bitdepth {
        r.f(1)?;
    }
    let mono = if profile == 1 { false } else { r.f(1)? == 1 };
    let (mut primaries, mut transfer, mut matrix) = (2, 2, 2);
    if r.f(1)? == 1 {
        primaries = r.f(8)?;
        transfer = r.f(8)?;
        matrix = r.f(8)?;
    }
    let full_range = if primaries == 1 && transfer == 13 && matrix == 0 { true } else { r.f(1)? == 1 };
    Some(Sequence { profile, reduced, width, height, high_bitdepth, mono, matrix, full_range })
}

/// Whether a frame (or frame header) OBU starts a key frame that is shown, read as far as its type.
fn starts_key(payload: &[u8], reduced: bool) -> bool {
    if reduced {
        return true;
    }
    let mut r = Bits { b: payload, p: 0 };
    matches!((r.f(1), r.f(2)), (Some(0), Some(0)))
}

const OBU_SEQUENCE: u8 = 1;
const OBU_TEMPORAL: u8 = 2;
const OBU_FRAME_HEADER: u8 = 3;
const OBU_FRAME: u8 = 6;

/// The video's frames as the game decodes them (temporal delimiters left out, the sequence header put first in frame 0
/// when the container kept it apart), its sequence header, and its colour.
fn check_video(v: &mut Video, file: &str) -> Result<(Sequence, u32)> {
    if v.frames.is_empty() {
        return Err(refuse("Cook.Sound.Empty", &[("uri", file.to_string())]));
    }
    let mut sequence: Option<(Sequence, Vec<u8>)> = None;
    if let Some(private) = &v.sequence {
        for (kind, whole, payload) in obus(private, file, 0)? {
            if kind == OBU_SEQUENCE {
                sequence = sequence_header(payload).map(|s| (s, whole.to_vec()));
            }
        }
    }
    for i in 0..v.frames.len() {
        let parts = obus(&v.frames[i], file, i)?;
        let mut kept = Vec::with_capacity(v.frames[i].len());
        let mut has_sequence = false;
        let mut first_frame: Option<&[u8]> = None;
        for (kind, whole, payload) in &parts {
            if *kind == OBU_TEMPORAL {
                continue;
            }
            if *kind == OBU_SEQUENCE {
                let s = sequence_header(payload).ok_or_else(|| bad(file, format!("frame {i}: a cut-off sequence header")))?;
                if let Some((known, _)) = &sequence {
                    if *known != s {
                        return Err(bad(file, format!("frame {i}: a second sequence header unlike the first")));
                    }
                }
                sequence = Some((s, whole.to_vec()));
                has_sequence = true;
            }
            if first_frame.is_none() && (*kind == OBU_FRAME || *kind == OBU_FRAME_HEADER) {
                first_frame = Some(payload);
            }
            kept.extend_from_slice(whole);
        }
        if i == 0 {
            let (s, whole) = sequence.as_ref().ok_or_else(|| bad(file, "no sequence header"))?;
            let frame = first_frame.ok_or_else(|| bad(file, "frame 0 has no frame"))?;
            if !starts_key(frame, s.reduced) {
                return Err(refuse("Cook.Movie.FirstKey", &[("uri", file.to_string())]));
            }
            if !has_sequence {
                let mut with = whole.clone();
                with.extend_from_slice(&kept);
                kept = with;
            }
        } else if first_frame.is_none() {
            return Err(bad(file, format!("frame {i} has no frame")));
        }
        v.frames[i] = kept;
    }
    let (s, _) = sequence.ok_or_else(|| bad(file, "no sequence header"))?;
    if s.profile != 0 || s.high_bitdepth || s.mono {
        let what = format!("profile {}, {} bits{}", s.profile, if s.high_bitdepth { "10 or 12" } else { "8" }, if s.mono { ", grey" } else { "" });
        return Err(refuse("Cook.Movie.Form", &[("uri", file.to_string()), ("found", what)]));
    }
    // 1: BT.709 limited (or unspecified, limited); 0: BT.601 full (or unspecified, full).
    let colour = match (s.matrix, s.full_range) {
        (1 | 2, false) => 1,
        (5 | 6 | 2, true) => 0,
        (m, full) => {
            let what = format!("matrix {m}, {} range", if full { "full" } else { "limited" });
            return Err(refuse("Cook.Movie.Colour", &[("uri", file.to_string()), ("found", what)]));
        }
    };
    Ok((s, colour))
}

// ---- sound

/// The sound as the game's movie records: stereo, 32 kHz, PCM16, interleaved.
fn sound(bytes: &[u8], file: &str) -> Result<Vec<i16>> {
    let pcm = super::sound_effect::decode(bytes, file)?;
    if pcm.channels == 0 || pcm.channels > 2 {
        return Err(refuse("Cook.Sound.Channels", &[("uri", file.to_string()), ("count", pcm.channels.to_string()), ("max", "2".into())]));
    }
    if pcm.rate < super::sound_effect::LOWEST_RATE || pcm.rate > super::sound_effect::HIGHEST_RATE {
        let args = [("uri", file.to_string()), ("value", pcm.rate.to_string()), ("min", super::sound_effect::LOWEST_RATE.to_string()), ("max", super::sound_effect::HIGHEST_RATE.to_string())];
        return Err(refuse("Cook.Sound.Rate", &args));
    }
    let seconds = pcm.samples.len() as f64 / pcm.channels as f64 / f64::from(pcm.rate);
    if seconds > MOST_SECONDS {
        return Err(refuse("Cook.Movie.TooLong", &[("seconds", format!("{seconds:.1}")), ("max", format!("{MOST_SECONDS:.0}"))]));
    }
    if pcm.samples.iter().any(|s| !s.is_finite()) {
        return Err(refuse("Cook.Sound.NotANumber", &[("uri", file.to_string())]));
    }
    let (samples, _) = super::sound_effect::resample_at(&pcm, None, AUDIO_RATE);
    let round = |s: f32| (s * 32768.0).round().clamp(-32768.0, 32767.0) as i16;
    Ok(if pcm.channels == 1 { samples.iter().flat_map(|&s| [round(s), round(s)]).collect() } else { samples.iter().map(|&s| round(s)).collect() })
}

/// The NMOV file: each frame's video and its share of the sound.
fn nmov(frames: &[Vec<u8>], rate: f64, size: (u32, u32), colour: u32, flags: u32, audio: Option<&[i16]>) -> Vec<u8> {
    let align = |n: usize| (n + 31) & !31;
    let mut out = vec![0u8; HEADER];
    let mut sizes = Vec::with_capacity(frames.len());
    for (i, video) in frames.iter().enumerate() {
        // Frame i's sound: from its start to the next one's, in stereo samples; past the sound, silence.
        let share = audio.map(|a| {
            let at = |k: usize| ((k as f64) * f64::from(AUDIO_RATE) / rate).round() as usize;
            let (from, to) = (at(i), at(i + 1));
            let mut s = vec![0i16; (to - from) * 2];
            for (j, v) in s.iter_mut().enumerate() {
                *v = a.get(from * 2 + j).copied().unwrap_or(0);
            }
            s
        });
        let record = share.as_ref().map_or(0, |s| 4 + s.len() * 2);
        let total = align(FRAME_HEADER + video.len() + record);
        let start = out.len();
        out.resize(start + total, 0);
        out[start + 4..start + 8].copy_from_slice(&(video.len() as u32).to_le_bytes());
        out[start + 8..start + 12].copy_from_slice(&(record as u32).to_le_bytes());
        out[start + FRAME_HEADER..start + FRAME_HEADER + video.len()].copy_from_slice(video);
        if let Some(s) = share {
            let at = start + FRAME_HEADER + video.len();
            out[at..at + 4].copy_from_slice(&((s.len() / 2) as u32).to_le_bytes());
            for (j, v) in s.iter().enumerate() {
                out[at + 4 + j * 2..at + 6 + j * 2].copy_from_slice(&v.to_le_bytes());
            }
        }
        sizes.push(total);
    }
    let first = sizes.first().copied().unwrap_or(0);
    let mut start = HEADER;
    for i in 0..sizes.len() {
        let next = sizes.get(i + 1).copied().unwrap_or(first);
        out[start..start + 4].copy_from_slice(&(next as u32).to_le_bytes());
        start += sizes[i];
    }
    let words: [u32; 12] = [
        u32::from_le_bytes(*super::thp::MAGIC),
        frames.len() as u32,
        (rate as f32).to_bits(),
        sizes.iter().copied().max().unwrap_or(0) as u32,
        first as u32,
        size.0,
        size.1,
        u32::from(audio.is_some()),
        (size.0 as f32 / size.1 as f32).to_bits(),
        1,
        colour,
        flags,
    ];
    for (i, w) in words.iter().enumerate() {
        out[i * 4..i * 4 + 4].copy_from_slice(&w.to_le_bytes());
    }
    out
}

// ---- the source

/// Where the files a `.movie.json` makes go: beside it with this ending, or at a game path.
#[derive(Clone, Debug, PartialEq)]
pub enum Made {
    Own(&'static str),
    Game(String),
}

/// What the cook needs from around the source: a file beside it, and whether the game has a movie by name.
pub struct Around<'a> {
    pub file: &'a dyn Fn(&str) -> Option<Vec<u8>>,
    pub game_movie: &'a dyn Fn(&str) -> Option<bool>,
}

fn file_part<'a>(map: &'a serde_json::Map<String, Value>, key: &str) -> Result<Option<&'a str>> {
    let Some(v) = map.get(key) else { return Ok(None) };
    let at = at_key("", key);
    let f = text(v, &at)?;
    if f.is_empty() || f.contains("..") || f.contains(':') || f.contains('\\') || f.starts_with('/') {
        return Err(refuse("Cook.Effect.FilePath", &[("where", at), ("uri", f.to_string())]));
    }
    Ok(Some(f))
}

/// `<name>.movie.json`'s bytes as the files it makes.
pub fn cook(json: &[u8], around: &Around) -> Result<(Vec<(Made, Vec<u8>)>, MovieFacts)> {
    let root: Value = serde_json::from_slice(json).map_err(|e| refuse("Cook.Model.Json", &[("line", e.line().to_string()), ("column", e.column().to_string())]))?;
    let map = object(&root, "/")?;
    only(map, "/", &PARTS)?;
    let file = file_part(map, "file")?.ok_or_else(|| missing("/", "file"))?;
    let audio_file = file_part(map, "audio")?;
    let flag = |key: &str, default: bool| -> Result<bool> {
        match map.get(key) {
            None => Ok(default),
            Some(v) => v.as_bool().ok_or_else(|| super::particle_json::not_kind(&at_key("", key), "Cook.Effect.Is.Bool")),
        }
    };
    // It plays once and may be skipped unless it says.
    let flags = u32::from(flag("loop", false)?) | u32::from(flag("skip", true)?) << 1;
    let replaces = match map.get("replace") {
        Some(v) => {
            let name = text(v, "/replace")?;
            if name.is_empty() || name.contains(['/', '\\', ':', '.']) {
                return Err(refuse("Cook.Movie.Name", &[("where", "/replace".into()), ("name", name.to_string())]));
            }
            if (around.game_movie)(name) == Some(false) {
                return Err(refuse("Cook.Movie.NoGameMovie", &[("where", "/replace".into()), ("name", name.to_string())]));
            }
            name.to_string()
        }
        None => String::new(),
    };
    let bytes = (around.file)(file).ok_or_else(|| refuse("Cook.Effect.FileMissing", &[("where", "/file".into()), ("uri", file.to_string())]))?;
    if bytes.is_empty() {
        return Err(refuse("Cook.Sound.Empty", &[("uri", file.to_string())]));
    }
    let (mut video, audio_tracks) = if bytes.starts_with(b"DKIF") {
        (read_ivf(&bytes, file)?, Vec::new())
    } else if bytes.starts_with(&[0x1A, 0x45, 0xDF, 0xA3]) {
        read_webm(&bytes, file)?
    } else if bytes.get(4..8) == Some(b"ftyp") {
        return Err(codec(file, "MP4"));
    } else {
        return Err(bad(file, "not a WebM or IVF file"));
    };
    for codec in audio_tracks {
        super::refuse::note("Cook.Movie.AudioTrack", &[("uri", file.to_string()), ("found", codec)]);
    }
    if !(video.rate > 0.0) || video.rate > MOST_RATE + 0.01 {
        return Err(refuse("Cook.Movie.Rate", &[("uri", file.to_string()), ("value", format!("{:.3}", video.rate)), ("max", format!("{MOST_RATE:.0}"))]));
    }
    let (sequence, colour) = check_video(&mut video, file)?;
    let (width, height) = (sequence.width, sequence.height);
    if width > MOST_WIDTH.max(1080) || height > MOST_WIDTH || u64::from(width) * u64::from(height) > MOST_PIXELS {
        return Err(refuse("Cook.Movie.Size", &[("uri", file.to_string()), ("width", width.to_string()), ("height", height.to_string())]));
    }
    if let Some((w, h)) = video.size {
        if (w, h) != (width, height) {
            return Err(bad(file, format!("the container says {w} x {h}, the AV1 sequence {width} x {height}")));
        }
    }
    let seconds = video.frames.len() as f64 / video.rate;
    if seconds > MOST_SECONDS {
        return Err(refuse("Cook.Movie.TooLong", &[("seconds", format!("{seconds:.1}")), ("max", format!("{MOST_SECONDS:.0}"))]));
    }
    let sound_samples = match audio_file {
        Some(a) => {
            let bytes = (around.file)(a).ok_or_else(|| refuse("Cook.Effect.FileMissing", &[("where", "/audio".into()), ("uri", a.to_string())]))?;
            Some(sound(&bytes, a)?)
        }
        None => None,
    };
    // Subtitles: a text key of the mod's text, shown from and to (seconds) inside the movie.
    let mut subtitle_rows = Vec::new();
    if let Some(v) = map.get("subtitles") {
        let list = v.as_array().ok_or_else(|| super::particle_json::not_kind("/subtitles", "Cook.Effect.Is.List"))?;
        for (i, s) in list.iter().enumerate() {
            let at = format!("/subtitles/{i}");
            let m = object(s, &at)?;
            only(m, &at, &["text", "from", "to"])?;
            let key = text(m.get("text").ok_or_else(|| missing(&at, "text"))?, &at_key(&at, "text"))?;
            let from = as_f32(m.get("from").ok_or_else(|| missing(&at, "from"))?, &at_key(&at, "from"))?;
            let to = as_f32(m.get("to").ok_or_else(|| missing(&at, "to"))?, &at_key(&at, "to"))?;
            if !(0.0..=seconds as f32).contains(&from) || !(0.0..=seconds as f32 + 0.001).contains(&to) || from >= to {
                return Err(refuse("Cook.Movie.Subtitle", &[("where", at), ("value", format!("{from}-{to}")), ("seconds", format!("{seconds:.3}"))]));
            }
            subtitle_rows.push(serde_json::json!({"text": key, "from": from, "to": to}));
        }
    }
    let out = nmov(&video.frames, video.rate, (width, height), colour, flags, sound_samples.as_deref());
    let facts = MovieFacts {
        frames: video.frames.len() as u32,
        width,
        height,
        rate: video.rate as f32,
        seconds: seconds as f32,
        audio_seconds: sound_samples.as_ref().map_or(0.0, |s| s.len() as f32 / 2.0 / AUDIO_RATE as f32),
        colour,
        subtitles: subtitle_rows.len() as u32,
        replaces: replaces.clone(),
    };
    let mut files = vec![(Made::Own(".nmov"), out.clone())];
    let has_subtitles = !subtitle_rows.is_empty();
    let json = serde_json::json!({"columns": {"text": "string", "from": "float", "to": "float"}, "rows": subtitle_rows});
    let subs = super::table::TableIr::from_json(json.to_string().as_bytes())?.to_ntbl();
    if has_subtitles {
        files.push((Made::Own(".subs"), subs.clone()));
    }
    if !replaces.is_empty() {
        files.push((Made::Game(format!("MovieData/{replaces}.thp")), out));
        // The replaced movie's lines, empty when it has none: the game's own timed lines for that movie give way to
        // them, so a replaced movie never shows the game's lines over another picture (J, 11-12).
        files.push((Made::Game(format!("MovieData/{replaces}.subs")), subs));
    }
    Ok((files, facts))
}

/// Whether a path is a movie's source (`.movie.json`).
pub fn is_movie_source(lower: &str) -> bool {
    lower.ends_with(".movie.json")
}

/// A video or sound file in a mod's assets that a `.movie.json` names, not a game file.
pub fn is_asset_source(path: &str) -> bool {
    let lower = path.trim_start_matches('/').to_ascii_lowercase();
    lower.starts_with("luma/") && [".webm", ".ivf", ".mkv"].iter().any(|e| lower.ends_with(e))
}

#[cfg(test)]
#[path = "movie_cook_tests.rs"]
mod tests;
