//! Files many mods add to at once: each mod brings only what it adds, in a layer's `merge/` folder, and the cooked file
//! gets all of it. Works on cooked files (host order), as the game's own store holds them.
//!
//! - `<table>.json`: rows for a table: `{ "rows": [ { "column": value, ... } ] }`. A column left out is zero or "".
//! - `Particles.jpc`, with `ParticleNames.bcsv.json` beside it: new particles. Each resource's user index in the file
//!   is its place in that list of names.

use anyhow::{bail, Context, Result};
use serde_json::Value;

fn le16(bytes: &[u8], at: usize) -> Result<u16> {
    let b = bytes.get(at..at + 2).context("past the file's end")?;
    Ok(u16::from_le_bytes([b[0], b[1]]))
}

fn le32(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 4).context("past the file's end")?;
    Ok(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
}

/// Words as tables keep them: the game's Japanese names are Shift-JIS.
pub fn table_text(text: &str) -> Vec<u8> {
    if text.is_ascii() {
        return text.as_bytes().to_vec();
    }
    encoding_rs::SHIFT_JIS.encode(text).0.into_owned()
}

/// A column's hash, as the game's tables have it.
pub fn column_hash(name: &str) -> u32 {
    name.bytes().fold(0u32, |hash, byte| hash.wrapping_mul(31).wrapping_add(u32::from(byte)))
}

struct Column {
    hash: u32,
    mask: u32,
    offset: usize,
    shift: u32,
    kind: u8,
}

fn columns(table: &[u8]) -> Result<Vec<Column>> {
    let fields = le32(table, 4)? as usize;
    let mut out = Vec::with_capacity(fields);
    for f in 0..fields {
        let at = 16 + 12 * f;
        out.push(Column {
            hash: le32(table, at)?,
            mask: le32(table, at + 4)?,
            offset: le16(table, at + 8)? as usize,
            shift: u32::from(*table.get(at + 10).context("past the file's end")?),
            kind: *table.get(at + 11).context("past the file's end")?,
        });
    }
    Ok(out)
}

/// How many rows a cooked table has.
pub fn row_count(table: &[u8]) -> Result<usize> {
    Ok(le32(table, 0)? as usize)
}

/// A cooked table with `rows` after its own. Their words go after the table's own words.
pub fn add_rows(table: &[u8], rows: &[Value]) -> Result<Vec<u8>> {
    let entries = le32(table, 0)? as usize;
    let data = le32(table, 8)? as usize;
    let size = le32(table, 12)? as usize;
    let columns = columns(table)?;
    let rows_end = data + entries * size;
    let mut pool = table.get(rows_end..).context("a table past the file's end")?.to_vec();
    let mut word = |text: &str| -> u32 {
        let at = pool.len() as u32;
        pool.extend_from_slice(&table_text(text));
        pool.push(0);
        at
    };
    let empty = word("");
    let mut added = vec![0u8; rows.len() * size];
    for (r, row) in rows.iter().enumerate() {
        let fields = row.as_object().with_context(|| format!("row {} isn't a set of columns", r + 1))?;
        let at = r * size;
        for column in &columns {
            if column.kind == 6 {
                added[at + column.offset..at + column.offset + 4].copy_from_slice(&empty.to_le_bytes());
            }
        }
        for (key, value) in fields {
            let hash = match key.strip_prefix("0x") {
                Some(hex) => u32::from_str_radix(hex, 16).with_context(|| format!("column {key}"))?,
                None => column_hash(key),
            };
            // A column this table doesn't have is set to nothing: another table of the same name in another layer may have it, and the row still counts.
            let Some(column) = columns.iter().find(|c| c.hash == hash) else {
                continue;
            };
            let place = at + column.offset;
            match column.kind {
                6 => {
                    let text = value.as_str().with_context(|| format!("{key} is a word"))?;
                    added[place..place + 4].copy_from_slice(&word(text).to_le_bytes());
                }
                2 => {
                    let number = value.as_f64().with_context(|| format!("{key} is a number"))? as f32;
                    added[place..place + 4].copy_from_slice(&number.to_le_bytes());
                }
                0 | 3 => {
                    let number = value.as_i64().with_context(|| format!("{key} is a whole number"))? as u32;
                    let old = u32::from_le_bytes(added[place..place + 4].try_into().unwrap());
                    let new = (old & !column.mask) | ((number << column.shift) & column.mask);
                    added[place..place + 4].copy_from_slice(&new.to_le_bytes());
                }
                4 => {
                    let number = value.as_i64().with_context(|| format!("{key} is a whole number"))? as u16;
                    added[place..place + 2].copy_from_slice(&number.to_le_bytes());
                }
                5 => added[place] = value.as_i64().with_context(|| format!("{key} is a whole number"))? as u8,
                other => bail!("column {key} is of kind {other}, which can't be added to"),
            }
        }
    }
    let mut out = table[..rows_end].to_vec();
    out.extend_from_slice(&added);
    out.extend_from_slice(&pool);
    while out.len() % 32 != 0 {
        out.push(0);
    }
    let total = (entries + rows.len()) as u32;
    out[0..4].copy_from_slice(&total.to_le_bytes());
    Ok(out)
}

/// A whole table from JSON, in the form the disc's are: `{ "rows": [ { "column": value } ], "columns": { "column": "float" } }`.
/// A column's kind comes from its values: text, whole numbers, or numbers with a fraction. `columns` says it for one that
/// has no value yet, or a float whose values all happen to be whole.
pub fn table_from_json(text: &[u8]) -> Result<Vec<u8>> {
    #[derive(Clone, Copy, PartialEq)]
    enum Kind {
        Int,
        Float,
        Text,
    }
    let root: Value = serde_json::from_slice(text).context("not JSON")?;
    let rows = root.get("rows").and_then(Value::as_array).context("it has no rows")?;
    let mut names: Vec<String> = Vec::new();
    let mut kinds: std::collections::BTreeMap<String, Kind> = std::collections::BTreeMap::new();
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
    let size = names.len() * 4;
    let header = 16 + names.len() * 12;
    let mut out = Vec::new();
    out.extend_from_slice(&(rows.len() as u32).to_be_bytes());
    out.extend_from_slice(&(names.len() as u32).to_be_bytes());
    out.extend_from_slice(&(header as u32).to_be_bytes());
    out.extend_from_slice(&(size as u32).to_be_bytes());
    for (i, name) in names.iter().enumerate() {
        out.extend_from_slice(&column_hash(name).to_be_bytes());
        out.extend_from_slice(&u32::MAX.to_be_bytes());
        out.extend_from_slice(&((i * 4) as u16).to_be_bytes());
        out.push(0);
        out.push(match kinds[name] {
            Kind::Int => 0,
            Kind::Float => 2,
            Kind::Text => 6,
        });
    }
    let mut pool = vec![0u8];
    for row in rows {
        for name in &names {
            let value = row.get(name);
            let word: u32 = match kinds[name] {
                Kind::Text => match value.and_then(Value::as_str) {
                    Some("") | None => 0,
                    Some(text) => {
                        let at = pool.len() as u32;
                        pool.extend_from_slice(&table_text(text));
                        pool.push(0);
                        at
                    }
                },
                Kind::Float => (value.and_then(Value::as_f64).unwrap_or(0.0) as f32).to_bits(),
                Kind::Int => value.and_then(Value::as_i64).unwrap_or(0) as u32,
            };
            out.extend_from_slice(&word.to_be_bytes());
        }
    }
    out.extend_from_slice(&pool);
    while out.len() % 32 != 0 {
        out.push(0x40);
    }
    Ok(out)
}

/// A mod's rows for a table.
pub fn read_rows(text: &[u8]) -> Result<Vec<Value>> {
    let root: Value = serde_json::from_slice(text).context("not JSON")?;
    if root.get("rows").is_none() && (root.get("change").is_some() || root.get("remove").is_some()) {
        return Ok(Vec::new());
    }
    root.get("rows").and_then(Value::as_array).cloned().context("it has no rows")
}

fn column_of<'a>(columns: &'a [Column], key: &str) -> Result<&'a Column> {
    let hash = match key.strip_prefix("0x") {
        Some(hex) => u32::from_str_radix(hex, 16).with_context(|| format!("column {key}"))?,
        None => column_hash(key),
    };
    columns.iter().find(|c| c.hash == hash).with_context(|| format!("this table has no column {key}"))
}

fn cell_is(column: &Column, row: &[u8], pool: &[u8], wanted: &Value) -> bool {
    let place = column.offset;
    match column.kind {
        6 => {
            let at = u32::from_le_bytes(row[place..place + 4].try_into().unwrap()) as usize;
            let end = pool.get(at..).and_then(|t| t.iter().position(|b| *b == 0)).map_or(at, |e| at + e);
            pool.get(at..end).is_some_and(|text| wanted.as_str().is_some_and(|w| table_text(w) == text))
        }
        2 => {
            let held = f32::from_le_bytes(row[place..place + 4].try_into().unwrap());
            wanted.as_f64().is_some_and(|w| (w as f32 - held).abs() < 0.0001)
        }
        4 => wanted.as_i64().is_some_and(|w| i64::from(u16::from_le_bytes(row[place..place + 2].try_into().unwrap())) & i64::from(column.mask) == w & i64::from(column.mask)),
        5 => wanted.as_i64().is_some_and(|w| i64::from(row[place]) == w),
        _ => {
            let held = (u32::from_le_bytes(row[place..place + 4].try_into().unwrap()) & column.mask) >> column.shift;
            wanted.as_i64().is_some_and(|w| (((w as u32) << column.shift) & column.mask) >> column.shift == held)
        }
    }
}

/// A file in `merge/` can do more than add: `remove` drops the rows matching any of its sets of columns, and `change` is a list
/// of `{ "where": { columns }, "set": { columns } }`. These run before its `rows` are added. Every row matching is changed: a
/// row that matches nothing is not an error, since another mod may have taken it.
pub fn edit_rows(table: &[u8], text: &[u8]) -> Result<Vec<u8>> {
    let root: Value = serde_json::from_slice(text).context("not JSON")?;
    if root.get("remove").is_none() && root.get("change").is_none() {
        return Ok(table.to_vec());
    }
    let entries = le32(table, 0)? as usize;
    let data = le32(table, 8)? as usize;
    let size = le32(table, 12)? as usize;
    let columns = columns(table)?;
    let rows_end = data + entries * size;
    let mut pool = table.get(rows_end..).context("a table past the file's end")?.to_vec();
    let mut rows: Vec<Vec<u8>> = table[data..rows_end].chunks_exact(size.max(1)).map(<[u8]>::to_vec).collect();
    let matches = |row: &[u8], pool: &[u8], wanted: &serde_json::Map<String, Value>| -> Result<bool> {
        for (key, value) in wanted {
            if !cell_is(column_of(&columns, key)?, row, pool, value) {
                return Ok(false);
            }
        }
        Ok(true)
    };
    if let Some(remove) = root.get("remove").and_then(Value::as_array) {
        for wanted in remove {
            let wanted = wanted.as_object().context("each row of remove is a set of columns")?;
            let mut kept = Vec::with_capacity(rows.len());
            for row in rows {
                if !matches(&row, &pool, wanted)? {
                    kept.push(row);
                }
            }
            rows = kept;
        }
    }
    if let Some(changes) = root.get("change").and_then(Value::as_array) {
        for change in changes {
            let wanted = change.get("where").and_then(Value::as_object).context("each change has a where")?;
            let set = change.get("set").and_then(Value::as_object).context("each change has a set")?;
            for row in &mut rows {
                if !matches(row, &pool, wanted)? {
                    continue;
                }
                for (key, value) in set {
                    let column = column_of(&columns, key)?;
                    let place = column.offset;
                    match column.kind {
                        6 => {
                            let text = value.as_str().with_context(|| format!("{key} is a word"))?;
                            let at = pool.len() as u32;
                            pool.extend_from_slice(&table_text(text));
                            pool.push(0);
                            row[place..place + 4].copy_from_slice(&at.to_le_bytes());
                        }
                        2 => row[place..place + 4].copy_from_slice(&(value.as_f64().with_context(|| format!("{key} is a number"))? as f32).to_le_bytes()),
                        4 => row[place..place + 2].copy_from_slice(&(value.as_i64().with_context(|| format!("{key} is a whole number"))? as u16).to_le_bytes()),
                        5 => row[place] = value.as_i64().with_context(|| format!("{key} is a whole number"))? as u8,
                        _ => {
                            let number = value.as_i64().with_context(|| format!("{key} is a whole number"))? as u32;
                            let old = u32::from_le_bytes(row[place..place + 4].try_into().unwrap());
                            let new = (old & !column.mask) | ((number << column.shift) & column.mask);
                            row[place..place + 4].copy_from_slice(&new.to_le_bytes());
                        }
                    }
                }
            }
        }
    }
    let mut out = table[..data].to_vec();
    for row in &rows {
        out.extend_from_slice(row);
    }
    out.extend_from_slice(&pool);
    while out.len() % 32 != 0 {
        out.push(0);
    }
    out[0..4].copy_from_slice(&(rows.len() as u32).to_le_bytes());
    Ok(out)
}

/// The end of a cooked particle file's resources, walking them; `visit` sees each one's place.
fn walk_resources(bytes: &[u8], mut visit: impl FnMut(usize, &[(usize, usize)]) -> Result<()>) -> Result<usize> {
    let resources = le16(bytes, 8)? as usize;
    let mut at = 0x10;
    for _ in 0..resources {
        let start = at;
        let blocks = le16(bytes, at + 2)? as usize;
        at += 8;
        let mut places = Vec::with_capacity(blocks);
        for _ in 0..blocks {
            let size = le32(bytes, at + 4)? as usize;
            if size < 8 || at + size > bytes.len() {
                bail!("a particle block at {at:#x} of {size:#x} bytes");
            }
            places.push((at, size));
            at += size;
        }
        visit(start, &places)?;
    }
    Ok(at)
}

/// A cooked particle file with `extra`'s particles after its own, their user indices from `first_index`, their
/// textures after its own.
pub fn add_particles(base: &[u8], extra: &[u8], first_index: u16) -> Result<Vec<u8>> {
    if !base.starts_with(b"JPAc2-10") || !extra.starts_with(b"JPAc2-10") {
        bail!("not a cooked particle file");
    }
    let base_resources = le16(base, 8)?;
    let base_textures = le16(base, 0xA)?;
    let base_texture_at = le32(base, 0xC)? as usize;
    let extra_resources = le16(extra, 8)?;
    let extra_textures = le16(extra, 0xA)?;
    let extra_texture_at = le32(extra, 0xC)? as usize;
    let base_end = walk_resources(base, |_, _| Ok(()))?;

    let mut moved = extra.to_vec();
    let extra_end = {
        let mut starts = Vec::new();
        let end = walk_resources(extra, |start, places| {
            starts.push((start, places.to_vec()));
            Ok(())
        })?;
        for (start, places) in starts {
            let index = le16(extra, start)?;
            let new_index = first_index.checked_add(index).context("too many particles")?;
            moved[start..start + 2].copy_from_slice(&new_index.to_le_bytes());
            for (at, size) in places {
                if &extra[at..at + 4] == b"TDB1" {
                    for place in (at + 8..at + (size & !1)).step_by(2) {
                        let texture = le16(extra, place)?;
                        let new = texture.checked_add(base_textures).context("too many particle textures")?;
                        moved[place..place + 2].copy_from_slice(&new.to_le_bytes());
                    }
                }
            }
        }
        end
    };

    let mut out = base[..base_end].to_vec();
    out.extend_from_slice(&moved[0x10..extra_end]);
    while out.len() % 32 != 0 {
        out.push(0);
    }
    let texture_at = out.len() as u32;
    out.extend_from_slice(base.get(base_texture_at..).context("particle textures past the file's end")?);
    out.extend_from_slice(moved.get(extra_texture_at..).context("particle textures past the file's end")?);
    out[8..10].copy_from_slice(&(base_resources.checked_add(extra_resources).context("too many particles")?).to_le_bytes());
    out[0xA..0xC].copy_from_slice(&(base_textures.checked_add(extra_textures).context("too many particle textures")?).to_le_bytes());
    out[0xC..0x10].copy_from_slice(&texture_at.to_le_bytes());
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A cooked table: an id (u32), a name (a word).
    fn table() -> Vec<u8> {
        let mut t = Vec::new();
        for v in [1u32, 2, 40, 8] {
            t.extend_from_slice(&v.to_le_bytes());
        }
        t.extend_from_slice(&column_hash("id").to_le_bytes());
        t.extend_from_slice(&u32::MAX.to_le_bytes());
        t.extend_from_slice(&[0, 0, 0, 0]);
        t.extend_from_slice(&column_hash("name").to_le_bytes());
        t.extend_from_slice(&u32::MAX.to_le_bytes());
        t.extend_from_slice(&[4, 0, 0, 6]);
        t.extend_from_slice(&7u32.to_le_bytes());
        t.extend_from_slice(&0u32.to_le_bytes());
        t.extend_from_slice(b"old\0");
        t
    }

    fn word(table: &[u8], row: usize) -> String {
        let entries = le32(table, 0).unwrap() as usize;
        let pool = 40 + entries * 8;
        let at = pool + le32(table, 40 + row * 8 + 4).unwrap() as usize;
        let end = table[at..].iter().position(|b| *b == 0).unwrap();
        String::from_utf8(table[at..at + end].to_vec()).unwrap()
    }

    #[test]
    fn rows_go_after_the_tables_own_with_their_words() {
        let rows = read_rows(br#"{ "rows": [ { "id": 9, "name": "new" }, { "id": 10 } ] }"#).unwrap();
        let out = add_rows(&table(), &rows).unwrap();
        assert_eq!(row_count(&out).unwrap(), 3);
        assert_eq!(le32(&out, 40).unwrap(), 7);
        assert_eq!(word(&out, 0), "old");
        assert_eq!(le32(&out, 48).unwrap(), 9);
        assert_eq!(word(&out, 1), "new");
        assert_eq!(le32(&out, 56).unwrap(), 10);
        assert_eq!(word(&out, 2), "");
    }

    #[test]
    fn a_new_table_comes_from_json() {
        let text = br#"{ "columns": { "Empty": "string", "Size": "float" }, "rows": [ { "PartName": "A", "TotalStep": 240, "Size": 1 }, { "PartName": "B", "Size": 0.5 } ] }"#;
        let table = table_from_json(text).unwrap();
        let be = |at: usize| u32::from_be_bytes(table[at..at + 4].try_into().unwrap());
        assert_eq!(be(0), 2);
        assert_eq!(be(4), 4);
        assert_eq!(be(8), 16 + 48);
        assert_eq!(be(12), 16);
        assert_eq!(table.len() % 32, 0);
        // Empty, Size, then the rows' own in the order they came.
        assert_eq!(be(16), column_hash("Empty"));
        assert_eq!(table[16 + 11], 6);
        assert_eq!(table[28 + 11], 2);
        let data = 64;
        assert_eq!(f32::from_bits(be(data + 4)), 1.0);
        assert_eq!(be(data + 12), 240);
        assert_eq!(f32::from_bits(be(data + 16 + 4)), 0.5);
        assert!(table_from_json(br#"{ "rows": [ { "A": 1 }, { "A": "x" } ] }"#).is_err());
        assert!(table_from_json(br#"{ "rows": [ { "A": [] } ] }"#).is_err());
    }

    #[test]
    fn a_column_the_table_lacks_is_named() {
        let rows = read_rows(br#"{ "rows": [ { "nope": 1 } ] }"#).unwrap();
        let error = add_rows(&table(), &rows).unwrap_err();
        assert!(format!("{error:#}").contains("no column nope"), "{error:#}");
    }

    /// A cooked particle file of resources (user index, the texture it names) and textures (bytes).
    fn particles(resources: &[(u16, u16)], textures: &[u8]) -> Vec<u8> {
        let mut p = b"JPAc2-10".to_vec();
        p.extend_from_slice(&(resources.len() as u16).to_le_bytes());
        p.extend_from_slice(&(textures.len() as u16).to_le_bytes());
        p.extend_from_slice(&[0; 4]);
        for (index, texture) in resources {
            p.extend_from_slice(&index.to_le_bytes());
            p.extend_from_slice(&1u16.to_le_bytes());
            p.extend_from_slice(&[0; 4]);
            p.extend_from_slice(b"TDB1");
            p.extend_from_slice(&12u32.to_le_bytes());
            p.extend_from_slice(&texture.to_le_bytes());
            p.extend_from_slice(&[0; 2]);
        }
        while p.len() % 32 != 0 {
            p.push(0);
        }
        let at = p.len() as u32;
        p[0xC..0x10].copy_from_slice(&at.to_le_bytes());
        for t in textures {
            p.extend_from_slice(b"TEX1");
            p.extend_from_slice(&32u32.to_le_bytes());
            p.extend_from_slice(&[*t; 24]);
        }
        p
    }

    #[test]
    fn particles_go_after_the_files_own_with_their_textures() {
        let base = particles(&[(0, 0), (1, 1)], &[0xA, 0xB]);
        let extra = particles(&[(0, 0), (1, 0)], &[0xC]);
        let out = add_particles(&base, &extra, 100).unwrap();
        assert_eq!(le16(&out, 8).unwrap(), 4);
        assert_eq!(le16(&out, 0xA).unwrap(), 3);
        let mut seen = Vec::new();
        walk_resources(&out, |start, places| {
            seen.push((le16(&out, start)?, le16(&out, places[0].0 + 8)?));
            Ok(())
        })
        .unwrap();
        assert_eq!(seen, [(0, 0), (1, 1), (100, 2), (101, 2)]);
        let textures = le32(&out, 0xC).unwrap() as usize;
        assert_eq!(textures % 32, 0);
        assert_eq!(out[textures + 8], 0xA);
        assert_eq!(out[textures + 32 + 8], 0xB);
        assert_eq!(out[textures + 64 + 8], 0xC);
    }
}
