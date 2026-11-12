use super::*;
use crate::cook::refuse::said;

/// A WAV of `tag` (1 PCM, 3 float) and `bits`, the samples as -1..1, with an optional `smpl` loop (end inclusive).
fn wav(channels: u16, rate: u32, tag: u16, bits: u16, samples: &[f64], looped: Option<(u32, u32)>) -> Vec<u8> {
    let mut data = Vec::new();
    for &s in samples {
        match (tag, bits) {
            (1, 8) => data.push(((s * 128.0).round() + 128.0).clamp(0.0, 255.0) as u8),
            (1, 16) => data.extend_from_slice(&((s * 32768.0).round().clamp(-32768.0, 32767.0) as i16).to_le_bytes()),
            (1, 24) => data.extend_from_slice(&((s * 8_388_608.0).round().clamp(-8_388_608.0, 8_388_607.0) as i32).to_le_bytes()[..3]),
            (3, 32) => data.extend_from_slice(&(s as f32).to_le_bytes()),
            _ => data.extend_from_slice(&[0; 4]),
        }
    }
    let mut out = b"RIFF\0\0\0\0WAVE".to_vec();
    let block = channels * bits / 8;
    out.extend_from_slice(b"fmt ");
    out.extend_from_slice(&16u32.to_le_bytes());
    for v in [tag, channels] {
        out.extend_from_slice(&v.to_le_bytes());
    }
    out.extend_from_slice(&rate.to_le_bytes());
    out.extend_from_slice(&(rate * u32::from(block)).to_le_bytes());
    for v in [block, bits] {
        out.extend_from_slice(&v.to_le_bytes());
    }
    out.extend_from_slice(b"data");
    out.extend_from_slice(&(data.len() as u32).to_le_bytes());
    out.extend_from_slice(&data);
    if data.len() % 2 == 1 {
        out.push(0);
    }
    if let Some((s, e)) = looped {
        out.extend_from_slice(b"smpl");
        out.extend_from_slice(&60u32.to_le_bytes());
        let mut body = [0u8; 60];
        body[28..32].copy_from_slice(&1u32.to_le_bytes());
        body[44..48].copy_from_slice(&s.to_le_bytes());
        body[48..52].copy_from_slice(&e.to_le_bytes());
        out.extend_from_slice(&body);
    }
    let n = out.len() as u32 - 8;
    out[4..8].copy_from_slice(&n.to_le_bytes());
    out
}

fn sine(frames: usize, channels: usize, rate: f64, hz: f64, peak: f64) -> Vec<f64> {
    (0..frames).flat_map(|i| std::iter::repeat_n(peak * (2.0 * std::f64::consts::PI * hz * i as f64 / rate).sin(), channels)).collect()
}

fn cook_with(json: &str, files: &[(&str, Vec<u8>)]) -> Result<(Vec<u8>, SoundFacts)> {
    let file = |name: &str| files.iter().find(|(n, _)| *n == name).map(|(_, b)| b.clone());
    let game = |name: &str| Some(name == "SE_SM_JUMP");
    cook(json.as_bytes(), &Around { file: &file, game_sound: &game })
}

fn word(b: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(b[at..at + 4].try_into().unwrap())
}
fn float(b: &[u8], at: usize) -> f32 {
    f32::from_le_bytes(b[at..at + 4].try_into().unwrap())
}
fn samples(b: &[u8]) -> Vec<i16> {
    b[HEADER..HEADER + 2 * (word(b, 12) * word(b, 16)) as usize].chunks_exact(2).map(|s| i16::from_le_bytes([s[0], s[1]])).collect()
}

#[test]
fn a_48k_effect_is_its_samples_with_how_it_plays() {
    let source = sine(4800, 1, 48000.0, 1000.0, 0.5);
    let file = wav(1, 48000, 1, 16, &source, Some((100, 1099)));
    let json = r#"{"kind":"effect","file":"jump.wav","volume":0.8,"pitch":[0.9,1.1],"distance":[100,2000],"priority":200,"bus":"ui","positioned":false,"replace":"SE_SM_JUMP"}"#;
    let (out, facts) = cook_with(json, &[("jump.wav", file)]).unwrap();
    assert_eq!(&out[..4], MAGIC);
    assert_eq!([word(&out, 4), word(&out, 8), word(&out, 12), word(&out, 16), word(&out, 20), word(&out, 24)], [1, 48000, 1, 4800, 100, 1100]);
    assert_eq!([float(&out, 28), float(&out, 32), float(&out, 36), float(&out, 40), float(&out, 44)], [0.8, 0.9, 1.1, 100.0, 2000.0]);
    assert_eq!(&out[48..52], &[200, 1, 0, 0]);
    let want: Vec<i16> = source.iter().map(|s| (s * 32768.0).round() as i16).collect();
    assert_eq!(samples(&out), want, "a 48 kHz 16-bit file's samples are kept exactly");
    let name_at = word(&out, 60) as usize;
    assert_eq!(&out[name_at..], b"SE_SM_JUMP\0");
    assert_eq!((facts.frames, facts.channels, facts.rate_in, facts.looped, facts.replaces.as_str()), (4800, 1, 48000, true, "SE_SM_JUMP"));
    assert!((facts.peak - 20.0 * 0.5f32.log10()).abs() < 0.01, "{}", facts.peak);
    // 0.1 s is shorter than one loudness block.
    assert!(facts.loudness.is_nan() && float(&out, 52).is_nan());
    // Defaults: whole volume, no pitch range, the default distances, middle priority, the effects bus, positioned,
    // and the file's own loop.
    let (out, _) = cook_with(r#"{"kind":"effect","file":"a.wav"}"#, &[("a.wav", wav(1, 48000, 1, 16, &source, Some((0, 4799))))]).unwrap();
    assert_eq!([float(&out, 28), float(&out, 32), float(&out, 36), float(&out, 40), float(&out, 44)], [1.0, 1.0, 1.0, 300.0, 3000.0]);
    assert_eq!((&out[48..51], word(&out, 20), word(&out, 24), word(&out, 60)), (&[128u8, 0, 1][..], 0, 4800, 0));
    // "loop": false drops the file's loop; true without one loops the whole sound.
    let (out, _) = cook_with(r#"{"kind":"effect","file":"a.wav","loop":false}"#, &[("a.wav", wav(1, 48000, 1, 16, &source, Some((0, 99))))]).unwrap();
    assert_eq!((word(&out, 20), word(&out, 24)), (u32::MAX, 0));
    let (out, _) = cook_with(r#"{"kind":"effect","file":"a.wav","loop":true}"#, &[("a.wav", wav(1, 48000, 1, 16, &source, None))]).unwrap();
    assert_eq!((word(&out, 20), word(&out, 24)), (0, 4800));
}

#[test]
fn loudness_is_measured_as_ebu_r128_says() {
    // EBU Tech 3341 case 1: a stereo 1 kHz sine at -23 dBFS reads -23.0 LUFS (±0.1).
    let peak = 10f64.powf(-23.0 / 20.0);
    let source = sine(48000 * 4, 2, 48000.0, 1000.0, peak);
    let (_, facts) = cook_with(r#"{"kind":"effect","file":"t.wav"}"#, &[("t.wav", wav(2, 48000, 3, 32, &source, None))]).unwrap();
    assert!((facts.loudness + 23.0).abs() < 0.1, "{}", facts.loudness);
    // Case 2's level, -33 dBFS: -33.0 LUFS; mono reads 3 LU under stereo.
    let source = sine(48000 * 4, 1, 48000.0, 1000.0, 10f64.powf(-33.0 / 20.0));
    let (_, facts) = cook_with(r#"{"kind":"effect","file":"t.wav"}"#, &[("t.wav", wav(1, 48000, 3, 32, &source, None))]).unwrap();
    assert!((facts.loudness + 36.01).abs() < 0.1, "{}", facts.loudness);
    // Silence has no loudness.
    let (_, facts) = cook_with(r#"{"kind":"effect","file":"t.wav"}"#, &[("t.wav", wav(1, 48000, 1, 16, &vec![0.0; 48000], None))]).unwrap();
    assert!(facts.loudness.is_nan() && facts.peak == f32::NEG_INFINITY);
}

#[test]
fn another_rate_is_resampled_with_its_pitch_level_and_loop() {
    let hz = 1000.0;
    for rate in [44100u32, 22050, 96000, 8000] {
        let frames = rate as usize; // one second
        let source = sine(frames, 1, f64::from(rate), hz, 0.5);
        let looped = Some((rate / 10, rate / 2 - 1));
        let (out, facts) = cook_with(r#"{"kind":"effect","file":"s.wav"}"#, &[("s.wav", wav(1, rate, 1, 16, &source, looped))]).unwrap();
        assert_eq!((facts.frames, facts.rate_in), (48000, rate));
        // The loop's frames in the new rate: exactly a tenth and a half of a second.
        assert_eq!((word(&out, 20), word(&out, 24)), (4800, 24000), "{rate}");
        let s = samples(&out);
        // Away from the ends (the filter's reach): the same frequency (zero crossings) and level (RMS).
        let middle = &s[2000..46000];
        let crossings = middle.windows(2).filter(|w| (w[0] < 0) != (w[1] < 0)).count();
        let want = 2.0 * hz * middle.len() as f64 / 48000.0;
        assert!((crossings as f64 - want).abs() <= 2.0, "{rate}: {crossings} crossings, want {want}");
        let rms = (middle.iter().map(|&v| (f64::from(v) / 32768.0).powi(2)).sum::<f64>() / middle.len() as f64).sqrt();
        assert!((rms - 0.5 / 2f64.sqrt()).abs() < 0.005, "{rate}: rms {rms}");
    }
}

#[test]
fn every_wav_form_reads_the_same() {
    let source = sine(4800, 2, 48000.0, 440.0, 0.25);
    let reference = samples(&cook_with(r#"{"kind":"effect","file":"a.wav"}"#, &[("a.wav", wav(2, 48000, 1, 16, &source, None))]).unwrap().0);
    for (tag, bits, tolerance) in [(1u16, 24u16, 1i32), (3, 32, 1), (1, 8, 260)] {
        let got = samples(&cook_with(r#"{"kind":"effect","file":"a.wav"}"#, &[("a.wav", wav(2, 48000, tag, bits, &source, None))]).unwrap().0);
        let worst = got.iter().zip(&reference).map(|(a, b)| (i32::from(*a) - i32::from(*b)).abs()).max().unwrap();
        assert!(worst <= tolerance, "{tag}/{bits}: off by {worst}");
    }
}

#[test]
fn a_sound_says_what_it_cannot_make() {
    let good = wav(1, 48000, 1, 16, &sine(480, 1, 48000.0, 440.0, 0.5), None);
    let mut cut_off = good.clone();
    cut_off.truncate(60);
    let mut liar = good.clone();
    let data_at = liar.windows(4).position(|w| w == b"data").unwrap();
    liar[data_at + 4..data_at + 8].copy_from_slice(&0x7FFF_FFF0u32.to_le_bytes());
    let mut nan = wav(1, 48000, 3, 32, &[0.0, 0.5, 0.0], None);
    let nan_at = nan.len() - 8;
    nan[nan_at..nan_at + 4].copy_from_slice(&f32::NAN.to_le_bytes());
    let mut opus = b"OggS\0\x02".to_vec();
    opus.extend_from_slice(&[0; 22]);
    opus.extend_from_slice(b"OpusHead");
    let cases: Vec<(&str, Vec<u8>, &str, &str)> = vec![
        ("{", good.clone(), "Cook.Model.Json", ""),
        (r#"{"kind":"effect","file":"a.wav","sound":1}"#, good.clone(), "Cook.Effect.UnknownPart", "/"),
        (r#"{"kind":"stream","file":"a.wav"}"#, good.clone(), "Cook.Effect.Choice", "/kind"),
        (r#"{"file":"a.wav"}"#, good.clone(), "Cook.Effect.Missing", "/"),
        (r#"{"kind":"effect"}"#, good.clone(), "Cook.Effect.Missing", "/"),
        (r#"{"kind":"effect","file":"../a.wav"}"#, good.clone(), "Cook.Effect.FilePath", "/file"),
        (r#"{"kind":"effect","file":"b.wav"}"#, good.clone(), "Cook.Effect.FileMissing", "/file"),
        (r#"{"kind":"effect","file":"a.wav","volume":5}"#, good.clone(), "Cook.Effect.Range", "/volume"),
        (r#"{"kind":"effect","file":"a.wav","pitch":[2,1]}"#, good.clone(), "Cook.Sound.Order", "/pitch"),
        (r#"{"kind":"effect","file":"a.wav","pitch":0.1}"#, good.clone(), "Cook.Effect.Range", "/pitch/0"),
        (r#"{"kind":"effect","file":"a.wav","distance":[5,5]}"#, good.clone(), "Cook.Sound.Order", "/distance"),
        (r#"{"kind":"effect","file":"a.wav","bus":"sfx"}"#, good.clone(), "Cook.Effect.Choice", "/bus"),
        (r#"{"kind":"effect","file":"a.wav","priority":300}"#, good.clone(), "Cook.Effect.Range", "/priority"),
        (r#"{"kind":"effect","file":"a.wav","replace":"SE_NONE"}"#, good.clone(), "Cook.Sound.NoGameSound", "/replace"),
        (r#"{"kind":"effect","file":"a.wav","loop":{"start":400,"end":600}}"#, good.clone(), "Cook.Sound.LoopOutside", ""),
        (r#"{"kind":"effect","file":"a.wav","loop":{"start":10,"end":10}}"#, good.clone(), "Cook.Sound.LoopOutside", ""),
        (r#"{"kind":"effect","file":"a.wav"}"#, Vec::new(), "Cook.Sound.Empty", ""),
        (r#"{"kind":"effect","file":"a.wav"}"#, b"MThd not a sound".to_vec(), "Cook.Sound.Unreadable", ""),
        (r#"{"kind":"effect","file":"a.wav"}"#, cut_off, "Cook.Sound.Cut", ""),
        (r#"{"kind":"effect","file":"a.wav"}"#, liar, "Cook.Sound.Cut", ""),
        (r#"{"kind":"effect","file":"a.wav"}"#, wav(1, 48000, 1, 16, &[], None), "Cook.Sound.Empty", ""),
        (r#"{"kind":"effect","file":"a.wav"}"#, wav(3, 48000, 1, 16, &[0.0; 30], None), "Cook.Sound.Channels", ""),
        (r#"{"kind":"effect","file":"a.wav"}"#, wav(1, 4000, 1, 16, &[0.0; 30], None), "Cook.Sound.Rate", ""),
        (r#"{"kind":"effect","file":"a.wav"}"#, wav(1, 8000, 1, 16, &vec![0.0; 8000 * 61], None), "Cook.Sound.TooLong", ""),
        (r#"{"kind":"effect","file":"a.wav"}"#, wav(1, 48000, 2, 4, &[0.0; 8], None), "Cook.Sound.WavKind", ""),
        (r#"{"kind":"effect","file":"a.wav"}"#, nan, "Cook.Sound.NotANumber", ""),
        (r#"{"kind":"effect","file":"a.wav"}"#, opus, "Cook.Sound.Opus", ""),
        (r#"{"kind":"effect","file":"a.wav"}"#, b"fLaC\x80\0\0\x02xx".to_vec(), "Cook.Sound.Broken", ""),
    ];
    for (json, bytes, why, at) in cases {
        let error = cook_with(json, &[("a.wav", bytes)]).unwrap_err();
        let (got, args) = said(&error);
        assert_eq!(got, why, "{json}: {args:?}");
        if !at.is_empty() {
            assert_eq!(args.iter().find(|(n, _)| *n == "where").map(|(_, v)| v.as_str()), Some(at), "{json}: {args:?}");
        }
    }
}

#[test]
fn a_sound_files_path_is_its_name() {
    assert_eq!(cooked_path("Luma/my.mod/sfx/Jump.sound.json").as_deref(), Some("Luma/my.mod/sfx/Jump.nsfx"));
    assert_eq!(cooked_path("Luma/my.mod/sfx/Jump.json"), None);
    assert!(is_asset_source("Luma/my.mod/sfx/jump.WAV") && is_asset_source("/Luma/m/a.flac") && is_asset_source("Luma/m/a.ogg"));
    assert!(!is_asset_source("AudioRes/Stream/Title.wav") && !is_asset_source("Luma/m/a.sound.json"));
}

/// The integer sources of data/sound/ (agents/luma/g/tools/make_sound_fixtures.py `source`, step for step).
fn fixture_source(channels: usize, wide: bool) -> Vec<i32> {
    let mut state: u64 = 12345;
    let mut out = Vec::new();
    for i in 0..8192u64 {
        for c in 0..channels as u64 {
            let t = (i * (37 + 11 * c)) % 400;
            let tri = (if t < 200 { t } else { 400 - t }) as i64 - 100;
            state = (state * 1_103_515_245 + 12345) % (1 << 31);
            let noise = ((state >> 16) % 2001) as i64 - 1000;
            let mut v = (tri * 150 + noise).clamp(-32768, 32767);
            if wide {
                v = v * 256 + ((state >> 8) % 256) as i64 - 128;
            }
            out.push(v as i32);
        }
    }
    out
}

#[test]
fn flac_from_a_real_encoder_decodes_to_its_exact_samples() {
    let stereo = fixture_source(2, false);
    for (name, bytes, looped) in [
        ("s16.flac", &include_bytes!("../../data/sound/s16.flac")[..], Some((1000, 6000))),
        ("s16-fixed.flac", &include_bytes!("../../data/sound/s16-fixed.flac")[..], None),
    ] {
        let f = crate::cook::flac::decode(bytes).unwrap();
        assert_eq!((f.channels, f.rate, f.bits, f.loop_points), (2, 44100, 16, looped), "{name}");
        assert!(f.samples == stereo, "{name}: first difference at {:?}", f.samples.iter().zip(&stereo).position(|(a, b)| a != b));
    }
    let f = crate::cook::flac::decode(include_bytes!("../../data/sound/m24.flac")).unwrap();
    assert_eq!((f.channels, f.rate, f.bits), (1, 48000, 24));
    let mono = fixture_source(1, true);
    assert!(f.samples == mono, "m24: first difference at {:?}", f.samples.iter().zip(&mono).position(|(a, b)| a != b));
    // Cooked: a 48 kHz 24-bit file keeps every sample to the nearest 16-bit step.
    let file = |_: &str| Some(include_bytes!("../../data/sound/m24.flac").to_vec());
    let (out, facts) = cook(br#"{"kind":"effect","file":"m24.flac"}"#, &Around { file: &file, game_sound: &|_| None }).unwrap();
    assert_eq!((facts.frames, facts.rate_in), (8192, 48000));
    let want: Vec<i16> = mono.iter().map(|&v| (f64::from(v) / 256.0).round().clamp(-32768.0, 32767.0) as i16).collect();
    assert_eq!(samples(&out), want);
}

#[test]
fn ogg_vorbis_from_a_real_encoder_decodes_close_to_its_source() {
    let pcm = decode(include_bytes!("../../data/sound/s16.ogg"), "s16.ogg").unwrap();
    let stereo = fixture_source(2, false);
    assert_eq!((pcm.channels, pcm.rate, pcm.loop_points), (2, 44100, Some((1000, 6000))));
    assert_eq!(pcm.samples.len(), stereo.len(), "every frame and no more");
    // Lossy: the error's power well under the signal's (quality 6 keeps it above 20 dB here).
    let signal: f64 = stereo.iter().map(|&v| (f64::from(v) / 32768.0).powi(2)).sum();
    let error: f64 = pcm.samples.iter().zip(&stereo).map(|(&a, &b)| (f64::from(a) - f64::from(b) / 32768.0).powi(2)).sum();
    let snr = 10.0 * (signal / error).log10();
    assert!(snr > 20.0, "{snr} dB");
}
