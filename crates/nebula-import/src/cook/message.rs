//! Message files (NMSG): a disc's BMG as the plain arrays MessageData reads.
//!
//! Layout, little-endian:
//! - header (48 bytes): "NMSG", then the counts and offsets of the messages, the text (16-bit units), the flow nodes
//!   and the branches, the version ([`VERSION`]), the offset of the runs, and a reserved word;
//! - messages (12 bytes): the text's first unit, the camera set (u16), then bytes: _6, camera type, talk type, balloon
//!   type, _A, _B;
//! - text (version 1, beside the runs until G7.5): UTF-16 units, each message ending in a 0. A 0x1A unit starts a tag:
//!   the next unit holds the tag's length in bytes (from the 0x1A) and its group as two bytes, then the tag's id and
//!   parameters in units;
//! - nodes (16 bytes): type (1 message, 2 branch, 3 event), group (an event's kind), reserved (u16, 0), index (a message's
//!   number, a branch's condition, an event's first branch), next (a message's next node, NONE at the end; a branch's
//!   parameter; an event's value), next group (a message's group; a branch's first branch);
//! - branches: node numbers, NONE for none;
//! - runs (version 2, 4-aligned, at the header's offset): each message parsed once ([`parse_runs`]). A header of 10
//!   words, the counts and offsets (from the file's start) of the lists, the runs, the tag records, the code points and
//!   the parameter units; then
//!   - lists (16 bytes, one a message): its first run, its runs, its plain text's first code point and their count;
//!   - runs (12 bytes): kind ([`RUN_TEXT`], [`RUN_TAG`]), first (a text run's first code point, a tag run's record),
//!     count (a text run's code points; 1 for a tag);
//!   - tag records (20 bytes): group (u8), 0 (u8), tag (u16), the first parameter unit and their count, the string's
//!     first code point and their count;
//!   - code points (u32): a message's plain text (its text runs one after another) and a 0, then each of its tags'
//!     strings, each with a 0;
//!   - parameter units (u16), as the game reads a tag's parameters.

use anyhow::{bail, Context, Result};
use std::collections::HashMap;

pub const MAGIC: &[u8; 4] = b"NMSG";
/// The file's version: 2, the runs beside the in-text form of version 1.
pub const VERSION: u32 = 2;
/// A node's reserved u16 (TalkNode::mReserved).
const NODE_RESERVED: [u8; 2] = [0, 0];
pub const NONE: u32 = u32::MAX;
const HEADER: usize = 48;
const RUNS_HEADER: usize = 40;
/// A run's kind: code points of the text, or a tag record.
pub const RUN_TEXT: u32 = 0;
pub const RUN_TAG: u32 = 1;
/// The unit that starts a tag in the version-1 text.
const TAG: u16 = 0x1A;
/// The groups (and the tag) whose tags name a string (CustomTagProcessor: exeSoundGroup, exeFontGroup,
/// exeSystemGroupRuby).
pub const GROUP_SOUND: u8 = 2;
pub const GROUP_FONT: u8 = 10;
pub const GROUP_SYSTEM: u8 = 255;
pub const SYSTEM_RUBY: u16 = 2;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Message {
    pub text: u32,
    pub camera_set: u16,
    pub bytes: [u8; 6],
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Node {
    pub kind: u8,
    pub group: u8,
    pub index: u32,
    pub next: u32,
    pub next_group: u32,
}

#[derive(Debug, Default, PartialEq)]
pub struct Nmsg {
    pub messages: Vec<Message>,
    pub text: Vec<u16>,
    pub nodes: Vec<Node>,
    pub branches: Vec<u32>,
}

/// A message tag as a typed record: its group and tag, its parameter units, and the string it names (a sound's name,
/// the number font's text, a ruby's reading; empty for the others).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct TagRecord {
    pub group: u8,
    pub tag: u16,
    pub params: Vec<u16>,
    pub string: Vec<u32>,
}

/// A piece of a message: code points of its text, or a tag.
#[derive(Clone, Debug, PartialEq)]
pub enum Run {
    Text(Vec<u32>),
    Tag(TagRecord),
}

/// The code point at `at` and the units it takes: a surrogate pair is one, a lone surrogate keeps its value (the text
/// as the disc has it).
fn code_point(units: &[u16], at: usize) -> (u32, usize) {
    let unit = units[at];
    match (unit, units.get(at + 1)) {
        (0xD800..=0xDBFF, Some(&low @ 0xDC00..=0xDFFF)) => {
            (0x10000 + ((u32::from(unit) - 0xD800) << 10) + (u32::from(low) - 0xDC00), 2)
        }
        _ => (u32::from(unit), 1),
    }
}

fn code_points(units: &[u16]) -> Vec<u32> {
    let mut out = Vec::with_capacity(units.len());
    let mut at = 0;
    while at < units.len() {
        let (code, used) = code_point(units, at);
        out.push(code);
        at += used;
    }
    out
}

/// The string a tag names, as the game reads it: a sound's name to its first 0 (exeSoundGroup), the number font's
/// text (exeFontGroup), a ruby's reading (exeSystemGroupRuby, which needs two units).
fn tag_string(group: u8, tag: u16, params: &[u16]) -> Vec<u32> {
    match (group, tag) {
        (GROUP_SOUND, _) => code_points(&params[..params.iter().position(|u| *u == 0).unwrap_or(params.len())]),
        (GROUP_FONT, _) => code_points(params),
        (GROUP_SYSTEM, SYSTEM_RUBY) if params.len() >= 2 => code_points(&ruby_reading(params)),
        _ => Vec::new(),
    }
}

/// A ruby's reading: on the disc its parameters are a byte, the count of the base's characters, then the reading's
/// units in big-endian order from the next byte (the console reads them at byte 1, its last byte a pad), so each of
/// its units is the low byte of one parameter unit and the high byte of the next (the parameters are units of the
/// disc's byte pairs).
fn ruby_reading(params: &[u16]) -> Vec<u16> {
    params.windows(2).map(|pair| (pair[0] & 0xFF) << 8 | pair[1] >> 8).collect()
}

/// The message whose first unit of `text` is `start`, to its 0, parsed into runs. A tag is a 0x1A unit, a unit of its
/// length in bytes from the 0x1A (even, at least 6) and its group, its tag and its parameters: one shorter, of an odd
/// length or past the text is refused, and so is a message that reaches the text's end before its 0.
pub fn parse_runs(text: &[u16], start: usize) -> Result<Vec<Run>> {
    let mut runs = Vec::new();
    let mut codes = Vec::new();
    let mut at = start;
    loop {
        let Some(&unit) = text.get(at) else { bail!("the message at unit {start} reaches the text's end before its 0") };
        if unit == 0 {
            break;
        }
        if unit != TAG {
            let (code, used) = code_point(text, at);
            codes.push(code);
            at += used;
            continue;
        }
        let header = *text.get(at + 1).with_context(|| format!("a tag at unit {at}: its length past the text"))?;
        let length = usize::from(header & 0xFF);
        if length < 6 || length % 2 != 0 {
            bail!("a tag at unit {at} of {length} bytes (a tag holds its id: 6 bytes or more, even)");
        }
        let end = at + length / 2;
        if end > text.len() {
            bail!("a tag at unit {at} of {length} bytes runs past the text's {} units", text.len());
        }
        if !codes.is_empty() {
            runs.push(Run::Text(std::mem::take(&mut codes)));
        }
        let (group, tag, params) = ((header >> 8) as u8, text[at + 2], text[at + 3..end].to_vec());
        let string = tag_string(group, tag, &params);
        runs.push(Run::Tag(TagRecord { group, tag, params, string }));
        at = end;
    }
    if !codes.is_empty() {
        runs.push(Run::Text(codes));
    }
    Ok(runs)
}

/// Runs written back in the version-1 form, without the message's 0: the inverse of [`parse_runs`].
pub fn runs_to_units(runs: &[Run]) -> Vec<u16> {
    let mut out = Vec::new();
    for run in runs {
        match run {
            Run::Text(codes) => {
                for &code in codes {
                    if code >= 0x10000 {
                        let c = code - 0x10000;
                        out.extend([0xD800 | (c >> 10) as u16, 0xDC00 | (c & 0x3FF) as u16]);
                    } else {
                        out.push(code as u16);
                    }
                }
            }
            Run::Tag(t) => {
                out.extend([TAG, (6 + 2 * t.params.len() as u16) | (u16::from(t.group) << 8), t.tag]);
                out.extend(&t.params);
            }
        }
    }
    out
}

/// The runs part's arrays, as written.
#[derive(Default)]
struct RunArrays {
    lists: Vec<[u32; 4]>,
    runs: Vec<[u32; 3]>,
    tags: Vec<(u8, u16, [u32; 4])>,
    codes: Vec<u32>,
    params: Vec<u16>,
}

fn be16(bytes: &[u8], at: usize) -> Result<u16> {
    let b = bytes.get(at..at + 2).context("past the message file's end")?;
    Ok(u16::from_be_bytes([b[0], b[1]]))
}

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 4).context("past the message file's end")?;
    Ok(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
}

fn le16(bytes: &[u8], at: usize) -> Result<u16> {
    let b = bytes.get(at..at + 2).context("past the message file's end")?;
    Ok(u16::from_le_bytes([b[0], b[1]]))
}

fn le32(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 4).context("past the message file's end")?;
    Ok(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
}

/// A disc BMG's blocks: (magic, start, end), the last block's end clipped to the file (its size may count padding the
/// archive does not store).
fn blocks(bytes: &[u8]) -> Result<Vec<([u8; 4], usize, usize)>> {
    if bytes.len() < 0x20 || !bytes.starts_with(b"MESGbmg1") {
        bail!("not a message file");
    }
    if bytes[0x10] != 2 {
        bail!("a message file in encoding {} (only UTF-16 is read)", bytes[0x10]);
    }
    let count = be32(bytes, 0xC)?;
    let mut at = 0x20usize;
    let mut out = Vec::new();
    for block in 0..count {
        let magic: [u8; 4] = bytes.get(at..at + 4).context("a block past the file's end")?.try_into().unwrap();
        let size = be32(bytes, at + 4)? as usize;
        if size < 8 || (at + size > bytes.len() && block + 1 != count) {
            bail!("block {} at {at:#x}: size {size:#x} outside the file", String::from_utf8_lossy(&magic));
        }
        let end = (at + size).min(bytes.len());
        out.push((magic, at, end));
        at = end;
    }
    Ok(out)
}

impl Nmsg {
    pub fn from_disc(bytes: &[u8]) -> Result<Nmsg> {
        let mut nmsg = Nmsg::default();
        for (magic, at, end) in blocks(bytes)? {
            match &magic {
                b"INF1" => {
                    let count = be16(bytes, at + 8)? as usize;
                    let item = be16(bytes, at + 10)? as usize;
                    if item < 12 || at + 16 + count * item > end {
                        bail!("INF1: {count} items of {item} bytes");
                    }
                    for i in 0..count {
                        let entry = at + 16 + i * item;
                        let offset = be32(bytes, entry)?;
                        if offset % 2 != 0 {
                            bail!("INF1: message {i}'s text at odd byte {offset}");
                        }
                        let mut message = Message { text: offset / 2, camera_set: be16(bytes, entry + 4)?, bytes: [0; 6] };
                        message.bytes.copy_from_slice(&bytes[entry + 6..entry + 12]);
                        nmsg.messages.push(message);
                    }
                }
                b"DAT1" => {
                    // Every unit big-endian, except the unit after a 0x1A, which holds two bytes.
                    let mut p = at + 8;
                    while p + 2 <= end {
                        let unit = be16(bytes, p)?;
                        nmsg.text.push(unit);
                        p += 2;
                        if unit != 0x1A || p + 2 > end {
                            continue;
                        }
                        let tag_end = (p - 2 + bytes[p] as usize).min(end);
                        if tag_end < p + 2 || bytes[p] % 2 != 0 {
                            bail!("DAT1: a tag at {:#x} of {} bytes", p - 2, bytes[p]);
                        }
                        nmsg.text.push(u16::from_le_bytes([bytes[p], bytes[p + 1]]));
                        p += 2;
                        while p + 2 <= tag_end {
                            nmsg.text.push(be16(bytes, p)?);
                            p += 2;
                        }
                        p = tag_end;
                    }
                }
                b"FLW1" => {
                    let nodes = be16(bytes, at + 8)? as usize;
                    let branches = be16(bytes, at + 10)? as usize;
                    if at + 16 + nodes * 8 + branches * 2 > end {
                        bail!("FLW1: {nodes} nodes and {branches} branches");
                    }
                    for i in 0..nodes {
                        let n = at + 16 + i * 8;
                        let kind = bytes[n];
                        let (next, next_group) = if kind == 3 {
                            (be32(bytes, n + 4)?, 0)
                        } else {
                            let next = be16(bytes, n + 4)?;
                            let next = if kind == 1 && next == 0xFFFF { NONE } else { u32::from(next) };
                            (next, u32::from(be16(bytes, n + 6)?))
                        };
                        nmsg.nodes.push(Node { kind, group: bytes[n + 1], index: u32::from(be16(bytes, n + 2)?), next, next_group });
                    }
                    for b in 0..branches {
                        let node = be16(bytes, at + 16 + nodes * 8 + b * 2)?;
                        nmsg.branches.push(if node == 0xFFFF { NONE } else { u32::from(node) });
                    }
                }
                b"FLI1" => {} // the game reads nothing in it
                _ => bail!("unknown block {} at {at:#x}", String::from_utf8_lossy(&magic)),
            }
        }
        for (i, message) in nmsg.messages.iter().enumerate() {
            if message.text as usize >= nmsg.text.len() && !nmsg.text.is_empty() {
                bail!("message {i}'s text at unit {} past the {} units", message.text, nmsg.text.len());
            }
        }
        Ok(nmsg)
    }

    /// Each message's text parsed once into runs; messages that share their text share their list.
    fn run_arrays(&self) -> Result<RunArrays> {
        let mut out = RunArrays::default();
        let mut parsed: HashMap<u32, [u32; 4]> = HashMap::new();
        for (i, message) in self.messages.iter().enumerate() {
            if let Some(list) = parsed.get(&message.text) {
                out.lists.push(*list);
                continue;
            }
            let runs = parse_runs(&self.text, message.text as usize).with_context(|| format!("message {i}"))?;
            let (first_run, plain) = (out.runs.len() as u32, out.codes.len() as u32);
            let mut tag = out.tags.len() as u32;
            for run in &runs {
                match run {
                    Run::Text(codes) => {
                        out.runs.push([RUN_TEXT, out.codes.len() as u32, codes.len() as u32]);
                        out.codes.extend(codes);
                    }
                    Run::Tag(_) => {
                        out.runs.push([RUN_TAG, tag, 1]);
                        tag += 1;
                    }
                }
            }
            let list = [first_run, runs.len() as u32, plain, out.codes.len() as u32 - plain];
            out.codes.push(0);
            for run in &runs {
                if let Run::Tag(t) = run {
                    let record = [out.params.len() as u32, t.params.len() as u32, out.codes.len() as u32, t.string.len() as u32];
                    out.params.extend(&t.params);
                    out.codes.extend(&t.string);
                    out.codes.push(0);
                    out.tags.push((t.group, t.tag, record));
                }
            }
            parsed.insert(message.text, list);
            out.lists.push(list);
        }
        Ok(out)
    }

    /// The cooked file, version 1's arrays and the runs; the runs read back and agree with the text ([`runs_agree`]).
    pub fn to_bytes(&self) -> Result<Vec<u8>> {
        let runs = self.run_arrays()?;
        let messages_at = HEADER;
        let text_at = messages_at + 12 * self.messages.len();
        let nodes_at = (text_at + 2 * self.text.len() + 3) & !3;
        let branches_at = nodes_at + 16 * self.nodes.len();
        let runs_at = branches_at + 4 * self.branches.len();
        let lists_at = runs_at + RUNS_HEADER;
        let run_at = lists_at + 16 * runs.lists.len();
        let tags_at = run_at + 12 * runs.runs.len();
        let codes_at = tags_at + 20 * runs.tags.len();
        let params_at = codes_at + 4 * runs.codes.len();
        let mut out = Vec::with_capacity(params_at + 2 * runs.params.len());
        out.extend_from_slice(MAGIC);
        for word in [
            self.messages.len(),
            messages_at,
            self.text.len(),
            text_at,
            self.nodes.len(),
            nodes_at,
            self.branches.len(),
            branches_at,
            VERSION as usize,
            runs_at,
        ] {
            out.extend_from_slice(&(word as u32).to_le_bytes());
        }
        out.extend_from_slice(&[0; 4]);
        for m in &self.messages {
            out.extend_from_slice(&m.text.to_le_bytes());
            out.extend_from_slice(&m.camera_set.to_le_bytes());
            out.extend_from_slice(&m.bytes);
        }
        for unit in &self.text {
            out.extend_from_slice(&unit.to_le_bytes());
        }
        out.resize(nodes_at, 0);
        for n in &self.nodes {
            out.extend_from_slice(&[n.kind, n.group]);
            out.extend_from_slice(&NODE_RESERVED);
            for word in [n.index, n.next, n.next_group] {
                out.extend_from_slice(&word.to_le_bytes());
            }
        }
        for b in &self.branches {
            out.extend_from_slice(&b.to_le_bytes());
        }
        for word in [
            runs.lists.len(),
            lists_at,
            runs.runs.len(),
            run_at,
            runs.tags.len(),
            tags_at,
            runs.codes.len(),
            codes_at,
            runs.params.len(),
            params_at,
        ] {
            out.extend_from_slice(&(word as u32).to_le_bytes());
        }
        for word in runs.lists.iter().flatten().chain(runs.runs.iter().flatten()) {
            out.extend_from_slice(&word.to_le_bytes());
        }
        for (group, tag, record) in &runs.tags {
            out.extend_from_slice(&[*group, 0]);
            out.extend_from_slice(&tag.to_le_bytes());
            for word in record {
                out.extend_from_slice(&word.to_le_bytes());
            }
        }
        for code in &runs.codes {
            out.extend_from_slice(&code.to_le_bytes());
        }
        for unit in &runs.params {
            out.extend_from_slice(&unit.to_le_bytes());
        }
        runs_agree(self, &out)?;
        Ok(out)
    }

    pub fn from_nmsg(bytes: &[u8]) -> Result<Nmsg> {
        if !bytes.starts_with(MAGIC) {
            bail!("not a cooked message file");
        }
        let w = |i: usize| -> Result<usize> { Ok(le32(bytes, 4 * i)? as usize) };
        if w(9)? != VERSION as usize {
            bail!("a message file of version {} (this importer writes {VERSION})", w(9)?);
        }
        let mut nmsg = Nmsg::default();
        for i in 0..w(1)? {
            let at = w(2)? + 12 * i;
            let mut m = Message { text: le32(bytes, at)?, camera_set: le16(bytes, at + 4)?, bytes: [0; 6] };
            m.bytes.copy_from_slice(bytes.get(at + 6..at + 12).context("past the end")?);
            nmsg.messages.push(m);
        }
        for i in 0..w(3)? {
            nmsg.text.push(le16(bytes, w(4)? + 2 * i)?);
        }
        for i in 0..w(5)? {
            let at = w(6)? + 16 * i;
            nmsg.nodes.push(Node {
                kind: bytes[at],
                group: bytes[at + 1],
                index: le32(bytes, at + 4)?,
                next: le32(bytes, at + 8)?,
                next_group: le32(bytes, at + 12)?,
            });
        }
        for i in 0..w(7)? {
            nmsg.branches.push(le32(bytes, w(8)? + 4 * i)?);
        }
        Ok(nmsg)
    }
}

/// A cooked file's runs, each message's read from the bytes as the game reads them: every index checked against its
/// array, every text run inside its message's plain text, the plain text and each string followed by a 0.
pub fn read_runs(bytes: &[u8]) -> Result<Vec<Vec<Run>>> {
    let w = |at: usize, i: usize| -> Result<usize> { Ok(le32(bytes, at + 4 * i)? as usize) };
    if !bytes.starts_with(MAGIC) || w(0, 9)? != VERSION as usize {
        bail!("not a cooked message file of version {VERSION}");
    }
    let at = w(0, 10)?;
    let (lists, runs, tags, codes, params) = ((w(at, 0)?, w(at, 1)?), (w(at, 2)?, w(at, 3)?), (w(at, 4)?, w(at, 5)?), (w(at, 6)?, w(at, 7)?), (w(at, 8)?, w(at, 9)?));
    if lists.0 != w(0, 1)? {
        bail!("{} run lists for {} messages", lists.0, w(0, 1)?);
    }
    let code = |i: usize| -> Result<u32> {
        if i >= codes.0 {
            bail!("code point {i} past the {}", codes.0);
        }
        le32(bytes, codes.1 + 4 * i)
    };
    let span = |first: usize, count: usize| -> Result<Vec<u32>> {
        if code(first + count)? != 0 {
            bail!("the code points {first} to {} end in no 0", first + count);
        }
        (first..first + count).map(code).collect()
    };
    let mut out = Vec::with_capacity(lists.0);
    for m in 0..lists.0 {
        let list = lists.1 + 16 * m;
        let (first_run, run_num, plain, plain_num) = (w(list, 0)?, w(list, 1)?, w(list, 2)?, w(list, 3)?);
        if first_run + run_num > runs.0 {
            bail!("message {m}: runs {first_run} to {} past the {}", first_run + run_num, runs.0);
        }
        let mut message = Vec::with_capacity(run_num);
        let mut text: Vec<u32> = Vec::new();
        for r in first_run..first_run + run_num {
            let (kind, first, count) = (w(runs.1 + 12 * r, 0)?, w(runs.1 + 12 * r, 1)?, w(runs.1 + 12 * r, 2)?);
            match kind as u32 {
                RUN_TEXT => {
                    if first != plain + text.len() || count == 0 {
                        bail!("message {m}: text run {r} at code point {first} of {count}, not next in its plain text");
                    }
                    let codes: Vec<u32> = (first..first + count).map(code).collect::<Result<_>>()?;
                    text.extend(&codes);
                    message.push(Run::Text(codes));
                }
                RUN_TAG => {
                    if first >= tags.0 || count != 1 {
                        bail!("message {m}: tag run {r} names record {first} of {}", tags.0);
                    }
                    let record = tags.1 + 20 * first;
                    let (param, param_num, string, string_num) = (w(record, 1)?, w(record, 2)?, w(record, 3)?, w(record, 4)?);
                    if param + param_num > params.0 {
                        bail!("message {m}: tag record {first}'s parameters past the {}", params.0);
                    }
                    let units = (param..param + param_num).map(|i| le16(bytes, params.1 + 2 * i)).collect::<Result<_>>()?;
                    message.push(Run::Tag(TagRecord {
                        group: *bytes.get(record).context("past the end")?,
                        tag: le16(bytes, record + 2)?,
                        params: units,
                        string: span(string, string_num)?,
                    }));
                }
                _ => bail!("message {m}: run {r} of kind {kind}"),
            }
        }
        if span(plain, plain_num)? != text {
            bail!("message {m}: its plain text is not its text runs");
        }
        out.push(message);
    }
    Ok(out)
}

/// The runs of `cooked` against `nmsg`'s text: each message's runs, written back in the version-1 form, are its units
/// to its 0, and each tag's string is the one the game reads from its parameters. Every cook proves it (the cook stops
/// on a difference).
pub fn runs_agree(nmsg: &Nmsg, cooked: &[u8]) -> Result<()> {
    for (i, (message, runs)) in nmsg.messages.iter().zip(read_runs(cooked)?).enumerate() {
        let mut units = runs_to_units(&runs);
        units.push(0);
        let start = message.text as usize;
        if nmsg.text.get(start..start + units.len()) != Some(&units[..]) {
            bail!("message {i}: its runs read other than its text");
        }
        for run in &runs {
            if let Run::Tag(t) = run {
                if t.string != tag_string(t.group, t.tag, &t.params) {
                    bail!("message {i}: a tag of group {} holds another string than its parameters name", t.group);
                }
            }
        }
    }
    Ok(())
}

/// A message file cooked: a disc's BMG to NMSG.
pub fn cook(bytes: &[u8]) -> Result<Vec<u8>> {
    let out = Nmsg::from_disc(bytes)?.to_bytes()?;
    #[cfg(feature = "proof-tools")]
    proof::reads_agree(bytes, &out)?;
    Ok(out)
}

/// What the game read from the disc file (MessageData and TalkNode on the BMG blocks) against NMSG: every message's
/// attributes and text to its 0, every node's fields as each node type reads them, every branch (proof-tools: the cook
/// stops on a difference).
#[cfg(feature = "proof-tools")]
pub mod proof {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    pub static FILES: AtomicU64 = AtomicU64::new(0);
    pub static READS: AtomicU64 = AtomicU64::new(0);

    pub fn reads_agree(bytes: &[u8], cooked: &[u8]) -> Result<()> {
        let nmsg = Nmsg::from_nmsg(cooked)?;
        if nmsg.to_bytes()? != cooked {
            bail!("NMSG reads back other than it was written");
        }
        let blocks = blocks(bytes)?;
        let block = |magic: &[u8; 4]| blocks.iter().find(|b| &b.0 == magic).map(|b| b.1);
        let mut reads = 0u64;
        if let (Some(inf), Some(dat)) = (block(b"INF1"), block(b"DAT1")) {
            let item = be16(bytes, inf + 10)? as usize;
            for (i, m) in nmsg.messages.iter().enumerate() {
                let entry = inf + 16 + i * item;
                if be16(bytes, entry + 4)? != m.camera_set || bytes[entry + 6..entry + 12] != m.bytes {
                    bail!("message {i}: attributes differ");
                }
                // The text to its 0: the units as the game read them (a tag's second unit as bytes).
                let mut p = dat + 8 + be32(bytes, entry)? as usize;
                let mut u = m.text as usize;
                let mut tag_end = 0usize; // past a tag's last byte while in one
                loop {
                    let in_tag = p < tag_end;
                    let old = be16(bytes, p)?;
                    let new = *nmsg.text.get(u).context("text past its end")?;
                    if old != new {
                        bail!("message {i}: unit {u} reads {old:#x} on the disc, {new:#x} in NMSG");
                    }
                    reads += 1;
                    if !in_tag && old == 0 {
                        break;
                    }
                    p += 2;
                    u += 1;
                    if !in_tag && old == 0x1A {
                        // The tag's length and group: two bytes, read as bytes.
                        let unit = u16::from_le_bytes([bytes[p], bytes[p + 1]]);
                        if nmsg.text.get(u) != Some(&unit) {
                            bail!("message {i}: tag header at unit {u} differs");
                        }
                        reads += 1;
                        tag_end = p - 2 + bytes[p] as usize;
                        p += 2;
                        u += 1;
                    }
                }
            }
        }
        if let Some(flw) = block(b"FLW1") {
            let count = be16(bytes, flw + 8)? as usize;
            for (i, n) in nmsg.nodes.iter().enumerate() {
                let at = flw + 16 + i * 8;
                let index = u32::from(be16(bytes, at + 2)?);
                let ok = n.kind == bytes[at]
                    && n.group == bytes[at + 1]
                    && n.index == index
                    && match n.kind {
                        3 => n.next == be32(bytes, at + 4)?,
                        1 if be16(bytes, at + 4)? == 0xFFFF => n.next == NONE && n.next_group == u32::from(be16(bytes, at + 6)?),
                        _ => n.next == u32::from(be16(bytes, at + 4)?) && n.next_group == u32::from(be16(bytes, at + 6)?),
                    };
                if !ok {
                    bail!("node {i}: {n:?} against the disc's");
                }
                reads += 1;
            }
            for (b, node) in nmsg.branches.iter().enumerate() {
                let old = be16(bytes, flw + 16 + count * 8 + 2 * b)?;
                if (old == 0xFFFF && *node != NONE) || (old != 0xFFFF && *node != u32::from(old)) {
                    bail!("branch {b}: {old:#x} on the disc, {node:#x} in NMSG");
                }
                reads += 1;
            }
        }
        FILES.fetch_add(1, Ordering::Relaxed);
        READS.fetch_add(reads, Ordering::Relaxed);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn disc() -> Vec<u8> {
        let mut out = b"MESGbmg1".to_vec();
        out.extend_from_slice(&[0; 4]); // size
        out.extend_from_slice(&3u32.to_be_bytes());
        out.push(2);
        out.resize(0x20, 0);
        // INF1: two messages of 12 bytes.
        let inf: Vec<u8> = [b"INF1".to_vec(), 40u32.to_be_bytes().to_vec(), 2u16.to_be_bytes().to_vec(), 12u16.to_be_bytes().to_vec(), vec![0; 4]]
            .concat()
            .into_iter()
            .chain([0, 0, 0, 0, 0, 7, 1, 2, 3, 4, 5, 6])
            .chain([0, 0, 0, 4, 0, 0, 0, 0, 1, 0, 0, 0])
            .collect();
        out.extend_from_slice(&inf);
        // DAT1: "A" 0, then a tag (0x1A, length 6, group 1, id 0x0203) and 0.
        let dat: Vec<u8> = [b"DAT1".to_vec(), 24u32.to_be_bytes().to_vec(), vec![0, b'A', 0, 0, 0, 0x1A, 6, 1, 2, 3, 0, 0, 0, 0, 0, 0]].concat();
        out.extend_from_slice(&dat);
        // FLW1: a message node to the end, an event node, one branch to node 0 and one to none.
        let flw: Vec<u8> = [
            b"FLW1".to_vec(),
            36u32.to_be_bytes().to_vec(),
            2u16.to_be_bytes().to_vec(),
            2u16.to_be_bytes().to_vec(),
            vec![0; 4],
            vec![1, 0, 0, 1, 0xFF, 0xFF, 0, 0],
            vec![3, 5, 0, 1, 0x12, 0x34, 0x56, 0x78],
            vec![0, 0, 0xFF, 0xFF],
        ]
        .concat();
        out.extend_from_slice(&flw);
        out
    }

    #[test]
    fn a_disc_message_file_cooks_to_arrays() {
        let nmsg = Nmsg::from_disc(&disc()).unwrap();
        assert_eq!(nmsg.messages[0], Message { text: 0, camera_set: 7, bytes: [1, 2, 3, 4, 5, 6] });
        assert_eq!(nmsg.messages[1].text, 2);
        assert_eq!(nmsg.text, [0x41, 0, 0x1A, u16::from_le_bytes([6, 1]), 0x0203, 0, 0, 0]);
        assert_eq!(nmsg.nodes[0], Node { kind: 1, group: 0, index: 1, next: NONE, next_group: 0 });
        assert_eq!(nmsg.nodes[1], Node { kind: 3, group: 5, index: 1, next: 0x1234_5678, next_group: 0 });
        assert_eq!(nmsg.branches, [0, NONE]);
        let cooked = cook(&disc()).unwrap();
        assert_eq!(Nmsg::from_nmsg(&cooked).unwrap(), nmsg);
        let tag = TagRecord { group: 1, tag: 0x0203, params: vec![], string: vec![] };
        assert_eq!(read_runs(&cooked).unwrap(), [vec![Run::Text(vec![0x41])], vec![Run::Tag(tag)]]);
    }

    fn utf16(text: &str) -> Vec<u16> {
        text.encode_utf16().collect()
    }

    fn codes(text: &str) -> Vec<u32> {
        text.chars().map(u32::from).collect()
    }

    /// Text of every kind: characters, a surrogate pair, a lone surrogate, and the tags that name a string (a ruby, a
    /// sound, the number font) and one that does not (a colour); a message that shares another's text, an empty one.
    fn fixture() -> Nmsg {
        let mut nmsg = Nmsg::default();
        let message = |nmsg: &mut Nmsg, units: Vec<u16>| {
            nmsg.messages.push(Message { text: nmsg.text.len() as u32, camera_set: 1, bytes: [0, 1, 2, 3, 4, 5] });
            nmsg.text.extend(units);
            nmsg.text.push(0);
        };
        let mut first = utf16("Hi");
        first.extend([TAG, 0xFF0A, SYSTEM_RUBY, 0x0130, 0x5400]); // the ruby of one character, read "ご" (bytes 01 30 54 00)
        first.extend(utf16("語"));
        first.extend([TAG, 0x0210, 0]);
        first.extend(utf16("SE_A\0"));
        first.extend(utf16("😀"));
        first.push(0xD800);
        first.extend([TAG, 0xFF08, 0, 0x0100]); // colour 1
        first.extend(utf16("!"));
        message(&mut nmsg, first);
        nmsg.messages.push(nmsg.messages[0]);
        message(&mut nmsg, vec![]);
        let mut font = vec![TAG, 0x0A0A, 0];
        font.extend(utf16("12"));
        message(&mut nmsg, font);
        nmsg.nodes.push(Node { kind: 1, group: 0, index: 0, next: NONE, next_group: 0 });
        nmsg.branches.push(NONE);
        nmsg
    }

    #[test]
    fn each_message_is_parsed_once_into_runs_of_code_points_and_typed_tags() {
        let nmsg = fixture();
        let runs = read_runs(&nmsg.to_bytes().unwrap()).unwrap();
        let tag = |group, tag, params: Vec<u16>, string: Vec<u32>| Run::Tag(TagRecord { group, tag, params, string });
        let first = vec![
            Run::Text(codes("Hi")),
            tag(GROUP_SYSTEM, SYSTEM_RUBY, vec![0x0130, 0x5400], codes("ご")),
            Run::Text(codes("語")),
            tag(GROUP_SOUND, 0, utf16("SE_A\0"), codes("SE_A")),
            Run::Text(vec![0x1F600, 0xD800]),
            tag(GROUP_SYSTEM, 0, vec![0x0100], vec![]),
            Run::Text(codes("!")),
        ];
        assert_eq!(runs, [first.clone(), first, vec![], vec![tag(GROUP_FONT, 0, utf16("12"), codes("12"))]]);
        // The shared text is written once.
        let bytes = nmsg.to_bytes().unwrap();
        let runs_at = le32(&bytes, 40).unwrap() as usize;
        assert_eq!(le32(&bytes, runs_at + 8).unwrap(), 7 + 1);
        assert_eq!(le32(&bytes, runs_at + 16).unwrap(), 4);
        let lists_at = le32(&bytes, runs_at + 4).unwrap() as usize;
        assert_eq!(bytes[lists_at..lists_at + 16], bytes[lists_at + 16..lists_at + 32]);
    }

    /// The game's run reader (MessageHolder.hpp MessageRunFile) reads this file in native/tests/message_runs_tests.cpp:
    /// the fixture's cooked bytes, as that test includes them. When they differ, the test prints the file as it must be.
    #[test]
    fn the_game_reads_the_runs_the_cook_writes() {
        let bytes = fixture().to_bytes().unwrap();
        let mut text = String::from("// The cook's NMSG of crates/nebula-import cook/message.rs tests::fixture (the test the_game_reads_the_runs_the_cook_writes\n// prints it when it differs).\n");
        for line in bytes.chunks(16) {
            let words: Vec<String> = line.iter().map(|b| format!("0x{b:02x},")).collect();
            text.push_str(&words.join(" "));
            text.push('\n');
        }
        let file = include_str!("../../../../native/tests/message_runs_fixture.inc").replace('\r', "");
        if file != text {
            println!("{text}");
        }
        assert!(file == text, "native/tests/message_runs_fixture.inc is not the cook's: the test printed it as it must be");
    }

    /// The parse's rule, written plainly: a message is well formed when it reaches its 0 and each tag on the way is
    /// even, holds its id and ends inside the text.
    fn well_formed(text: &[u16], start: usize) -> bool {
        let mut at = start;
        while at < text.len() {
            match text[at] {
                0 => return true,
                TAG => {
                    let Some(header) = text.get(at + 1) else { return false };
                    let length = usize::from(header & 0xFF);
                    if length < 6 || length % 2 == 1 || at + length / 2 > text.len() {
                        return false;
                    }
                    at += length / 2;
                }
                _ => at += 1,
            }
        }
        false
    }

    #[test]
    fn malformed_tags_are_refused_and_no_text_breaks_the_parse() {
        for (text, why) in [
            (vec![TAG], "past the text"),
            (vec![TAG, 0x0105, 1, 0], "5 bytes"),
            (vec![TAG, 0x0104, 0, 0], "4 bytes"),
            (vec![TAG, 0x0102, 0], "2 bytes"),
            (vec![TAG, 0x0100], "0 bytes"),
            (vec![0x41, TAG, 0x010A, 1, 0], "runs past"),
            (vec![0x41, 0x42], "before its 0"),
            (vec![], "before its 0"),
        ] {
            let error = format!("{:#}", parse_runs(&text, 0).unwrap_err());
            assert!(error.contains(why), "{text:x?}: {error}");
        }
        // Random texts heavy in tag starts, lengths and surrogates (a fixed seed): a text is parsed exactly when it is
        // well formed, and its runs written back are its units to its 0; a file of such messages is written whole or
        // refused, never half.
        let mut seed = 0x2545_F491_4F6C_DD1Du64;
        let mut next = |n: u64| {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            seed % n
        };
        let (mut parsed, mut refused) = (0, 0);
        for _ in 0..200_000 {
            let length = next(28) as usize;
            let text: Vec<u16> = (0..length)
                .map(|_| match next(9) {
                    0 | 1 => TAG,
                    2 => (next(24) as u16) | ((next(3) as u16) << 8),
                    3 => 0,
                    4 => 0xD800 + next(0x800) as u16,
                    5 => 0xFF00 | next(0x100) as u16,
                    _ => next(0x10000) as u16,
                })
                .collect();
            let start = next(length as u64 + 1) as usize;
            match parse_runs(&text, start) {
                Ok(runs) => {
                    assert!(well_formed(&text, start), "{text:x?} from {start}");
                    let units = runs_to_units(&runs);
                    assert_eq!(text.get(start..start + units.len()), Some(&units[..]), "{text:x?} from {start}");
                    assert_eq!(text.get(start + units.len()), Some(&0));
                    parsed += 1;
                }
                Err(_) => {
                    assert!(!well_formed(&text, start), "{text:x?} from {start}");
                    refused += 1;
                }
            }
            let nmsg = Nmsg { messages: vec![Message { text: start as u32, ..Message::default() }], text: text.clone(), ..Nmsg::default() };
            match nmsg.to_bytes() {
                Ok(bytes) => assert_eq!(read_runs(&bytes).unwrap().len(), 1),
                Err(_) => assert!(!well_formed(&text, start)),
            }
        }
        // Both ways were taken often.
        assert!(parsed > 10_000 && refused > 10_000, "{parsed} parsed, {refused} refused");
    }

    #[test]
    fn a_file_of_another_version_is_refused() {
        let mut bytes = fixture().to_bytes().unwrap();
        bytes[36] = 1;
        assert!(Nmsg::from_nmsg(&bytes).is_err());
        assert!(read_runs(&bytes).is_err());
    }

    #[test]
    fn runs_that_disagree_with_the_text_are_found() {
        let nmsg = fixture();
        let bytes = nmsg.to_bytes().unwrap();
        let codes_at = le32(&bytes, le32(&bytes, 40).unwrap() as usize + 28).unwrap() as usize;
        // The first code point, 'H', made 'J'.
        let mut wrong = bytes.clone();
        wrong[codes_at] = b'J';
        assert!(runs_agree(&nmsg, &wrong).is_err());
        // A run's code points past the array.
        let mut past = bytes;
        let run_at = le32(&past, le32(&past, 40).unwrap() as usize + 12).unwrap() as usize;
        past[run_at + 8..run_at + 12].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(read_runs(&past).is_err());
    }
}
