//! Conformance tests for patches by key (an auditor's, from luma_patch.h and table-keys.md; not from the applier's
//! code). Only the C interface is called; TableIr builds and reads the cooked tables.
//! Run: copy into crates/nebula-import/tests/ of a copy of the branch and `cargo test --test f_conformance`.

use nebula_import::cook::table::{column_hash, Cell, Column, Kind, TableIr};
use nebula_import::patch_c::*;
use std::ffi::{c_char, CStr, CString};
use std::ptr::null;

const LANGUAGES: [&str; 14] = [
    "UsEnglish", "EuEnglish", "UsFrench", "EuFrench", "UsSpanish", "EuSpanish", "EuGerman", "EuItalian", "EuDutch",
    "UsPortuguese", "JpJapanese", "KrKorean", "CnSimpChinese", "TwTradChinese",
];
const OBJINFO: &str = "StageData/AstroGalaxy/jmp/Placement/Common/ObjInfo";

fn col(name: &str, kind: Kind) -> Column {
    Column { hash: column_hash(name), mask: u32::MAX, kind, width: 32 }
}

/// Three objects, l_id 1 to 3.
fn objinfo() -> TableIr {
    TableIr {
        columns: vec![col("l_id", Kind::Int), col("name", Kind::Text), col("Obj_arg0", Kind::Int)],
        rows: (1..=3u32).map(|i| vec![Cell::Bits(i), Cell::Text(format!("Obj{i}").into_bytes()), Cell::Bits(u32::MAX)]).collect(),
    }
}

unsafe fn s(p: *const c_char) -> Option<String> {
    (!p.is_null()).then(|| CStr::from_ptr(p).to_str().unwrap().to_string())
}

struct Fact {
    kind: u32,
    mod_id: Option<String>,
    file: Option<String>,
    other: Option<String>,
    row: Option<String>,
    column: Option<String>,
    why: Option<String>,
    selector: u32,
    rows: u32,
    code: u32,
    reasons: Vec<Option<String>>,
}

struct Out {
    state: i32,
    error: Option<String>,
    table: Option<TableIr>,
    facts: Vec<Fact>,
}

/// One call: `patches` are (mod, file, json), from the bottom of the list up.
fn call(path: &str, table: &[u8], size: u32, patches: &[(&str, &str, &str)], links: &[(&str, Vec<u8>, Vec<(&str, &str, &str)>)]) -> Out {
    call_on(path, table, size, patches, links, &[])
}

/// As `call`, with `mods`: every mod that is on, from the bottom of the list up.
fn call_on(path: &str, table: &[u8], size: u32, patches: &[(&str, &str, &str)], links: &[(&str, Vec<u8>, Vec<(&str, &str, &str)>)], mods: &[&str]) -> Out {
    let mod_keep: Vec<CString> = mods.iter().map(|m| CString::new(*m).unwrap()).collect();
    let mod_ptrs: Vec<*const c_char> = mod_keep.iter().map(|m| m.as_ptr()).collect();
    let keep: Vec<(CString, CString, Vec<u8>)> = patches.iter().map(|p| (CString::new(p.0).unwrap(), CString::new(p.1).unwrap(), p.2.as_bytes().to_vec())).collect();
    let c: Vec<LumaTablePatch> = keep.iter().map(|k| LumaTablePatch { mod_id: k.0.as_ptr(), file: k.1.as_ptr(), json: k.2.as_ptr(), json_size: k.2.len() as u64 }).collect();
    let link_keep: Vec<(CString, Vec<(CString, CString, Vec<u8>)>)> = links.iter().map(|(p, _, ps)| {
        (CString::new(*p).unwrap(), ps.iter().map(|p| (CString::new(p.0).unwrap(), CString::new(p.1).unwrap(), p.2.as_bytes().to_vec())).collect())
    }).collect();
    let link_patches: Vec<Vec<LumaTablePatch>> = link_keep.iter().map(|(_, ps)| ps.iter().map(|k| LumaTablePatch { mod_id: k.0.as_ptr(), file: k.1.as_ptr(), json: k.2.as_ptr(), json_size: k.2.len() as u64 }).collect()).collect();
    let c_links: Vec<LumaLinkTable> = (0..links.len()).map(|i| LumaLinkTable {
        path: link_keep[i].0.as_ptr(),
        table: links[i].1.as_ptr(),
        table_size: links[i].1.len() as u64,
        patches: link_patches[i].as_ptr(),
        patch_count: link_patches[i].len() as u32,
    }).collect();
    let path = CString::new(path).unwrap();
    let input = LumaPatchInput {
        size,
        path: path.as_ptr(),
        table: table.as_ptr(),
        table_size: table.len() as u64,
        patches: c.as_ptr(),
        patch_count: c.len() as u32,
        links: if c_links.is_empty() { null() } else { c_links.as_ptr() },
        link_count: c_links.len() as u32,
        mods: if mod_ptrs.is_empty() { null() } else { mod_ptrs.as_ptr() },
        mod_count: mod_ptrs.len() as u32,
    };
    unsafe {
        let r = luma_patch_table(&input);
        assert!(!r.is_null(), "luma_patch_table is never null");
        let state = luma_patched_state(r);
        let error = s(luma_patched_error(r));
        let mut size = 0u64;
        let t = luma_patched_table(r, &mut size);
        let table = (!t.is_null()).then(|| TableIr::from_ntbl(std::slice::from_raw_parts(t, size as usize)).unwrap());
        let n = luma_patched_fact_count(r);
        let mut facts = Vec::new();
        for i in 0..n {
            let f = &*luma_patched_fact_at(r, i);
            let mut reasons = Vec::new();
            for l in LANGUAGES {
                let l = CString::new(l).unwrap();
                reasons.push(s(luma_patched_fact_reason(r, i, l.as_ptr())));
            }
            let unknown = CString::new("Klingon").unwrap();
            let fallback = s(luma_patched_fact_reason(r, i, unknown.as_ptr()));
            let english = s(luma_patched_fact_reason(r, i, null()));
            assert_eq!(fallback, english, "an unknown language is UsEnglish");
            facts.push(Fact { kind: f.kind, mod_id: s(f.mod_id), file: s(f.file), other: s(f.other), row: s(f.row), column: s(f.column), why: s(f.why), selector: f.selector, rows: f.rows, code: f.code, reasons });
        }
        assert!(luma_patched_fact_at(r, n).is_null(), "null past the last");
        assert!(luma_patched_fact_reason(r, n, null()).is_null(), "null past the last");
        luma_patched_free(r);
        Out { state, error, table, facts }
    }
}

fn size() -> u32 {
    std::mem::size_of::<LumaPatchInput>() as u32
}

fn arg0(t: &TableIr, l_id: u32) -> Option<u32> {
    t.rows.iter().find(|r| r[0] == Cell::Bits(l_id)).map(|r| match r[2] { Cell::Bits(b) => b, _ => panic!() })
}

fn set(l_id: u32, v: i32) -> String {
    format!(r#"{{"patch": 1, "set": [{{"where": {{"l_id": {l_id}}}, "values": {{"Obj_arg0": {v}}}}}]}}"#)
}

#[test]
fn f01_a_size_other_than_the_callers_is_refused_and_the_game_table_stands() {
    let t = objinfo().to_ntbl();
    for wrong in [0, size() - 1, size() + 8] {
        let o = call(OBJINFO, &t, wrong, &[("a", "p.json", &set(1, 4))], &[]);
        assert_eq!(o.state, -1);
        assert!(o.error.as_deref().is_some_and(|e| !e.is_empty()));
        assert!(o.table.is_none(), "null table when the state is -1");
    }
}

#[test]
fn f02_a_table_that_cannot_be_read_is_minus_one_never_a_crash() {
    for bytes in [vec![], b"NTBL".to_vec(), vec![0xffu8; 64]] {
        let o = call(OBJINFO, &bytes, size(), &[("a", "p.json", &set(1, 4))], &[]);
        assert_eq!(o.state, -1);
        assert!(o.error.is_some() && o.table.is_none());
    }
}

#[test]
fn f03_no_patches_gives_the_game_table_unchanged() {
    let o = call(OBJINFO, &objinfo().to_ntbl(), size(), &[], &[]);
    assert_eq!(o.state, 0);
    assert_eq!(o.table.unwrap(), objinfo());
    assert!(o.facts.is_empty());
}

#[test]
fn f04_the_higher_mod_wins_and_each_overridden_cell_is_named_by_column() {
    // Three mods set one cell, bottom of the list first: the top one wins; each conflict names the higher as
    // mod_id, the lower as other, the row by its key, the column by name.
    let o = call(OBJINFO, &objinfo().to_ntbl(), size(), &[("low", "a.json", &set(2, 10)), ("mid", "b.json", &set(2, 20)), ("top", "c.json", &set(2, 30))], &[]);
    assert_eq!(o.state, 0, "{:?}", o.error);
    let t = o.table.unwrap();
    assert_eq!(arg0(&t, 2), Some(30));
    assert_eq!(arg0(&t, 1), Some(u32::MAX), "other rows untouched");
    let conflicts: Vec<_> = o.facts.iter().filter(|f| f.kind == LUMA_PATCH_CONFLICT).collect();
    assert!(!conflicts.is_empty());
    for c in &conflicts {
        assert_eq!(c.row.as_deref(), Some("2"));
        assert_eq!(c.column.as_deref(), Some("Obj_arg0"), "never a hash (P-2)");
        assert!(c.file.is_none() && c.why.is_none() && c.code == 0);
        assert!(c.reasons.iter().all(Option::is_none), "reason only for a failed fact");
    }
    assert!(conflicts.iter().any(|c| c.mod_id.as_deref() == Some("top") && c.other.as_deref() == Some("mid")));
    // Reversed order: the other end wins (the list decides, not the names).
    let o = call(OBJINFO, &objinfo().to_ntbl(), size(), &[("top", "c.json", &set(2, 30)), ("mid", "b.json", &set(2, 20)), ("low", "a.json", &set(2, 10))], &[]);
    assert_eq!(arg0(&o.table.unwrap(), 2), Some(10));
}

#[test]
fn f05_a_patch_that_cannot_apply_is_left_out_whole_named_and_translated_and_the_others_apply() {
    // "bad" sets a good cell and names a column the table has not (L403): none of its changes may stay.
    let bad = r#"{"patch": 1, "set": [{"where": {"l_id": 1}, "values": {"Obj_arg0": 77}}, {"where": {"l_id": 2}, "values": {"NoSuchColumn": 1}}]}"#;
    let o = call(OBJINFO, &objinfo().to_ntbl(), size(), &[("below", "a.json", &set(3, 5)), ("bad", "bad.json", bad), ("above", "c.json", &set(3, 6))], &[]);
    assert_eq!(o.state, 0, "one mod failing does not fail the table");
    let t = o.table.unwrap();
    assert_eq!(arg0(&t, 1), Some(u32::MAX), "the failed mod's good cell is left out too (whole)");
    assert_eq!(arg0(&t, 3), Some(6), "the others apply");
    let failed: Vec<_> = o.facts.iter().filter(|f| f.kind == LUMA_PATCH_FAILED).collect();
    assert_eq!(failed.len(), 1);
    let f = failed[0];
    assert_eq!((f.mod_id.as_deref(), f.file.as_deref()), (Some("bad"), Some("bad.json")));
    assert_eq!(f.code, 403);
    assert!(f.why.as_deref().is_some_and(|w| w.contains("NoSuchColumn")), "{:?}", f.why);
    assert_eq!(f.reasons[0], f.why, "why is the UsEnglish reason");
    for (l, r) in LANGUAGES.iter().zip(&f.reasons) {
        let r = r.as_deref().unwrap_or_else(|| panic!("{l}: no reason"));
        assert!(!r.is_empty() && !r.contains("{L") && !r.contains("{found}") && !r.contains("{column}"), "{l}: {r}");
    }
    assert_ne!(f.reasons[3], f.reasons[0], "EuFrench is not English");
}

#[test]
fn f06_not_json_and_a_wrong_shape_are_failed_facts_not_errors() {
    for (json, code) in [("{ not json", 400u32), (r#"{"patch": 1, "set": 3}"#, 400)] {
        let o = call(OBJINFO, &objinfo().to_ntbl(), size(), &[("m", "m.json", json)], &[]);
        assert_eq!(o.state, 0);
        assert_eq!(o.table.unwrap(), objinfo());
        let f = o.facts.iter().find(|f| f.kind == LUMA_PATCH_FAILED).expect("named");
        assert_eq!((f.mod_id.as_deref(), f.code), (Some("m"), code), "{json}: {:?}", f.why);
    }
}

#[test]
fn f07_a_row_that_is_not_there_is_named_and_who_removed_it() {
    let remove = r#"{"patch": 1, "remove": [{"l_id": 2}]}"#;
    let o = call(OBJINFO, &objinfo().to_ntbl(), size(), &[("remover", "r.json", remove), ("setter", "s.json", &set(2, 9)), ("lost", "l.json", &set(42, 1))], &[]);
    assert_eq!(o.state, 0);
    let t = o.table.unwrap();
    assert_eq!(arg0(&t, 2), None, "the lower's remove stands; the higher's set has no row");
    let missing: Vec<_> = o.facts.iter().filter(|f| f.kind == LUMA_PATCH_MISSING).collect();
    assert!(missing.iter().any(|m| m.mod_id.as_deref() == Some("setter") && m.row.as_deref() == Some("2") && m.other.as_deref() == Some("remover") && m.file.as_deref() == Some("s.json")));
    assert!(missing.iter().any(|m| m.mod_id.as_deref() == Some("lost") && m.row.as_deref() == Some("42") && m.other.is_none()));
}

#[test]
fn f08_a_selector_says_how_many_rows_it_matched_zero_included() {
    let sel = r#"{"patch": 1, "select": [{"match": {"name": {"glob": "Obj*"}}, "values": {"Obj_arg0": 1}}, {"match": {"name": {"glob": "Nothing*"}}, "values": {"Obj_arg0": 2}}]}"#;
    let o = call(OBJINFO, &objinfo().to_ntbl(), size(), &[("s", "s.json", sel)], &[]);
    let t = o.table.unwrap();
    assert!((1..=3).all(|i| arg0(&t, i) == Some(1)));
    let sel: Vec<(u32, u32)> = o.facts.iter().filter(|f| f.kind == LUMA_PATCH_SELECTED).map(|f| (f.selector, f.rows)).collect();
    assert_eq!(sel, vec![(0, 3), (1, 0)]);
}

#[test]
fn f09_an_added_l_id_must_be_new_to_the_zone_in_every_layer() {
    // table-keys T-2: an l_id the game has in another layer of the zone leaves the patch out (L402); one a higher
    // mod adds there leaves the lower's row out (a conflict; the higher's row stays).
    let layer_a = "StageData/AstroGalaxy/jmp/Placement/LayerA/ObjInfo";
    let other = TableIr { columns: objinfo().columns, rows: vec![vec![Cell::Bits(50), Cell::Text(b"Other".to_vec()), Cell::Bits(0)]] };
    let add = |id: u32, name: &str| format!(r#"{{"patch": 1, "add": [{{"l_id": {id}, "name": "{name}"}}]}}"#);
    let o = call(OBJINFO, &objinfo().to_ntbl(), size(), &[("clash", "c.json", &add(50, "Mine"))], &[(layer_a, other.to_ntbl(), vec![])]);
    let f = o.facts.iter().find(|f| f.kind == LUMA_PATCH_FAILED).expect("L402 named");
    assert_eq!((f.mod_id.as_deref(), f.code), (Some("clash"), 402));
    assert_eq!(o.table.unwrap(), objinfo());

    let a = add(60, "Low");
    let b = add(60, "High");
    let o = call_on(OBJINFO, &objinfo().to_ntbl(), size(), &[("low", "l.json", &a)], &[(layer_a, other.to_ntbl(), vec![("high", "h.json", &b)])], &["low", "high"]);
    let t = o.table.unwrap();
    assert!(t.rows.iter().all(|r| r[0] != Cell::Bits(60)), "the lower's row is left out");
    assert!(o.facts.iter().any(|f| f.kind == LUMA_PATCH_CONFLICT && f.mod_id.as_deref() == Some("high") && f.other.as_deref() == Some("low") && f.row.as_deref() == Some("60") && f.column.is_none()));
    // The same two mods the other way round: this table's mod is the higher, its row stays and is not named here
    // (the other table's call names it).
    let o = call_on(OBJINFO, &objinfo().to_ntbl(), size(), &[("high", "h.json", &b)], &[(layer_a, other.to_ntbl(), vec![("low", "l.json", &a)])], &["low", "high"]);
    assert!(o.table.unwrap().rows.iter().any(|r| r[0] == Cell::Bits(60)), "the higher's row stays");
    // An l_id new to the zone is added, with the kind's usual values for what it leaves out (table-keys.md 10.4).
    let o = call(OBJINFO, &objinfo().to_ntbl(), size(), &[("ok", "o.json", &add(70, "New"))], &[(layer_a, other.to_ntbl(), vec![])]);
    let t = o.table.unwrap();
    assert_eq!(arg0(&t, 70), Some(u32::MAX), "Obj_arg0's usual value is -1");
}

#[test]
fn f10_a_two_column_key_and_a_bytes_key_are_written_as_the_header_says() {
    let pos = "StageData/AstroGalaxy/jmp/GeneralPos/Common/GeneralPosInfo";
    let t = TableIr {
        columns: vec![col("PosName", Kind::Text), col("Obj_ID", Kind::Int), col("pos_x", Kind::Float)],
        rows: vec![vec![Cell::Text("合流点".as_bytes().to_vec()), Cell::Bits(-3i32 as u32), Cell::Bits(0)]],
    };
    let p = |v: f32| format!(r#"{{"patch": 1, "set": [{{"where": {{"PosName": "合流点", "Obj_ID": -3}}, "values": {{"pos_x": {v}}}}}]}}"#);
    let o = call(pos, &t.to_ntbl(), size(), &[("a", "a.json", &p(1.0)), ("b", "b.json", &p(2.0))], &[]);
    let c = o.facts.iter().find(|f| f.kind == LUMA_PATCH_CONFLICT).expect("conflict");
    assert_eq!(c.row.as_deref(), Some("合流点\u{1f}-3"), "key values in key order, U+001F, signed");
    assert_eq!(c.column.as_deref(), Some("pos_x"));

    let cam = "StageData/AstroGalaxy/camera/CameraParam.bcam";
    let t = TableIr { columns: vec![col("id", Kind::Text), col("dist", Kind::Float)], rows: vec![vec![Cell::Text(b"e:\x91".to_vec()), Cell::Bits(0)]] };
    let p = |v: f32| format!(r#"{{"patch": 1, "set": [{{"where": {{"id": {{"bytes": "653a91"}}}}, "values": {{"dist": {v}}}}}]}}"#);
    let o = call(cam, &t.to_ntbl(), size(), &[("a", "a.json", &p(1.0)), ("b", "b.json", &p(2.0))], &[]);
    let c = o.facts.iter().find(|f| f.kind == LUMA_PATCH_CONFLICT).expect("conflict");
    assert_eq!(c.row.as_deref(), Some("{bytes:653a91}"));
}

#[test]
fn f11_a_disc_path_with_its_archive_is_the_store_path() {
    let disc = "StageData/AstroGalaxy/AstroGalaxy.arc/jmp/Placement/Common/ObjInfo";
    let o = call(disc, &objinfo().to_ntbl(), size(), &[("a", "a.json", &set(1, 8))], &[]);
    assert_eq!(o.state, 0, "{:?}", o.error);
    assert_eq!(arg0(&o.table.unwrap(), 1), Some(8));
}

#[test]
fn f12_a_place_table_refuses_remove_and_checks_every_place() {
    // table-keys.md 3: rows by place; remove refused (L402), a place past the game's rows refused (L402), rows
    // replaces.
    let rail = "ObjectData/Kuribo/Kuribo.arc/Kuribo.pa";
    let t = TableIr { columns: vec![col("frame", Kind::Int), col("value", Kind::Float)], rows: (0..3u32).map(|i| vec![Cell::Bits(i), Cell::Bits(0)]).collect() };
    let o = call(rail, &t.to_ntbl(), size(), &[("r", "r.json", r##"{"patch": 1, "remove": [{"#": 1}]}"##)], &[]);
    let f = o.facts.iter().find(|f| f.kind == LUMA_PATCH_FAILED).expect("remove refused");
    assert_eq!(f.code, 402);
    // A place past the table's rows: the validator's L402 is against the game's count; the applier's table may be a
    // mod's whole file, so here it is a row that is not there, named, never silent.
    let o = call(rail, &t.to_ntbl(), size(), &[("p", "p.json", r##"{"patch": 1, "set": [{"where": {"#": 3}, "values": {"frame": 9}}]}"##)], &[]);
    assert!(o.facts.iter().any(|f| f.kind == LUMA_PATCH_MISSING && f.row.as_deref() == Some("#3") && f.mod_id.as_deref() == Some("p")));
    assert_eq!(o.table.unwrap(), t);
    let o = call(rail, &t.to_ntbl(), size(), &[("w", "w.json", r##"{"patch": 1, "rows": [{"frame": 7, "value": 1.5}]}"##)], &[]);
    let out = o.table.unwrap();
    assert_eq!(out.rows.len(), 1);
    assert_eq!(out.rows[0][0], Cell::Bits(7));
}

#[test]
fn f13_a_link_patch_of_a_mod_the_list_does_not_name_is_refused_never_guessed() {
    // Which of two mods adding one l_id in two layers is higher comes from `mods`. A link patch
    // whose mod `mods` does not name (or no `mods` at all) cannot be ordered: the input is not valid (-1, the mod
    // named), never both rows kept without a word (a duplicate l_id in the zone, which the game links wrongly).
    let layer_a = "StageData/AstroGalaxy/jmp/Placement/LayerA/ObjInfo";
    let other = TableIr { columns: objinfo().columns, rows: vec![] };
    let a = r#"{"patch": 1, "add": [{"l_id": 60, "name": "Low"}]}"#;
    let b = r#"{"patch": 1, "add": [{"l_id": 60, "name": "High"}]}"#;
    for mods in [&[][..], &["low"][..]] {
        let o = call_on(OBJINFO, &objinfo().to_ntbl(), size(), &[("low", "l.json", a)], &[(layer_a, other.to_ntbl(), vec![("high", "h.json", b)])], mods);
        assert_eq!(o.state, -1, "mods {mods:?}");
        assert!(o.error.as_deref().is_some_and(|e| e.contains("high")), "mods {mods:?}: {:?}", o.error);
    }
}
