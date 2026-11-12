//! The sound data as the game plays it, in the forms native/petari/include/Game/Sound/SoundData.hpp and MeTrack.hpp
//! declare: host order, fixed records read in place.
//!
//! - NSND, the sound archive (AudioRes/SMR.szs, the disc's BAA): the sound table (BST) and its names (BSTN) as one tree,
//!   the effect programs (BSC) as one NSEQ, the instrument banks (IBNK) as NBNK, the wave systems (WSYS) as NWBK.
//! - NSEQ, a sequence program (the BSC, and each BMS of AudioRes/Seqs/JaiSeq.arc): its operations decoded along every
//!   path from its entries, immediate branch targets as operation indices. The program's bytes stay for the tables,
//!   envelopes and filters the operations name.
//! - NMES, the Me phrases (AudioRes/Info/JaiMe.arc's BME): the program's bytes, and per entry its start and the tracks
//!   it can have alive at once.
//!
//! What the console read past its own arrays is refused here, so the game never has to check it.

use super::audio_data::{self, WavePlace};
use anyhow::{bail, Context, Result};
use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};

const SEQ_MAGIC: &[u8; 4] = b"NSEQ";
const SEQ_VERSION: u32 = 2;
const BANK_MAGIC: &[u8; 4] = b"NBNK";
const BANK_VERSION: u32 = 1;
const WAVE_BANK_MAGIC: &[u8; 4] = b"NWBK";
const WAVE_BANK_VERSION: u32 = 1;
const ARCHIVE_MAGIC: &[u8; 4] = b"NSND";
const ARCHIVE_VERSION: u32 = 1;
const ME_MAGIC: &[u8; 4] = b"NMES";
const ME_VERSION: u32 = 2;

/// Where the game finds a wave system's AW files.
const WAVE_FOLDER: &str = "/AudioRes/Waves/";

// ---- reading the disc's big-endian data

fn be(b: &[u8], at: usize, width: usize) -> Result<u32> {
    let end = at.checked_add(width).context("an offset past any file")?;
    let bytes = b.get(at..end).with_context(|| format!("{width} bytes at {at:#x}, past the data's end ({:#x})", b.len()))?;
    Ok(bytes.iter().fold(0u32, |v, x| v << 8 | *x as u32))
}

fn be8(b: &[u8], at: usize) -> Result<u32> {
    be(b, at, 1)
}

fn be16(b: &[u8], at: usize) -> Result<u32> {
    be(b, at, 2)
}

fn be32(b: &[u8], at: usize) -> Result<u32> {
    be(b, at, 4)
}

fn bef32(b: &[u8], at: usize) -> Result<f32> {
    Ok(f32::from_bits(be32(b, at)?))
}

/// A word of a record at `base`: an offset from `base`.
fn offset(b: &[u8], base: usize, at: usize) -> Result<usize> {
    Ok(base + be32(b, at)? as usize)
}

fn text(b: &[u8], at: usize) -> Result<&[u8]> {
    let rest = b.get(at..).with_context(|| format!("a name at {at:#x} past the data's end"))?;
    let end = rest.iter().position(|c| *c == 0).with_context(|| format!("a name at {at:#x} with no end"))?;
    Ok(&rest[..end])
}

// ---- writing host order

fn put32(out: &mut Vec<u8>, v: u32) {
    out.extend_from_slice(&v.to_le_bytes());
}

fn puti32(out: &mut Vec<u8>, v: i32) {
    out.extend_from_slice(&v.to_le_bytes());
}

fn put16(out: &mut Vec<u8>, v: u16) {
    out.extend_from_slice(&v.to_le_bytes());
}

fn putf32(out: &mut Vec<u8>, v: f32) {
    out.extend_from_slice(&v.to_le_bytes());
}

fn align(out: &mut Vec<u8>, to: usize) {
    out.resize(out.len().div_ceil(to) * to, 0);
}

fn size32(n: usize, what: &str) -> Result<u32> {
    u32::try_from(n).with_context(|| format!("{what}: {n} is past 32 bits"))
}

/// Names, each once, each ended by 0.
#[derive(Default)]
struct Names {
    bytes: Vec<u8>,
    at: HashMap<Vec<u8>, u32>,
}

impl Names {
    fn add(&mut self, name: &[u8]) -> Result<u32> {
        if let Some(at) = self.at.get(name) {
            return Ok(*at);
        }
        let at = size32(self.bytes.len(), "names")?;
        self.bytes.extend_from_slice(name);
        self.bytes.push(0);
        self.at.insert(name.to_vec(), at);
        Ok(at)
    }
}

// ---- sequence programs (NSEQ)

/// SeqCode (SoundData.hpp), in its order.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u16)]
enum Code {
    NoteOn,
    NoteGate,
    NoteOff,
    LastNote,
    ParamRaw,
    ParamByte,
    OpenTrack,
    CloseTrack,
    Call,
    CallIf,
    Return,
    ReturnIf,
    Jump,
    JumpIf,
    JumpTable,
    CallTable,
    LoopStart,
    LoopEnd,
    ReadPort,
    WritePort,
    CheckImport,
    CheckExport,
    ParentWritePort,
    ChildWritePort,
    ParentReadPort,
    ChildReadPort,
    RegLoad,
    Reg,
    RegUnary,
    RegTable,
    Tempo,
    BankProgram,
    Bank,
    Program,
    EnvScale,
    EnvSet,
    SimpleAdsr,
    BusConnect,
    IirCutoff,
    IirSet,
    FirSet,
    Wait,
    InterruptTable,
    InterruptOn,
    InterruptOff,
    InterruptReturn,
    InterruptClear,
    InterruptTimer,
    Sync,
    Nop,
    Finish,
}

/// SeqOp::cRegisterForm: written with the register prefix.
const REGISTER_FORM: u16 = 0x8000;
/// The console's track had 16 children and a channel 8 note slots.
const CHILDREN: u32 = 16;
const NOTE_SLOTS: u32 = 8;

/// An operation from 0xA0 up as the console's parser reads it: its code, operand count and operand widths, two bits an
/// operand (0 a byte, 1 16 bits, 2 24 bits, 3 a register's number). None: an operation that does nothing and has no
/// operands.
fn command(byte: u8) -> Option<(Code, usize, u16)> {
    use Code::*;
    Some(match byte {
        0xB1 => (NoteOn, 3, 0),
        0xB2 => (NoteOff, 1, 0),
        0xB3 => (NoteGate, 4, 0x40),
        0xB4 => (LastNote, 1, 0),
        0xB8 => (ParamByte, 2, 0),
        0xB9 => (ParamRaw, 2, 4),
        0xBA => (ParamByte, 3, 0x10),
        0xBB => (ParamRaw, 3, 0x14),
        0xC1 => (OpenTrack, 2, 8),
        0xC2 => (CloseTrack, 1, 0),
        0xC3 => (Call, 1, 2),
        0xC4 => (CallIf, 2, 8),
        0xC5 => (Return, 0, 0),
        0xC6 => (ReturnIf, 1, 0),
        0xC7 => (Jump, 1, 2),
        0xC8 => (JumpIf, 2, 8),
        0xC9 => (JumpTable, 2, 0xB),
        0xCA => (CallTable, 2, 0xB),
        0xCB => (LoopStart, 1, 1),
        0xCC => (LoopEnd, 0, 0),
        0xD0 => (ReadPort, 2, 0),
        0xD1 => (WritePort, 2, 0xC),
        0xD2 => (CheckImport, 1, 0),
        0xD3 => (CheckExport, 1, 0),
        0xD4 => (ParentWritePort, 2, 0xC),
        0xD5 => (ChildWritePort, 2, 0xC),
        0xD6 => (ParentReadPort, 2, 0),
        0xD7 => (ChildReadPort, 2, 0),
        0xD8 => (RegLoad, 2, 4),
        0xD9 => (Reg, 3, 0x30),
        0xDA => (Reg, 3, 0x10),
        0xDB => (RegUnary, 2, 0),
        0xDC => (RegTable, 4, 0xE0),
        0xE0 => (Tempo, 1, 1),
        0xE1 => (BankProgram, 1, 1),
        0xE2 => (Bank, 1, 0),
        0xE3 => (Program, 1, 0),
        0xE7 => (EnvScale, 2, 4),
        0xE8 => (EnvSet, 2, 8),
        0xE9 => (SimpleAdsr, 5, 0x155),
        0xEA => (BusConnect, 2, 4),
        0xEB => (IirCutoff, 1, 0),
        0xEC => (IirSet, 4, 0x55),
        0xED => (FirSet, 1, 2),
        0xF0 => (Wait, 0, 0),
        0xF1 => (Wait, 1, 0),
        0xF3 => (InterruptTable, 1, 2),
        0xF4 => (InterruptOn, 1, 1),
        0xF5 => (InterruptOff, 1, 1),
        0xF6 => (InterruptReturn, 0, 0),
        0xF7 => (InterruptClear, 0, 0),
        0xF8 => (InterruptTimer, 2, 4),
        0xF9 => (Sync, 1, 1),
        0xFF => (Finish, 0, 0),
        _ => return None,
    })
}

#[derive(Clone, Debug)]
struct Op {
    at: usize,
    end: usize,
    code: Code,
    registers: u16,
    args: [u32; 5],
}

/// The operands that are branch targets: immediates become operation indices.
fn branch_args(code: Code) -> &'static [usize] {
    match code {
        Code::Call | Code::Jump => &[0],
        Code::CallIf | Code::JumpIf | Code::OpenTrack => &[1],
        _ => &[],
    }
}

/// Whether the next operation can run after this one. A return with no call to return to goes on with the program's
/// next bytes (its operand, as the game reads it), which is not where the program's code goes.
fn falls(code: Code) -> bool {
    !matches!(code, Code::Return | Code::Jump | Code::JumpTable | Code::Finish)
}

struct Reader<'a> {
    b: &'a [u8],
    p: usize,
}

impl Reader<'_> {
    fn read(&mut self, width: usize) -> Result<u32> {
        let v = be(self.b, self.p, width)?;
        self.p += width;
        Ok(v)
    }

    fn u8(&mut self) -> Result<u32> {
        self.read(1)
    }

    /// A BMS variable-length value: 7 bits a byte, at most four bytes (more reads 0).
    fn midi(&mut self) -> Result<u32> {
        let first = self.u8()?;
        if first & 0x80 == 0 {
            return Ok(first);
        }
        let mut value = first & 0x7F;
        let mut i = 0;
        loop {
            if i > 2 {
                return Ok(0);
            }
            value <<= 7;
            let byte = self.u8()?;
            value |= byte & 0x7F;
            if byte & 0x80 == 0 {
                return Ok(value);
            }
            i += 1;
        }
    }

    /// A print operation's text, to its 0, and a byte for each value it names.
    fn printf(&mut self) -> Result<()> {
        let mut values = 0usize;
        let mut i = 0;
        while i < 0x80 {
            let c = self.u8()?;
            if c == 0 {
                break;
            }
            if c == u32::from(b'\\') {
                if self.u8()? == 0 {
                    break;
                }
            } else if c == u32::from(b'%') {
                i += 1;
                if self.u8()? == 0 {
                    break;
                }
                values += 1;
            }
            i += 1;
        }
        if values > 4 {
            bail!("a print operation with {values} values (the console's had room for 4)");
        }
        for _ in 0..values {
            self.u8()?;
        }
        Ok(())
    }
}

/// The BMS operation at `at`.
fn decode(b: &[u8], at: usize) -> Result<Op> {
    let mut r = Reader { b, p: at };
    let mut op = Op { at, end: 0, code: Code::Nop, registers: 0, args: [0; 5] };
    let first = r.u8()?;
    if first < 0x80 {
        let flags = r.u8()?;
        let velocity = r.u8()?;
        if flags & 7 == 0 {
            op.code = Code::NoteGate;
            op.args = [flags, first, velocity, r.midi()?, 0];
        } else {
            op.code = Code::NoteOn;
            op.args[..3].copy_from_slice(&[flags & 7, first, velocity]);
        }
    } else if first & 0xF0 == 0x80 {
        if first & 7 != 0 {
            op.code = Code::NoteOff;
            op.args[0] = first & 7;
        }
    } else {
        let mut widths = 0u16;
        let mut cmd = first;
        if first & 0xF0 == 0x90 {
            let mut mask = r.u8()? as u8;
            let mut bits = 3u16;
            for _ in 0..(first & 7) + 1 {
                if mask & 0x80 != 0 {
                    widths |= bits;
                }
                mask <<= 1;
                bits = bits.wrapping_shl(2);
            }
            cmd = r.u8()?;
            if cmd < 0xA0 {
                bail!("a register prefix on {cmd:#04x} (the console read past its table)");
            }
            op.registers = REGISTER_FORM;
        }
        if cmd == 0xB0 {
            r.u8()?; // the extended table's one operation does nothing
        } else if cmd == 0xFD {
            r.printf()?;
        } else if let Some((code, count, shape)) = command(cmd as u8) {
            let mut kinds = shape | widths;
            let mut raw = [0u32; 5];
            let mut registers = 0u16;
            for (i, value) in raw.iter_mut().enumerate().take(count) {
                *value = match kinds & 3 {
                    0 => r.u8()?,
                    1 => r.read(2)?,
                    2 => r.read(3)?,
                    _ => {
                        registers |= 1 << i;
                        r.u8()?
                    }
                };
                kinds >>= 2;
            }
            op.code = code;
            match cmd {
                // Key, slot, velocity: the game's operation takes the slot first.
                0xB1 => {
                    op.args[..3].copy_from_slice(&[raw[1], raw[0], raw[2]]);
                    registers = (registers & 4) | (registers >> 1 & 1) | (registers << 1 & 2);
                }
                // A parameter with no time: 0.
                0xB8 | 0xB9 => op.args[..2].copy_from_slice(&raw[..2]),
                0xF0 => op.args[0] = r.midi()?,
                _ => op.args = raw,
            }
            op.registers |= registers;
        }
    }
    op.end = r.p;
    if op.code == Code::Return {
        op.args[0] = size32(op.end, "a program")?;
    }
    Ok(op)
}

struct Program {
    ops: Vec<Op>,
    /// Each table branch's operation index: the operation indices of its table's entries.
    tables: HashMap<usize, Vec<u32>>,
    index: HashMap<usize, u32>,
}

/// Every operation reachable from `roots`, each where the console's parser would read it. Code the programs reach only
/// through a register is refused (none of the game's do), and so are operations that overlap.
fn decode_program(b: &[u8], roots: &[usize]) -> Result<Program> {
    let mut found: BTreeMap<usize, Op> = BTreeMap::new();
    let mut tables: BTreeMap<usize, (usize, Vec<usize>)> = BTreeMap::new();
    let mut work: Vec<usize> = roots.to_vec();
    while let Some(at) = work.pop() {
        if found.contains_key(&at) {
            continue;
        }
        if at >= b.len() {
            bail!("code at {at:#x}, past the program's end ({:#x})", b.len());
        }
        let op = decode(b, at).with_context(|| format!("the operation at {at:#x}"))?;
        for &i in branch_args(op.code) {
            if op.registers >> i & 1 != 0 {
                bail!("a branch to an address in a register at {at:#x}");
            }
            work.push(op.args[i] as usize);
        }
        match op.code {
            Code::OpenTrack if op.registers & 1 != 0 => bail!("a track opened by a register's number at {at:#x}"),
            Code::OpenTrack if op.args[0] >= CHILDREN => bail!("track {} opened at {at:#x} (the console had {CHILDREN})", op.args[0]),
            Code::NoteOn if op.registers & 1 == 0 && op.args[0] >= NOTE_SLOTS => {
                bail!("note slot {} at {at:#x} (the console had {NOTE_SLOTS})", op.args[0])
            }
            Code::InterruptTable => bail!("an interrupt table at {at:#x}"),
            Code::JumpTable | Code::CallTable => {
                if op.registers & 2 != 0 {
                    bail!("a branch table at an address in a register at {at:#x}");
                }
                // Its entries until one names no byte of the program (the next bytes are code or data again).
                let base = op.args[1] as usize;
                let mut entries = Vec::new();
                while let Ok(t) = be(b, base + 3 * entries.len(), 3) {
                    if t as usize >= b.len() {
                        break;
                    }
                    entries.push(t as usize);
                }
                if entries.is_empty() {
                    bail!("a branch table at {base:#x} with no entry");
                }
                work.extend(&entries);
                tables.insert(at, (base, entries));
            }
            _ => {}
        }
        if falls(op.code) {
            work.push(op.end);
        }
        found.insert(at, op);
    }
    let mut end = 0;
    for op in found.values() {
        if op.at < end {
            bail!("operations that overlap at {:#x}", op.at);
        }
        end = op.end;
    }
    for (at, (base, entries)) in &tables {
        let to = base + 3 * entries.len();
        let before = found.range(..to).next_back().map(|(_, op)| op.end > *base).unwrap_or(false);
        if before {
            bail!("the branch table of {at:#x} at {base:#x} overlaps the program's code");
        }
    }
    let ops: Vec<Op> = found.into_values().collect();
    let index: HashMap<usize, u32> = ops.iter().enumerate().map(|(i, op)| (op.at, i as u32)).collect();
    let mut program = Program { ops, tables: HashMap::new(), index };
    for i in 0..program.ops.len() {
        if falls(program.ops[i].code) && program.ops.get(i + 1).map(|n| n.at) != Some(program.ops[i].end) {
            bail!("the operation at {:#x} runs into the program's end", program.ops[i].at);
        }
        for &a in branch_args(program.ops[i].code) {
            let target = program.ops[i].args[a] as usize;
            program.ops[i].args[a] = program.index[&target];
        }
    }
    for (at, (_, entries)) in tables {
        let i = program.index[&at] as usize;
        program.tables.insert(i, entries.iter().map(|t| program.index[t]).collect());
    }
    Ok(program)
}

/// The tracks a track started at operation `entry` can have alive at once: itself, and for each child number it opens, the
/// most any of the programs it opens there can.
fn track_bound(program: &Program, entry: u32, memo: &mut HashMap<u32, u32>, open: &mut HashSet<u32>) -> Result<u32> {
    if let Some(bound) = memo.get(&entry) {
        return Ok(*bound);
    }
    if !open.insert(entry) {
        bail!("a track that opens itself (at {:#x})", program.ops[entry as usize].at);
    }
    let mut children: BTreeMap<u32, BTreeSet<u32>> = BTreeMap::new();
    let mut seen: HashSet<u32> = HashSet::new();
    let mut work = vec![entry];
    while let Some(i) = work.pop() {
        if !seen.insert(i) {
            continue;
        }
        let op = &program.ops[i as usize];
        if falls(op.code) {
            work.push(i + 1);
        }
        if op.code == Code::OpenTrack {
            children.entry(op.args[0]).or_default().insert(op.args[1]);
        } else {
            work.extend(branch_args(op.code).iter().map(|a| op.args[*a]));
        }
        if let Some(entries) = program.tables.get(&(i as usize)) {
            work.extend(entries);
        }
    }
    let mut total = 1u32;
    for targets in children.values() {
        let mut most = 0;
        for t in targets {
            most = most.max(track_bound(program, *t, memo, open)?);
        }
        total = total.checked_add(most).context("a track bound past 32 bits")?;
    }
    open.remove(&entry);
    memo.insert(entry, total);
    Ok(total)
}

/// Past this many nested calls and loops a program is taken to nest without end.
const STACK_LIMIT: u32 = 4096;

/// The most call and loop entries a track started at operation `entry` holds at once. A call goes on at the next
/// operation once it returns, so a return ends its path; a loop's end goes on one entry shallower. A child track has its
/// own stack (its entry is counted where the file's entries are).
fn stack_bound(program: &Program, entry: u32) -> Result<u32> {
    let mut seen: HashSet<(u32, u32)> = HashSet::new();
    let mut work = vec![(entry, 0u32)];
    let mut most = 0;
    while let Some((i, depth)) = work.pop() {
        if !seen.insert((i, depth)) {
            continue;
        }
        most = most.max(depth);
        let op = &program.ops[i as usize];
        let at = op.at;
        let deeper = || -> Result<u32> {
            if depth >= STACK_LIMIT {
                bail!("calls or loops that nest without end (at {at:#x})");
            }
            Ok(depth + 1)
        };
        match op.code {
            Code::Call | Code::CallIf => work.push((op.args[branch_args(op.code)[0]], deeper()?)),
            Code::CallTable => {
                let d = deeper()?;
                work.extend(program.tables[&(i as usize)].iter().map(|t| (*t, d)));
            }
            Code::Jump | Code::JumpIf => work.push((op.args[branch_args(op.code)[0]], depth)),
            Code::JumpTable => work.extend(program.tables[&(i as usize)].iter().map(|t| (*t, depth))),
            _ => {}
        }
        if falls(op.code) {
            let next = match op.code {
                Code::LoopStart => deeper()?,
                Code::LoopEnd => depth.saturating_sub(1),
                _ => depth,
            };
            work.push((i + 1, next));
        }
    }
    Ok(most)
}

/// The points of the envelope an envelope operation names (6 bytes each), through the first whose mode (0xD, 0xE, 0xF) ends it.
fn envelope_points(data: &[u8], op: &Op) -> Result<u32> {
    if op.registers & 2 != 0 {
        bail!("an envelope at an address in a register at {:#x}", op.at);
    }
    let start = op.args[1] as usize;
    let mut points = 0u32;
    loop {
        let mode = be16(data, start + 6 * points as usize).with_context(|| format!("the envelope of {:#x} at {start:#x}", op.at))? as i16;
        be16(data, start + 6 * points as usize + 4).with_context(|| format!("the envelope of {:#x} at {start:#x}", op.at))?;
        points += 1;
        if mode >= 0xd {
            return Ok(points);
        }
    }
}

/// A program as NSEQ: its bytes `data`, entered at the byte offsets of `groups`.
fn program_file(data: &[u8], groups: &[Vec<usize>]) -> Result<Vec<u8>> {
    let roots: Vec<usize> = groups.iter().flatten().copied().collect();
    let program = decode_program(data, &roots)?;
    let entries: Vec<u32> = roots.iter().map(|at| program.index[at]).collect();
    let mut memo = HashMap::new();
    let mut open = HashSet::new();
    let mut tracks = 1;
    for e in &entries {
        tracks = tracks.max(track_bound(&program, *e, &mut memo, &mut open)?);
    }
    let mut slots = 1;
    let mut stack = 0;
    let mut envelope = 0;
    for e in &entries {
        stack = stack.max(stack_bound(&program, *e)?);
    }
    for op in &program.ops {
        if op.code == Code::NoteOn {
            slots = slots.max(if op.registers & 1 != 0 { NOTE_SLOTS } else { op.args[0] + 1 });
        }
        if op.code == Code::OpenTrack {
            stack = stack.max(stack_bound(&program, op.args[1])?);
        }
        if op.code == Code::EnvSet {
            envelope = envelope.max(envelope_points(data, op)?);
        }
    }
    let mut out = Vec::new();
    out.extend_from_slice(SEQ_MAGIC);
    put32(&mut out, SEQ_VERSION);
    put32(&mut out, size32(program.ops.len(), "operations")?);
    put32(&mut out, size32(data.len(), "a program")?);
    put32(&mut out, size32(groups.len(), "groups")?);
    put32(&mut out, size32(entries.len(), "entries")?);
    put32(&mut out, tracks);
    put32(&mut out, slots);
    put32(&mut out, stack);
    put32(&mut out, envelope);
    out.extend_from_slice(&[0; 8]);
    let mut first = 0;
    put32(&mut out, 0);
    for g in groups {
        first += g.len();
        put32(&mut out, size32(first, "entries")?);
    }
    for e in &entries {
        put32(&mut out, *e);
    }
    for op in &program.ops {
        put32(&mut out, size32(op.at, "a program")?);
    }
    for op in &program.ops {
        put16(&mut out, op.code as u16);
        put16(&mut out, op.registers);
        for a in op.args {
            put32(&mut out, a);
        }
    }
    out.extend_from_slice(data);
    Ok(out)
}

/// Whether bytes are a cooked file of `magic`: this version's, or refused as another's.
fn cooked(bytes: &[u8], magic: &[u8; 4], version: u32) -> Result<bool> {
    if bytes.len() < 8 || &bytes[..4] != magic {
        return Ok(false);
    }
    let found = u32::from_le_bytes(bytes[4..8].try_into().unwrap());
    if found != version {
        bail!("a {} file of version {found}, not {version} (import the game again)", String::from_utf8_lossy(magic));
    }
    Ok(true)
}

/// A BMS program (a member of AudioRes/Seqs/JaiSeq.arc) as NSEQ, entered at its first byte.
pub fn sequence(bytes: &[u8]) -> Result<Vec<u8>> {
    if cooked(bytes, SEQ_MAGIC, SEQ_VERSION)? {
        return Ok(bytes.to_vec());
    }
    program_file(bytes, &[vec![0]])
}

/// Whether a file is a member of the sequence archive.
pub fn is_sequence_path(name: &str) -> bool {
    let lower = name.to_ascii_lowercase();
    lower.rsplit_once('/').is_some_and(|(folder, _)| folder.ends_with("jaiseq.arc"))
}

// ---- Me programs (NMES)

/// A Me operation: where the next one starts, whether the program goes on to it, where it can branch, a track it opens.
struct MeOp {
    code: u32,
    end: usize,
    goes_on: bool,
    targets: Vec<usize>,
    open: Option<(u32, usize)>,
}

/// The console's Me track had 8 children and 64 registers.
const ME_CHILDREN: u32 = 8;
const ME_REGISTERS: u32 = 64;

/// The operation at `at`, as MeTrack::parse reads it.
fn me_op(b: &[u8], at: usize) -> Result<MeOp> {
    let code = be8(b, at)?;
    let arg = |o: usize, width: usize| be(b, at + o, width);
    let mut op = MeOp { code, end: at + 1, goes_on: true, targets: Vec::new(), open: None };
    let operands = match code {
        0x00 | 0x01 | 0x11 | 0x20 | 0x2A | 0x50..=0x54 | 0x58 | 0x59 | 0x5C | 0x60 | 0x84 | 0x86 | 0x87 => 1,
        0x02 | 0x03 | 0x33 | 0x5A | 0x5B | 0x5D | 0x68 | 0x69 | 0x8A => 2,
        0x28 | 0x30 | 0x31 | 0x35 | 0x36 | 0x3A | 0x3B | 0x78 | 0x79 | 0x81..=0x83 | 0x90 | 0x91 => 3,
        0x10 | 0x40 => 4,
        0x41 | 0x42 => 5,
        0x43 => 6,
        0x04 => 10,
        0xA0.. => bail!("Me operation {code:#04x} at {at:#x} (the console read past its table)"),
        _ => 0,
    };
    op.end = at + 1 + operands;
    if op.end > b.len() {
        bail!("a Me operation at {at:#x} past the program's end");
    }
    match code {
        0x10 => {
            let child = arg(1, 1)?;
            if child >= ME_CHILDREN {
                bail!("Me track {child} opened at {at:#x} (the console had {ME_CHILDREN})");
            }
            op.open = Some((child, arg(2, 3)? as usize));
        }
        0x11 | 0x84 if arg(1, 1)? >= ME_CHILDREN => bail!("Me track {} named at {at:#x} (the console had {ME_CHILDREN})", arg(1, 1)?),
        0x69 if arg(1, 1)? >= ME_CHILDREN => bail!("Me child table entry {} at {at:#x} (the console had {ME_CHILDREN})", arg(1, 1)?),
        0x68 | 0x87 if arg(1, 1)? >= ME_REGISTERS => bail!("Me register {} at {at:#x} (the console had {ME_REGISTERS})", arg(1, 1)?),
        0x81..=0x83 => {
            let list = arg(1, 3)? as usize;
            let count = be8(b, list)? as usize;
            if list + 2 + count > b.len() {
                bail!("a Me note list at {list:#x} past the program's end");
            }
        }
        0x30 | 0x31 | 0x35 | 0x36 | 0x3A | 0x3B | 0x40 => op.targets.push(arg(1, 3)? as usize),
        _ => {}
    }
    op.goes_on = !matches!(code, 0x24 | 0x30 | 0x32 | 0x38 | 0x71);
    Ok(op)
}

fn me_bound(b: &[u8], start: usize, memo: &mut HashMap<usize, u32>, open: &mut HashSet<usize>) -> Result<u32> {
    if let Some(bound) = memo.get(&start) {
        return Ok(*bound);
    }
    if !open.insert(start) {
        bail!("a Me track that opens itself (at {start:#x})");
    }
    let mut children: BTreeMap<u32, BTreeSet<usize>> = BTreeMap::new();
    let mut seen: HashSet<usize> = HashSet::new();
    let mut work = vec![start];
    while let Some(at) = work.pop() {
        if !seen.insert(at) {
            continue;
        }
        let op = me_op(b, at).with_context(|| format!("the Me operation at {at:#x}"))?;
        if op.goes_on {
            work.push(op.end);
        }
        work.extend(&op.targets);
        if let Some((child, target)) = op.open {
            children.entry(child).or_default().insert(target);
        }
    }
    let mut total = 1u32;
    for targets in children.values() {
        let mut most = 0;
        for t in targets {
            most = most.max(me_bound(b, *t, memo, open)?);
        }
        total += most;
    }
    open.remove(&start);
    memo.insert(start, total);
    Ok(total)
}

/// The most call and loop entries a Me track started at `start` holds at once, as stack_bound: calls 0x31, 0x3A, 0x3B and a
/// note's definition 0x40; loop start 0x33, loop end 0x34. A child track (0x10) has its own stack.
fn me_stack_bound(b: &[u8], start: usize, memo: &mut HashMap<usize, u32>) -> Result<u32> {
    if let Some(bound) = memo.get(&start) {
        return Ok(*bound);
    }
    let mut seen: HashSet<(usize, u32)> = HashSet::new();
    let mut children: BTreeSet<usize> = BTreeSet::new();
    let mut work = vec![(start, 0u32)];
    let mut most = 0;
    while let Some((at, depth)) = work.pop() {
        if !seen.insert((at, depth)) {
            continue;
        }
        most = most.max(depth);
        let op = me_op(b, at).with_context(|| format!("the Me operation at {at:#x}"))?;
        let deeper = || -> Result<u32> {
            if depth >= STACK_LIMIT {
                bail!("Me calls or loops that nest without end (at {at:#x})");
            }
            Ok(depth + 1)
        };
        let into = if matches!(op.code, 0x31 | 0x3A | 0x3B | 0x40) { deeper()? } else { depth };
        work.extend(op.targets.iter().map(|t| (*t, into)));
        if let Some((_, target)) = op.open {
            children.insert(target);
        }
        if op.goes_on {
            let next = match op.code {
                0x33 => deeper()?,
                0x34 => depth.saturating_sub(1),
                _ => depth,
            };
            work.push((op.end, next));
        }
    }
    memo.insert(start, most);
    for c in children {
        if !memo.contains_key(&c) {
            memo.insert(c, 0);
            let child = me_stack_bound(b, c, memo)?;
            memo.insert(c, child);
        }
        most = most.max(memo[&c]);
    }
    memo.insert(start, most);
    Ok(most)
}

/// A BME (count, start offsets, the program; its addresses from the file's start) as NMES.
pub fn me_program(bytes: &[u8]) -> Result<Vec<u8>> {
    if cooked(bytes, ME_MAGIC, ME_VERSION)? {
        return Ok(bytes.to_vec());
    }
    let count = be32(bytes, 0)? as usize;
    let mut memo = HashMap::new();
    let mut open = HashSet::new();
    let mut stacks = HashMap::new();
    let mut entries = Vec::new();
    for i in 0..count {
        let start = be32(bytes, 4 + 4 * i)? as usize;
        let bound = me_bound(bytes, start, &mut memo, &mut open).with_context(|| format!("Me entry {i}"))?;
        let stack = me_stack_bound(bytes, start, &mut stacks).with_context(|| format!("Me entry {i}"))?;
        entries.push((size32(start, "a Me program")?, bound, stack));
    }
    let mut out = Vec::new();
    out.extend_from_slice(ME_MAGIC);
    put32(&mut out, ME_VERSION);
    put32(&mut out, size32(count, "Me entries")?);
    put32(&mut out, size32(bytes.len(), "a Me program")?);
    for (start, bound, stack) in entries {
        put32(&mut out, start);
        put32(&mut out, bound);
        put32(&mut out, stack);
    }
    out.extend_from_slice(bytes);
    Ok(out)
}

// ---- banks (NBNK)

#[derive(Default)]
struct BankOut {
    programs: Vec<u8>,
    keys: Vec<u8>,
    velos: Vec<u8>,
    percs: Vec<u8>,
    effects: Vec<u8>,
    counts: [u32; 5], // programs, keys, velos, percs, effects
}

impl BankOut {
    /// Effects named by `count` offsets at `at` (from the bank's start): their first index and count.
    fn effects(&mut self, b: &[u8], base: usize, at: usize, count: usize) -> Result<(u32, u32)> {
        let first = self.counts[4];
        for e in 0..count {
            let record = offset(b, base, at + 4 * e)?;
            let kind = b.get(record..record + 4).context("a bank effect past the archive")?;
            match kind {
                b"Rand" => {
                    put32(&mut self.effects, 0);
                    put32(&mut self.effects, be8(b, record + 4)?);
                    putf32(&mut self.effects, bef32(b, record + 8)?);
                    putf32(&mut self.effects, bef32(b, record + 0xC)?);
                    put32(&mut self.effects, 0);
                    put32(&mut self.effects, 0);
                }
                b"Sens" => {
                    put32(&mut self.effects, 1);
                    put32(&mut self.effects, be8(b, record + 4)?);
                    putf32(&mut self.effects, bef32(b, record + 8)?);
                    putf32(&mut self.effects, bef32(b, record + 0xC)?);
                    put32(&mut self.effects, be8(b, record + 5)?);
                    put32(&mut self.effects, be8(b, record + 6)?);
                }
                _ => bail!("a bank effect at {:#x} of neither kind", record - base),
            }
            self.counts[4] += 1;
        }
        Ok((first, count as u32))
    }

    /// Velocity regions at `at` (a count, then 16 bytes each: the high velocity's byte, a word whose low half is the
    /// wave id, volume, pitch): their first index and count, and where the list ends.
    fn velos(&mut self, b: &[u8], at: usize) -> Result<(u32, u32, usize)> {
        let first = self.counts[2];
        let count = be32(b, at)? as usize;
        let mut p = at + 4;
        for _ in 0..count {
            puti32(&mut self.velos, be8(b, p)? as i32);
            puti32(&mut self.velos, (be32(b, p + 4)? & 0xFFFF) as i32);
            putf32(&mut self.velos, bef32(b, p + 8)?);
            putf32(&mut self.velos, bef32(b, p + 0xC)?);
            p += 16;
        }
        self.counts[2] += count as u32;
        Ok((first, count as u32, p))
    }
}

/// An instrument bank (IBNK version 1) at `base`, of wave bank `wave_bank`, as NBNK.
fn bank_file(b: &[u8], base: usize, wave_bank: u32) -> Result<Vec<u8>> {
    if b.get(base..base + 4) != Some(b"IBNK".as_slice()) {
        bail!("no IBNK magic");
    }
    let size = be32(b, base + 4)? as usize;
    let number = be32(b, base + 8)?;
    let version = be32(b, base + 0xC)?;
    if version != 1 {
        bail!("bank version {version}");
    }
    // The chunks as findChunk walks them, the first of each kind: each that starts before the bank's stated end (a last
    // one may run past it).
    let mut chunks: HashMap<[u8; 4], (usize, usize)> = HashMap::new();
    let mut at = base + 0x20;
    while at < base + size {
        let kind: [u8; 4] = b.get(at..at + 4).context("a bank chunk past the archive")?.try_into().unwrap();
        let length = be32(b, at + 4)? as usize;
        if at + 8 + length > b.len() {
            bail!("bank chunk {:?} past the archive", String::from_utf8_lossy(&kind));
        }
        chunks.entry(kind).or_insert((at + 8, length));
        at = (at + 0xB + length) & !3;
    }
    let chunk = |kind: &[u8; 4]| chunks.get(kind).copied().with_context(|| format!("a bank with no {} chunk", String::from_utf8_lossy(kind)));
    let (envelopes, envelope_bytes) = chunk(b"ENVT")?;
    let (oscillators, _) = chunk(b"OSCT")?;
    let (list, _) = chunk(b"LIST")?;

    let mut points = Vec::with_capacity(envelope_bytes);
    for p in (envelopes..envelopes + (envelope_bytes & !1)).step_by(2) {
        put16(&mut points, be16(b, p)? as u16);
    }
    align(&mut points, 4);
    let osc_count = be32(b, oscillators)? as usize;
    let mut oscs = Vec::new();
    for i in 0..osc_count {
        let r = oscillators + 4 + 0x1C * i;
        let table = be32(b, r + 0xC)? as usize;
        let release = be32(b, r + 0x10)? as usize;
        if table >= envelope_bytes || release >= envelope_bytes {
            bail!("oscillator {i}'s envelopes past the bank's points");
        }
        put32(&mut oscs, be8(b, r + 4)?);
        putf32(&mut oscs, bef32(b, r + 8)?);
        puti32(&mut oscs, table as i32);
        puti32(&mut oscs, release as i32);
        putf32(&mut oscs, bef32(b, r + 0x14)?);
        putf32(&mut oscs, bef32(b, r + 0x18)?);
    }

    let mut out = BankOut::default();
    let programs = be32(b, list)? as usize;
    for i in 0..programs {
        let o = be32(b, list + 4 + 4 * i)? as usize;
        let mut program = [0u32; 9]; // kind, volume, pitch, osc 0, osc 1, effect first and count, first, count
        program[3] = u32::MAX;
        program[4] = u32::MAX;
        if o != 0 {
            let record = base + o;
            match b.get(record..record + 4).context("a bank record past the archive")? {
                b"Inst" => {
                    let mut p = record + 4;
                    let count = be32(b, p)? as usize;
                    if count > 2 {
                        bail!("instrument {i} with {count} oscillators (the console had room for 2)");
                    }
                    for j in 0..count {
                        let index = be32(b, p + 4 + 4 * j)?;
                        if index as usize >= osc_count {
                            bail!("instrument {i}'s oscillator {index} of {osc_count}");
                        }
                        program[3 + j] = index;
                    }
                    p += 4 + 4 * count;
                    let effects = be32(b, p)? as usize;
                    (program[5], program[6]) = out.effects(b, base, p + 4, effects)?;
                    p += 4 + 4 * effects;
                    let keys = be32(b, p)? as usize;
                    p += 4;
                    program[7] = out.counts[1];
                    program[8] = keys as u32;
                    for _ in 0..keys {
                        let high = be32(b, p)? >> 24;
                        let (first, count, next) = out.velos(b, p + 4)?;
                        puti32(&mut out.keys, high as i32);
                        put32(&mut out.keys, first);
                        put32(&mut out.keys, count);
                        out.counts[1] += 1;
                        p = next;
                    }
                    program[0] = 1;
                    program[1] = bef32(b, p)?.to_bits();
                    program[2] = bef32(b, p + 4)?.to_bits();
                }
                b"Perc" => {
                    let count = be32(b, record + 4)? as usize;
                    program[0] = 2;
                    program[7] = out.counts[3];
                    program[8] = count as u32;
                    for j in 0..count {
                        let o = be32(b, record + 8 + 4 * j)? as usize;
                        let mut perc = Vec::with_capacity(36);
                        if o == 0 {
                            perc.resize(36, 0);
                        } else {
                            let m = base + o;
                            if b.get(m..m + 4) != Some(b"Pmap".as_slice()) {
                                bail!("drum {j} of program {i} is no Pmap");
                            }
                            let effects = be32(b, m + 0x10)? as usize;
                            let (effect_first, effect_count) = out.effects(b, base, m + 0x14, effects)?;
                            let (velo_first, velo_count, _) = out.velos(b, m + 0x14 + 4 * effects)?;
                            put32(&mut perc, 1);
                            putf32(&mut perc, bef32(b, m + 4)?);
                            putf32(&mut perc, bef32(b, m + 8)?);
                            putf32(&mut perc, be8(b, m + 0xC)? as f32 / 127.0);
                            put32(&mut perc, be16(b, m + 0xE)?);
                            put32(&mut perc, effect_first);
                            put32(&mut perc, effect_count);
                            put32(&mut perc, velo_first);
                            put32(&mut perc, velo_count);
                        }
                        out.percs.extend_from_slice(&perc);
                        out.counts[3] += 1;
                    }
                }
                kind => bail!("bank record {i} of kind {:?}", String::from_utf8_lossy(kind)),
            }
        }
        for v in program {
            put32(&mut out.programs, v);
        }
        out.counts[0] += 1;
    }

    let mut file = Vec::new();
    file.extend_from_slice(BANK_MAGIC);
    put32(&mut file, BANK_VERSION);
    put32(&mut file, number);
    put32(&mut file, wave_bank);
    for c in out.counts {
        put32(&mut file, c);
    }
    put32(&mut file, osc_count as u32);
    put32(&mut file, size32(points.len(), "envelope points")?);
    put32(&mut file, 0);
    for part in [&out.programs, &out.keys, &out.velos, &out.percs, &out.effects, &oscs, &points] {
        file.extend_from_slice(part);
    }
    Ok(file)
}

// ---- wave banks (NWBK)

/// A wave system (WSYS) at `base` as NWBK, each wave's place the cooked AW file's (`layout`).
fn wave_bank_file(b: &[u8], base: usize, number: u32, layout: &BTreeMap<String, Vec<WavePlace>>) -> Result<Vec<u8>> {
    if b.get(base..base + 4) != Some(b"WSYS".as_slice()) {
        bail!("no WSYS magic");
    }
    let archives = offset(b, base, base + 0x10)?;
    let control = offset(b, base, base + 0x14)?;
    let groups = be32(b, control + 8)? as usize;
    if groups > be32(b, archives + 4)? as usize {
        bail!("{groups} wave groups and fewer archives");
    }
    let mut names = Names::default();
    let mut group_records = Vec::new();
    let mut waves = Vec::new();
    let mut wave_count = 0u32;
    let mut ids = 1u32;
    for g in 0..groups {
        let scene = offset(b, base, control + 0xC + 4 * g)?;
        let ctrl = offset(b, base, scene + 0xC)?;
        let count = be32(b, ctrl + 4)? as usize;
        let archive_at = be32(b, archives + 8 + 4 * g)? as usize;
        if archive_at == 0 {
            bail!("wave group {g} with no archive");
        }
        let archive = base + archive_at;
        if count > be32(b, archive + 0x70)? as usize {
            bail!("wave group {g}: {count} waves, its archive fewer");
        }
        let field = b.get(archive..archive + 0x70).context("a wave archive's name past the archive")?;
        let file = &field[..field.iter().position(|c| *c == 0).unwrap_or(field.len())];
        let key = audio_data::archive_name(field);
        let mut path = WAVE_FOLDER.as_bytes().to_vec();
        path.extend_from_slice(file);
        put32(&mut group_records, names.add(&path)?);
        put32(&mut group_records, wave_count);
        put32(&mut group_records, count as u32);
        for w in 0..count {
            let wave = offset(b, base, archive + 0x74 + 4 * w)?;
            let ctrl_wave = offset(b, base, ctrl + 8 + 4 * w)?;
            let id = be32(b, ctrl_wave)? & 0xFFFF;
            let format = be8(b, wave + 1)? as u8;
            let disc_offset = be32(b, wave + 8)? as usize;
            let disc_length = be32(b, wave + 0xC)? as usize;
            let place = audio_data::place_of(layout, &key, disc_offset, disc_length, format)
                .with_context(|| format!("wave {w} of group {g} ({key}) not in the wave layout (format {format})"))?;
            put32(&mut waves, id);
            waves.push(format);
            waves.push(be8(b, wave + 2)? as u8);
            put16(&mut waves, 0);
            putf32(&mut waves, bef32(b, wave + 4)?);
            put32(&mut waves, size32(place.new_offset, "a wave archive")?);
            put32(&mut waves, size32(place.new_length, "a wave")?);
            put32(&mut waves, if be32(b, wave + 0x10)? != 0 { 0xFF } else { 0 });
            put32(&mut waves, be32(b, wave + 0x14)?);
            put32(&mut waves, be32(b, wave + 0x18)?);
            put32(&mut waves, be32(b, wave + 0x1C)?);
            ids = ids.max(id + 1);
        }
        wave_count += count as u32;
    }
    let mut file = Vec::new();
    file.extend_from_slice(WAVE_BANK_MAGIC);
    put32(&mut file, WAVE_BANK_VERSION);
    put32(&mut file, number);
    put32(&mut file, groups as u32);
    put32(&mut file, wave_count);
    put32(&mut file, ids);
    align(&mut names.bytes, 4);
    put32(&mut file, size32(names.bytes.len(), "wave bank names")?);
    put32(&mut file, 0);
    file.extend_from_slice(&group_records);
    file.extend_from_slice(&waves);
    file.extend_from_slice(&names.bytes);
    Ok(file)
}

// ---- the sound archive (NSND)

const IN_TABLE: u32 = 1;
const IN_NAMES: u32 = 2;

/// A level of the BST or BSTN tree at `at`: a count, `skip` bytes, the entries' offsets (0: none) from the table's start.
fn entries(b: &[u8], at: usize, skip: usize) -> Result<Vec<usize>> {
    let count = be32(b, at)? as usize;
    (0..count).map(|i| Ok(be32(b, at + 4 + skip + 4 * i)? as usize)).collect()
}

#[derive(Default)]
struct Tree {
    sections: Vec<u8>,
    groups: Vec<u8>,
    items: Vec<u8>,
    counts: [u32; 3],
}

/// The sound table (BST at `table`) and its names (BSTN at `names`) as one tree:
/// each level as long as the longer of the two, and each entry saying which trees hold it.
fn sound_tree(b: &[u8], table: Option<usize>, name_table: Option<usize>, names: &mut Names) -> Result<Tree> {
    let root = |at: Option<usize>, magic: &[u8; 4]| -> Result<Option<(usize, Vec<usize>)>> {
        let Some(base) = at else { return Ok(None) };
        if b.get(base..base + 4) != Some(magic.as_slice()) {
            bail!("no {} magic", String::from_utf8_lossy(magic));
        }
        let root = offset(b, base, base + 0xC)?;
        Ok(Some((base, entries(b, root, 0)?)))
    };
    let table = root(table, b"BST ")?;
    let name_table = root(name_table, b"BSTN")?;
    let level = |tree: &Option<(usize, Vec<usize>)>, i: usize| tree.as_ref().and_then(|(base, list)| list.get(i).filter(|o| **o != 0).map(|o| base + o));
    let sections = table.as_ref().map_or(0, |t| t.1.len()).max(name_table.as_ref().map_or(0, |t| t.1.len()));
    let mut tree = Tree::default();
    for s in 0..sections {
        let in_table = level(&table, s);
        let in_names = level(&name_table, s);
        let table_groups = match (in_table, &table) {
            (Some(at), Some((base, _))) => Some((*base, entries(b, at, 0)?)),
            _ => None,
        };
        let name_groups = match (in_names, &name_table) {
            (Some(at), Some((base, _))) => Some((*base, entries(b, at, 4)?)),
            _ => None,
        };
        let mut section_in = 0;
        if in_table.is_some() {
            section_in |= IN_TABLE;
        }
        if in_names.is_some() {
            section_in |= IN_NAMES;
        }
        let count_table = table_groups.as_ref().map_or(0, |g| g.1.len());
        let count_names = name_groups.as_ref().map_or(0, |g| g.1.len());
        put32(&mut tree.sections, section_in);
        put32(&mut tree.sections, tree.counts[1]);
        put32(&mut tree.sections, count_table as u32);
        put32(&mut tree.sections, count_names as u32);
        tree.counts[0] += 1;
        for g in 0..count_table.max(count_names) {
            let group_table = level(&table_groups, g);
            let group_names = level(&name_groups, g);
            // BST group: count, a word, the items' words (type in the top byte, offset below). BSTN: count, the
            // category's name, the names' offsets.
            let items = match (group_table, &table) {
                (Some(at), Some((base, _))) => Some((*base, entries(b, at, 4)?)),
                _ => None,
            };
            let item_names = match (group_names, &name_table) {
                (Some(at), Some((base, _))) => Some((*base, entries(b, at, 4)?)),
                _ => None,
            };
            let mut group_in = 0;
            if group_table.is_some() {
                group_in |= IN_TABLE;
            }
            if group_names.is_some() {
                group_in |= IN_NAMES;
            }
            let items_table = items.as_ref().map_or(0, |i| i.1.len());
            let items_names = item_names.as_ref().map_or(0, |i| i.1.len());
            put32(&mut tree.groups, group_in);
            put32(&mut tree.groups, tree.counts[2]);
            put32(&mut tree.groups, items_table as u32);
            put32(&mut tree.groups, items_names as u32);
            tree.counts[1] += 1;
            for i in 0..items_table.max(items_names) {
                let mut record = [0u8; 24];
                let mut item_in = 0u8;
                let mut stream_path = u32::MAX;
                if let Some((base, words)) = &items {
                    if let Some(&word) = words.get(i) {
                        let kind = (word >> 24) as u8;
                        record[0] = kind;
                        let at = word & 0xFF_FFFF;
                        if at != 0 {
                            item_in |= IN_TABLE as u8;
                            let item = base + at;
                            record[2] = be8(b, item)? as u8;
                            record[3] = be8(b, item + 1)? as u8;
                            let mut field = |slot: usize, at: usize| -> Result<()> {
                                record[slot..slot + 2].copy_from_slice(&(be16(b, at)? as u16).to_le_bytes());
                                Ok(())
                            };
                            match kind & 0xF0 {
                                0x50 => {
                                    field(8, item + 2)?;
                                    field(10, item + 4)?;
                                }
                                0x60 => {
                                    field(4, item + 2)?;
                                    field(6, item + 4)?;
                                    field(10, item + 6)?;
                                }
                                0x70 => {
                                    field(12, item + 2)?;
                                    stream_path = names.add(text(b, offset(b, *base, item + 4)?)?)?;
                                }
                                t => bail!("a sound item of type {t:#x} at {item:#x}"),
                            }
                        }
                    }
                }
                let mut name = u32::MAX;
                if let Some((base, list)) = &item_names {
                    if let Some(&at) = list.get(i) {
                        if at != 0 {
                            item_in |= IN_NAMES as u8;
                            name = names.add(text(b, base + at)?)?;
                        }
                    }
                }
                record[1] = item_in;
                record[16..20].copy_from_slice(&stream_path.to_le_bytes());
                record[20..24].copy_from_slice(&name.to_le_bytes());
                tree.items.extend_from_slice(&record);
                tree.counts[2] += 1;
            }
        }
    }
    Ok(tree)
}

/// The effect collection (BSC at `base`, to `end`) as one NSEQ: its groups the collection's tables, each entry an
/// effect's program.
fn effect_programs(b: &[u8], base: usize, end: usize) -> Result<Vec<u8>> {
    let data = b.get(base..end).context("the effect collection past the archive")?;
    if data.get(..2) != Some(b"SC".as_slice()) {
        bail!("no SC magic");
    }
    let groups = be16(data, 2)? as usize;
    let mut entries = Vec::with_capacity(groups);
    for g in 0..groups {
        let table = be32(data, 8 + 4 * g)? as usize;
        if table == 0 {
            bail!("effect group {g} with no table");
        }
        let count = be32(data, table)? as usize;
        let list = (0..count).map(|w| Ok(be32(data, table + 4 + 4 * w)? as usize)).collect::<Result<Vec<_>>>()?;
        entries.push(list);
    }
    program_file(data, &entries)
}

/// The disc's audio archive (BAA, AudioRes/SMR.szs expanded) as NSND.
pub fn archive(bytes: &[u8]) -> Result<Vec<u8>> {
    if cooked(bytes, ARCHIVE_MAGIC, ARCHIVE_VERSION)? {
        return Ok(bytes.to_vec());
    }
    if bytes.get(..4) != Some(b"AA_<".as_slice()) {
        bail!("not an audio archive");
    }
    let layout = audio_data::wave_layout(bytes).context("the wave systems")?;
    let (mut table, mut name_table, mut collection) = (None, None, None);
    let mut wave_banks: BTreeMap<u32, usize> = BTreeMap::new();
    let mut banks: Vec<(u32, usize)> = Vec::new();
    let mut p = 4;
    loop {
        let command: [u8; 4] = bytes.get(p..p + 4).context("an audio archive command past the file's end")?.try_into().unwrap();
        p += 4;
        let word = |i: usize| be32(bytes, p + 4 * i).map(|v| v as usize);
        match &command {
            b">_AA" => break,
            b"ws  " => {
                if wave_banks.insert(word(0)? as u32, word(1)?).is_some() {
                    bail!("wave bank {} twice", word(0)?);
                }
                p += 12;
            }
            b"bnk " => {
                banks.push((word(0)? as u32, word(1)?));
                p += 8;
            }
            b"bst " => {
                table = Some(word(0)?);
                p += 8;
            }
            b"bstn" => {
                name_table = Some(word(0)?);
                p += 8;
            }
            b"bsc " => {
                collection = Some((word(0)?, word(1)?));
                p += 8;
            }
            _ => bail!("audio archive command {:?} at {:#x} (an archive with it is not read)", String::from_utf8_lossy(&command), p - 4),
        }
    }
    let mut names = Names::default();
    let tree = sound_tree(bytes, table, name_table, &mut names).context("the sound table")?;
    let mut files: Vec<Vec<u8>> = Vec::new();
    let effects = match collection {
        Some((from, to)) => {
            files.push(effect_programs(bytes, from, to).context("the effect programs (BSC)")?);
            Some(files.len() - 1)
        }
        None => None,
    };
    let mut bank_files = Vec::new();
    for (wave_bank, at) in &banks {
        files.push(bank_file(bytes, *at, *wave_bank).with_context(|| format!("the bank at {at:#x}"))?);
        bank_files.push(files.len() - 1);
    }
    let wave_bank_count = wave_banks.keys().next_back().map_or(0, |n| n + 1);
    let mut wave_bank_files = vec![None; wave_bank_count as usize];
    for (number, at) in &wave_banks {
        files.push(wave_bank_file(bytes, *at, *number, &layout).with_context(|| format!("wave bank {number}"))?);
        wave_bank_files[*number as usize] = Some(files.len() - 1);
    }
    align(&mut names.bytes, 4);

    let head = 48 + tree.sections.len() + tree.groups.len() + tree.items.len() + 8 * (banks.len() + wave_bank_count as usize) + names.bytes.len();
    let mut places = Vec::with_capacity(files.len());
    let mut at = head.div_ceil(16) * 16;
    for f in &files {
        places.push((size32(at, "the sound archive")?, size32(f.len(), "a sound archive part")?));
        at = (at + f.len()).div_ceil(16) * 16;
    }
    let mut out = Vec::with_capacity(at);
    out.extend_from_slice(ARCHIVE_MAGIC);
    put32(&mut out, ARCHIVE_VERSION);
    for c in tree.counts {
        put32(&mut out, c);
    }
    put32(&mut out, 0); // statics: the game's archive has none, and one that has is refused above
    put32(&mut out, banks.len() as u32);
    put32(&mut out, wave_bank_count);
    put32(&mut out, size32(names.bytes.len(), "sound names")?);
    let (effects_at, effects_bytes) = effects.map_or((0, 0), |i| places[i]);
    put32(&mut out, effects_at);
    put32(&mut out, effects_bytes);
    let mut flags = 0;
    if table.is_some() {
        flags |= 1;
    }
    if name_table.is_some() {
        flags |= 2;
    }
    put32(&mut out, flags);
    out.extend_from_slice(&tree.sections);
    out.extend_from_slice(&tree.groups);
    out.extend_from_slice(&tree.items);
    for i in &bank_files {
        put32(&mut out, places[*i].0);
        put32(&mut out, places[*i].1);
    }
    for f in &wave_bank_files {
        let (file, size) = f.map_or((0, 0), |i| places[i]);
        put32(&mut out, file);
        put32(&mut out, size);
    }
    out.extend_from_slice(&names.bytes);
    for (f, (place, _)) in files.iter().zip(&places) {
        out.resize(*place as usize, 0);
        out.extend_from_slice(f);
    }
    Ok(out)
}

/// The game's sound names (its BSTN, the names `replace` of a mod's sound effect takes) from a cooked NSND, in the
/// table's order. A name the table has twice is listed once.
pub fn sound_names(nsnd: &[u8]) -> Result<Vec<String>> {
    if !cooked(nsnd, ARCHIVE_MAGIC, ARCHIVE_VERSION)? {
        bail!("not a cooked sound archive");
    }
    let word = |i: usize| -> Result<usize> {
        let at = 4 * i;
        Ok(u32::from_le_bytes(nsnd.get(at..at + 4).context("the sound archive's head")?.try_into().unwrap()) as usize)
    };
    let (sections, groups, items) = (word(2)?, word(3)?, word(4)?);
    let (banks, wave_banks, names_size) = (word(6)?, word(7)?, word(8)?);
    let items_at = 48usize
        .checked_add(sections.checked_mul(16).context("sections")?)
        .and_then(|v| v.checked_add(groups.checked_mul(16)?))
        .context("groups")?;
    let names_at = items_at
        .checked_add(items.checked_mul(24).context("items")?)
        .and_then(|v| v.checked_add(banks.checked_add(wave_banks)?.checked_mul(8)?))
        .context("banks")?;
    let names = nsnd.get(names_at..names_at.checked_add(names_size).context("names")?).context("the sound names past the archive")?;
    let mut out = Vec::new();
    let mut seen = HashSet::new();
    for i in 0..items {
        let record = &nsnd[items_at + 24 * i..items_at + 24 * i + 24];
        let name = u32::from_le_bytes(record[20..24].try_into().unwrap());
        if u32::from(record[1]) & IN_NAMES == 0 || name == u32::MAX {
            continue;
        }
        let from = names.get(name as usize..).context("a sound name past the names")?;
        let end = from.iter().position(|&c| c == 0).context("a sound name with no end")?;
        let text = String::from_utf8_lossy(&from[..end]).into_owned();
        if seen.insert(text.clone()) {
            out.push(text);
        }
    }
    Ok(out)
}

#[cfg(test)]
#[path = "sound_tests.rs"]
mod tests;
