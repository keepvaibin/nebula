//! Patches against tables as docs/modding/format/table-keys.md gives them: keys the game repeats, tables named by place
//! (`#`), whole tables (`rows`), words given as bytes, columns known only by hash, and selectors checked against the
//! game's values (L407). Every finding here is also filled in all 14 languages.

use luma_validate::semver::Version;
use luma_validate::{column_hash, validate, Arg, ColumnType, Finding, Game, Input, Source, Store, Table};
use std::collections::{HashMap, HashSet};

mod common;
use common::*;

const KEYED: &str = "Stage/Keyed.bcsv";
const POINTS: &str = "Stage/Points.bcsv";

struct Tables;

fn speed() -> String {
    format!("0x{:08x}", column_hash("speed"))
}

impl Store for Tables {
    fn path(&self, lower: &str) -> Option<String> {
        [KEYED, POINTS].iter().find(|p| p.to_lowercase() == lower).map(|p| p.to_string())
    }
    fn table(&self, path: &str) -> Option<Table> {
        let text = |rows: &[&[&str]]| rows.iter().map(|r| r.iter().map(|c| c.to_string()).collect()).collect::<Vec<Vec<String>>>();
        match path {
            KEYED => Some(Table {
                key: vec!["name".into()],
                // `speed` is a column the store knows only by its hash, as the game's tables keep it.
                columns: vec![("name".into(), ColumnType::Text), ("count".into(), ColumnType::Int { min: 0, max: 10 }), (speed(), ColumnType::Float)],
                rows: Some(HashSet::from(["Alpha".to_string(), "Beta".into(), "Twin".into()])),
                repeated: HashMap::from([("Twin".to_string(), 2)]),
                cells: Some(text(&[&["Alpha", "1", "0.5"], &["Beta", "2", "1"], &["Twin", "3", "0"], &["Twin", "4", "0"]])),
                case_insensitive: false,
            }),
            POINTS => Some(Table {
                key: vec!["#".into()],
                columns: vec![("x".into(), ColumnType::Float)],
                rows: Some(HashSet::from(["0".to_string(), "1".into(), "2".into()])),
                cells: Some(text(&[&["0"], &["1"], &["2"]])),
                ..Default::default()
            }),
            _ => None,
        }
    }
    fn may_be_new(&self, _: &str) -> bool {
        false
    }
}

struct Seen {
    findings: Vec<Finding>,
    cells: Vec<String>,
    adds: Vec<String>,
    removes: Vec<String>,
}

fn check(target: &str, patch: &str) -> Seen {
    let store = Tables;
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: None, store: Some(&store) };
    let name = format!("layers/base/patches/{}.json", target.to_lowercase());
    let bytes = luma(&[("layers", r#"[{"name": "base"}]"#)], &[file(&name, patch.as_bytes())]);
    let report = validate(&Input { source: Source::Bytes(&bytes, "test.luma"), game: Some(&game), installed: &[], platforms: &["windows-x64"], options: None, position: None });
    for f in &report.findings {
        assert_eq!(unfilled(f), None, "{f:?}");
    }
    Seen { findings: report.findings, cells: report.touches.cells, adds: report.touches.adds, removes: report.touches.removes }
}

/// Each finding as its code and the phrase that says why.
fn said(seen: &Seen) -> Vec<(u16, String)> {
    seen.findings
        .iter()
        .map(|f| {
            let why = f.args.iter().find(|(k, _)| *k == "why" || *k == "found").map(|(_, a)| match a {
                Arg::Phrase(p) => p.to_string(),
                Arg::Text(t) => t.clone(),
            });
            (f.code, why.unwrap_or_default())
        })
        .collect()
}

fn one(code: u16, why: &str) -> Vec<(u16, String)> {
    vec![(code, why.to_string())]
}

const K: &str = r#""patch": 1, "key": ["name"]"#;
const H: &str = r##""patch": 1, "key": ["#"]"##;

#[test]
fn a_key_the_game_repeats_is_changed_with_select() {
    let seen = check(KEYED, &format!(r#"{{{K}, "set": [{{"where": {{"name": "Twin"}}, "values": {{"count": 5}}}}]}}"#));
    assert_eq!(said(&seen), one(402, "Patch.KeyRepeated"));
    let seen = check(KEYED, &format!(r#"{{{K}, "remove": [{{"name": "Twin"}}]}}"#));
    assert_eq!(said(&seen), one(402, "Patch.KeyRepeated"));
    let seen = check(KEYED, &format!(r#"{{{K}, "select": [{{"match": {{"name": {{"equals": "Twin"}}}}, "values": {{"count": 5}}}}]}}"#));
    assert_eq!(said(&seen), vec![]);
}

#[test]
fn a_column_known_by_hash_takes_its_name_or_its_hash() {
    let seen = check(KEYED, &format!(r#"{{{K}, "set": [{{"where": {{"name": "Alpha"}}, "values": {{"speed": 2.5}}}}]}}"#));
    assert_eq!(said(&seen), vec![]);
    assert_eq!(seen.cells, vec!["stage/keyed.bcsv\u{1f}Alpha\u{1f}speed".to_string()]);
    let seen = check(KEYED, &format!(r#"{{{K}, "set": [{{"where": {{"name": "Alpha"}}, "values": {{"{}": 2.5}}}}]}}"#, speed()));
    assert_eq!(said(&seen), vec![]);
    let seen = check(KEYED, &format!(r#"{{{K}, "set": [{{"where": {{"name": "Alpha"}}, "values": {{"speed": "fast"}}}}]}}"#));
    assert_eq!(said(&seen), one(403, "Patch.ValueType"));
    let seen = check(KEYED, &format!(r#"{{{K}, "set": [{{"where": {{"name": "Alpha"}}, "values": {{"sped": 1}}}}]}}"#));
    assert_eq!(said(&seen), one(403, "Patch.NoSuchColumn"));
}

#[test]
fn the_game_hashes_a_name_over_signed_bytes() {
    // MR::getHashCode adds each byte as s8: "é" is C3 A9, which count as -61 and -87.
    let expected = ((-61i32 as u32).wrapping_mul(31)).wrapping_add(-87i32 as u32);
    assert_eq!(column_hash("é"), expected);
    assert_eq!(column_hash("name"), 0x00337a8b);
}

#[test]
fn a_table_named_by_place() {
    let seen = check(POINTS, &format!(r##"{{{H}, "set": [{{"where": {{"#": 1}}, "values": {{"x": 4}}}}]}}"##));
    assert_eq!(said(&seen), vec![]);
    assert_eq!(seen.cells, vec!["stage/points.bcsv\u{1f}1\u{1f}x".to_string()]);

    let seen = check(POINTS, &format!(r##"{{{H}, "set": [{{"where": {{"#": 3}}, "values": {{"x": 4}}}}]}}"##));
    assert_eq!(said(&seen), one(402, "Patch.PlaceNotFound"));
    let seen = check(POINTS, &format!(r##"{{{H}, "set": [{{"where": {{"#": -1}}, "values": {{"x": 4}}}}]}}"##));
    assert_eq!(said(&seen), one(403, "Patch.KeyValue"));
    let seen = check(POINTS, &format!(r##"{{{H}, "set": [{{"where": {{"#": "1"}}, "values": {{"x": 4}}}}]}}"##));
    assert_eq!(said(&seen), one(403, "Patch.KeyValue"));

    // An added row goes at the end, with no key to give.
    let seen = check(POINTS, &format!(r#"{{{H}, "add": [{{"x": 1}}, {{"x": 2}}]}}"#));
    assert_eq!(said(&seen), vec![]);
    assert_eq!(seen.adds, vec!["stage/points.bcsv\u{1f}+0".to_string(), "stage/points.bcsv\u{1f}+1".into()]);

    // A remove would move every later row.
    let seen = check(POINTS, &format!(r##"{{{H}, "remove": [{{"#": 1}}]}}"##));
    assert_eq!(said(&seen), one(402, "Patch.RemoveByPlace"));
    assert!(seen.removes.is_empty());

    // The whole table.
    let seen = check(POINTS, &format!(r#"{{{H}, "rows": [{{"x": 1}}, {{"x": 2}}]}}"#));
    assert_eq!(said(&seen), vec![]);
    assert_eq!(seen.adds, vec!["stage/points.bcsv\u{1f}*".to_string()]);
    let seen = check(POINTS, &format!(r#"{{{H}, "rows": [{{"x": "far"}}]}}"#));
    assert_eq!(said(&seen), one(403, "Patch.ValueType"));
    let seen = check(POINTS, &format!(r#"{{{H}, "rows": {{"x": 1}}}}"#));
    assert_eq!(said(&seen), one(400, "Patch.NotArray"));

    // The key a table is named by is the contract's.
    let seen = check(POINTS, &format!(r#"{{{K}, "set": [{{"where": {{"name": "a"}}, "values": {{"x": 4}}}}]}}"#));
    assert!(seen.findings.iter().any(|f| f.code == 401), "{:?}", seen.findings);
}

#[test]
fn rows_take_only_a_table_named_by_place() {
    let seen = check(KEYED, &format!(r#"{{{K}, "rows": [{{"name": "Alpha", "count": 1}}]}}"#));
    assert_eq!(said(&seen), one(402, "Patch.RowsNotByPlace"));
}

#[test]
fn a_word_given_as_bytes() {
    // "Alpha" as bytes names the row "Alpha".
    let seen = check(KEYED, &format!(r#"{{{K}, "set": [{{"where": {{"name": {{"bytes": "416c706861"}}}}, "values": {{"count": 2}}}}]}}"#));
    assert_eq!(said(&seen), vec![]);
    assert_eq!(seen.cells, vec!["stage/keyed.bcsv\u{1f}Alpha\u{1f}count".to_string()]);
    // A word that is not UTF-8 names no row of these.
    let seen = check(KEYED, &format!(r#"{{{K}, "set": [{{"where": {{"name": {{"bytes": "82a0"}}}}, "values": {{"count": 2}}}}]}}"#));
    assert_eq!(said(&seen), one(402, "Patch.RowNotFound"));
    assert!(seen.findings[0].args.iter().any(|(k, a)| *k == "row" && *a == Arg::Text("{bytes:82a0}".into())));
    // A value given as bytes.
    let seen = check(KEYED, &format!(r#"{{{K}, "add": [{{"name": "Gamma", "count": 1, "speed": 1}}, {{"name": {{"bytes": "ff"}}}}]}}"#));
    assert_eq!(said(&seen), vec![]);
    assert_eq!(seen.adds, vec!["stage/keyed.bcsv\u{1f}Gamma".to_string(), "stage/keyed.bcsv\u{1f}{bytes:ff}".into()]);
    for bad in [r#"{"bytes": "abc"}"#, r#"{"bytes": "zz"}"#, r#"{"bytes": "ab", "more": 1}"#] {
        let seen = check(KEYED, &format!(r#"{{{K}, "set": [{{"where": {{"name": {bad}}}, "values": {{"count": 2}}}}]}}"#));
        assert_eq!(said(&seen), one(403, "Patch.BytesShape"), "{bad}");
        assert!(seen.cells.is_empty());
        let seen = check(KEYED, &format!(r#"{{{K}, "set": [{{"where": {{"name": "Alpha"}}, "values": {{"name": {bad}}}}}]}}"#));
        assert_eq!(said(&seen), one(403, "Patch.BytesShape"), "{bad}");
        let seen = check(KEYED, &format!(r#"{{{K}, "remove": [{{"name": {bad}}}]}}"#));
        assert_eq!(said(&seen), one(403, "Patch.BytesShape"), "{bad}");
        let seen = check(KEYED, &format!(r#"{{{K}, "add": [{{"name": {bad}}}]}}"#));
        assert_eq!(said(&seen), one(403, "Patch.BytesShape"), "{bad}");
    }
}

#[test]
fn a_key_value_that_names_no_row_is_said() {
    for bad in ["null", "[1]", r#"{"text": "Alpha"}"#] {
        let seen = check(KEYED, &format!(r#"{{{K}, "set": [{{"where": {{"name": {bad}}}, "values": {{"count": 2}}}}]}}"#));
        assert_eq!(said(&seen), one(403, "Patch.KeyValue"), "{bad}");
        let seen = check(KEYED, &format!(r#"{{{K}, "remove": [{{"name": {bad}}}]}}"#));
        assert_eq!(said(&seen), one(403, "Patch.KeyValue"), "{bad}");
        let seen = check(KEYED, &format!(r#"{{{K}, "add": [{{"name": {bad}, "count": 1}}]}}"#));
        assert_eq!(said(&seen), one(403, "Patch.KeyValue"), "{bad}");
    }
    // A key given as the wrong kind of value for its column.
    let seen = check(KEYED, r#"{"patch": 1, "key": ["count"], "set": [{"where": {"count": "two"}, "values": {"name": "x"}}]}"#);
    assert!(said(&seen).contains(&(403, "Patch.ValueType".to_string())), "{:?}", said(&seen));
    // A key column missing from an added row.
    let seen = check(KEYED, &format!(r#"{{{K}, "add": [{{"count": 1}}]}}"#));
    assert_eq!(said(&seen), one(400, "Patch.AddNoKey"));
}

#[test]
fn a_selector_is_matched_against_the_game_values() {
    let sel = |m: &str| check(KEYED, &format!(r#"{{{K}, "select": [{{"match": {m}, "values": {{"count": 1}}}}]}}"#));
    assert_eq!(said(&sel(r#"{"name": {"equals": "Gamma"}}"#)), one(407, ""));
    assert_eq!(said(&sel(r#"{"name": {"prefix": "Al"}}"#)), vec![]);
    assert_eq!(said(&sel(r#"{"name": {"glob": "T?in"}}"#)), vec![]);
    assert_eq!(said(&sel(r#"{"name": {"glob": "T?n"}}"#)), one(407, ""));
    assert_eq!(said(&sel(r#"{"count": {"equals": 2}}"#)), vec![]);
    assert_eq!(said(&sel(r#"{"speed": {"equals": 0.5}}"#)), vec![]);
    assert_eq!(said(&sel(r#"{"speed": {"equals": 0.25}}"#)), one(407, ""));
    // Every column must match.
    assert_eq!(said(&sel(r#"{"name": {"equals": "Alpha"}, "count": {"equals": 2}}"#)), one(407, ""));
    // A row this patch adds is matched too.
    let seen = check(KEYED, &format!(r#"{{{K}, "add": [{{"name": "Gamma", "count": 1}}], "select": [{{"match": {{"name": {{"equals": "Gamma"}}}}, "values": {{"count": 2}}}}]}}"#));
    assert_eq!(said(&seen), vec![]);
    // On a table named by place.
    let seen = check(POINTS, &format!(r#"{{{H}, "select": [{{"match": {{"x": {{"equals": 2}}}}, "values": {{"x": 3}}}}]}}"#));
    assert_eq!(said(&seen), vec![]);
}
