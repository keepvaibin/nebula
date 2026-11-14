//! A mod's sequence (docs/modding/content/sound.md section 10): `<name>.sound.json` of kind
//! `sequence` names a Standard MIDI file beside it and maps its programs to the game's instruments. The cook writes the
//! song as a program of the game's sequence operations (the BMS operations sound.rs reads), then cooks that as the
//! game's own sequences are cooked: `Luma/<id>/<name>.nseq`, and with `replace` the same in place of a game sequence.
//!
//! The subset kept: notes (on, off, velocity), tempo, program change, volume (CC 7), pan (CC 10), pitch bend (±2
//! semitones), the loop (markers `loopStart`/`loopEnd`, CC 111 as the loop's start, or the source's `loop`), the end of
//! each track. Everything else is left out, each kind counted and named in the fact and a Note
//! (Cook.Sound.MidiDropped). More than 7 notes at once on a channel (the game's track has 7 note slots): the extra
//! notes are left out and counted the same way.

use super::particle_json::{as_f32, as_int, at_key, missing, object, only, text};
use super::refuse::refuse;
use super::sound_effect::{Around, Cooked, Made, SoundFacts};
use anyhow::Result;
use serde_json::Value;
use std::collections::BTreeMap;

const PARTS: [&str; 6] = ["kind", "file", "instruments", "volume", "loop", "replace"];
/// The game's track has 16 children; channel tracks are its children.
const CHANNELS: usize = 16;
/// Note slots 1 to 7 of a track (0 is the gate form).
const SLOTS: u8 = 7;
/// A MIDI file's most events: past this it is not a song the game plays.
const MOST_EVENTS: usize = 1 << 20;
/// The game's bend: the track's bend sense times a third of a semitone at full bend; 6 is MIDI's ±2 semitones.
const BEND_SENSE: u16 = 6;
/// The sequence register of the bend sense, and of the timebase (SeqTrack::writeReg 0x40 + 0x24, 0x40 + 0x22).
const REG_BEND_SENSE: u8 = 0x64;
const REG_TIMEBASE: u8 = 0x62;

#[derive(Clone, Copy, Debug, PartialEq)]
enum Event {
    On { key: u8, velocity: u8 },
    Off { key: u8 },
    Program(u8),
    Volume(u8),
    Pan(u8),
    Bend(i16),
    Tempo(u32),
    LoopStart,
    LoopEnd,
}

/// A MIDI file read: ticks a quarter note, each event at its tick with its channel (tempo and loop marks on none), in
/// file order within a tick; what was left out, by kind.
struct Song {
    division: u16,
    events: Vec<(u64, Option<u8>, Event)>,
    dropped: BTreeMap<&'static str, u32>,
}

fn bad(file: &str, detail: &str) -> anyhow::Error {
    refuse("Cook.Sound.Midi", &[("uri", file.to_string()), ("detail", detail.to_string())])
}

struct Bytes<'a> {
    b: &'a [u8],
    p: usize,
}

impl<'a> Bytes<'a> {
    fn u8(&mut self) -> Option<u8> {
        let v = *self.b.get(self.p)?;
        self.p += 1;
        Some(v)
    }
    fn take(&mut self, n: usize) -> Option<&'a [u8]> {
        let end = self.p.checked_add(n)?;
        let b: &'a [u8] = self.b;
        let s = b.get(self.p..end)?;
        self.p = end;
        Some(s)
    }
    /// A variable-length number: at most 4 bytes.
    fn var(&mut self) -> Option<u32> {
        let mut v = 0u32;
        for _ in 0..4 {
            let b = self.u8()?;
            v = v << 7 | u32::from(b & 0x7F);
            if b & 0x80 == 0 {
                return Some(v);
            }
        }
        None
    }
}

fn read_midi(b: &[u8], file: &str) -> Result<Song> {
    if b.is_empty() {
        return Err(refuse("Cook.Sound.Empty", &[("uri", file.to_string())]));
    }
    if b.get(..4) != Some(b"MThd") {
        return Err(bad(file, "not a Standard MIDI file (no MThd)"));
    }
    let mut r = Bytes { b, p: 4 };
    let cut = || refuse("Cook.Sound.Cut", &[("uri", file.to_string())]);
    let size = u32::from_be_bytes(r.take(4).ok_or_else(cut)?.try_into().unwrap()) as usize;
    let head = r.take(size.max(6)).ok_or_else(cut)?;
    let format = u16::from_be_bytes([head[0], head[1]]);
    let tracks = u16::from_be_bytes([head[2], head[3]]);
    let division = u16::from_be_bytes([head[4], head[5]]);
    if format > 1 {
        return Err(bad(file, &format!("format {format} (0 and 1 are read)")));
    }
    if division & 0x8000 != 0 || division == 0 {
        return Err(bad(file, "time in frames a second (ticks a quarter note are read)"));
    }
    let mut song = Song { division, events: Vec::new(), dropped: BTreeMap::new() };
    for _ in 0..tracks {
        let tag = r.take(4).ok_or_else(cut)?;
        let size = u32::from_be_bytes(r.take(4).ok_or_else(cut)?.try_into().unwrap()) as usize;
        let body = r.take(size).ok_or_else(cut)?;
        if tag != b"MTrk" {
            continue; // unknown chunks are skipped, as the format says
        }
        read_track(body, file, &mut song)?;
    }
    // Stable: a tick's events keep their file order, offs before ons so a slot freed at a tick is free for that tick.
    song.events.sort_by_key(|(tick, _, e)| (*tick, !matches!(e, Event::Off { .. })));
    Ok(song)
}

fn read_track(body: &[u8], file: &str, song: &mut Song) -> Result<()> {
    let mut r = Bytes { b: body, p: 0 };
    let cut = || refuse("Cook.Sound.Cut", &[("uri", file.to_string())]);
    let mut tick = 0u64;
    let mut status = 0u8;
    while r.p < body.len() {
        tick += u64::from(r.var().ok_or_else(cut)?);
        let mut first = r.u8().ok_or_else(cut)?;
        if first < 0x80 {
            if status < 0x80 {
                return Err(bad(file, "a running status with no status before it"));
            }
            r.p -= 1;
            first = status;
        } else if first < 0xF0 {
            status = first;
        }
        let channel = first & 15;
        let push = |e: Event, song: &mut Song| -> Result<()> {
            if song.events.len() >= MOST_EVENTS {
                return Err(bad(file, &format!("more than {MOST_EVENTS} events")));
            }
            song.events.push((tick, Some(channel), e));
            Ok(())
        };
        match first & 0xF0 {
            0x80 => {
                let d = r.take(2).ok_or_else(cut)?;
                push(Event::Off { key: d[0] & 0x7F }, song)?;
            }
            0x90 => {
                let d = r.take(2).ok_or_else(cut)?;
                let e = if d[1] == 0 { Event::Off { key: d[0] & 0x7F } } else { Event::On { key: d[0] & 0x7F, velocity: d[1] & 0x7F } };
                push(e, song)?;
            }
            0xA0 => {
                r.take(2).ok_or_else(cut)?;
                *song.dropped.entry("key pressure").or_default() += 1;
            }
            0xB0 => {
                let d = r.take(2).ok_or_else(cut)?;
                match d[0] {
                    7 => push(Event::Volume(d[1] & 0x7F), song)?,
                    10 => push(Event::Pan(d[1] & 0x7F), song)?,
                    111 => {
                        if song.events.len() < MOST_EVENTS {
                            song.events.push((tick, None, Event::LoopStart));
                        }
                    }
                    _ => *song.dropped.entry("controller").or_default() += 1,
                }
            }
            0xC0 => {
                let d = r.take(1).ok_or_else(cut)?;
                push(Event::Program(d[0] & 0x7F), song)?;
            }
            0xD0 => {
                r.take(1).ok_or_else(cut)?;
                *song.dropped.entry("channel pressure").or_default() += 1;
            }
            0xE0 => {
                let d = r.take(2).ok_or_else(cut)?;
                let value = (i32::from(d[1] & 0x7F) << 7 | i32::from(d[0] & 0x7F)) - 8192;
                push(Event::Bend(value as i16), song)?;
            }
            _ => match first {
                0xF0 | 0xF7 => {
                    let n = r.var().ok_or_else(cut)? as usize;
                    r.take(n).ok_or_else(cut)?;
                    *song.dropped.entry("system exclusive").or_default() += 1;
                }
                0xFF => {
                    let kind = r.u8().ok_or_else(cut)?;
                    let n = r.var().ok_or_else(cut)? as usize;
                    let data = r.take(n).ok_or_else(cut)?;
                    let mark = |e: Event, song: &mut Song| {
                        if song.events.len() < MOST_EVENTS {
                            song.events.push((tick, None, e));
                        }
                    };
                    match kind {
                        0x2F => break,
                        0x51 if n == 3 => mark(Event::Tempo(u32::from(data[0]) << 16 | u32::from(data[1]) << 8 | u32::from(data[2])), song),
                        0x06 | 0x01 => {
                            let words = String::from_utf8_lossy(data).to_ascii_lowercase().replace(['_', ' ', '-'], "");
                            if words == "loopstart" {
                                mark(Event::LoopStart, song);
                            } else if words == "loopend" {
                                mark(Event::LoopEnd, song);
                            }
                        }
                        _ => {} // names, text, signatures: nothing the game plays
                    }
                }
                _ => return Err(bad(file, &format!("status {first:#04x}"))),
            },
        }
    }
    Ok(())
}

/// The game instrument a MIDI program plays as: bank and program.
fn instruments(v: Option<&Value>) -> Result<BTreeMap<String, (u8, u8)>> {
    let mut out = BTreeMap::new();
    let Some(v) = v else { return Ok(out) };
    let map = object(v, "/instruments")?;
    for (key, value) in map {
        let at = at_key("/instruments", key);
        let valid = key == "drums" || key.parse::<u8>().is_ok_and(|n| n < 128);
        if !valid {
            return Err(refuse("Cook.Sound.InstrumentKey", &[("where", at), ("name", key.clone())]));
        }
        let m = object(value, &at)?;
        only(m, &at, &["bank", "program"])?;
        let bank = as_int(m.get("bank").ok_or_else(|| missing(&at, "bank"))?, &at_key(&at, "bank"), 0, 255)? as u8;
        let program = as_int(m.get("program").ok_or_else(|| missing(&at, "program"))?, &at_key(&at, "program"), 0, 255)? as u8;
        out.insert(key.clone(), (bank, program));
    }
    Ok(out)
}

/// A track's bytes and where its branches point inside it.
#[derive(Default)]
struct Track {
    bytes: Vec<u8>,
    /// The loop's start, an offset in `bytes`.
    loop_at: Option<usize>,
    /// The jump back's operand, an offset in `bytes`.
    jump_at: Option<usize>,
}

impl Track {
    fn wait(&mut self, mut ticks: u64) {
        while ticks > 0 {
            let step = ticks.min(0x0FFF_FFFF) as u32;
            self.bytes.push(0xF0);
            let mut groups = vec![(step & 0x7F) as u8];
            let mut v = step >> 7;
            while v > 0 {
                groups.push((v & 0x7F) as u8 | 0x80);
                v >>= 7;
            }
            self.bytes.extend(groups.iter().rev());
            ticks -= u64::from(step);
        }
    }
    fn reg(&mut self, reg: u8, value: u16) {
        self.bytes.extend_from_slice(&[0xD8, reg]);
        self.bytes.extend_from_slice(&value.to_be_bytes());
    }
    fn tempo(&mut self, micros: u32) {
        let bpm = (60_000_000.0 / f64::from(micros.max(1))).round().clamp(1.0, 65535.0) as u16;
        self.bytes.push(0xE0);
        self.bytes.extend_from_slice(&bpm.to_be_bytes());
    }
    fn instrument(&mut self, (bank, program): (u8, u8)) {
        self.bytes.extend_from_slice(&[0xE1, bank, program]);
    }
    fn param(&mut self, param: u8, value: u8) {
        self.bytes.extend_from_slice(&[0xB8, param, value]);
    }
    fn bend(&mut self, value: i16) {
        let v = (i32::from(value) * 4).clamp(-32767, 32767) as i16;
        self.bytes.extend_from_slice(&[0xB9, 1]);
        self.bytes.extend_from_slice(&v.to_be_bytes());
    }
    fn jump_back(&mut self) {
        self.bytes.push(0xC7);
        self.jump_at = Some(self.bytes.len());
        self.bytes.extend_from_slice(&[0, 0, 0]);
    }
}

/// A channel's state where the loop starts, so each time round starts as the first.
#[derive(Clone, Copy)]
struct State {
    program: u8,
    volume: Option<u8>,
    pan: Option<u8>,
    bend: i16,
}

/// The BMS program of a song: the root track sets the timebase and tempo, opens a track for each channel used, and
/// keeps the tempo; each channel's track plays its notes. Returns the program, the notes kept, and the end's tick.
fn program(song: &Song, map: &BTreeMap<String, (u8, u8)>, volume: f32, loop_ticks: Option<(u64, u64)>, file: &str, dropped: &mut BTreeMap<&'static str, u32>) -> Result<(Vec<u8>, u32)> {
    let end = song.events.iter().map(|(t, _, _)| *t).max().unwrap_or(0);
    let (loop_start, loop_end) = match loop_ticks {
        Some((s, e)) => (Some(s), e),
        None => (None, end),
    };
    let instrument_of = |channel: u8, program: u8| -> Result<(u8, u8)> {
        let key = if channel == 9 { "drums".to_string() } else { program.to_string() };
        map.get(&key).copied().ok_or_else(|| refuse("Cook.Sound.NoInstrument", &[("uri", file.to_string()), ("name", key)]))
    };
    let used: Vec<u8> = (0..CHANNELS as u8).filter(|c| song.events.iter().any(|(_, ch, e)| *ch == Some(*c) && matches!(e, Event::On { .. }))).collect();
    let mut notes = 0u32;
    let mut tracks = Vec::new();
    for &channel in &used {
        let mut t = Track::default();
        t.reg(REG_BEND_SENSE, BEND_SENSE);
        // The program its first note plays with: the last change at or before that note (0 when none).
        let first_note = song.events.iter().position(|(_, ch, e)| *ch == Some(channel) && matches!(e, Event::On { .. })).unwrap_or(0);
        let program = song.events[..first_note]
            .iter()
            .rev()
            .find_map(|(_, ch, e)| if let (Some(c), Event::Program(p)) = (ch, e) { (*c == channel).then_some(*p) } else { None })
            .unwrap_or(0);
        let mut state = State { program, volume: None, pan: None, bend: 0 };
        t.instrument(instrument_of(channel, program)?);
        let mut at = 0u64;
        let mut slots: [Option<u8>; SLOTS as usize] = [None; SLOTS as usize];
        let mut looped_here = false;
        let start_loop = |t: &mut Track, state: &State, slots: &mut [Option<u8>; SLOTS as usize]| -> Result<()> {
            t.loop_at = Some(t.bytes.len());
            t.instrument(instrument_of(channel, state.program)?);
            t.param(0, state.volume.unwrap_or(127));
            t.param(3, state.pan.unwrap_or(64));
            t.bend(state.bend);
            *slots = [None; SLOTS as usize];
            Ok(())
        };
        for (tick, ch, e) in &song.events {
            if *tick >= loop_end && loop_start.is_some() {
                break;
            }
            if let Some(ls) = loop_start {
                if !looped_here && *tick >= ls {
                    t.wait(ls - at);
                    at = ls;
                    // Notes still held are let go: the loop starts with none, as each time round does.
                    for (i, s) in slots.iter().enumerate() {
                        if s.is_some() {
                            t.bytes.push(0x81 + i as u8);
                        }
                    }
                    start_loop(&mut t, &state, &mut slots)?;
                    looped_here = true;
                }
            }
            if *ch != Some(channel) {
                continue;
            }
            t.wait(tick - at);
            at = *tick;
            match *e {
                Event::On { key, velocity } => match slots.iter().position(Option::is_none) {
                    Some(i) => {
                        slots[i] = Some(key);
                        t.bytes.extend_from_slice(&[key, i as u8 + 1, velocity]);
                        notes += 1;
                    }
                    // The wait before it stays: time still passes.
                    None => *dropped.entry("notes over 7 at once").or_default() += 1,
                },
                Event::Off { key } => {
                    if let Some(i) = slots.iter().position(|s| *s == Some(key)) {
                        slots[i] = None;
                        t.bytes.push(0x81 + i as u8);
                    }
                }
                Event::Program(p) if p != state.program => {
                    state.program = p;
                    t.instrument(instrument_of(channel, p)?);
                }
                Event::Program(_) => {}
                Event::Volume(v) => {
                    state.volume = Some(v);
                    t.param(0, v);
                }
                Event::Pan(v) => {
                    state.pan = Some(v);
                    t.param(3, v);
                }
                Event::Bend(v) => {
                    state.bend = v;
                    t.bend(v);
                }
                Event::Tempo(_) | Event::LoopStart | Event::LoopEnd => {}
            }
        }
        if let Some(ls) = loop_start {
            if !looped_here {
                t.wait(ls.saturating_sub(at));
                at = ls.max(at);
                for (i, s) in slots.iter().enumerate() {
                    if s.is_some() {
                        t.bytes.push(0x81 + i as u8);
                    }
                }
                start_loop(&mut t, &state, &mut slots)?;
            }
            t.wait(loop_end.saturating_sub(at));
            for (i, s) in slots.iter().enumerate() {
                if s.is_some() {
                    t.bytes.push(0x81 + i as u8);
                }
            }
            t.jump_back();
        } else {
            for (i, s) in slots.iter().enumerate() {
                if s.is_some() {
                    t.bytes.push(0x81 + i as u8);
                }
            }
            t.bytes.push(0xFF);
        }
        tracks.push(t);
    }
    // The root: timebase, the first tempo, the song's volume, each channel's track, then the tempo's changes.
    let mut root = Track::default();
    root.reg(REG_TIMEBASE, song.division);
    let tempos: Vec<(u64, u32)> = song.events.iter().filter_map(|(t, _, e)| if let Event::Tempo(m) = e { Some((*t, *m)) } else { None }).collect();
    let first = tempos.iter().find(|(t, _)| *t == 0).map_or(500_000, |(_, m)| *m);
    root.tempo(first);
    root.param(0, (volume.sqrt() * 127.0).round().clamp(0.0, 127.0) as u8);
    let mut opens = Vec::new();
    for (i, _) in tracks.iter().enumerate() {
        root.bytes.extend_from_slice(&[0xC1, i as u8]);
        opens.push(root.bytes.len());
        root.bytes.extend_from_slice(&[0, 0, 0]);
    }
    let mut at = 0u64;
    let mut current = first;
    let mut looped_here = false;
    for &(tick, micros) in &tempos {
        if loop_start.is_some() && tick >= loop_end {
            break;
        }
        if let Some(ls) = loop_start {
            if !looped_here && tick >= ls {
                root.wait(ls - at);
                at = ls;
                root.loop_at = Some(root.bytes.len());
                root.tempo(current);
                looped_here = true;
            }
        }
        if tick == 0 {
            continue;
        }
        root.wait(tick - at);
        at = tick;
        current = micros;
        root.tempo(micros);
    }
    if let Some(ls) = loop_start {
        if !looped_here {
            root.wait(ls.saturating_sub(at));
            at = ls.max(at);
            root.loop_at = Some(root.bytes.len());
            root.tempo(current);
        }
        root.wait(loop_end.saturating_sub(at));
        root.jump_back();
    } else {
        root.wait(end.saturating_sub(at));
        root.bytes.push(0xFF);
    }
    // Lay out: the root at 0, then each track; branch operands are addresses in the program, 24 bits.
    let mut out = root.bytes.clone();
    let mut starts = Vec::new();
    for t in &tracks {
        starts.push(out.len());
        out.extend_from_slice(&t.bytes);
    }
    if out.len() >= 1 << 24 {
        return Err(bad(file, "a song past 16 MiB of operations"));
    }
    let put = |out: &mut Vec<u8>, at: usize, target: usize| out[at..at + 3].copy_from_slice(&(target as u32).to_be_bytes()[1..]);
    for (i, &operand) in opens.iter().enumerate() {
        put(&mut out, operand, starts[i]);
    }
    if let (Some(j), Some(l)) = (root.jump_at, root.loop_at) {
        put(&mut out, j, l);
    }
    for (i, t) in tracks.iter().enumerate() {
        if let (Some(j), Some(l)) = (t.jump_at, t.loop_at) {
            put(&mut out, starts[i] + j, starts[i] + l);
        }
    }
    Ok((out, notes))
}

/// `<name>.sound.json` of kind `sequence` as the files it makes.
pub fn cook(map: &serde_json::Map<String, Value>, around: &Around) -> Result<Cooked> {
    only(map, "/", &PARTS)?;
    let file = super::sound_effect::file_of(map)?;
    let map_of = instruments(map.get("instruments"))?;
    let volume = match map.get("volume") {
        Some(v) => super::sound_effect::in_range("/volume", as_f32(v, "/volume")?, 0.0, 1.0)?,
        None => 1.0,
    };
    let replaces = match map.get("replace") {
        Some(v) => {
            let name = text(v, "/replace")?;
            if name.is_empty() || name.contains("..") || name.contains([':', '\\']) || name.starts_with('/') {
                return Err(refuse("Cook.Sound.SequenceName", &[("where", "/replace".into()), ("name", name.to_string())]));
            }
            if (around.game_sequence)(name) == Some(false) {
                return Err(refuse("Cook.Sound.NoGameSequence", &[("where", "/replace".into()), ("name", name.to_string())]));
            }
            name.to_string()
        }
        None => String::new(),
    };
    let bytes = (around.file)(file).ok_or_else(|| refuse("Cook.Effect.FileMissing", &[("where", "/file".into()), ("uri", file.to_string())]))?;
    let song = read_midi(&bytes, file)?;
    let end = song.events.iter().map(|(t, _, _)| *t).max().unwrap_or(0);
    if !song.events.iter().any(|(_, _, e)| matches!(e, Event::On { .. })) {
        return Err(refuse("Cook.Sound.Empty", &[("uri", file.to_string())]));
    }
    let marked = |which: Event| song.events.iter().find(|(_, _, e)| *e == which).map(|(t, _, _)| *t);
    let loop_ticks = match map.get("loop") {
        None => marked(Event::LoopStart).map(|s| (s, marked(Event::LoopEnd).unwrap_or(end))),
        Some(Value::Bool(false)) => None,
        Some(Value::Bool(true)) => Some((marked(Event::LoopStart).unwrap_or(0), marked(Event::LoopEnd).unwrap_or(end))),
        Some(v) => {
            let l = object(v, "/loop")?;
            only(l, "/loop", &["start", "end"])?;
            let start = as_int(l.get("start").ok_or_else(|| missing("/loop", "start"))?, &at_key("/loop", "start"), 0, i64::MAX)? as u64;
            let stop = match l.get("end") {
                Some(e) => as_int(e, &at_key("/loop", "end"), 0, i64::MAX)? as u64,
                None => end,
            };
            Some((start, stop))
        }
    };
    if let Some((s, e)) = loop_ticks {
        if s >= e || e > end {
            return Err(refuse("Cook.Sound.LoopOutside", &[("uri", file.to_string()), ("value", format!("{s}-{e}")), ("count", end.to_string())]));
        }
    }
    let mut dropped = song.dropped.clone();
    let (bms, notes) = program(&song, &map_of, volume, loop_ticks, file, &mut dropped)?;
    let nseq = super::sound::sequence(&bms).map_err(|e| refuse("Cook.Sound.Sequence", &[("uri", file.to_string()), ("detail", format!("{e:#}"))]))?;
    let dropped_text = dropped.iter().map(|(k, n)| format!("{k} x {n}")).collect::<Vec<_>>().join(", ");
    if !dropped_text.is_empty() {
        super::refuse::note("Cook.Sound.MidiDropped", &[("uri", file.to_string()), ("detail", dropped_text.clone())]);
    }
    let facts = SoundFacts {
        kind: "sequence".into(),
        frames: end.min(u64::from(u32::MAX)) as u32,
        channels: song.events.iter().filter_map(|(_, c, e)| matches!(e, Event::On { .. }).then_some(*c).flatten()).collect::<std::collections::BTreeSet<_>>().len() as u32,
        rate_in: u32::from(song.division),
        looped: loop_ticks.is_some(),
        loudness: f32::NAN,
        peak: f32::NEG_INFINITY,
        replaces: replaces.clone(),
        notes,
        dropped: dropped_text,
    };
    let mut files = vec![(Made::Own(".nseq"), nseq.clone())];
    if !replaces.is_empty() {
        files.push((Made::Game(format!("AudioRes/Seqs/JaiSeq.arc/{replaces}")), nseq));
    }
    Ok((files, facts))
}

#[cfg(test)]
#[path = "sound_sequence_tests.rs"]
mod tests;
