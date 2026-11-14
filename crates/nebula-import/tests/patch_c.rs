//! The applier's C interface (include/luma_patch.h) against the Rust one: the same table and the same facts, a mod
//! that cannot apply named and left out, and every input fault refused with its reason, never a crash.

use nebula_import::cook::patch::{apply, Fact, ModPatch};
use nebula_import::cook::table::{column_hash, Cell, Column, Kind, TableIr};
use nebula_import::patch_c::*;
use serde_json::json;
use std::ffi::{c_char, CStr, CString};
use std::ptr::null;

fn nebula_import_reason(f: &luma_validate::Finding, language: &str) -> String {
    luma_validate::reason(f, language)
}

const OBJINFO: &str = "StageData/AstroGalaxy/jmp/Placement/Common/ObjInfo";

fn objinfo() -> TableIr {
    let col = |name: &str, kind| Column { hash: column_hash(name), mask: u32::MAX, kind, width: 32 };
    TableIr {
        columns: vec![col("l_id", Kind::Int), col("name", Kind::Text), col("Obj_arg0", Kind::Int)],
        rows: vec![
            vec![Cell::Bits(1), Cell::Text(b"Kuribo".to_vec()), Cell::Bits(u32::MAX)],
            vec![Cell::Bits(2), Cell::Text(b"Takobo".to_vec()), Cell::Bits(u32::MAX)],
        ],
    }
}

unsafe fn s(p: *const c_char) -> Option<String> {
    (!p.is_null()).then(|| CStr::from_ptr(p).to_str().unwrap().to_string())
}

struct Given {
    ids: Vec<CString>,
    files: Vec<CString>,
    json: Vec<Vec<u8>>,
}

impl Given {
    fn new(patches: &[(&str, &str, &str)]) -> Self {
        Given {
            ids: patches.iter().map(|p| CString::new(p.0).unwrap()).collect(),
            files: patches.iter().map(|p| CString::new(p.1).unwrap()).collect(),
            json: patches.iter().map(|p| p.2.as_bytes().to_vec()).collect(),
        }
    }
    fn c(&self) -> Vec<LumaTablePatch> {
        (0..self.ids.len()).map(|i| LumaTablePatch { mod_id: self.ids[i].as_ptr(), file: self.files[i].as_ptr(), json: self.json[i].as_ptr(), json_size: self.json[i].len() as u64 }).collect()
    }
}

#[test]
fn the_c_call_gives_the_rust_applier_table_and_facts() {
    let a = r#"{"patch": 1, "key": ["l_id"], "set": [{"where": {"l_id": 1}, "values": {"Obj_arg0": 4}}], "add": [{"l_id": 9, "name": "Nokonoko"}]}"#;
    let b = r#"{"patch": 1, "key": ["l_id"], "set": [{"where": {"l_id": 1}, "values": {"Obj_arg0": 5}}, {"where": {"l_id": 7}, "values": {"Obj_arg0": 1}}],
               "select": [{"match": {"name": {"glob": "Ta*"}}, "values": {"Obj_arg0": 2}}]}"#;
    let wrong = r#"{"patch": 1, "key": ["l_id"], "set": [{"where": {"name": "Kuribo"}, "values": {"Obj_arg0": 3}}]}"#;
    let given = Given::new(&[("a", "pa.json", a), ("broken", "pw.json", wrong), ("not.json", "pn.json", "{ no"), ("b", "pb.json", b)]);
    let patches = given.c();
    let table = objinfo().to_ntbl();
    let path = CString::new("StageData/AstroGalaxy/AstroGalaxy.arc/jmp/Placement/Common/ObjInfo").unwrap();
    let input = LumaPatchInput { size: std::mem::size_of::<LumaPatchInput>() as u32, path: path.as_ptr(), table: table.as_ptr(), table_size: table.len() as u64, patches: patches.as_ptr(), patch_count: patches.len() as u32, links: null(), link_count: 0, mods: null(), mod_count: 0 };

    // The same in Rust, without the patch that is not JSON (named first by the C call).
    let values: Vec<serde_json::Value> = [a, wrong, b].iter().map(|t| serde_json::from_str(t).unwrap()).collect();
    let mut expected = objinfo();
    let rust = apply(OBJINFO, &mut expected, &[
        ModPatch { mod_id: "a", file: "pa.json", patch: &values[0] },
        ModPatch { mod_id: "broken", file: "pw.json", patch: &values[1] },
        ModPatch { mod_id: "b", file: "pb.json", patch: &values[2] },
    ]).unwrap();

    unsafe {
        let r = luma_patch_table(&input);
        assert_eq!(luma_patched_state(r), 0, "{:?}", s(luma_patched_error(r)));
        assert!(luma_patched_error(r).is_null());
        let mut size = 0u64;
        let out = luma_patched_table(r, &mut size);
        let patched = TableIr::from_ntbl(std::slice::from_raw_parts(out, size as usize)).unwrap();
        assert_eq!(patched, expected);
        assert_eq!(patched.rows.len(), 3);

        let n = luma_patched_fact_count(r);
        assert_eq!(n as usize, rust.len() + 1);
        let first = &*luma_patched_fact_at(r, 0);
        assert_eq!((first.kind, s(first.mod_id).as_deref(), s(first.file).as_deref()), (LUMA_PATCH_FAILED, Some("not.json"), Some("pn.json")));
        // Not JSON: the validator's L400 with its own reading of the fault, said in each language.
        assert_eq!(first.code, 400);
        let english = s(luma_patched_fact_reason(r, 0, null())).unwrap();
        assert_eq!(s(first.why).as_deref(), Some(english.as_str()));
        let lang = CString::new("EuFrench").unwrap();
        let french = s(luma_patched_fact_reason(r, 0, lang.as_ptr())).unwrap();
        assert!(!english.is_empty() && french != english && !french.contains('{'), "{english} / {french}");
        for (i, fact) in rust.iter().enumerate() {
            let c = &*luma_patched_fact_at(r, i as u32 + 1);
            match fact {
                Fact::Conflict { row, column, lower, higher, .. } => {
                    assert_eq!(c.kind, LUMA_PATCH_CONFLICT);
                    assert_eq!((s(c.mod_id), s(c.other), s(c.row), s(c.column)), (Some(higher.clone()), Some(lower.clone()), Some(row.clone()), column.clone()));
                }
                Fact::Missing { row, mod_id, file, removed_by, .. } => {
                    assert_eq!(c.kind, LUMA_PATCH_MISSING);
                    assert_eq!((s(c.mod_id), s(c.file), s(c.row), s(c.other)), (Some(mod_id.clone()), Some(file.clone()), Some(row.clone()), removed_by.clone()));
                }
                Fact::Selected { mod_id, selector, rows, .. } => {
                    assert_eq!((c.kind, s(c.mod_id), c.selector as usize, c.rows as usize), (LUMA_PATCH_SELECTED, Some(mod_id.clone()), *selector, *rows));
                }
                Fact::Failed { mod_id, why, refusal, .. } => {
                    assert_eq!((c.kind, s(c.mod_id), s(c.why)), (LUMA_PATCH_FAILED, Some(mod_id.clone()), Some(why.clone())));
                    assert_eq!(c.code, u32::from(refusal.code));
                    let lang = CString::new("JpJapanese").unwrap();
                    assert_eq!(s(luma_patched_fact_reason(r, i as u32 + 1, lang.as_ptr())), Some(nebula_import_reason(refusal, "JpJapanese")));
                }
            }
        }
        // What the facts say, here: a and b both set row 1's Obj_arg0 (b's is used); b names row 7, not there; b's
        // selector matched 1 row; broken's file is left out.
        let kinds: Vec<u32> = (1..n).map(|i| (*luma_patched_fact_at(r, i)).kind).collect();
        assert_eq!(kinds, vec![LUMA_PATCH_FAILED, LUMA_PATCH_CONFLICT, LUMA_PATCH_MISSING, LUMA_PATCH_SELECTED], "{rust:?}");
        assert!(luma_patched_fact_at(r, n).is_null());
        assert!(luma_patched_fact_reason(r, n, null()).is_null());
        let conflict = (0..n).find(|&i| (*luma_patched_fact_at(r, i)).kind == LUMA_PATCH_CONFLICT).unwrap();
        assert!(luma_patched_fact_reason(r, conflict, null()).is_null());
        assert_eq!((*luma_patched_fact_at(r, conflict)).code, 0);
        // The column by the name the patch wrote, not its hash.
        assert_eq!(s((*luma_patched_fact_at(r, conflict)).column).as_deref(), Some("Obj_arg0"));
        luma_patched_free(r);
    }
    assert!(rust.iter().any(|f| matches!(f, Fact::Conflict { lower, higher, .. } if lower == "a" && higher == "b")));
}

#[test]
fn every_input_fault_is_refused_with_its_reason() {
    let table = objinfo().to_ntbl();
    let path = CString::new(OBJINFO).unwrap();
    let ok = LumaPatchInput { size: std::mem::size_of::<LumaPatchInput>() as u32, path: path.as_ptr(), table: table.as_ptr(), table_size: table.len() as u64, patches: null(), patch_count: 0, links: null(), link_count: 0, mods: null(), mod_count: 0 };
    let damaged = b"NTBL\xff\xff\xff\xff";
    let no_key = TableIr { columns: vec![Column { hash: column_hash("name"), mask: u32::MAX, kind: Kind::Text, width: 32 }], rows: vec![vec![Cell::Text(b"x".to_vec())]] }.to_ntbl();
    let null_id = [LumaTablePatch { mod_id: null(), file: null(), json: null(), json_size: 0 }];
    let one = Given::new(&[("a", "pa", "{}")]);
    let one_c = one.c();
    let cases: Vec<(LumaPatchInput, &str)> = vec![
        (LumaPatchInput { size: 4, ..ok }, "size"),
        (LumaPatchInput { size: std::mem::size_of::<LumaPatchInput>() as u32 + 8, ..ok }, "size"),
        (LumaPatchInput { path: null(), ..ok }, "path"),
        (LumaPatchInput { table: null(), table_size: 4, ..ok }, "table"),
        (LumaPatchInput { table: damaged.as_ptr(), table_size: damaged.len() as u64, ..ok }, "StageData"),
        (LumaPatchInput { patch_count: 2, ..ok }, "patches"),
        (LumaPatchInput { patches: null_id.as_ptr(), patch_count: 1, ..ok }, "mod id"),
        (LumaPatchInput { table: no_key.as_ptr(), table_size: no_key.len() as u64, patches: one_c.as_ptr(), patch_count: 1, ..ok }, "l_id"),
    ];
    unsafe {
        let r = luma_patch_table(null());
        assert_eq!(luma_patched_state(r), -1);
        assert!(s(luma_patched_error(r)).unwrap().contains("null"));
        luma_patched_free(r);
        for (input, says) in &cases {
            let r = luma_patch_table(input);
            assert_eq!(luma_patched_state(r), -1, "{says}");
            let why = s(luma_patched_error(r)).unwrap();
            assert!(why.contains(says), "{says}: {why}");
            let mut size = 7u64;
            assert!(luma_patched_table(r, &mut size).is_null());
            assert_eq!((size, luma_patched_fact_count(r)), (0, 0));
            luma_patched_free(r);
        }
        // No patches: the game's table, unchanged.
        let r = luma_patch_table(&ok);
        let mut size = 0u64;
        let out = luma_patched_table(r, &mut size);
        assert_eq!(std::slice::from_raw_parts(out, size as usize), table.as_slice());
        luma_patched_free(r);
        // Null results are answered, not followed.
        assert_eq!(luma_patched_state(null()), -1);
        assert_eq!(luma_patched_fact_count(null()), 0);
        assert!(luma_patched_fact_at(null(), 0).is_null());
        assert!(luma_patched_fact_reason(null(), 0, null()).is_null());
        luma_patched_free(std::ptr::null_mut());
    }
}

#[test]
fn a_patch_that_names_no_row_is_a_fact() {
    // json! here only to show the shape a game passes as bytes.
    let p = json!({"patch": 1, "key": ["l_id"], "set": [{"where": {"l_id": 5}, "values": {"Obj_arg0": 1}}]}).to_string();
    let given = Given::new(&[("m", "pm", &p)]);
    let patches = given.c();
    let table = objinfo().to_ntbl();
    let path = CString::new(OBJINFO).unwrap();
    let input = LumaPatchInput { size: std::mem::size_of::<LumaPatchInput>() as u32, path: path.as_ptr(), table: table.as_ptr(), table_size: table.len() as u64, patches: patches.as_ptr(), patch_count: 1, links: null(), link_count: 0, mods: null(), mod_count: 0 };
    unsafe {
        let r = luma_patch_table(&input);
        assert_eq!(luma_patched_fact_count(r), 1);
        let f = &*luma_patched_fact_at(r, 0);
        assert_eq!((f.kind, s(f.row).as_deref(), f.other.is_null()), (LUMA_PATCH_MISSING, Some("5"), true));
        luma_patched_free(r);
    }
}

/// Table-keys 13 through the C call: a key added to one layer of a zone must be new to its other layers.
#[test]
fn a_link_key_is_new_to_the_zone_through_the_c_call() {
    let layer_a = TableIr { columns: objinfo().columns, rows: vec![vec![Cell::Bits(50), Cell::Text(b"Kuribo".to_vec()), Cell::Bits(u32::MAX)]] }.to_ntbl();
    let common = objinfo().to_ntbl();
    let high = Given::new(&[("high", "ph.json", r#"{"patch": 1, "key": ["l_id"], "add": [{"l_id": 60, "name": "Teresa"}]}"#)]);
    let low = Given::new(&[
        ("low", "pl.json", r#"{"patch": 1, "key": ["l_id"], "add": [{"l_id": 60, "name": "Kuribo"}, {"l_id": 70, "name": "Kuribo"}]}"#),
        ("taken", "pt.json", r#"{"patch": 1, "key": ["l_id"], "add": [{"l_id": 50, "name": "Kuribo"}]}"#),
    ]);
    let (high_c, low_c) = (high.c(), low.c());
    let common_path = CString::new(OBJINFO).unwrap();
    // A disc path names the same table (its archive is a folder of the store).
    let a_path = CString::new("StageData/AstroGalaxy/AstroGalaxy.arc/jmp/Placement/LayerA/ObjInfo").unwrap();
    // Every mod with a patch here, from the bottom of the list up (F3.11-1: one it leaves out is refused below).
    let ids = [CString::new("low").unwrap(), CString::new("taken").unwrap(), CString::new("high").unwrap()];
    let mods: Vec<*const c_char> = ids.iter().map(|m| m.as_ptr()).collect();
    let input = |path: &CString, table: &[u8], patches: &[LumaTablePatch], links: &[LumaLinkTable]| LumaPatchInput {
        size: std::mem::size_of::<LumaPatchInput>() as u32,
        path: path.as_ptr(),
        table: table.as_ptr(),
        table_size: table.len() as u64,
        patches: patches.as_ptr(),
        patch_count: patches.len() as u32,
        links: links.as_ptr(),
        link_count: links.len() as u32,
        mods: mods.as_ptr(),
        mod_count: mods.len() as u32,
    };
    let ids_of = |bytes: &[u8]| -> Vec<u32> {
        TableIr::from_ntbl(bytes).unwrap().rows.iter().map(|r| if let Cell::Bits(b) = r[0] { b } else { 0 }).collect()
    };
    unsafe {
        // Common, with LayerA (its game row 50, high's add of 60) as its group.
        let links = [LumaLinkTable { path: a_path.as_ptr(), table: layer_a.as_ptr(), table_size: layer_a.len() as u64, patches: high_c.as_ptr(), patch_count: 1 }];
        let r = luma_patch_table(&input(&common_path, &common, &low_c, &links));
        assert_eq!(luma_patched_state(r), 0, "{:?}", s(luma_patched_error(r)));
        let facts: Vec<&LumaPatchFact> = (0..luma_patched_fact_count(r)).map(|i| &*luma_patched_fact_at(r, i)).collect();
        // 60: the higher mod's row in LayerA stays; low's is left out and named, the rest of low's patch applies.
        assert!(
            facts.iter().any(|f| f.kind == LUMA_PATCH_CONFLICT
                && s(f.mod_id).as_deref() == Some("high")
                && s(f.other).as_deref() == Some("low")
                && s(f.row).as_deref() == Some("60")
                && f.column.is_null()),
            "{}",
            facts.len()
        );
        // 50 is LayerA's in the game: "taken" is left out whole, L402, naming LayerA's table.
        let failed = facts.iter().position(|f| f.kind == LUMA_PATCH_FAILED).expect("a failed fact");
        assert_eq!((s(facts[failed].mod_id).as_deref(), facts[failed].code), (Some("taken"), 402));
        let why = s(luma_patched_fact_reason(r, failed as u32, null())).unwrap();
        assert!(why.contains("LayerA/ObjInfo") && why.contains("50"), "{why}");
        let mut size = 0;
        assert_eq!(ids_of(std::slice::from_raw_parts(luma_patched_table(r, &mut size), size as usize)), vec![1, 2, 70]);
        luma_patched_free(r);

        // A mod the list does not name cannot be ordered: -1, naming it, and the game's table stands (F3.11-1).
        let short = LumaPatchInput { mod_count: 1, ..input(&common_path, &common, &low_c, &links) };
        let r = luma_patch_table(&short);
        assert_eq!(luma_patched_state(r), -1);
        assert!(s(luma_patched_error(r)).is_some_and(|e| e.starts_with("taken: ")), "{:?}", s(luma_patched_error(r)));
        assert!(luma_patched_table(r, &mut size).is_null());
        luma_patched_free(r);

        // LayerA, with Common and low's patches as its group: high's 60 stays, and the conflict is Common's fact only.
        let links = [LumaLinkTable { path: common_path.as_ptr(), table: common.as_ptr(), table_size: common.len() as u64, patches: low_c.as_ptr(), patch_count: 2 }];
        let r = luma_patch_table(&input(&a_path, &layer_a, &high_c, &links));
        assert_eq!(luma_patched_state(r), 0, "{:?}", s(luma_patched_error(r)));
        assert_eq!(luma_patched_fact_count(r), 0);
        let mut size = 0;
        assert_eq!(ids_of(std::slice::from_raw_parts(luma_patched_table(r, &mut size), size as usize)), vec![50, 60]);
        luma_patched_free(r);
    }
}
