use super::*;
use crate::cook::refuse::said;
use crate::cook::sound_effect::cook_sound;

fn cook_with(json: &str, files: &[(&str, Vec<u8>)], streams: &[&str]) -> Result<(Vec<(Made, Vec<u8>)>, SoundFacts)> {
    let file = |name: &str| files.iter().find(|(n, _)| *n == name).map(|(_, b)| b.clone());
    let stream = |name: &str| Some(streams.contains(&name));
    cook_sound(json.as_bytes(), &Around { file: &file, game_sound: &|_| None, game_stream: &stream, game_sequence: &|_| None })
}

fn word(b: &[u8], i: usize) -> u32 {
    u32::from_le_bytes(b[i * 4..i * 4 + 4].try_into().unwrap())
}

/// The NSTR file's header words and its samples, interleaved again.
fn read_back(b: &[u8]) -> ([u32; 8], Vec<i16>) {
    let header: [u32; 8] = std::array::from_fn(|i| word(b, i));
    let (channels, frames) = (header[1] as usize, header[3] as usize);
    let planar: Vec<i16> = b[32..].chunks_exact(2).map(|s| i16::from_le_bytes([s[0], s[1]])).collect();
    assert_eq!(planar.len(), channels * frames, "each channel's samples, nothing more");
    let mut out = Vec::with_capacity(planar.len());
    for f in 0..frames {
        for c in 0..channels {
            out.push(planar[c * frames + f]);
        }
    }
    (header, out)
}

/// A 16-bit WAV with an optional `smpl` loop (end inclusive, as the chunk has it).
fn wav(channels: u16, rate: u32, samples: &[i16], looped: Option<(u32, u32)>) -> Vec<u8> {
    let data: Vec<u8> = samples.iter().flat_map(|s| s.to_le_bytes()).collect();
    let mut out = b"RIFF\0\0\0\0WAVEfmt ".to_vec();
    out.extend_from_slice(&16u32.to_le_bytes());
    out.extend_from_slice(&1u16.to_le_bytes());
    out.extend_from_slice(&channels.to_le_bytes());
    out.extend_from_slice(&rate.to_le_bytes());
    out.extend_from_slice(&(rate * 2 * u32::from(channels)).to_le_bytes());
    out.extend_from_slice(&(2 * channels).to_le_bytes());
    out.extend_from_slice(&16u16.to_le_bytes());
    out.extend_from_slice(b"data");
    out.extend_from_slice(&(data.len() as u32).to_le_bytes());
    out.extend_from_slice(&data);
    if let Some((s, e)) = looped {
        let mut body = [0u8; 60];
        body[28..32].copy_from_slice(&1u32.to_le_bytes());
        body[44..48].copy_from_slice(&s.to_le_bytes());
        body[48..52].copy_from_slice(&e.to_le_bytes());
        out.extend_from_slice(b"smpl");
        out.extend_from_slice(&60u32.to_le_bytes());
        out.extend_from_slice(&body);
    }
    let n = out.len() as u32 - 8;
    out[4..8].copy_from_slice(&n.to_le_bytes());
    out
}

fn ramp(frames: usize, channels: usize) -> Vec<i16> {
    (0..frames * channels).map(|i| (((i * 37) % 60000) as i32 - 30000) as i16).collect()
}

/// The first of the two files: a WAV round-trips sample for sample, each channel in turn, with its loop and volume.
#[test]
fn a_wav_stream_round_trips_with_its_loop() {
    let source = ramp(5000, 2);
    let (made, facts) = cook_with(r#"{"kind":"stream","file":"m.wav","volume":0.5}"#, &[("m.wav", wav(2, 32000, &source, Some((1000, 3999))))], &[]).unwrap();
    assert_eq!(made.len(), 1);
    assert_eq!(made[0].0, Made::Own(".ast"));
    let b = &made[0].1;
    assert_eq!(&b[..4], b"RTSN", "StreamPlayer's tag, 'NSTR' as a number");
    let (h, samples) = read_back(b);
    assert_eq!(h, [u32::from_be_bytes(*b"NSTR"), 2, 32000, 5000, 1, 1000, 4000, 64]);
    assert_eq!(samples, source);
    assert_eq!((facts.kind.as_str(), facts.frames, facts.channels, facts.rate_in, facts.looped), ("stream", 5000, 2, 32000, true));
    // Looped 100 times, the seam is the source's: what follows the loop's last frame is its first, every time.
    let (start, end) = (h[5] as usize, h[6] as usize);
    let mut played = Vec::new();
    played.extend_from_slice(&samples[..end * 2]);
    for _ in 0..100 {
        played.extend_from_slice(&samples[start * 2..end * 2]);
    }
    let mut want = source[..end * 2].to_vec();
    for _ in 0..100 {
        want.extend_from_slice(&source[start * 2..end * 2]);
    }
    assert!(played == want);
}

/// The second: the Ogg Vorbis fixture (44.1 kHz stereo, LOOPSTART 1000, LOOPEND 6000) keeps its rate, frames and loop,
/// and its samples are its decode's to the 16-bit step.
#[test]
fn an_ogg_stream_round_trips_its_loop_and_rate() {
    let bytes = include_bytes!("../../data/sound/s16.ogg").to_vec();
    let pcm = decode(&bytes, "s.ogg").unwrap();
    let (made, facts) = cook_with(r#"{"kind":"stream","file":"s.ogg"}"#, &[("s.ogg", bytes)], &[]).unwrap();
    let (h, samples) = read_back(&made[0].1);
    assert_eq!((h[1], h[2], h[3], h[4], h[5], h[6], h[7]), (2, 44100, 8192, 1, 1000, 6000, 127));
    let want: Vec<i16> = pcm.samples.iter().map(|s| (s * 32768.0).round().clamp(-32768.0, 32767.0) as i16).collect();
    assert_eq!(samples, want);
    // 8,192 frames are shorter than one 400 ms block: no loudness, said as none.
    assert!(facts.loudness.is_nan() && facts.peak < 0.0, "{facts:?}");
}

/// Without a loop of its own a stream loops whole; `loop: false` plays once; `{start, end}` is the source's.
#[test]
fn a_streams_loop_is_the_files_the_whole_or_none() {
    let files = [("m.wav", wav(1, 22050, &ramp(800, 1), None))];
    let header = |json: &str| read_back(&cook_with(json, &files, &[]).unwrap().0[0].1).0;
    let h = header(r#"{"kind":"stream","file":"m.wav"}"#);
    assert_eq!((h[4], h[5], h[6]), (1, 0, 800));
    let h = header(r#"{"kind":"stream","file":"m.wav","loop":false}"#);
    assert_eq!((h[4], h[5], h[6]), (0, 0, 800));
    let h = header(r#"{"kind":"stream","file":"m.wav","loop":{"start":100,"end":700}}"#);
    assert_eq!((h[4], h[5], h[6]), (1, 100, 700));
    let (why, args) = said(&cook_with(r#"{"kind":"stream","file":"m.wav","loop":{"start":100,"end":900}}"#, &files, &[]).unwrap_err());
    assert_eq!(why, "Cook.Sound.LoopOutside", "{args:?}");
}

/// `replace` puts the same file at the game stream's path, when the game has it.
#[test]
fn a_stream_replaces_a_game_stream_by_name() {
    let files = [("m.wav", wav(1, 32000, &ramp(100, 1), None))];
    let (made, facts) = cook_with(r#"{"kind":"stream","file":"m.wav","replace":"smg_galaxy01"}"#, &files, &["smg_galaxy01"]).unwrap();
    assert_eq!(made.len(), 2);
    assert_eq!(made[1].0, Made::Game("AudioRes/Stream/smg_galaxy01.ast".into()));
    assert_eq!(made[0].1, made[1].1);
    assert_eq!(facts.replaces, "smg_galaxy01");
    let (why, args) = said(&cook_with(r#"{"kind":"stream","file":"m.wav","replace":"nope"}"#, &files, &["smg_galaxy01"]).unwrap_err());
    assert_eq!((why, args[1].1.as_str()), ("Cook.Sound.NoGameStream", "nope"));
    let (why, _) = said(&cook_with(r#"{"kind":"stream","file":"m.wav","replace":"AudioRes/x"}"#, &files, &[]).unwrap_err());
    assert_eq!(why, "Cook.Sound.StreamName");
}

/// A 96 kHz source is resampled to 48 kHz, its loop scaled; 7 channels and an effect's parts are refused.
#[test]
fn a_streams_rate_and_channels() {
    let (made, facts) = cook_with(r#"{"kind":"stream","file":"h.wav"}"#, &[("h.wav", wav(1, 96000, &ramp(9600, 1), Some((0, 9599))))], &[]).unwrap();
    let (h, _) = read_back(&made[0].1);
    assert_eq!((h[2], h[3], h[5], h[6]), (48000, 4800, 0, 4800));
    assert_eq!(facts.rate_in, 96000);
    let (why, args) = said(&cook_with(r#"{"kind":"stream","file":"s.wav"}"#, &[("s.wav", wav(7, 32000, &ramp(10, 7), None))], &[]).unwrap_err());
    assert_eq!(why, "Cook.Sound.StreamChannels", "{args:?}");
    let (why, _) = said(&cook_with(r#"{"kind":"stream","file":"m.wav","pitch":2}"#, &[("m.wav", wav(1, 32000, &ramp(10, 1), None))], &[]).unwrap_err());
    assert_eq!(why, "Cook.Effect.UnknownPart");
}

/// Hostile files: none of them is read past its bytes or allocates what it claims.
#[test]
fn hostile_stream_files_are_refused_by_name() {
    let refused = |name: &str, bytes: Vec<u8>| said(&cook_with(&format!(r#"{{"kind":"stream","file":"{name}"}}"#), &[(name, bytes)], &[]).unwrap_err()).0;
    assert_eq!(refused("z.wav", Vec::new()), "Cook.Sound.Empty");
    // A WAV whose data chunk claims 2 GB.
    let mut huge = wav(1, 32000, &ramp(10, 1), None);
    let at = huge.windows(4).position(|w| w == b"data").unwrap() + 4;
    huge[at..at + 4].copy_from_slice(&0x7FFF_FFF0u32.to_le_bytes());
    assert!(refused("h.wav", huge).starts_with("Cook.Sound."));
    // The Ogg fixture cut in half.
    let ogg = include_bytes!("../../data/sound/s16.ogg");
    assert!(refused("c.ogg", ogg[..ogg.len() / 2].to_vec()).starts_with("Cook.Sound."));
    // A BRSTM header claiming four billion frames and nothing after it.
    let mut rstm = vec![0u8; 0x60];
    rstm[..4].copy_from_slice(b"RSTM");
    rstm[4..6].copy_from_slice(&[0xFE, 0xFF]);
    assert_eq!(refused("b.brstm", rstm), "Cook.Sound.StreamFile");
}
