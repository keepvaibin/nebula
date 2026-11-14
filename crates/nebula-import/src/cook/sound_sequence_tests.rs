use super::*;
use crate::cook::refuse::said;
use crate::cook::sound::program_ops;
use crate::cook::sound_effect::cook_sound;

/// A format-0 MIDI file of `division` ticks a quarter, its one track's events given as (delta, bytes).
fn midi(division: u16, events: &[(u32, Vec<u8>)]) -> Vec<u8> {
    let mut track = Vec::new();
    for (delta, bytes) in events {
        let mut groups = vec![(delta & 0x7F) as u8];
        let mut v = delta >> 7;
        while v > 0 {
            groups.push((v & 0x7F) as u8 | 0x80);
            v >>= 7;
        }
        track.extend(groups.iter().rev());
        track.extend_from_slice(bytes);
    }
    track.extend_from_slice(&[0, 0xFF, 0x2F, 0]);
    let mut out = b"MThd".to_vec();
    out.extend_from_slice(&6u32.to_be_bytes());
    out.extend_from_slice(&[0, 0, 0, 1]);
    out.extend_from_slice(&division.to_be_bytes());
    out.extend_from_slice(b"MTrk");
    out.extend_from_slice(&(track.len() as u32).to_be_bytes());
    out.extend_from_slice(&track);
    out
}

/// 16 notes a step apart, an eighth each at 96 ticks a quarter, on program 5 at 120 BPM.
fn sixteen(extra: &[(u32, Vec<u8>)]) -> Vec<u8> {
    let mut events = vec![(0, vec![0xFF, 0x51, 3, 0x07, 0xA1, 0x20]), (0, vec![0xC0, 5])];
    events.extend_from_slice(extra);
    for i in 0..16u8 {
        events.push((0, vec![0x90, 60 + i, 100]));
        events.push((48, vec![0x80, 60 + i, 0]));
    }
    events.push((0, vec![0xFF, 0x2F, 0]));
    midi(96, &events)
}

const MAP: &str = r#""instruments":{"5":{"bank":2,"program":12}}"#;

fn cook_with(json: &str, bytes: Vec<u8>) -> Result<(Vec<(Made, Vec<u8>)>, SoundFacts)> {
    let file = move |name: &str| (name == "m.mid").then(|| bytes.clone());
    let seq = |name: &str| Some(name == "smg_boss.bms");
    cook_sound(json.as_bytes(), &Around { file: &file, game_sound: &|_| None, game_stream: &|_| None, game_sequence: &seq })
}

/// The program a song makes, read back by the game's sequence reader: its operations in address order.
fn ops_of(bytes: &[u8], loop_ticks: Option<(u64, u64)>) -> (Vec<(String, [u32; 5])>, u32, BTreeMap<&'static str, u32>) {
    let song = read_midi(bytes, "m.mid").unwrap();
    let map = instruments(Some(&serde_json::json!({"5": {"bank": 2, "program": 12}, "drums": {"bank": 3, "program": 0}}))).unwrap();
    let mut dropped = song.dropped.clone();
    let (bms, notes) = program(&song, &map, 1.0, loop_ticks, "m.mid", &mut dropped).unwrap();
    (program_ops(&bms).unwrap(), notes, dropped)
}

/// The second file of the round trip: a 16-note MIDI file's pitches, velocities, tempo, timebase and instrument, read
/// back from the program the game's sequence reader decodes.
#[test]
fn sixteen_notes_round_trip_through_the_games_sequence_reader() {
    let (ops, notes, dropped) = ops_of(&sixteen(&[]), None);
    assert_eq!(notes, 16);
    assert!(dropped.is_empty(), "{dropped:?}");
    let on: Vec<[u32; 3]> = ops.iter().filter(|(c, _)| c == "NoteOn").map(|(_, a)| [a[0], a[1], a[2]]).collect();
    let want: Vec<[u32; 3]> = (0..16).map(|i| [1, 60 + i, 100]).collect();
    assert_eq!(on, want, "slot, key, velocity");
    assert_eq!(ops.iter().filter(|(c, _)| c == "NoteOff").count(), 16);
    assert!(ops.iter().any(|(c, a)| c == "Tempo" && a[0] == 120));
    assert!(ops.iter().any(|(c, a)| c == "RegLoad" && a[0] == 0x62 && a[1] == 96), "the timebase");
    assert!(ops.iter().any(|(c, a)| c == "RegLoad" && a[0] == 0x64 && a[1] == 6), "the bend sense");
    assert!(ops.iter().any(|(c, a)| c == "BankProgram" && a[0] == 2 << 8 | 12), "program 5 as bank 2 program 12");
    assert!(ops.iter().any(|(c, _)| c == "OpenTrack"));
    assert_eq!(ops.iter().filter(|(c, _)| c == "Finish").count(), 2, "the root and the channel's track end");
    // Each note waits an eighth (48 ticks) before its off.
    let waits: Vec<u32> = ops.iter().filter(|(c, _)| c == "Wait").map(|(_, a)| a[0]).collect();
    assert!(waits.iter().filter(|w| **w == 48).count() >= 16, "{waits:?}");
    // Cooked, it is the game's sequence form.
    let (made, facts) = cook_with(&format!(r#"{{"kind":"sequence","file":"m.mid",{MAP}}}"#), sixteen(&[])).unwrap();
    assert_eq!(made[0].0, Made::Own(".nseq"));
    assert_eq!(&made[0].1[..4], b"NSEQ");
    assert_eq!((facts.kind.as_str(), facts.notes, facts.channels, facts.rate_in, facts.looped), ("sequence", 16, 1, 96, false));
}

/// A loop between markers: each track jumps back to where its state is set again; held notes are let go first.
#[test]
fn a_sequence_loops_between_its_markers() {
    let marks = [(0, b"\xFF\x06\x09loopStart".to_vec())];
    let mut song = sixteen(&marks);
    // loopEnd at tick 576: add it by cooking with an explicit loop instead, the markers giving the start.
    let (ops, _, _) = ops_of(&song, Some((0, 576)));
    let jumps: Vec<u32> = ops.iter().filter(|(c, _)| c == "Jump").map(|(_, a)| a[0]).collect();
    assert_eq!(jumps.len(), 2, "the root and the channel's track");
    let kinds: Vec<&str> = jumps.iter().map(|&i| ops[i as usize].0.as_str()).collect();
    assert!(kinds.contains(&"Tempo") && kinds.contains(&"BankProgram"), "{kinds:?}");
    assert_eq!(ops.iter().filter(|(c, _)| c == "Finish").count(), 0);
    // From the marker: `loop` absent loops from loopStart to the end.
    let (_, facts) = cook_with(&format!(r#"{{"kind":"sequence","file":"m.mid",{MAP}}}"#), song.clone()).unwrap();
    assert!(facts.looped);
    let (why, args) = said(&cook_with(&format!(r#"{{"kind":"sequence","file":"m.mid",{MAP},"loop":{{"start":0,"end":99999}}}}"#), song.split_off(0)).unwrap_err());
    assert_eq!(why, "Cook.Sound.LoopOutside", "{args:?}");
}

/// Outside the subset is left out and named; an eighth note at once on a channel is too.
#[test]
fn what_the_game_does_not_play_is_named() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    // Eight keys at once, let go 10 ticks later: the eighth has no slot.
    let chord: Vec<(u32, Vec<u8>)> = (0..8u8)
        .map(|k| (0, vec![0x90, 40 + k, 90]))
        .chain([(0, vec![0xB0, 64, 127]), (0, vec![0xD0, 9])])
        .chain((0..8u8).map(|k| (if k == 0 { 10 } else { 0 }, vec![0x80, 40 + k, 0])))
        .collect();
    let (_, notes, dropped) = ops_of(&sixteen(&chord), None);
    assert_eq!(notes, 16 + 7);
    assert_eq!(dropped.get("notes over 7 at once"), Some(&1));
    assert_eq!(dropped.get("controller"), Some(&1));
    assert_eq!(dropped.get("channel pressure"), Some(&1));
    crate::cook::refuse::take_notes();
    let (_, facts) = cook_with(&format!(r#"{{"kind":"sequence","file":"m.mid",{MAP}}}"#), sixteen(&chord)).unwrap();
    assert!(facts.dropped.contains("controller x 1"), "{facts:?}");
    let notes = crate::cook::refuse::said_notes();
    assert!(notes.iter().any(|(why, _)| *why == "Cook.Sound.MidiDropped"), "{notes:?}");
}

/// Refused by name: an unmapped program, frames-a-second time, a cut file, no notes, a bad map key, a replaced name
/// the game does not have; a replaced one the game has is written at its place.
#[test]
fn sequences_are_refused_by_name() {
    let json = |more: &str| format!(r#"{{"kind":"sequence","file":"m.mid",{MAP}{more}}}"#);
    let refused = |json: &str, bytes: Vec<u8>| said(&cook_with(json, bytes).unwrap_err()).0;
    assert_eq!(refused(r#"{"kind":"sequence","file":"m.mid"}"#, sixteen(&[])), "Cook.Sound.NoInstrument");
    let mut smpte = sixteen(&[]);
    smpte[12..14].copy_from_slice(&[0xE7, 0x28]);
    assert_eq!(refused(&json(""), smpte), "Cook.Sound.Midi");
    let whole = sixteen(&[]);
    assert_eq!(refused(&json(""), whole[..whole.len() - 20].to_vec()), "Cook.Sound.Cut");
    assert_eq!(refused(&json(""), midi(96, &[(0, vec![0xC0, 5])])), "Cook.Sound.Empty");
    assert_eq!(refused(&json(""), Vec::new()), "Cook.Sound.Empty");
    assert_eq!(refused(&json(""), b"RIFF....".to_vec()), "Cook.Sound.Midi");
    assert_eq!(refused(r#"{"kind":"sequence","file":"m.mid","instruments":{"200":{"bank":0,"program":0}}}"#, sixteen(&[])), "Cook.Sound.InstrumentKey");
    assert_eq!(refused(&json(r#","replace":"nope.bms""#), sixteen(&[])), "Cook.Sound.NoGameSequence");
    let (made, _) = cook_with(&json(r#","replace":"smg_boss.bms""#), sixteen(&[])).unwrap();
    assert_eq!(made[1].0, Made::Game("AudioRes/Seqs/JaiSeq.arc/smg_boss.bms".into()));
}

/// A voice line (layer 4) is an effect on the voice bus that must name the game line it replaces.
#[test]
fn a_voice_line_replaces_a_game_line() {
    let wav = {
        let mut out = b"RIFF\0\0\0\0WAVEfmt ".to_vec();
        for v in [16u32.to_le_bytes().to_vec(), 1u16.to_le_bytes().to_vec(), 1u16.to_le_bytes().to_vec(), 32000u32.to_le_bytes().to_vec(), 64000u32.to_le_bytes().to_vec(), 2u16.to_le_bytes().to_vec(), 16u16.to_le_bytes().to_vec()] {
            out.extend_from_slice(&v);
        }
        out.extend_from_slice(b"data");
        out.extend_from_slice(&200u32.to_le_bytes());
        out.extend_from_slice(&[1u8; 200]);
        let n = out.len() as u32 - 8;
        out[4..8].copy_from_slice(&n.to_le_bytes());
        out
    };
    let file = |name: &str| (name == "v.wav").then(|| wav.clone());
    let game = |name: &str| Some(name == "SE_PV_JUMP");
    let around = Around { file: &file, game_sound: &game, game_stream: &|_| None, game_sequence: &|_| None };
    let (made, facts) = cook_sound(br#"{"kind":"voice","file":"v.wav","replace":"SE_PV_JUMP"}"#, &around).unwrap();
    assert_eq!(made[0].0, Made::Own(".nsfx"));
    assert_eq!(made[0].1[49], 2, "the voice bus");
    assert_eq!((facts.kind.as_str(), facts.replaces.as_str()), ("voice", "SE_PV_JUMP"));
    let (why, _) = said(&cook_sound(br#"{"kind":"voice","file":"v.wav"}"#, &around).unwrap_err());
    assert_eq!(why, "Cook.Effect.Missing");
    let (why, _) = said(&cook_sound(br#"{"kind":"voice","file":"v.wav","replace":"SE_NONE"}"#, &around).unwrap_err());
    assert_eq!(why, "Cook.Sound.NoGameSound");
}
