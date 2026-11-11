//! Tables (NTBL): every table the game reads (the disc's BCSV: .bcsv, .tbl, .pa, .bcam, .banmt, the stages' placement
//! tables, main.dol's two) and the ones mods write, in the one form the game's reader (Game/Util/Table) takes.
//!
//! Layout, little-endian:
//! - header (32 bytes): "NTBL", rows, columns, rows_at, text_at, text_size, two reserved words (0);
//! - columns at 32, 12 bytes each: hash (x31 of the name), value mask, kind (u8), width (u8), reserved (u16, 0);
//! - rows at rows_at: rows x columns cells, each a u32;
//! - text at text_at: UTF-8, NUL-terminated; offset 0 is "".
//!
//! A BCSV value is masked and shifted once, here: a whole-number cell holds (raw & mask) >> shift. Kinds:
//! - int: a full-width long, short or byte (mask all ones, no shift); read signed at its width (8, 16, 32);
//! - field: any other whole-number column (packed, or long-2); read unsigned only;
//! - float: the 32 bits;
//! - text: an offset into the text (the disc's Shift-JIS turns UTF-8).
//! The value mask is what a cell of the column can hold; writers keep a value inside it.

use anyhow::{bail, Context, Result};
use serde_json::Value;
use std::collections::HashMap;

pub const MAGIC: &[u8; 4] = b"NTBL";
/// A column's reserved u16 (TableColumn::mReserved).
const COLUMN_RESERVED: [u8; 2] = [0, 0];
const HEADER: usize = 32;
const COLUMN: usize = 12;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u8)]
pub enum Kind {
    Int = 0,
    Field = 1,
    Float = 2,
    Text = 3,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Column {
    pub hash: u32,
    pub mask: u32,
    pub kind: Kind,
    /// An int's sign width in bits; 32 for the others.
    pub width: u8,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Cell {
    Bits(u32),
    /// A word's bytes: UTF-8, but for a disc byte no Shift-JIS character starts with (decode_text).
    Text(Vec<u8>),
}

/// A table between its sources (BCSV, JSON, NTBL) and NTBL: columns, and rows of one cell per column.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct TableIr {
    pub columns: Vec<Column>,
    pub rows: Vec<Vec<Cell>>,
}

/// A column's hash, as the game's tables have it: x31 of the bytes, each signed as MR::getHashCode takes it.
pub fn column_hash(name: &str) -> u32 {
    name.bytes().fold(0u32, |hash, byte| hash.wrapping_mul(31).wrapping_add(byte as i8 as u32))
}

/// A word's bytes from JSON: a string, or `{ "bytes": "<hex>" }`.
pub(crate) fn word_bytes(value: &Value) -> Option<Vec<u8>> {
    if let Some(text) = value.as_str() {
        return Some(text.as_bytes().to_vec());
    }
    let hex = value.as_object().filter(|o| o.len() == 1)?.get("bytes")?.as_str()?;
    if hex.len() % 2 != 0 || !hex.bytes().all(|b| b.is_ascii_hexdigit()) {
        return None;
    }
    (0..hex.len()).step_by(2).map(|i| u8::from_str_radix(hex.get(i..i + 2)?, 16).ok()).collect()
}

/// A column a mod's JSON names: by its name, or by its hash ("0x0a3b36a3") for one whose name nobody knows, as
/// `lumalink extract` writes those.
fn key_hash(key: &str) -> Result<u32> {
    match key.strip_prefix("0x") {
        Some(hex) => u32::from_str_radix(hex, 16).with_context(|| format!("column {key}")),
        None => Ok(column_hash(key)),
    }
}

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 4).context("past the table's end")?;
    Ok(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
}

fn le32(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 4).context("past the table's end")?;
    Ok(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
}

/// The bytes a BCSV field of `kind` occupies.
fn bcsv_width(kind: u8) -> Option<usize> {
    match kind {
        0 | 2 | 3 | 6 => Some(4), // long, float, long 2, string offset
        4 => Some(2),             // short
        5 => Some(1),             // byte
        1 => Some(32),            // embedded string
        _ => None,
    }
}

/// A big-endian BCSV's layout (entries, fields, data offset, entry size) when every part lies in the file.
fn bcsv_layout(bytes: &[u8]) -> Option<(usize, usize, usize, usize)> {
    if bytes.len() < 16 {
        return None;
    }
    let entries = be32(bytes, 0).ok()? as i32;
    let fields = be32(bytes, 4).ok()? as usize;
    let data = be32(bytes, 8).ok()? as usize;
    let size = be32(bytes, 12).ok()? as usize;
    // An empty table (no fields, no entries) is a header alone.
    let empty = fields == 0 && entries == 0;
    if entries < 0 || fields > 1024 || data != 16 + 12 * fields || size > 0x10000 || (!empty && (fields == 0 || size == 0)) {
        return None;
    }
    let end = data.checked_add((entries as usize).checked_mul(size)?)?;
    if end > bytes.len() {
        return None;
    }
    let strings = bytes.len() - end;
    for f in 0..fields {
        let at = 16 + 12 * f;
        let offset = u16::from_be_bytes([bytes[at + 8], bytes[at + 9]]) as usize;
        let kind = bytes[at + 11];
        let width = bcsv_width(kind)?;
        if bytes[at + 10] >= 32 || offset + width.min(4) > size {
            return None;
        }
        if kind == 6 {
            for e in 0..entries as usize {
                if be32(bytes, data + e * size + offset).ok()? as usize >= strings.max(1) {
                    return None;
                }
            }
        }
    }
    Some((entries as usize, fields, data, size))
}

/// Words of the disc's tables and recordings, Shift-JIS (ASCII for most), as UTF-8. A byte no Shift-JIS character
/// starts with stays itself: the game makes names with %c of a number ("ワープカメラ %d-%c" of WarpCube and WarpPod
/// gives 0x91 for 80), and its tables hold those bytes, which the game compares with strcmp.
pub(crate) fn decode_text(bytes: &[u8]) -> Vec<u8> {
    if bytes.is_ascii() {
        return bytes.to_vec();
    }
    let sjis = |part: &[u8]| {
        encoding_rs::SHIFT_JIS.decode_without_bom_handling_and_without_replacement(part).map(|text| text.into_owned())
    };
    let mut out = Vec::with_capacity(bytes.len() * 3 / 2);
    let mut at = 0;
    while at < bytes.len() {
        let byte = bytes[at];
        if byte < 0x80 {
            out.push(byte);
            at += 1;
            continue;
        }
        let pair = bytes.get(at..at + 2).and_then(sjis).filter(|text| text.chars().count() == 1);
        match (pair, sjis(&bytes[at..at + 1])) {
            (Some(text), _) => {
                out.extend_from_slice(text.as_bytes());
                at += 2;
            }
            (None, Some(text)) => {
                out.extend_from_slice(text.as_bytes());
                at += 1;
            }
            (None, None) => {
                out.push(byte);
                at += 1;
            }
        }
    }
    out
}

fn nul_terminated(bytes: &[u8]) -> &[u8] {
    &bytes[..bytes.iter().position(|b| *b == 0).unwrap_or(bytes.len())]
}

impl TableIr {
    /// A disc table (big-endian BCSV); None when the bytes are not one.
    pub fn from_bcsv(bytes: &[u8]) -> Result<Option<TableIr>> {
        let Some((entries, fields, data, size)) = bcsv_layout(bytes) else {
            return Ok(None);
        };
        let strings = data + entries * size;
        let mut sources = Vec::with_capacity(fields);
        let mut columns = Vec::with_capacity(fields);
        for f in 0..fields {
            let at = 16 + 12 * f;
            let hash = be32(bytes, at)?;
            let mask = be32(bytes, at + 4)?;
            let offset = u16::from_be_bytes([bytes[at + 8], bytes[at + 9]]) as usize;
            let shift = u32::from(bytes[at + 10]);
            let source = bytes[at + 11];
            let full = match source {
                4 => 0xFFFF,
                5 => 0xFF,
                _ => u32::MAX,
            };
            let (kind, width) = match source {
                2 => (Kind::Float, 32),
                1 | 6 => (Kind::Text, 32),
                0 | 4 | 5 if mask == full && shift == 0 => (Kind::Int, (bcsv_width(source).unwrap() * 8) as u8),
                _ => (Kind::Field, 32),
            };
            let mask = match kind {
                Kind::Int | Kind::Field => (mask & full) >> shift,
                _ => u32::MAX,
            };
            columns.push(Column { hash, mask, kind, width });
            sources.push((offset, shift, source, be32(bytes, at + 4)?));
        }
        let mut rows = Vec::with_capacity(entries);
        for e in 0..entries {
            let entry = data + e * size;
            let mut row = Vec::with_capacity(fields);
            for &(offset, shift, source, mask) in &sources {
                let place = entry + offset;
                let cell = match source {
                    6 => {
                        let at = strings + be32(bytes, place)? as usize;
                        Cell::Text(decode_text(nul_terminated(&bytes[at..])))
                    }
                    1 => {
                        let end = (place + 32).min(entry + size);
                        Cell::Text(decode_text(nul_terminated(bytes.get(place..end).context("an embedded string past the row")?)))
                    }
                    2 => Cell::Bits(be32(bytes, place)?),
                    4 => Cell::Bits((u32::from(u16::from_be_bytes([bytes[place], bytes[place + 1]])) & mask) >> shift),
                    5 => Cell::Bits((u32::from(bytes[place]) & mask) >> shift),
                    _ => Cell::Bits((be32(bytes, place)? & mask) >> shift),
                };
                row.push(cell);
            }
            rows.push(row);
        }
        Ok(Some(TableIr { columns, rows }))
    }

    /// A cooked table.
    pub fn from_ntbl(bytes: &[u8]) -> Result<TableIr> {
        if !bytes.starts_with(MAGIC) {
            bail!("not a cooked table");
        }
        let rows = le32(bytes, 4)? as usize;
        let count = le32(bytes, 8)? as usize;
        let rows_at = le32(bytes, 12)? as usize;
        let text_at = le32(bytes, 16)? as usize;
        let text_size = le32(bytes, 20)? as usize;
        let text = bytes.get(text_at..text_at + text_size).context("a table's text past its end")?;
        let mut columns = Vec::with_capacity(count);
        for c in 0..count {
            let at = HEADER + COLUMN * c;
            let kind = match bytes.get(at + 8).context("past the table's end")? {
                0 => Kind::Int,
                1 => Kind::Field,
                2 => Kind::Float,
                3 => Kind::Text,
                other => bail!("column {c} of kind {other}"),
            };
            columns.push(Column { hash: le32(bytes, at)?, mask: le32(bytes, at + 4)?, kind, width: bytes[at + 9] });
        }
        let mut out = Vec::with_capacity(rows);
        for r in 0..rows {
            let mut row = Vec::with_capacity(count);
            for (c, column) in columns.iter().enumerate() {
                let cell = le32(bytes, rows_at + 4 * (r * count + c))?;
                row.push(match column.kind {
                    Kind::Text => {
                        let at = text.get(cell as usize..).context("a word past the table's text")?;
                        Cell::Text(nul_terminated(at).to_vec())
                    }
                    _ => Cell::Bits(cell),
                });
            }
            out.push(row);
        }
        Ok(TableIr { columns, rows: out })
    }

    /// The cooked table: equal words share their bytes.
    pub fn to_ntbl(&self) -> Vec<u8> {
        let count = self.columns.len();
        let rows_at = HEADER + COLUMN * count;
        let text_at = rows_at + 4 * count * self.rows.len();
        let mut text = vec![0u8];
        let mut placed: HashMap<&[u8], u32> = HashMap::new();
        placed.insert(b"", 0);
        let mut out = Vec::with_capacity(text_at + 64);
        out.extend_from_slice(MAGIC);
        for word in [self.rows.len(), count, rows_at, text_at, 0, 0, 0] {
            out.extend_from_slice(&(word as u32).to_le_bytes());
        }
        for column in &self.columns {
            out.extend_from_slice(&column.hash.to_le_bytes());
            out.extend_from_slice(&column.mask.to_le_bytes());
            out.extend_from_slice(&[column.kind as u8, column.width]);
            out.extend_from_slice(&COLUMN_RESERVED);
        }
        for row in &self.rows {
            for cell in row {
                let word = match cell {
                    Cell::Bits(bits) => *bits,
                    Cell::Text(word) => *placed.entry(word.as_slice()).or_insert_with(|| {
                        let at = text.len() as u32;
                        text.extend_from_slice(word);
                        text.push(0);
                        at
                    }),
                };
                out.extend_from_slice(&word.to_le_bytes());
            }
        }
        out[20..24].copy_from_slice(&(text.len() as u32).to_le_bytes());
        out.extend_from_slice(&text);
        while out.len() % 32 != 0 {
            out.push(0);
        }
        out
    }

    pub(crate) fn column_of(&self, key: &str) -> Result<Option<usize>> {
        let hash = key_hash(key)?;
        Ok(self.columns.iter().position(|c| c.hash == hash))
    }

    /// A cell of `column` holding a mod's value. A word may be `{ "bytes": "<hex>" }` for one that is not UTF-8
    /// (contract table-keys 5).
    pub(crate) fn cell(column: &Column, key: &str, value: &Value) -> Result<Cell> {
        Ok(match column.kind {
            Kind::Text => Cell::Text(word_bytes(value).with_context(|| format!("{key} is a word"))?),
            Kind::Float => Cell::Bits((value.as_f64().with_context(|| format!("{key} is a number"))? as f32).to_bits()),
            Kind::Int | Kind::Field => Cell::Bits(value.as_i64().with_context(|| format!("{key} is a whole number"))? as u32 & column.mask),
        })
    }

    pub(crate) fn cell_is(column: &Column, cell: &Cell, wanted: &Value) -> bool {
        match (column.kind, cell) {
            (Kind::Text, Cell::Text(word)) => word_bytes(wanted).as_deref() == Some(word.as_slice()),
            (Kind::Float, Cell::Bits(bits)) => wanted.as_f64().is_some_and(|w| (w as f32 - f32::from_bits(*bits)).abs() < 0.0001),
            (_, Cell::Bits(bits)) => wanted.as_i64().is_some_and(|w| w as u32 & column.mask == *bits),
            _ => false,
        }
    }

    /// Rows a mod adds: a column left out is 0 or "", a column the table lacks is left out (another table of the same
    /// name in another layer may have it, and the row still counts).
    pub fn add_rows(&mut self, rows: &[Value]) -> Result<()> {
        for (r, row) in rows.iter().enumerate() {
            let fields = row.as_object().with_context(|| format!("row {} isn't a set of columns", r + 1))?;
            let mut cells: Vec<Cell> =
                self.columns.iter().map(|c| if c.kind == Kind::Text { Cell::Text(Vec::new()) } else { Cell::Bits(0) }).collect();
            for (key, value) in fields {
                if let Some(c) = self.column_of(key)? {
                    cells[c] = Self::cell(&self.columns[c], key, value)?;
                }
            }
            self.rows.push(cells);
        }
        Ok(())
    }

    /// A mod's `remove` (drops the rows matching any of its sets of columns) and `change` (a list of
    /// `{ "where": { columns }, "set": { columns } }`), in that order. A remove or change that matches no row is not an
    /// error (another mod's rows may come later), but it is named: what it said, for the log and the Mods screen.
    pub fn edit_rows(&mut self, root: &Value) -> Result<Vec<String>> {
        let mut unmatched = Vec::new();
        let matches = |table: &TableIr, row: &[Cell], wanted: &serde_json::Map<String, Value>| -> Result<bool> {
            for (key, value) in wanted {
                let c = table.column_of(key)?.with_context(|| format!("this table has no column {key}"))?;
                if !Self::cell_is(&table.columns[c], &row[c], value) {
                    return Ok(false);
                }
            }
            Ok(true)
        };
        if let Some(remove) = root.get("remove").and_then(Value::as_array) {
            for wanted in remove {
                let wanted = wanted.as_object().context("each row of remove is a set of columns")?;
                let before = self.rows.len();
                let mut kept = Vec::with_capacity(before);
                for row in std::mem::take(&mut self.rows) {
                    if !matches(self, &row, wanted)? {
                        kept.push(row);
                    }
                }
                if kept.len() == before {
                    unmatched.push(format!("remove {} matched no row", Value::Object(wanted.clone())));
                }
                self.rows = kept;
            }
        }
        if let Some(changes) = root.get("change").and_then(Value::as_array) {
            for change in changes {
                let wanted = change.get("where").and_then(Value::as_object).context("each change has a where")?;
                let set = change.get("set").and_then(Value::as_object).context("each change has a set")?;
                let mut sets = Vec::with_capacity(set.len());
                for (key, value) in set {
                    let c = self.column_of(key)?.with_context(|| format!("this table has no column {key}"))?;
                    sets.push((c, Self::cell(&self.columns[c], key, value)?));
                }
                let mut changed = false;
                for r in 0..self.rows.len() {
                    if matches(self, &self.rows[r], wanted)? {
                        changed = true;
                        for (c, cell) in &sets {
                            self.rows[r][*c] = cell.clone();
                        }
                    }
                }
                if !changed {
                    unmatched.push(format!("change where {} matched no row", Value::Object(wanted.clone())));
                }
            }
        }
        Ok(unmatched)
    }

    /// A whole table from JSON: `{ "rows": [ { "column": value } ], "columns": { "column": "float" } }`. A column's kind
    /// comes from its values: text, whole numbers, or numbers with a fraction. `columns` says it for one that has no
    /// value yet, or a float whose values all happen to be whole.
    pub fn from_json(text: &[u8]) -> Result<TableIr> {
        let root: Value = serde_json::from_slice(text).context("not JSON")?;
        let rows = root.get("rows").and_then(Value::as_array).context("it has no rows")?;
        let mut names: Vec<String> = Vec::new();
        let mut kinds: HashMap<String, Kind> = HashMap::new();
        if let Some(columns) = root.get("columns").and_then(Value::as_object) {
            for (name, kind) in columns {
                let kind = match kind.as_str() {
                    Some("int") => Kind::Int,
                    Some("float") => Kind::Float,
                    Some("string") => Kind::Text,
                    _ => bail!("column {name} is int, float or string"),
                };
                names.push(name.clone());
                kinds.insert(name.clone(), kind);
            }
        }
        for (r, row) in rows.iter().enumerate() {
            let fields = row.as_object().with_context(|| format!("row {} isn't a set of columns", r + 1))?;
            for (name, value) in fields {
                let kind = match value {
                    Value::String(_) => Kind::Text,
                    Value::Number(n) if n.is_f64() => Kind::Float,
                    Value::Number(_) => Kind::Int,
                    _ => bail!("row {}: {name} is text or a number", r + 1),
                };
                match kinds.get(name) {
                    None => {
                        names.push(name.clone());
                        kinds.insert(name.clone(), kind);
                    }
                    // Whole numbers fit a column with fractions; nothing else mixes.
                    Some(Kind::Int) if kind == Kind::Float => {
                        kinds.insert(name.clone(), Kind::Float);
                    }
                    Some(Kind::Float) if kind == Kind::Int => {}
                    Some(known) if *known == kind => {}
                    Some(_) => bail!("row {}: {name} has been another kind of value", r + 1),
                }
            }
        }
        let columns = names
            .iter()
            .map(|name| Ok(Column { hash: key_hash(name)?, mask: u32::MAX, kind: kinds[name], width: 32 }))
            .collect::<Result<Vec<Column>>>()?;
        let mut table = TableIr { columns, rows: Vec::with_capacity(rows.len()) };
        table.add_rows(rows)?;
        Ok(table)
    }
}

/// A table file cooked: Some(NTBL) for a disc table, None for a file that is not one or is cooked already.
/// `listed`: the name says it is a table, so one that does not read as one stops the cook.
pub fn cook(bytes: &[u8], listed: bool) -> Result<Option<Vec<u8>>> {
    if bytes.starts_with(MAGIC) {
        return Ok(None);
    }
    match TableIr::from_bcsv(bytes)? {
        Some(table) => {
            let cooked = table.to_ntbl();
            #[cfg(feature = "proof-tools")]
            proof::reads_agree(bytes, &cooked)?;
            Ok(Some(cooked))
        }
        None if listed => bail!("a table that does not read as one"),
        None => Ok(None),
    }
}

/// Whether the bytes are a disc table (big-endian BCSV).
pub fn is_bcsv(bytes: &[u8]) -> bool {
    bcsv_layout(bytes).is_some()
}

/// How many rows a cooked table has.
pub fn row_count(table: &[u8]) -> Result<usize> {
    if !table.starts_with(MAGIC) {
        bail!("not a cooked table");
    }
    Ok(le32(table, 4)? as usize)
}

/// A mod's rows for a table.
pub fn read_rows(text: &[u8]) -> Result<Vec<Value>> {
    let root: Value = serde_json::from_slice(text).context("not JSON")?;
    if root.get("rows").is_none() && (root.get("change").is_some() || root.get("remove").is_some()) {
        return Ok(Vec::new());
    }
    root.get("rows").and_then(Value::as_array).cloned().context("it has no rows")
}

/// A cooked table with a mod's `merge/` file applied: its remove and change, then its rows. With it, each remove or
/// change that matched no row (edit_rows).
pub fn merge(table: &[u8], text: &[u8], rows: &[Value]) -> Result<(Vec<u8>, Vec<String>)> {
    let root: Value = serde_json::from_slice(text).context("not JSON")?;
    let mut ir = TableIr::from_ntbl(table)?;
    let unmatched = ir.edit_rows(&root)?;
    ir.add_rows(rows)?;
    Ok((ir.to_ntbl(), unmatched))
}

/// The game's reads of a disc table, old (BCSV, as Game/Util/Table read it in host order) against new (NTBL, as it
/// reads now): every row x column x read (u32, s32, f32, bool, text) agrees. The cook stops on any difference.
#[cfg(feature = "proof-tools")]
pub mod proof {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    pub static READS: AtomicU64 = AtomicU64::new(0);
    pub static TABLES: AtomicU64 = AtomicU64::new(0);

    /// A read's result: None for a failed read.
    #[derive(PartialEq, Debug)]
    struct Reads {
        u32: Option<u32>,
        s32: Option<i32>,
        f32: u32,
        bool: bool,
        text: Option<Vec<u8>>,
    }

    fn old_reads(bytes: &[u8], data: usize, size: usize, entries: usize, row: usize, column: usize) -> Result<Reads> {
        let at = 16 + 12 * column;
        let mask = be32(bytes, at + 4)?;
        let offset = u16::from_be_bytes([bytes[at + 8], bytes[at + 9]]) as usize;
        let shift = u32::from(bytes[at + 10]);
        let kind = bytes[at + 11];
        let place = data + row * size + offset;
        let raw4 = |p: usize| -> u32 {
            let mut b = [0u8; 4];
            for (i, slot) in b.iter_mut().enumerate() {
                *slot = *bytes.get(p + i).unwrap_or(&0);
            }
            u32::from_be_bytes(b)
        };
        let raw = match kind {
            0 | 3 => Some(raw4(place)),
            4 => Some(u32::from(u16::from_be_bytes([bytes[place], bytes[place + 1]]))),
            5 => Some(u32::from(bytes[place])),
            _ => None,
        };
        let s32 = match kind {
            0 if shift == 0 && mask == u32::MAX => Some(raw4(place) as i32),
            4 if shift == 0 && mask == 0xFFFF => Some(i32::from(u16::from_be_bytes([bytes[place], bytes[place + 1]]) as i16)),
            5 if shift == 0 && mask == 0xFF => Some(i32::from(bytes[place] as i8)),
            _ => None,
        };
        let text = match kind {
            6 => {
                let pool = data + entries * size;
                let at = pool + raw4(place) as usize;
                Some(decode_text(nul_terminated(&bytes[at..])))
            }
            1 => Some(decode_text(nul_terminated(&bytes[place..(place + 32).min(data + row * size + size)]))),
            _ => None,
        };
        // The game read a field's bool from 4 bytes at its offset, host order: the cooker swapped a short or a byte
        // in place, so the masked bits are the value's.
        let bool_bits = match kind {
            4 => u32::from(u16::from_be_bytes([bytes[place], bytes[place + 1]])),
            5 => u32::from(bytes[place]),
            _ => raw4(place),
        };
        Ok(Reads {
            u32: raw.map(|r| (r & mask) >> shift),
            s32,
            f32: if kind == 2 { raw4(place) } else { 0 },
            bool: !matches!(kind, 1 | 6) && bool_bits & mask != 0,
            text,
        })
    }

    fn new_reads(table: &[u8], row: usize, column: usize) -> Result<Reads> {
        let count = le32(table, 8)? as usize;
        let rows_at = le32(table, 12)? as usize;
        let text_at = le32(table, 16)? as usize;
        let at = HEADER + COLUMN * column;
        let kind = table[at + 8];
        let width = table[at + 9];
        let cell = le32(table, rows_at + 4 * (row * count + column))?;
        Ok(Reads {
            u32: matches!(kind, 0 | 1).then_some(cell),
            s32: (kind == 0).then(|| match width {
                8 => i32::from(cell as u8 as i8),
                16 => i32::from(cell as u16 as i16),
                _ => cell as i32,
            }),
            f32: if kind == 2 { cell } else { 0 },
            bool: kind != 3 && cell != 0,
            text: (kind == 3).then(|| nul_terminated(&table[text_at + cell as usize..]).to_vec()),
        })
    }

    /// Every read of `bcsv` (big-endian) and its cooked `ntbl` agrees. f32 reads are compared on float columns and bool
    /// reads on non-text columns only: the game makes no other.
    pub fn reads_agree(bcsv: &[u8], ntbl: &[u8]) -> Result<()> {
        let (entries, fields, data, size) = bcsv_layout(bcsv).context("not a table")?;
        for row in 0..entries {
            for column in 0..fields {
                let old = old_reads(bcsv, data, size, entries, row, column)?;
                let new = new_reads(ntbl, row, column)?;
                if old != new {
                    bail!("row {row} column {column}: BCSV reads {old:?}, NTBL reads {new:?}");
                }
            }
        }
        READS.fetch_add((entries * fields * 5) as u64, Ordering::Relaxed);
        TABLES.fetch_add(1, Ordering::Relaxed);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A disc table: an id (long), a name (string offset), a scale (float), a packed byte at bits 8-15, a short.
    fn bcsv() -> Vec<u8> {
        let mut out = Vec::new();
        for word in [2u32, 5, 16 + 12 * 5, 14] {
            out.extend_from_slice(&word.to_be_bytes());
        }
        let mut column = |hash: u32, mask: u32, offset: u16, shift: u8, kind: u8| {
            out.extend_from_slice(&hash.to_be_bytes());
            out.extend_from_slice(&mask.to_be_bytes());
            out.extend_from_slice(&offset.to_be_bytes());
            out.extend_from_slice(&[shift, kind]);
        };
        column(column_hash("id"), u32::MAX, 0, 0, 0);
        column(column_hash("name"), u32::MAX, 4, 0, 6);
        column(column_hash("scale"), u32::MAX, 8, 0, 2);
        column(column_hash("packed"), 0xFF00, 0, 8, 0);
        column(column_hash("short"), 0xFFFF, 12, 0, 4);
        for (id, name, scale, short) in [(0x0000_1203u32, 0u32, 1.5f32, 0xFFFEu16), (0x0000_3404, 7, -2.0, 7)] {
            out.extend_from_slice(&id.to_be_bytes());
            out.extend_from_slice(&name.to_be_bytes());
            out.extend_from_slice(&scale.to_bits().to_be_bytes());
            out.extend_from_slice(&short.to_be_bytes());
        }
        out.extend_from_slice(b"Kuribo\0");
        out.extend_from_slice(&[0x83, 0x65, 0x83, 0x58, 0x83, 0x67, 0]); // テスト
        out
    }

    #[test]
    fn a_disc_table_cooks_masked_and_in_utf8() {
        let ir = TableIr::from_bcsv(&bcsv()).unwrap().unwrap();
        assert_eq!(ir.columns[0].kind, Kind::Int);
        assert_eq!(ir.columns[3], Column { hash: column_hash("packed"), mask: 0xFF, kind: Kind::Field, width: 32 });
        assert_eq!(ir.columns[4].width, 16);
        assert_eq!(ir.rows[0][3], Cell::Bits(0x12));
        assert_eq!(ir.rows[1][1], Cell::Text("テスト".into()));
        let cooked = cook(&bcsv(), true).unwrap().unwrap();
        assert!(cooked.starts_with(MAGIC));
        assert_eq!(TableIr::from_ntbl(&cooked).unwrap(), ir);
        assert_eq!(cook(&cooked, true).unwrap(), None);
        assert!(cook(b"not a table at all", true).is_err());
    }

    #[test]
    fn a_byte_no_character_starts_with_stays_itself() {
        // "e:ワープカメラ 0-" and %c of 80, as AstroGalaxy's camera table and WarpCube make it.
        let disc = [b"e:".as_slice(), &[0x83, 0x8F, 0x81, 0x5B, 0x83, 0x76, 0x83, 0x4A, 0x83, 0x81, 0x83, 0x89], b" 0-", &[0x91]].concat();
        let code = ["e:ワープカメラ 0-".as_bytes(), &[0x91]].concat();
        assert_eq!(decode_text(&disc), code);
        // Half-width katakana is one byte a character.
        assert_eq!(decode_text(&[0xB1, 0x41]), "ｱA".as_bytes());
    }

    #[test]
    fn a_mod_adds_and_changes_rows() {
        let cooked = cook(&bcsv(), true).unwrap().unwrap();
        let text = br#"{ "remove": [ { "name": "Kuribo" } ], "change": [ { "where": { "packed": 52 }, "set": { "scale": 3.0 } } ] }"#;
        let rows = vec![serde_json::json!({ "id": 9, "packed": 0x1FF, "name": "Teresa", "missing": 1 })];
        let (merged, unmatched) = merge(&cooked, text, &rows).unwrap();
        assert!(unmatched.is_empty());
        let merged = TableIr::from_ntbl(&merged).unwrap();
        assert_eq!(merged.rows.len(), 2);
        assert_eq!(merged.rows[0][2], Cell::Bits(3.0f32.to_bits()));
        assert_eq!(merged.rows[1][0], Cell::Bits(9));
        assert_eq!(merged.rows[1][3], Cell::Bits(0xFF));
        assert_eq!(merged.rows[1][1], Cell::Text("Teresa".into()));
        assert_eq!(merged.rows[1][2], Cell::Bits(0));
    }

    #[test]
    fn a_change_or_remove_matching_no_row_is_named() {
        let cooked = cook(&bcsv(), true).unwrap().unwrap();
        let text = br#"{ "remove": [ { "name": "Nobody" } ], "change": [ { "where": { "id": 77 }, "set": { "scale": 3.0 } } ] }"#;
        let (merged, unmatched) = merge(&cooked, text, &[]).unwrap();
        assert_eq!(TableIr::from_ntbl(&merged).unwrap().rows.len(), 2);
        assert_eq!(unmatched.len(), 2);
        assert!(unmatched[0].starts_with("remove ") && unmatched[0].contains("Nobody"));
        assert!(unmatched[1].starts_with("change where ") && unmatched[1].contains("77"));
        // A column the table lacks is still an error.
        assert!(merge(&cooked, br#"{ "change": [ { "where": { "nope": 1 }, "set": { "id": 1 } } ] }"#, &[]).is_err());
    }

    #[test]
    fn a_new_table_comes_from_json() {
        let table = TableIr::from_json(br#"{ "rows": [ { "A": 1, "B": "x" }, { "A": 2.5 } ] }"#).unwrap();
        assert_eq!(table.columns[0].kind, Kind::Float);
        assert_eq!(table.columns[1].kind, Kind::Text);
        assert_eq!(table.rows[1][1], Cell::Text(Vec::new()));
        assert!(TableIr::from_json(br#"{ "rows": [ { "A": 1 }, { "A": "x" } ] }"#).is_err());
        assert!(TableIr::from_json(br#"{ "rows": [ { "A": [] } ] }"#).is_err());
        assert_eq!(table.to_ntbl().len() % 32, 0);
        // A column named by its hash is that column, with its value.
        let hashed = TableIr::from_json(br#"{ "rows": [ { "0xe4ef3738": "Normal", "Material": "Normal" } ] }"#).unwrap();
        assert_eq!(hashed.columns[0].hash, 0xe4ef_3738);
        assert_eq!(hashed.rows[0][0], Cell::Text(b"Normal".to_vec()));
        assert_eq!(hashed.columns[1].hash, column_hash("Material"));
        assert!(TableIr::from_json(br#"{ "rows": [ { "0xnothex": 1 } ] }"#).is_err());
    }
}
