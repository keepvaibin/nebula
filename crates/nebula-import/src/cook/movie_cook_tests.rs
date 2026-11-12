use super::*;
use crate::cook::refuse::said;

/// Bits written most significant first, as AV1's headers read them.
#[derive(Default)]
struct Writer {
    bytes: Vec<u8>,
    bits: usize,
}

impl Writer {
    fn put(&mut self, n: u32, v: u64) {
        for i in (0..n).rev() {
            if self.bits % 8 == 0 {
                self.bytes.push(0);
            }
            let bit = (v >> i & 1) as u8;
            *self.bytes.last_mut().unwrap() |= bit << (7 - self.bits % 8);
            self.bits += 1;
        }
    }
    /// The trailing bits: a 1, then zeros to the byte.
    fn done(mut self) -> Vec<u8> {
        self.put(1, 1);
        self.bytes
    }
}

/// A sequence header's payload: main profile unless said, no timing, one operating point.
fn sequence_payload(w: u32, h: u32, high_bitdepth: bool, matrix: Option<u64>, full: bool) -> Vec<u8> {
    let mut b = Writer::default();
    b.put(3, 0); // profile
    b.put(1, 0); // still picture
    b.put(1, 0); // reduced header
    b.put(1, 0); // timing info
    b.put(1, 0); // initial display delay
    b.put(5, 0); // one operating point
    b.put(12, 0);
    b.put(5, 4); // level 3.0
    b.put(4, 11); // width bits - 1
    b.put(4, 11);
    b.put(12, u64::from(w - 1));
    b.put(12, u64::from(h - 1));
    b.put(1, 0); // frame ids
    b.put(3, 0); // 128x128, filter intra, intra edge
    b.put(4, 0); // interintra, masked, warped, dual filter
    b.put(1, 0); // order hint
    b.put(1, 0); // choose screen content tools: no
    b.put(1, 0); // force screen content tools: 0
    b.put(3, 0); // superres, cdef, restoration
    b.put(1, u64::from(high_bitdepth));
    b.put(1, 0); // mono
    match matrix {
        Some(m) => {
            b.put(1, 1);
            b.put(8, 1);
            b.put(8, 1);
            b.put(8, m);
        }
        None => b.put(1, 0),
    }
    b.put(1, u64::from(full));
    b.put(2, 0); // chroma sample position
    b.put(1, 0); // separate uv delta q
    b.put(1, 0); // film grain
    b.done()
}

fn obu(kind: u8, payload: &[u8]) -> Vec<u8> {
    let mut out = vec![kind << 3 | 2];
    let mut n = payload.len();
    loop {
        let byte = (n & 0x7F) as u8;
        n >>= 7;
        out.push(if n > 0 { byte | 0x80 } else { byte });
        if n == 0 {
            break;
        }
    }
    out.extend_from_slice(payload);
    out
}

fn seq(w: u32, h: u32) -> Vec<u8> {
    obu(1, &sequence_payload(w, h, false, Some(1), false))
}

/// A temporal unit: a delimiter, the sequence header when given, a frame (key or not) with some bytes after its type.
fn tu(sequence: Option<&[u8]>, key: bool, tag: u8) -> Vec<u8> {
    let mut out = obu(2, &[]);
    if let Some(s) = sequence {
        out.extend_from_slice(s);
    }
    out.extend_from_slice(&obu(6, &[if key { 0x10 } else { 0x30 }, tag, tag, tag]));
    out
}

fn ivf(w: u16, h: u16, rate: u32, frames: &[Vec<u8>]) -> Vec<u8> {
    let mut out = b"DKIF".to_vec();
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&32u16.to_le_bytes());
    out.extend_from_slice(b"AV01");
    out.extend_from_slice(&w.to_le_bytes());
    out.extend_from_slice(&h.to_le_bytes());
    out.extend_from_slice(&rate.to_le_bytes());
    out.extend_from_slice(&1u32.to_le_bytes());
    out.extend_from_slice(&(frames.len() as u32).to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    for (i, f) in frames.iter().enumerate() {
        out.extend_from_slice(&(f.len() as u32).to_le_bytes());
        out.extend_from_slice(&(i as u64).to_le_bytes());
        out.extend_from_slice(f);
    }
    out
}

/// An EBML element with an 8-byte size.
fn el(id: u32, body: &[u8]) -> Vec<u8> {
    let mut out: Vec<u8> = id.to_be_bytes().iter().copied().skip_while(|b| *b == 0).collect();
    out.push(0x01);
    out.extend_from_slice(&(body.len() as u64).to_be_bytes()[1..]);
    out.extend_from_slice(body);
    out
}

fn num(id: u32, v: u64) -> Vec<u8> {
    el(id, &v.to_be_bytes())
}

/// A WebM: one AV1 track (the sequence header in its av1C), one Opus track, a cluster of key-flagged first block.
fn webm(codec: &str, w: u32, h: u32, frame_ns: u64, sequence: &[u8], frames: &[Vec<u8>]) -> Vec<u8> {
    let mut av1c = vec![0x81, 0, 0, 0];
    av1c.extend_from_slice(sequence);
    let video = [num(0xD7, 1), num(0x83, 1), el(0x86, codec.as_bytes()), num(0x23E383, frame_ns), el(0x63A2, &av1c), el(0xE0, &[num(0xB0, w.into()), num(0xBA, h.into())].concat())].concat();
    let audio = [num(0xD7, 2), num(0x83, 2), el(0x86, b"A_OPUS")].concat();
    let tracks = el(0x1654_AE6B, &[el(0xAE, &video), el(0xAE, &audio)].concat());
    let mut cluster = num(0xE7, 0);
    for (i, f) in frames.iter().enumerate() {
        let mut block = vec![0x81];
        block.extend_from_slice(&((i as i16) * 33).to_be_bytes());
        block.push(if i == 0 { 0x80 } else { 0 });
        block.extend_from_slice(f);
        cluster.extend_from_slice(&el(0xA3, &block));
    }
    let segment = el(0x1853_8067, &[el(0x1549_A966, &num(0x2AD7B1, 1_000_000)), tracks, el(0x1F43_B675, &cluster)].concat());
    [el(0x1A45_DFA3, &el(0x4282, b"webm")), segment].concat()
}

fn mono_wav(rate: u32, samples: &[i16]) -> Vec<u8> {
    let data: Vec<u8> = samples.iter().flat_map(|s| s.to_le_bytes()).collect();
    let mut out = b"RIFF\0\0\0\0WAVEfmt ".to_vec();
    for v in [16u32.to_le_bytes().to_vec(), 1u16.to_le_bytes().to_vec(), 1u16.to_le_bytes().to_vec(), rate.to_le_bytes().to_vec(), (rate * 2).to_le_bytes().to_vec(), 2u16.to_le_bytes().to_vec(), 16u16.to_le_bytes().to_vec()] {
        out.extend_from_slice(&v);
    }
    out.extend_from_slice(b"data");
    out.extend_from_slice(&(data.len() as u32).to_le_bytes());
    out.extend_from_slice(&data);
    let n = out.len() as u32 - 8;
    out[4..8].copy_from_slice(&n.to_le_bytes());
    out
}

fn cook_with(json: &str, files: &[(&str, Vec<u8>)]) -> Result<(Vec<(Made, Vec<u8>)>, MovieFacts)> {
    let file = |name: &str| files.iter().find(|(n, _)| *n == name).map(|(_, b)| b.clone());
    let game = |name: &str| Some(name == "PrologueA");
    cook(json.as_bytes(), &Around { file: &file, game_movie: &game })
}

fn word(b: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(b[at..at + 4].try_into().unwrap())
}

/// Each frame of an NMOV: (video, audio record).
fn frames_of(b: &[u8]) -> Vec<(Vec<u8>, Vec<u8>)> {
    let mut out = Vec::new();
    let (mut at, mut size) = (HEADER, word(b, 16) as usize);
    for _ in 0..word(b, 4) {
        let (video, audio) = (word(b, at + 4) as usize, word(b, at + 8) as usize);
        let v = b[at + FRAME_HEADER..at + FRAME_HEADER + video].to_vec();
        let a = b[at + FRAME_HEADER + video..at + FRAME_HEADER + video + audio].to_vec();
        out.push((v, a));
        let next = word(b, at) as usize;
        at += size;
        size = next;
    }
    out
}

/// The first file of the round trip: an IVF's frames come out frame for frame (its delimiters left out), with the
/// header J's reader takes.
#[test]
fn an_ivf_movie_round_trips_to_nmov() {
    let s = seq(64, 48);
    let source = vec![tu(Some(&s), true, 1), tu(None, false, 2), tu(None, false, 3)];
    let (made, facts) = cook_with(r#"{"file":"m.ivf"}"#, &[("m.ivf", ivf(64, 48, 30, &source))]).unwrap();
    assert_eq!(made.len(), 1);
    assert_eq!(made[0].0, Made::Own(".nmov"));
    let b = &made[0].1;
    assert_eq!(&b[..4], b"NMOV");
    assert_eq!((word(b, 4), f32::from_bits(word(b, 8)), word(b, 20), word(b, 24), word(b, 28)), (3, 30.0, 64, 48, 0));
    assert_eq!((word(b, 0x24), word(b, 0x28), word(b, 0x2C)), (1, 1, 2), "AV1, BT.709 limited, may be skipped");
    assert_eq!(f32::from_bits(word(b, 32)), 64.0 / 48.0);
    let frames = frames_of(b);
    let delimiter = obu(2, &[]);
    for (i, (video, audio)) in frames.iter().enumerate() {
        assert_eq!(video[..], source[i][delimiter.len()..], "frame {i}");
        assert!(audio.is_empty());
    }
    assert_eq!((facts.frames, facts.width, facts.height, facts.colour), (3, 64, 48, 1));
}

/// The second: a WebM whose blocks leave the sequence header to its av1C gets it put first in frame 0; its Opus
/// track is named in a Note; the sound file is cut into 32 kHz stereo records, a frame's share each; subtitles and
/// the replaced game movie are written beside it.
#[test]
fn a_webm_movie_round_trips_with_its_sound_and_subtitles() {
    let s = seq(32, 32);
    let blocks = vec![obu(6, &[0x10, 9]), obu(6, &[0x30, 8]), obu(6, &[0x30, 7]), obu(6, &[0x30, 6])];
    // 25 frames a second; one second of sound at 16 kHz.
    let sound: Vec<i16> = (0..16000).map(|i| ((i % 100) * 300 - 15000) as i16).collect();
    crate::cook::refuse::take_notes();
    let json = r#"{"file":"m.webm","audio":"m.wav","replace":"PrologueA","loop":true,"skip":false,"subtitles":[{"text":"Intro.Line1","from":0,"to":0.1}]}"#;
    let (made, facts) = cook_with(json, &[("m.webm", webm("V_AV1", 32, 32, 40_000_000, &s, &blocks)), ("m.wav", mono_wav(16000, &sound))]).unwrap();
    let notes = crate::cook::refuse::said_notes();
    assert!(notes.iter().any(|(why, args)| *why == "Cook.Movie.AudioTrack" && args.contains(&("found", "A_OPUS".to_string()))), "{notes:?}");
    let places: Vec<&Made> = made.iter().map(|(m, _)| m).collect();
    assert_eq!(places, [&Made::Own(".nmov"), &Made::Own(".subs"), &Made::Game("MovieData/PrologueA.thp".into())]);
    let b = &made[0].1;
    assert_eq!((word(b, 4), f32::from_bits(word(b, 8)), word(b, 28), word(b, 0x2C)), (4, 25.0, 1, 1));
    let frames = frames_of(b);
    assert_eq!(frames[0].0, [s.clone(), blocks[0].clone()].concat(), "the sequence header first in frame 0");
    for i in 1..4 {
        assert_eq!(frames[i].0, blocks[i]);
    }
    // 32,000 / 25 = 1,280 stereo samples a frame, the left and right the same (the sound is mono).
    for (i, (_, record)) in frames.iter().enumerate() {
        assert_eq!(word(record, 0), 1280, "frame {i}");
        assert_eq!(record.len(), 4 + 1280 * 4);
        let first: Vec<i16> = record[4..8].chunks_exact(2).map(|c| i16::from_le_bytes([c[0], c[1]])).collect();
        assert_eq!(first[0], first[1]);
    }
    assert_eq!(&made[1].1[..4], b"NTBL");
    assert_eq!((facts.subtitles, facts.replaces.as_str(), facts.seconds), (1, "PrologueA", 0.16));
    assert!((facts.audio_seconds - 1.0).abs() < 0.001, "{facts:?}");
}

/// Refused by name: other codecs, other AV1 forms and colours, too large, too fast, no key frame first, cut files,
/// subtitles outside the movie, a game movie the game does not have.
#[test]
fn movies_are_refused_by_name() {
    let refused = |json: &str, name: &str, bytes: Vec<u8>| said(&cook_with(json, &[(name, bytes)]).unwrap_err()).0;
    let s = seq(64, 48);
    let good = || vec![tu(Some(&s), true, 1), tu(None, false, 2)];
    assert_eq!(refused(r#"{"file":"m.webm"}"#, "m.webm", webm("V_VP9", 64, 48, 33_333_333, &s, &good())), "Cook.Movie.Codec");
    assert_eq!(refused(r#"{"file":"m.mp4"}"#, "m.mp4", b"\0\0\0\x18ftypisom".to_vec()), "Cook.Movie.Codec");
    let deep = obu(1, &sequence_payload(64, 48, true, Some(1), false));
    assert_eq!(refused(r#"{"file":"m.ivf"}"#, "m.ivf", ivf(64, 48, 30, &[tu(Some(&deep), true, 1)])), "Cook.Movie.Form");
    let odd = obu(1, &sequence_payload(64, 48, false, Some(9), false));
    assert_eq!(refused(r#"{"file":"m.ivf"}"#, "m.ivf", ivf(64, 48, 30, &[tu(Some(&odd), true, 1)])), "Cook.Movie.Colour");
    let wide = seq(3840, 2160);
    assert_eq!(refused(r#"{"file":"m.ivf"}"#, "m.ivf", ivf(3840, 2160, 30, &[tu(Some(&wide), true, 1)])), "Cook.Movie.Size");
    assert_eq!(refused(r#"{"file":"m.ivf"}"#, "m.ivf", ivf(64, 48, 120, &good())), "Cook.Movie.Rate");
    assert_eq!(refused(r#"{"file":"m.ivf"}"#, "m.ivf", ivf(64, 48, 30, &[tu(Some(&s), false, 1)])), "Cook.Movie.FirstKey");
    let whole = ivf(64, 48, 30, &good());
    assert_eq!(refused(r#"{"file":"m.ivf"}"#, "m.ivf", whole[..whole.len() - 3].to_vec()), "Cook.Sound.Cut");
    assert_eq!(refused(r#"{"file":"m.ivf"}"#, "m.ivf", Vec::new()), "Cook.Sound.Empty");
    assert_eq!(refused(r#"{"file":"m.ivf"}"#, "m.ivf", b"nothing".to_vec()), "Cook.Movie.File");
    assert_eq!(refused(r#"{"file":"m.ivf","subtitles":[{"text":"A","from":0,"to":9}]}"#, "m.ivf", whole.clone()), "Cook.Movie.Subtitle");
    assert_eq!(refused(r#"{"file":"m.ivf","replace":"Nope"}"#, "m.ivf", whole.clone()), "Cook.Movie.NoGameMovie");
    assert_eq!(refused(r#"{"file":"m.ivf","replace":"MovieData/x.thp"}"#, "m.ivf", whole.clone()), "Cook.Movie.Name");
    assert_eq!(refused(r#"{"file":"m.ivf","speed":2}"#, "m.ivf", whole), "Cook.Effect.UnknownPart");
}

#[test]
fn the_sequence_header_reads_as_written() {
    let s = sequence_header(&sequence_payload(1920, 1080, false, Some(1), false)).unwrap();
    assert_eq!(s, Sequence { profile: 0, reduced: false, width: 1920, height: 1080, high_bitdepth: false, mono: false, matrix: 1, full_range: false });
    let s = sequence_header(&sequence_payload(8, 8, false, None, true)).unwrap();
    assert_eq!((s.matrix, s.full_range), (2, true));
}
