//! The validator's view of an imported game: `luma_validate::Store` over a game.pak (cook::store's format), so LumaLink
//! checks a mod's layer files and patches against the player's own game (L302, L303, L401-L407). Tables are the store's
//! cooked tables (NTBL); each is keyed as contract table-keys says (cook::table_keys), its columns known by hash, its
//! rows and values given as the patch rules and the applier (cook::patch) both write them.

use crate::cook::patch::cell_text;
use crate::cook::store::{store_path, MAGIC};
use crate::cook::table::{self, Column, Kind, TableIr};
use crate::cook::table_columns::{columns_of, widen_empty};
use crate::cook::table_keys::{self, Key};
use anyhow::{bail, Context, Result};
use luma_validate::{ColumnType, Store, Table};
use std::collections::HashMap;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

const NODE: usize = 40;

/// Disc paths (lower case) that may be new, the kinds the game finds by name (luma-format 5): each with why. A kind's
/// contract adds its own here.
const NEW: [(&str, &str); 4] = [
    ("objectdata/*.arc", "an object's archive: the game loads ObjectData/<name>.arc by the object's name"),
    ("**/*.arc/**", "a file in an archive: the game's resource holders find an archive's files by name"),
    ("stagedata/**", "a galaxy or zone (3.9): the game finds a stage's archives by the stage's name"),
    ("**/layoutdata/*.arc", "a layout's archive: the game loads it by the layout's name"),
];

struct Entry {
    /// The store path in the game's case.
    path: String,
    position: u64,
    size: u64,
    folder: bool,
}

/// An imported game, for the validator.
pub struct PakStore {
    pak: PathBuf,
    game_id: String,
    /// Lower-case store paths without a leading '/'.
    entries: HashMap<String, Entry>,
    new: Vec<Vec<&'static str>>,
}

fn lower_store_path(path: &str) -> String {
    store_path(path.trim_start_matches('/')).to_ascii_lowercase()
}

impl PakStore {
    pub fn open(pak: &Path) -> Result<Self> {
        let mut file = File::open(pak).with_context(|| format!("{} cannot be read", pak.display()))?;
        let length = file.metadata()?.len();
        let mut header = [0u8; 64];
        file.read_exact(&mut header).with_context(|| format!("{} is too short to be an asset store", pak.display()))?;
        if &header[..8] != MAGIC {
            bail!("{} is not an asset store", pak.display());
        }
        let word = |at: usize| u64::from_le_bytes(header[at..at + 8].try_into().unwrap());
        let count = u32::from_le_bytes(header[12..16].try_into().unwrap()) as usize;
        let (nodes_at, names_at, names_size) = (word(16), word(32), word(40));
        // A damaged header may say any size: room is made only for what the file holds.
        let nodes_size = count.checked_mul(NODE).filter(|&n| n as u64 <= length).context("the store's index is larger than the store")?;
        let names_size = usize::try_from(names_size).ok().filter(|&n| n as u64 <= length).context("the store's names are larger than the store")?;
        let mut nodes = vec![0u8; nodes_size];
        file.seek(SeekFrom::Start(nodes_at))?;
        file.read_exact(&mut nodes).context("the store's index is cut short")?;
        let mut names = vec![0u8; names_size];
        file.seek(SeekFrom::Start(names_at))?;
        file.read_exact(&mut names).context("the store's names are cut short")?;

        let mut full: Vec<String> = Vec::with_capacity(count);
        let mut entries = HashMap::with_capacity(count);
        for i in 0..count {
            let node = &nodes[i * NODE..(i + 1) * NODE];
            let u64_at = |at: usize| u64::from_le_bytes(node[at..at + 8].try_into().unwrap());
            let u32_at = |at: usize| u32::from_le_bytes(node[at..at + 4].try_into().unwrap());
            let (position, size, name_at, parent, flags) = (u64_at(0), u64_at(8), u32_at(16) as usize, u32_at(20) as usize, u32_at(24));
            if i == 0 {
                full.push(String::new());
                continue;
            }
            let tail = names.get(name_at..).unwrap_or(&[]);
            let end = tail.iter().position(|&b| b == 0).unwrap_or(tail.len());
            let name = String::from_utf8_lossy(&tail[..end]).into_owned();
            let path = match full.get(parent).map(String::as_str) {
                Some("") | None => name,
                Some(parent) => format!("{parent}/{name}"),
            };
            entries.insert(path.to_ascii_lowercase(), Entry { path: path.clone(), position, size, folder: flags & 1 != 0 });
            full.push(path);
        }
        let game_id = String::from_utf8_lossy(&header[48..54]).into_owned();
        Ok(Self { pak: pak.to_path_buf(), game_id, entries, new: NEW.iter().map(|(p, _)| p.split('/').collect()).collect() })
    }

    pub fn game_id(&self) -> &str {
        &self.game_id
    }

    /// Every file's store path, in the game's case (in no set order).
    pub fn files(&self) -> impl Iterator<Item = &str> {
        self.entries.values().filter(|e| !e.folder).map(|e| e.path.as_str())
    }

    fn entry(&self, path: &str) -> Option<&Entry> {
        self.entries.get(&lower_store_path(path))
    }

    fn read(&self, entry: &Entry, size: u64) -> Result<Vec<u8>> {
        let mut file = File::open(&self.pak).with_context(|| format!("{} cannot be read", self.pak.display()))?;
        let length = file.metadata()?.len();
        if entry.position.checked_add(size).map_or(true, |end| end > length) {
            bail!("the store is damaged: {} lies past its end", entry.path);
        }
        let mut bytes = vec![0u8; usize::try_from(size)?];
        file.seek(SeekFrom::Start(entry.position))?;
        file.read_exact(&mut bytes)?;
        Ok(bytes)
    }

    /// A cooked table of the game, by disc or store path in any case; None when the path is no table.
    pub fn table_ir(&self, path: &str) -> Result<Option<TableIr>> {
        let Some(entry) = self.entry(path).filter(|e| !e.folder && e.size >= table::MAGIC.len() as u64) else { return Ok(None) };
        if self.read(entry, table::MAGIC.len() as u64)? != table::MAGIC {
            return Ok(None);
        }
        let bytes = self.read(entry, entry.size)?;
        TableIr::from_ntbl(&bytes).with_context(|| format!("the game's {} is damaged", entry.path)).map(Some)
    }
}

/// A column's type for the validator: an int by its width, a field by its mask.
pub(crate) fn column_type(c: &Column) -> ColumnType {
    match c.kind {
        Kind::Int => {
            let width = u32::from(c.width.clamp(1, 32));
            ColumnType::Int { min: -(1i64 << (width - 1)), max: (1i64 << (width - 1)) - 1 }
        }
        Kind::Field => ColumnType::Int { min: 0, max: i64::from(c.mask) },
        Kind::Float => ColumnType::Float,
        Kind::Text => ColumnType::Text,
    }
}

/// A table as the validator knows it: keyed by its rule, every value as text; an empty one with its kind's columns,
/// as the applier patches it (table-keys 10).
pub fn table_view(path: &str, ir: &TableIr) -> Table {
    let mut wide;
    let ir = if ir.rows.is_empty() {
        wide = ir.clone();
        widen_empty(path, &mut wide);
        &wide
    } else {
        ir
    };
    let rule = table_keys::rule_for(path);
    let hash_name = |hash: u32| format!("0x{hash:08x}");
    let (key, key_at): (Vec<String>, Option<Vec<usize>>) = match &rule.key {
        Key::Position => (vec!["#".into()], Some(Vec::new())),
        Key::Columns(names) => {
            let at: Option<Vec<usize>> = names.iter().map(|n| ir.columns.iter().position(|c| c.hash == table::column_hash(n))).collect();
            (names.clone(), at)
        }
    };
    let columns = ir
        .columns
        .iter()
        .map(|c| {
            let name = match &rule.key {
                Key::Columns(names) => names.iter().find(|n| table::column_hash(n) == c.hash).cloned().unwrap_or_else(|| hash_name(c.hash)),
                Key::Position => hash_name(c.hash),
            };
            (name, column_type(c))
        })
        .collect();
    let cells: Vec<Vec<String>> = ir.rows.iter().map(|row| ir.columns.iter().zip(row).map(|(c, cell)| cell_text(c, cell)).collect()).collect();
    // Row ids as the rules write them (patches::SEP between key values); a key the table lacks leaves rows unknown,
    // and the rules say so (not checked) rather than guess.
    let ids: Option<Vec<String>> = key_at.map(|at| {
        if at.is_empty() {
            (0..cells.len()).map(|i| i.to_string()).collect()
        } else {
            cells.iter().map(|row| at.iter().map(|&c| row[c].as_str()).collect::<Vec<_>>().join("\u{1f}")).collect()
        }
    });
    let mut repeated: HashMap<String, usize> = HashMap::new();
    let rows = ids.map(|ids| {
        let mut counts: HashMap<String, usize> = HashMap::new();
        for id in &ids {
            *counts.entry(if rule.case_insensitive { id.to_lowercase() } else { id.clone() }).or_default() += 1;
        }
        repeated = counts.into_iter().filter(|(_, n)| *n > 1).collect();
        ids.into_iter().collect()
    });
    Table { key, columns, rows, repeated, cells: Some(cells), case_insensitive: rule.case_insensitive }
}

/// The game's own view of an empty table (the C store, validate_c) given its kind's columns, as `table_view` does.
pub fn widen_view(path: &str, table: &mut Table) {
    if !table.rows.as_ref().is_some_and(|r| r.is_empty()) {
        return;
    }
    for k in columns_of(&store_path(path.trim_start_matches('/'))).unwrap_or(&[]) {
        let hash = format!("0x{:08x}", k.column.hash);
        if !table.columns.iter().any(|(name, _)| luma_validate::same_column(name, &hash)) {
            table.columns.push((hash, column_type(&k.column)));
        }
    }
}

impl Store for PakStore {
    fn path(&self, lower: &str) -> Option<String> {
        self.entry(lower).map(|e| e.path.clone())
    }

    fn table(&self, path: &str) -> Option<Table> {
        // A store this program cooked; a table of it that cannot be read is no table (the patch rules then name the
        // file as one without rows).
        let ir = self.table_ir(path).ok().flatten()?;
        Some(table_view(&store_path(path.trim_start_matches('/')), &ir))
    }

    fn may_be_new(&self, path: &str) -> bool {
        let lower = path.trim_start_matches('/').to_ascii_lowercase();
        let parts: Vec<&str> = lower.split('/').collect();
        self.new.iter().any(|p| table_keys::matches(p, &parts))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::cook::table::{Cell, Column};

    fn col(name: &str, kind: Kind, width: u8, mask: u32) -> Column {
        Column { hash: table::column_hash(name), mask, kind, width }
    }

    #[test]
    fn a_keyed_table_names_its_rows_and_repeats() {
        let ir = TableIr {
            columns: vec![col("PosName", Kind::Text, 32, u32::MAX), col("Obj_ID", Kind::Int, 16, 0xffff), col("pos_x", Kind::Float, 32, u32::MAX)],
            rows: vec![
                vec![Cell::Text(b"a".to_vec()), Cell::Bits(0xffff), Cell::Bits(1.5f32.to_bits())],
                vec![Cell::Text(b"a".to_vec()), Cell::Bits(0xffff), Cell::Bits(0)],
                vec![Cell::Text(vec![0x82, 0xa0]), Cell::Bits(3), Cell::Bits(0)],
            ],
        };
        let t = table_view("StageData/X/jmp/GeneralPos/Common/GeneralPosInfo", &ir);
        assert_eq!(t.key, vec!["PosName".to_string(), "Obj_ID".into()]);
        assert_eq!(t.columns[0], ("PosName".into(), ColumnType::Text));
        assert_eq!(t.columns[1], ("Obj_ID".into(), ColumnType::Int { min: -32768, max: 32767 }));
        assert_eq!(t.columns[2], (format!("0x{:08x}", table::column_hash("pos_x")), ColumnType::Float));
        let rows = t.rows.unwrap();
        assert!(rows.contains("a\u{1f}-1") && rows.contains("{bytes:82a0}\u{1f}3"));
        assert_eq!(t.repeated.get("a\u{1f}-1"), Some(&2));
        assert_eq!(t.cells.unwrap()[0], vec!["a".to_string(), "-1".into(), "1.5".into()]);
    }

    #[test]
    fn a_store_answers_paths_tables_and_new_files() {
        use crate::cook::store::{write_index, Placed, HEADER};
        use std::io::Write;
        let dir = tempfile::tempdir().unwrap();
        let pak_path = dir.path().join("game.pak");
        let ir = TableIr { columns: vec![col("ZoneName", Kind::Text, 32, u32::MAX)], rows: vec![vec![Cell::Text(b"Main".to_vec())], vec![Cell::Text(b"Side".to_vec())]] };
        let ntbl = ir.to_ntbl();
        let model = b"J3D2bdl4 not a table".to_vec();
        let mut pak = File::create(&pak_path).unwrap();
        pak.seek(SeekFrom::Start(HEADER)).unwrap();
        pak.write_all(&ntbl).unwrap();
        pak.write_all(&model).unwrap();
        let files = [
            Placed { path: "StageData/AstroGalaxy/ZoneList.bcsv".into(), position: HEADER, size: ntbl.len() as u64, folder: false },
            Placed { path: "ObjectData/Kuribo.arc/Kuribo.bdl".into(), position: HEADER + ntbl.len() as u64, size: model.len() as u64, folder: false },
        ];
        write_index(&mut pak, HEADER + (ntbl.len() + model.len()) as u64, &files, b"RMGE01\0\0").unwrap();
        drop(pak);

        let store = PakStore::open(&pak_path).unwrap();
        assert_eq!(store.game_id(), "RMGE01");
        assert_eq!(store.path("stagedata/astrogalaxy/zonelist.bcsv").as_deref(), Some("StageData/AstroGalaxy/ZoneList.bcsv"));
        // A disc path with its archive ending finds the store's folder.
        assert_eq!(store.path("objectdata/kuribo.arc/kuribo.bdl").as_deref(), Some("ObjectData/Kuribo/Kuribo.bdl"));
        assert_eq!(store.path("objectdata/kuribo.arc").as_deref(), Some("ObjectData/Kuribo"));
        assert!(store.path("objectdata/nokotaro.arc").is_none());

        let t = store.table("StageData/AstroGalaxy/ZoneList.bcsv").unwrap();
        assert_eq!(t.key, vec!["ZoneName".to_string()]);
        assert_eq!(t.rows.unwrap(), ["Main".to_string(), "Side".into()].into_iter().collect());
        assert!(store.table("ObjectData/Kuribo/Kuribo.bdl").is_none());
        assert!(store.table("ObjectData/Kuribo").is_none());

        assert!(store.may_be_new("objectdata/nokotaro.arc"));
        assert!(store.may_be_new("objectdata/kuribo.arc/kuribo_new.btk"));
        assert!(store.may_be_new("stagedata/mygalaxy/mygalaxymap.arc"));
        assert!(!store.may_be_new("audiores/stream/new.ast"));
        assert!(!store.may_be_new("objectdata/new.bcsv"));
    }

    #[test]
    fn a_damaged_store_is_refused() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("game.pak");
        std::fs::write(&path, [0u8; 100]).unwrap();
        assert!(PakStore::open(&path).is_err());
        let mut header = [0u8; 64];
        header[..8].copy_from_slice(MAGIC);
        header[12..16].copy_from_slice(&u32::MAX.to_le_bytes());
        std::fs::write(&path, header).unwrap();
        assert!(PakStore::open(&path).is_err());
    }

    #[test]
    fn a_table_by_place_and_a_key_the_table_lacks() {
        let ir = TableIr { columns: vec![col("x", Kind::Field, 32, 0xff)], rows: vec![vec![Cell::Bits(1)], vec![Cell::Bits(2)]] };
        let t = table_view("Anything/else.bcsv", &ir);
        assert_eq!(t.key, vec!["#".to_string()]);
        assert_eq!(t.columns[0].1, ColumnType::Int { min: 0, max: 255 });
        assert_eq!(t.rows.unwrap(), ["0".to_string(), "1".into()].into_iter().collect());
        // The rule names l_id; this table has none: its rows are unknown, not guessed.
        let t = table_view("StageData/X/jmp/Placement/Common/ObjInfo", &ir);
        assert!(t.rows.is_none() && t.cells.is_some());
    }
}
