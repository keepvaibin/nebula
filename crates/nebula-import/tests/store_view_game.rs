//! Every table of a real imported game through the validator's view (contract table-keys 1): run with
//! LUMA_GAME_PAK=<game.pak> and --ignored. Each table's key is found, its rows are known, and only the files the
//! contract names repeat a key.

use luma_validate::{ColumnType, Store};
use nebula_import::cook::patch::{apply, ModPatch};
use nebula_import::store_view::PakStore;
use serde_json::{json, Map, Value};

#[test]
#[ignore = "needs an imported game: LUMA_GAME_PAK"]
fn every_table_of_the_game_has_its_keyed_rows() {
    let pak = std::env::var("LUMA_GAME_PAK").expect("LUMA_GAME_PAK names a game.pak");
    let store = PakStore::open(std::path::Path::new(&pak)).unwrap();
    let mut files: Vec<String> = store.files().map(str::to_string).collect();
    files.sort();
    let (mut tables, mut by_place, mut repeats, mut failures) = (0, 0, Vec::new(), Vec::new());
    for path in &files {
        let Some(table) = store.table(path) else { continue };
        tables += 1;
        if table.key == ["#"] {
            by_place += 1;
        }
        if store.path(&path.to_lowercase()).as_deref() != Some(path.as_str()) {
            failures.push(format!("{path}: not found by its own path"));
        }
        match &table.rows {
            None => failures.push(format!("{path}: key {:?} not in the table", table.key)),
            Some(rows) => {
                let n = table.cells.as_ref().map_or(0, Vec::len);
                let counted: usize = rows.len() + table.repeated.values().map(|c| c - 1).sum::<usize>();
                if counted != n {
                    failures.push(format!("{path}: {} ids for {n} rows", counted));
                }
            }
        }
        if !table.repeated.is_empty() {
            repeats.push(path.clone());
        }
        // The applier adds a row to it: a key no row has, every other column left to the kind's usual value.
        let mut ir = store.table_ir(path).unwrap().unwrap();
        let before = ir.rows.len();
        let mut row = Map::new();
        if table.key != ["#"] {
            for k in &table.key {
                let ty = table.columns.iter().find(|(n, _)| n == k).map(|(_, t)| *t);
                let value = match ty {
                    Some(ColumnType::Text) => json!("LumaCoreTestRow"),
                    Some(ColumnType::Float) => json!(12345.5),
                    Some(ColumnType::Int { max, .. }) => json!(max),
                    None => Value::Null,
                };
                row.insert(k.clone(), value);
            }
        }
        let patch = json!({"patch": 1, "key": table.key, "add": [Value::Object(row)]});
        match apply(path, &mut ir, &[ModPatch { mod_id: "test", file: "patch.json", patch: &patch }]) {
            Ok(facts) if ir.rows.len() == before + 1 && facts.is_empty() => {}
            Ok(facts) if !facts.is_empty() => failures.push(format!("{path}: an added row: {facts:?}")),
            Ok(_) => failures.push(format!("{path}: an added row made {} rows of {before}", ir.rows.len())),
            Err(e) => failures.push(format!("{path}: an added row: {e:#}")),
        }
    }
    println!("{tables} tables, {by_place} by place, {} repeat a key: {repeats:?}", repeats.len());
    assert!(tables > 0, "no tables in {pak}");
    assert!(failures.is_empty(), "\n{}", failures.join("\n"));
    assert!(repeats.iter().all(|p| p.to_lowercase().contains("/jmp/generalpos/")), "{repeats:?}");
}
