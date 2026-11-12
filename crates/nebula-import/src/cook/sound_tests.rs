//! The sound cook's tests (`sound.rs`).
use super::*;

const ALL: [Code; 51] = {
    use Code::*;
    [
        NoteOn, NoteGate, NoteOff, LastNote, ParamRaw, ParamByte, OpenTrack, CloseTrack, Call, CallIf, Return, ReturnIf, Jump,
        JumpIf, JumpTable, CallTable, LoopStart, LoopEnd, ReadPort, WritePort, CheckImport, CheckExport, ParentWritePort,
        ChildWritePort, ParentReadPort, ChildReadPort, RegLoad, Reg, RegUnary, RegTable, Tempo, BankProgram, Bank, Program,
        EnvScale, EnvSet, SimpleAdsr, BusConnect, IirCutoff, IirSet, FirSet, Wait, InterruptTable, InterruptOn, InterruptOff,
        InterruptReturn, InterruptClear, InterruptTimer, Sync, Nop, Finish,
    ]
};

fn screaming(code: Code) -> String {
    let mut s = String::from("SEQ_");
    for (i, c) in format!("{code:?}").chars().enumerate() {
        if c.is_ascii_uppercase() && i > 0 {
            s.push('_');
        }
        s.push(c.to_ascii_uppercase());
    }
    s
}

#[test]
fn codes_are_the_games() {
    let header = include_str!("../../../../native/petari/include/Game/Sound/SoundData.hpp");
    let body = &header[header.find("enum SeqCode").unwrap()..header.find("SEQ_CODES").unwrap()];
    let names: Vec<&str> = body
        .lines()
        .filter_map(|l| l.trim().strip_prefix("SEQ_"))
        .map(|l| l.split(|c: char| !(c.is_ascii_alphanumeric() || c == '_')).next().unwrap())
        .collect();
    let ours: Vec<String> = ALL.iter().map(|c| screaming(*c)[4..].to_string()).collect();
    assert_eq!(names, ours);
    for (i, c) in ALL.iter().enumerate() {
        assert_eq!(*c as usize, i);
    }
}

#[test]
fn a_program_decodes_with_its_targets_as_operations() {
    #[rustfmt::skip]
    let b = [
        0xE0, 0x00, 0x78,             // 0x00: tempo 120
        0xC1, 0x02, 0x00, 0x00, 0x14, // 0x03: open track 2 at 0x14
        0xC3, 0x00, 0x00, 0x10,       // 0x08: call 0x10
        0xF0, 0x81, 0x00,             // 0x0C: wait 128
        0xFF,                         // 0x0F: finish
        0x3C, 0x03, 0x64,             // 0x10: note on (key 60, slot 3, velocity 100)
        0xC5,                         // 0x13: return
        0x40, 0x00, 0x50, 0x00,       // 0x14: gate (key 64, velocity 80, length 0)
        0xFF,                         // 0x18: finish
    ];
    let program = decode_program(&b, &[0]).unwrap();
    let codes: Vec<Code> = program.ops.iter().map(|o| o.code).collect();
    use Code::*;
    assert_eq!(codes, vec![Tempo, OpenTrack, Call, Wait, Finish, NoteOn, Return, NoteGate, Finish]);
    assert_eq!(program.ops[1].args[..2], [2, 7]);
    assert_eq!(program.ops[2].args[0], 5);
    assert_eq!(program.ops[3].args[0], 128);
    assert_eq!(program.ops[5].args[..3], [3, 60, 100]);
    assert_eq!(program.ops[6].args[0], 0x14); // where a return with no call goes on
    assert_eq!(program.ops[7].args[..4], [0, 0x40, 0x50, 0]);
    let file = program_file(&b, &[vec![0]]).unwrap();
    assert_eq!(&file[..4], b"NSEQ");
    let word = |i: usize| u32::from_le_bytes(file[4 * i..4 * i + 4].try_into().unwrap());
    assert_eq!(word(2), 9); // operations
    assert_eq!(word(6), 2); // tracks: the root and its child
    assert_eq!(word(7), 4); // note slots: slot 3
    assert_eq!(word(8), 1); // stack: one call
    assert_eq!(word(9), 0); // envelope points: none set
}

#[test]
fn the_envelope_bound_counts_through_the_ending_mode() {
    #[rustfmt::skip]
    let b = [
        0xE8, 0x00, 0x00, 0x00, 0x06, // 0x00: envelope of oscillator 0 at 0x06
        0xFF,                         // 0x05: finish
        0x00, 0x00, 0x00, 0x10, 0x7F, 0xFF, // 0x06: mode 0
        0x00, 0x01, 0x00, 0x10, 0x00, 0x00, // 0x0C: mode 1
        0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, // 0x12: mode 0xF ends it
    ];
    let file = program_file(&b, &[vec![0]]).unwrap();
    assert_eq!(word(&file, 9), 3);
    // No ending mode before the program's end.
    let b = [0xE8, 0x00, 0x00, 0x00, 0x06, 0xFF, 0x00, 0x00, 0x00, 0x10, 0x7F, 0xFF];
    assert!(program_file(&b, &[vec![0]]).is_err());
}

#[test]
fn the_stack_bound_counts_nested_calls_and_loops() {
    #[rustfmt::skip]
    let b = [
        0xCB, 0x00, 0x02,             // 0x00: loop 2
        0xC3, 0x00, 0x00, 0x09,       // 0x03: call 0x09
        0xCC,                         // 0x07: loop end
        0xFF,                         // 0x08: finish
        0xCB, 0x00, 0x03,             // 0x09: loop 3
        0xC3, 0x00, 0x00, 0x12,       // 0x0C: call 0x12
        0xCC,                         // 0x10: loop end
        0xC5,                         // 0x11: return
        0xC5,                         // 0x12: return
    ];
    let file = program_file(&b, &[vec![0]]).unwrap();
    // loop, call, loop, call.
    assert_eq!(word(&file, 8), 4);
    // A call to itself nests without end.
    let b = [0xC3, 0x00, 0x00, 0x00, 0xFF];
    assert!(program_file(&b, &[vec![0]]).is_err());
}

#[test]
fn a_register_prefix_marks_its_operands() {
    // 0x91 (two operands): mask 0x40 marks the second; parameter 5 from register 2.
    let op = decode(&[0x91, 0x40, 0xB9, 0x05, 0x02], 0).unwrap();
    assert_eq!(op.code, Code::ParamRaw);
    assert_eq!(op.registers, REGISTER_FORM | 2);
    assert_eq!(op.args[..3], [5, 2, 0]);
    assert_eq!(op.end, 5);
    // Note on by key, slot, velocity: the slot moves first, its register bit with it.
    let op = decode(&[0x91, 0x80, 0xB1, 0x07, 0x01, 0x7F], 0).unwrap();
    assert_eq!(op.args[..3], [1, 7, 0x7F]);
    assert_eq!(op.registers, REGISTER_FORM | 2);
}

#[test]
fn overlapping_code_is_refused() {
    // A jump into the middle of the tempo operation's operand.
    let b = [0xE0, 0xC7, 0x00, 0x00, 0x01, 0xFF];
    assert!(decode_program(&b, &[0, 1]).is_err());
}

fn word(file: &[u8], i: usize) -> u32 {
    u32::from_le_bytes(file[4 * i..4 * i + 4].try_into().unwrap())
}

/// The game's own sound data cooks whole. NEBULA_DISC_FILES: an extracted disc's files folder; NEBULA_SOUND_ARCHIVES:
/// more SMR.szs files (mods'), separated by ';'. cargo test -p nebula-import --lib the_disc_cooks -- --ignored
/// --nocapture
#[test]
#[ignore]
fn the_disc_cooks() {
    use super::super::archive;
    let read = |path: &str| {
        let bytes = std::fs::read(path).unwrap_or_else(|e| panic!("{path}: {e}"));
        if archive::is_compressed(&bytes) { archive::expand(&bytes).unwrap() } else { bytes }
    };
    let files = std::env::var("NEBULA_DISC_FILES").expect("NEBULA_DISC_FILES");
    let mut archives = vec![format!("{files}/AudioRes/SMR.szs")];
    if let Ok(more) = std::env::var("NEBULA_SOUND_ARCHIVES") {
        archives.extend(more.split(';').filter(|s| !s.is_empty()).map(str::to_string));
    }
    for path in &archives {
        let file = archive(&read(path)).unwrap_or_else(|e| panic!("{path}: {e:#}"));
        assert_eq!(archive(&file).unwrap(), file);
        let names = sound_names(&file).unwrap();
        println!("{path}: {} sound names", names.len());
        if path.ends_with("/AudioRes/SMR.szs") && path.starts_with(&files) {
            assert_eq!(names.len(), 3487);
            assert!(names.iter().any(|n| n == "SE_PM_JUMP_M") && names.iter().any(|n| n == "SE_SY_2P_ASSIST_JUMP"));
        }
        let effects = word(&file, 9) as usize;
        let seq = &file[effects..effects + word(&file, 10) as usize];
        println!(
            "{path}: {} bytes, sections {} groups {} items {}, banks {} wave banks {}, effects: {} groups {} entries {} ops, tracks {} slots {} stack {} envelope {}",
            file.len(), word(&file, 2), word(&file, 3), word(&file, 4), word(&file, 6), word(&file, 7),
            word(seq, 4), word(seq, 5), word(seq, 2), word(seq, 6), word(seq, 7), word(seq, 8), word(seq, 9)
        );
    }
    let (mut count, mut ops, mut tracks, mut slots, mut stack, mut envelope) = (0, 0, 0, 0, 0, 0);
    for member in archive::members(&read(&format!("{files}/AudioRes/Seqs/JaiSeq.arc"))).unwrap() {
        let name = format!("AudioRes/Seqs/JaiSeq.arc/{}", member.path.rsplit('/').next().unwrap());
        assert!(is_sequence_path(&name));
        let file = sequence(&member.bytes).unwrap_or_else(|e| panic!("{name}: {e:#}"));
        count += 1;
        ops += word(&file, 2);
        tracks = tracks.max(word(&file, 6));
        slots = slots.max(word(&file, 7));
        stack = stack.max(word(&file, 8));
        envelope = envelope.max(word(&file, 9));
    }
    println!("sequences {count}: {ops} ops, tracks at most {tracks}, slots at most {slots}, stack at most {stack}, envelope at most {envelope}");
    for member in archive::members(&read(&format!("{files}/AudioRes/Info/JaiMe.arc"))).unwrap() {
        if !member.path.to_ascii_lowercase().ends_with(".bme") {
            continue;
        }
        let file = me_program(&member.bytes).unwrap_or_else(|e| panic!("{}: {e:#}", member.path));
        let entries = word(&file, 2) as usize;
        let most = (0..entries).map(|i| word(&file, 5 + 3 * i)).max().unwrap_or(0);
        let stack = (0..entries).map(|i| word(&file, 6 + 3 * i)).max().unwrap_or(0);
        println!("{}: {entries} entries, {} bytes, tracks at most {most}, stack at most {stack}", member.path, word(&file, 3));
    }
}

#[test]
fn a_long_midi_value_reads_zero() {
    let mut r = Reader { b: &[0x81, 0x82, 0x83, 0x84, 0x05], p: 0 };
    assert_eq!(r.midi().unwrap(), 0);
    assert_eq!(r.p, 4);
}

#[test]
fn a_me_bound_counts_children_once_each() {
    // Addresses from the file's start. Entry (8): open 0 at A, open 1 at B, end. A (0x13): open 0 at B, end. B (0x19): end.
    #[rustfmt::skip]
    let bytes = [
        0, 0, 0, 1, 0, 0, 0, 8,       // one entry, at 8
        0x10, 0x00, 0x00, 0x00, 0x13, // 0x08
        0x10, 0x01, 0x00, 0x00, 0x19, // 0x0D
        0x24,                         // 0x12
        0x10, 0x00, 0x00, 0x00, 0x19, // 0x13
        0x24,                         // 0x18
        0x24,                         // 0x19
    ];
    let file = me_program(&bytes).unwrap();
    assert_eq!(&file[..4], b"NMES");
    let word = |i: usize| u32::from_le_bytes(file[4 * i..4 * i + 4].try_into().unwrap());
    assert_eq!(word(2), 1);
    assert_eq!(word(4), 8); // start
    assert_eq!(word(5), 4); // itself, child 0 (A and its child), child 1
    assert_eq!(word(6), 0); // stack: no call or loop
}

#[test]
fn a_me_stack_bound_counts_calls_loops_and_children() {
    // Entry (8): loop 2, call C, loop end, open 0 at D, end. C (0x16): return. D (0x17): call C, end.
    #[rustfmt::skip]
    let bytes = [
        0, 0, 0, 1, 0, 0, 0, 8,       // one entry, at 8
        0x33, 0x00, 0x02,             // 0x08: loop 2
        0x31, 0x00, 0x00, 0x16,       // 0x0B: call C
        0x34,                         // 0x0F: loop end
        0x10, 0x00, 0x00, 0x00, 0x17, // 0x10: open 0 at D
        0x24,                         // 0x15: end
        0x32,                         // 0x16: C: return
        0x31, 0x00, 0x00, 0x16,       // 0x17: D: call C
        0x24,                         // 0x1B: end
    ];
    let file = me_program(&bytes).unwrap();
    let word = |i: usize| u32::from_le_bytes(file[4 * i..4 * i + 4].try_into().unwrap());
    assert_eq!(word(6), 2); // the entry's loop and call; D's one call
    // A call to itself nests without end.
    let bytes = [0, 0, 0, 1, 0, 0, 0, 8, 0x31, 0x00, 0x00, 0x08, 0x24];
    assert!(me_program(&bytes).is_err());
}

/// The names of a cooked archive: items with a name, in order, each once; an item only in the table has none.
#[test]
fn a_cooked_archives_sound_names_are_read_in_order() {
    let names = b"SE_A\0SE_B\0\0\0";
    let mut file = b"NSND".to_vec();
    for v in [1u32, 1, 1, 4, 0, 0, 0, names.len() as u32, 0, 0, 3] {
        file.extend_from_slice(&v.to_le_bytes());
    }
    file.extend_from_slice(&[0; 32]); // a section and a group (not read)
    for (flags, name) in [(3u8, 5u32), (1, u32::MAX), (2, 0), (2, 5)] {
        let mut record = [0u8; 24];
        record[1] = flags;
        record[20..24].copy_from_slice(&name.to_le_bytes());
        file.extend_from_slice(&record);
    }
    file.extend_from_slice(names);
    assert_eq!(sound_names(&file).unwrap(), ["SE_B", "SE_A"]);
    // A name past the names, or an archive cut short, is an error, not a guess.
    let mut bad = file.clone();
    bad[48 + 32 + 20..48 + 32 + 24].copy_from_slice(&99u32.to_le_bytes());
    assert!(sound_names(&bad).is_err());
    assert!(sound_names(&file[..file.len() - 4]).is_err());
    assert!(sound_names(b"AA_<").is_err());
}
