//! A mod's stream (plan 3.7 layer 3, contract board/contracts/sound.md section 8): `<name>.sound.json` of kind `stream`
//! names a WAV, FLAC, Ogg Vorbis, BRSTM, BCSTM or BFSTM file beside it; the cook writes `Luma/<id>/<name>.ast`, the
//! game's stream form (NSTR, StreamPlayer.hpp's Header, then each channel's PCM16 samples in turn), its loop
//! sample-exact. With `replace`, the same file also takes a game stream's place (`AudioRes/Stream/<name>.ast`).
//!
//! A mod plays its own stream by the track functions (`music.add_track(name, "Luma/<id>/<name>")`); a replaced game
//! stream plays wherever the game plays that one.

use super::particle_json::{as_f32, as_int, at_key, missing, object, only, text};
use super::refuse::refuse;
use super::sound_effect::{decode, loudness, resample_to_rate, Around, Cooked, Made, Pcm, SoundFacts, HIGHEST_RATE, LOWEST_RATE, RATE};
use anyhow::Result;
use serde_json::Value;

/// The stream form's channels (StreamPlayer::cChannels).
pub const MOST_CHANNELS: usize = 6;
/// A stream's longest: 20 minutes.
pub const MOST_SECONDS: f64 = 1200.0;
/// The header's volume of 1 (StreamPlayer: volume / 127).
const UNIT_VOLUME: f32 = 127.0;
const HEADER: usize = 32;
const PARTS: [&str; 5] = ["kind", "file", "volume", "loop", "replace"];

/// A Nintendo stream decoded, as the other sound files are.
fn nintendo(bytes: &[u8], file: &str) -> Result<Pcm> {
    let (channels, rate, samples, loop_points) = super::modern_audio::read_nintendo(bytes)
        .map_err(|e| refuse("Cook.Sound.StreamFile", &[("uri", file.to_string()), ("detail", format!("{e:#}"))]))?;
    Ok(Pcm {
        channels,
        rate,
        samples: samples.iter().map(|&s| f32::from(s) / 32768.0).collect(),
        loop_points: loop_points.map(|(s, e)| (u64::from(s), u64::from(e))),
    })
}

/// Whether bytes are a Nintendo stream, by their first four.
fn is_nintendo(b: &[u8]) -> bool {
    matches!(b.get(..4), Some(b"RSTM" | b"CSTM" | b"FSTM"))
}

/// The NSTR file of decoded sound: each channel in turn, rounded to 16 bits.
pub fn nstr(channels: usize, rate: u32, samples: &[f32], looped: bool, loop_points: (u64, u64), volume: f32) -> Vec<u8> {
    let frames = samples.len() / channels;
    let header: [u32; 8] = [
        u32::from_be_bytes(*b"NSTR"),
        channels as u32,
        rate,
        frames as u32,
        u32::from(looped),
        loop_points.0 as u32,
        loop_points.1 as u32,
        (volume * UNIT_VOLUME).round() as u32,
    ];
    let mut out = Vec::with_capacity(HEADER + samples.len() * 2);
    for v in header {
        out.extend_from_slice(&v.to_le_bytes());
    }
    for channel in 0..channels {
        for frame in 0..frames {
            let v = (samples[frame * channels + channel] * 32768.0).round().clamp(-32768.0, 32767.0) as i16;
            out.extend_from_slice(&v.to_le_bytes());
        }
    }
    out
}

/// `<name>.sound.json` of kind `stream` as the files it makes.
pub fn cook(map: &serde_json::Map<String, Value>, around: &Around) -> Result<Cooked> {
    only(map, "/", &PARTS)?;
    let file = super::sound_effect::file_of(map)?;
    let volume = match map.get("volume") {
        Some(v) => super::sound_effect::in_range("/volume", as_f32(v, "/volume")?, 0.0, 4.0)?,
        None => 1.0,
    };
    let replaces = match map.get("replace") {
        Some(v) => {
            let name = text(v, "/replace")?;
            if name.is_empty() || name.contains(['/', '\\', ':', '.']) {
                return Err(refuse("Cook.Sound.StreamName", &[("where", "/replace".into()), ("name", name.to_string())]));
            }
            if (around.game_stream)(name) == Some(false) {
                return Err(refuse("Cook.Sound.NoGameStream", &[("where", "/replace".into()), ("name", name.to_string())]));
            }
            name.to_string()
        }
        None => String::new(),
    };
    let bytes = (around.file)(file).ok_or_else(|| refuse("Cook.Effect.FileMissing", &[("where", "/file".into()), ("uri", file.to_string())]))?;
    if bytes.is_empty() {
        return Err(refuse("Cook.Sound.Empty", &[("uri", file.to_string())]));
    }
    let pcm = if is_nintendo(&bytes) { nintendo(&bytes, file)? } else { decode(&bytes, file)? };
    if pcm.channels == 0 || pcm.channels > MOST_CHANNELS {
        return Err(refuse("Cook.Sound.StreamChannels", &[("uri", file.to_string()), ("count", pcm.channels.to_string()), ("max", MOST_CHANNELS.to_string())]));
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
        return Err(refuse("Cook.Sound.StreamTooLong", &[("seconds", format!("{seconds:.1}")), ("max", format!("{MOST_SECONDS:.0}"))]));
    }
    if pcm.samples.iter().any(|s| !s.is_finite()) {
        return Err(refuse("Cook.Sound.NotANumber", &[("uri", file.to_string())]));
    }
    // A stream loops unless it says not to: the source's loop, else the file's, else the whole sound.
    let loop_points = match map.get("loop") {
        None | Some(Value::Bool(true)) => Some(pcm.loop_points.unwrap_or((0, frames))),
        Some(Value::Bool(false)) => None,
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
    // Kept at its rate up to 48 kHz (the mixer reads any); above, resampled to 48 kHz with the loop's seam kept.
    let (rate, samples, out_loop) = if pcm.rate > RATE {
        let (samples, out_loop) = resample_to_rate(&pcm, loop_points);
        (RATE, samples, out_loop)
    } else {
        (pcm.rate, pcm.samples.clone(), loop_points)
    };
    let out_frames = (samples.len() / pcm.channels) as u64;
    let looped = out_loop.is_some();
    let points = out_loop.unwrap_or((0, out_frames));
    let made = nstr(pcm.channels, rate, &samples, looped, points, volume);
    let peak = samples.iter().fold(0f32, |m, s| m.max(s.abs()));
    // Loudness is measured at 48 kHz: the K-weighting's filters are given for it.
    let measured = if rate == RATE { loudness(&samples, pcm.channels) } else { loudness(&resample_to_rate(&pcm, None).0, pcm.channels) };
    let facts = SoundFacts {
        kind: "stream".into(),
        frames: out_frames as u32,
        channels: pcm.channels as u32,
        rate_in: pcm.rate,
        looped,
        loudness: measured,
        peak: if peak > 0.0 { 20.0 * peak.log10() } else { f32::NEG_INFINITY },
        replaces: replaces.clone(),
        ..SoundFacts::default()
    };
    let mut files = vec![(Made::Own(".ast"), made.clone())];
    if !replaces.is_empty() {
        files.push((Made::Game(format!("AudioRes/Stream/{replaces}.ast")), made));
    }
    Ok((files, facts))
}

#[cfg(test)]
#[path = "sound_stream_tests.rs"]
mod tests;
