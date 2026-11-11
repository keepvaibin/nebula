//! The columns of each table kind (contract table-keys 10), from a real imported game: LUMA_GAME_PAK=<game.pak>,
//! --ignored --nocapture. With LUMA_WRITE_TABLE_COLUMNS=<file> it writes `data/table-columns.tsv`; without, it checks
//! the compiled file against the game (every kind's columns, forms and usual values).

use nebula_import::cook::patch::cell_text;
use nebula_import::cook::table::Kind;
use nebula_import::cook::table_columns::{kind_of, KINDS_HEADER};
use nebula_import::store_view::PakStore;
use std::collections::{BTreeMap, HashMap};

type Forms = HashMap<(u8, u8, u32), usize>;
/// A kind's columns by hash: the forms its files give each, and how many rows have each value.
type Kinds = BTreeMap<String, BTreeMap<u32, (Forms, HashMap<String, usize>)>>;

#[test]
#[ignore = "needs an imported game: LUMA_GAME_PAK"]
fn the_kinds_columns_are_the_games() {
    let pak = std::env::var("LUMA_GAME_PAK").expect("LUMA_GAME_PAK names a game.pak");
    let store = PakStore::open(std::path::Path::new(&pak)).unwrap();
    let mut files: Vec<String> = store.files().map(str::to_string).collect();
    files.sort();
    // kind -> hash -> (form -> files having it, value -> rows having it)
    let mut kinds: Kinds = BTreeMap::new();
    let mut tables = 0;
    for path in &files {
        let Some(kind) = kind_of(path) else { continue };
        let Ok(Some(ir)) = store.table_ir(path) else { continue };
        tables += 1;
        let k = kinds.entry(kind).or_default();
        for (c, column) in ir.columns.iter().enumerate() {
            let e = k.entry(column.hash).or_default();
            *e.0.entry((column.kind as u8, column.width, column.mask)).or_default() += 1;
            for row in &ir.rows {
                *e.1.entry(cell_text(column, &row[c])).or_default() += 1;
            }
        }
    }
    let mut out = String::from(KINDS_HEADER);
    out.push('\n');
    let mut unlike = Vec::new();
    for (kind, columns) in &kinds {
        for (hash, (forms, values)) in columns {
            if forms.len() > 1 {
                unlike.push(format!("{kind} 0x{hash:08x}: {forms:?}"));
            }
            // The form most files have (ties: the lowest), and the value most rows have (ties: the lowest text).
            let ((k, width, mask), _) = forms.iter().max_by(|a, b| a.1.cmp(b.1).then(b.0.cmp(a.0))).unwrap();
            let usual = if *k == Kind::Text as u8 {
                String::new()
            } else {
                values.iter().max_by(|a, b| a.1.cmp(b.1).then(b.0.cmp(a.0))).map(|v| v.0.clone()).unwrap_or_else(|| "0".into())
            };
            let type_name = ["int", "field", "float", "text"][*k as usize];
            out.push_str(&format!("{kind}\t0x{hash:08x}\t{type_name}\t{width}\t0x{mask:08x}\t{usual}\n"));
        }
    }
    println!("{} kinds over {tables} tables, {} columns; columns of unlike forms in one kind: {unlike:#?}", kinds.len(), out.lines().count() - 1);
    match std::env::var("LUMA_WRITE_TABLE_COLUMNS") {
        Ok(target) => std::fs::write(&target, out).unwrap(),
        Err(_) => {
            let compiled = include_str!("../data/table-columns.tsv").replace("\r\n", "\n");
            assert!(compiled == out, "data/table-columns.tsv differs from this game's tables: write it again");
        }
    }
}
