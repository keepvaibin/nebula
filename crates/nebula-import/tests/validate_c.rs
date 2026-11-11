//! The validator's C interface (include/luma_validate.h) against the Rust one: the same findings, field for field and
//! text for text, through a store given as C functions; the kept report at a start; other mods by their kept reports;
//! and every input fault refused with its reason, never a crash.

#[path = "../../luma-validate/tests/common/mod.rs"]
mod common;
use common::*;

use luma_validate::semver::Version;
use luma_validate::{message, validate, ColumnType, Game, Input, Names, Source, Store, Table};
use nebula_import::validate_c::*;
use std::collections::HashSet;
use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr::null;

const TABLE: &str = "Stage/Table.bcsv";

// ---------------------------------------------------------------- one store, as Rust and as C functions

struct RustStore;
impl Store for RustStore {
    fn path(&self, lower: &str) -> Option<String> {
        [TABLE, "ObjectData/Kuribo.arc"].iter().find(|p| p.to_lowercase() == lower).map(|p| p.to_string())
    }
    fn table(&self, path: &str) -> Option<Table> {
        (path == TABLE).then(|| Table {
            key: vec!["name".into()],
            columns: vec![("name".into(), ColumnType::Text), ("count".into(), ColumnType::Int { min: 0, max: 10 })],
            rows: Some(HashSet::from(["Alpha".to_string()])),
            case_insensitive: false,
            ..Default::default()
        })
    }
    fn may_be_new(&self, path: &str) -> bool {
        path.ends_with(".wav")
    }
}

struct CTable {
    _names: Vec<CString>,
    key: Vec<*const c_char>,
    columns: Vec<LumaTableColumn>,
    rows: Vec<*const c_char>,
    table: LumaTable,
}

fn c_table() -> Box<CTable> {
    let names: Vec<CString> = ["name", "count", "Alpha"].iter().map(|s| CString::new(*s).unwrap()).collect();
    let mut t = Box::new(CTable {
        key: vec![names[0].as_ptr()],
        columns: vec![
            LumaTableColumn { name: names[0].as_ptr(), kind: 2, min: 0, max: 0 },
            LumaTableColumn { name: names[1].as_ptr(), kind: 0, min: 0, max: 10 },
        ],
        rows: vec![names[2].as_ptr()],
        _names: names,
        table: LumaTable { key: null(), key_count: 0, columns: null(), column_count: 0, rows: null(), row_count: 0, case_insensitive: 0 },
    });
    t.table = LumaTable { key: t.key.as_ptr(), key_count: 1, columns: t.columns.as_ptr(), column_count: 2, rows: t.rows.as_ptr(), row_count: 1, case_insensitive: 0 };
    t
}

unsafe extern "C" fn c_path(_: *mut c_void, lower: *const c_char, out: *mut c_char, size: u32) -> u32 {
    let lower = CStr::from_ptr(lower).to_str().unwrap();
    let Some(found) = RustStore.path(lower) else { return 0 };
    let n = found.len() as u32;
    if n < size {
        std::ptr::copy_nonoverlapping(found.as_ptr(), out.cast(), found.len());
        *out.add(found.len()) = 0;
    }
    n
}
unsafe extern "C" fn c_table_of(context: *mut c_void, path: *const c_char) -> *const LumaTable {
    let path = CStr::from_ptr(path).to_str().unwrap();
    if path == TABLE {
        &(*(context as *const CTable)).table
    } else {
        null()
    }
}
unsafe extern "C" fn c_may_be_new(_: *mut c_void, path: *const c_char) -> u32 {
    RustStore.may_be_new(CStr::from_ptr(path).to_str().unwrap()) as u32
}

// ---------------------------------------------------------------- the two calls

const ROWS: &str = "MR::getPlayerPos\tVec3f *()\thook\t1\nGameSystem::update\tvoid()\thook\t2\nLiveActor::LiveActor\t(const char *)\ttell\t3\n";

struct Call {
    path: CString,
    platform: CString,
    nebula: CString,
    region: CString,
    store_key: CString,
    table: Box<CTable>,
    store: LumaGameStore,
    installed: Vec<LumaInstalledMod>,
    files: Vec<CString>,
}

impl Call {
    fn new(path: &std::path::Path, platform: &str) -> Box<Call> {
        let mut c = Box::new(Call {
            path: CString::new(path.to_str().unwrap()).unwrap(),
            platform: CString::new(platform).unwrap(),
            nebula: CString::new("2.0.0").unwrap(),
            region: CString::new("RMGE01").unwrap(),
            store_key: CString::new("store 1").unwrap(),
            table: c_table(),
            store: LumaGameStore { context: std::ptr::null_mut(), path: Some(c_path), table: Some(c_table_of), may_be_new: Some(c_may_be_new) },
            installed: Vec::new(),
            files: Vec::new(),
        });
        c.store.context = (&*c.table as *const CTable).cast_mut().cast();
        c
    }
    fn input(&self, kept: Option<&[u8]>) -> LumaValidateInput {
        LumaValidateInput {
            size: std::mem::size_of::<LumaValidateInput>() as u32,
            path: self.path.as_ptr(),
            platform: self.platform.as_ptr(),
            nebula: self.nebula.as_ptr(),
            region: self.region.as_ptr(),
            languages: null(),
            language_count: 0,
            names: ROWS.as_ptr().cast(),
            names_size: ROWS.len() as u64,
            luma_api: 1,
            permissions: null(),
            permission_count: 0,
            store: &self.store,
            store_key: self.store_key.as_ptr(),
            installed: self.installed.as_ptr(),
            installed_count: self.installed.len() as u32,
            position: u32::MAX,
            options: null(),
            option_count: 0,
            kept: kept.map_or(null(), |k| k.as_ptr()),
            kept_size: kept.map_or(0, |k| k.len() as u64),
        }
    }
}

/// A report read through the C functions: state, findings as (code, level, mod id, file, line, column, pointer, English
/// text, German text), the kept bytes.
#[derive(Debug, PartialEq)]
struct Got {
    state: i32,
    findings: Vec<(u32, u32, Option<String>, String, u32, u32, Option<String>, String, String)>,
    errors: u32,
    kept: Vec<u8>,
    invalid: Option<String>,
}

unsafe fn s(p: *const c_char) -> Option<String> {
    (!p.is_null()).then(|| CStr::from_ptr(p).to_str().unwrap().to_string())
}

fn c_call(input: Option<&LumaValidateInput>) -> Got {
    unsafe {
        let r = luma_validate(input.map_or(null(), |i| i as *const _));
        assert!(!r.is_null());
        let mut findings = Vec::new();
        for i in 0..luma_report_count(r) {
            let en = &*luma_report_at(r, i, b"UsEnglish\0".as_ptr().cast());
            let row = (en.code, en.level, s(en.mod_id), s(en.file).unwrap(), en.line, en.column, s(en.pointer), s(en.text).unwrap());
            let de = s((*luma_report_at(r, i, b"EuGerman\0".as_ptr().cast())).text).unwrap();
            findings.push((row.0, row.1, row.2, row.3, row.4, row.5, row.6, row.7, de));
        }
        assert!(luma_report_at(r, luma_report_count(r), null()).is_null());
        let mut size = 0u64;
        let k = luma_report_kept(r, &mut size);
        let kept = if k.is_null() { Vec::new() } else { std::slice::from_raw_parts(k, size as usize).to_vec() };
        let got = Got { state: luma_report_state(r), findings, errors: luma_report_errors(r), kept, invalid: s(luma_report_invalid(r)) };
        luma_report_free(r);
        got
    }
}

fn rust_call(path: &std::path::Path, platform: &str) -> Vec<(u32, u32, Option<String>, String, u32, u32, Option<String>, String, String)> {
    let mut names = Names { api: 1, ..Names::default() };
    names.add_rows(ROWS).unwrap();
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], names: Some(&names), store: Some(&RustStore) };
    let platforms = [platform];
    let r = validate(&Input { source: Source::File(path), game: Some(&game), installed: &[], platforms: &platforms, options: None, position: None });
    r.findings
        .iter()
        .map(|f| {
            let level = if f.level == luma_validate::Level::Error { 0 } else { 1 };
            (f.code as u32, level, f.mod_id.clone(), f.file.clone(), f.line.unwrap_or(0), f.column.unwrap_or(0), f.pointer.clone(), message(f, "UsEnglish"), message(f, "EuGerman"))
        })
        .collect()
}

fn write(dir: &std::path::Path, name: &str, bytes: &[u8]) -> std::path::PathBuf {
    let p = dir.join(name);
    std::fs::write(&p, bytes).unwrap();
    p
}

fn fixture(path: &str) -> Vec<u8> {
    std::fs::read(std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../luma-validate/tests/data").join(path)).unwrap()
}

#[test]
fn the_c_interface_gives_what_the_rust_one_gives() {
    let dir = tempfile::tempdir().unwrap();
    let bad_row = br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "Omega"}, "values": {"count": 3}}]}"#;
    let mut broken = file("layers/base/files/ObjectData/Kuribo.arc", &[7; 64]);
    broken.crc = Some(0x1234_5678);
    let mods = [
        ("passes.luma", luma(&[], &[])),
        ("broken.luma", luma(&[("layers", r#"[{"name": "base"}]"#), ("needs", r#"[{"id": "other.mod"}]"#)], &[file("layers/base/patches/stage/table.bcsv.json", bad_row), broken])),
        ("not-a-zip.luma", b"not a zip".to_vec()),
        ("bad-json.luma", luma(&[("name", "5")], &[])),
        ("code.luma", luma(&[("code", r#"{"platforms": ["windows-x64"]}"#)], &[file("code/windows-x64/mod.dll", &fixture("pe-x64/missing.dll"))])),
    ];
    for (name, bytes) in &mods {
        let path = write(dir.path(), name, bytes);
        let call = Call::new(&path, "windows-x64");
        let full = c_call(Some(&call.input(None)));
        assert_eq!(full.state, 0, "{name}: {:?}", full.invalid);
        assert_eq!(full.findings, rust_call(&path, "windows-x64"), "{name}");
        assert_eq!(full.errors as usize, full.findings.iter().filter(|f| f.1 == 0).count());
        // At a start, with the kept report: the same findings, and the same kept bytes.
        let start = c_call(Some(&call.input(Some(&full.kept))));
        let zip = !name.starts_with("not-a-zip");
        assert_eq!(start.state, if zip { 1 } else { 0 }, "{name}");
        assert_eq!(start.findings, full.findings, "{name}");
        assert_eq!(start.kept, full.kept, "{name}");
    }
}

#[test]
fn other_mods_are_given_by_their_kept_reports() {
    let dir = tempfile::tempdir().unwrap();
    let needs = write(dir.path(), "needs.luma", &luma(&[("needs", r#"[{"id": "other.mod", "version": ">=1.0.0"}]"#)], &[]));
    let other = write(dir.path(), "other.luma", &luma(&[("id", r#""other.mod""#)], &[]));
    let other_kept = c_call(Some(&Call::new(&other, "windows-x64").input(None))).kept;

    let mut call = Call::new(&needs, "windows-x64");
    let codes = |c: &Call| c_call(Some(&c.input(None))).findings.iter().map(|f| f.0).collect::<Vec<_>>();
    assert_eq!(codes(&call), vec![800]);
    call.files.push(CString::new("other.luma").unwrap());
    call.installed.push(LumaInstalledMod { file: call.files[0].as_ptr(), on: 1, kept: other_kept.as_ptr(), kept_size: other_kept.len() as u64 });
    assert_eq!(codes(&call), Vec::<u32>::new());
    call.installed[0].on = 0;
    assert_eq!(codes(&call), vec![800]);

    // Two mods replacing one function: the one loading later is refused.
    let replace = luma(&[("code", r#"{"platforms": ["windows-x64"]}"#)], &[file("code/windows-x64/mod.dll", &fixture("pe-x64/replace.dll"))]);
    let first = write(dir.path(), "first.luma", &replace);
    let second_bytes = {
        let mut over = vec![("id", r#""second.mod""#)];
        over.push(("code", r#"{"platforms": ["windows-x64"]}"#));
        luma(&over, &[file("code/windows-x64/mod.dll", &fixture("pe-x64/replace.dll"))])
    };
    let second = write(dir.path(), "second.luma", &second_bytes);
    let first_kept = c_call(Some(&Call::new(&first, "windows-x64").input(None))).kept;
    let mut call = Call::new(&second, "windows-x64");
    call.files.push(CString::new("first.luma").unwrap());
    call.installed.push(LumaInstalledMod { file: call.files[0].as_ptr(), on: 1, kept: first_kept.as_ptr(), kept_size: first_kept.len() as u64 });
    let got = c_call(Some(&call.input(None)));
    assert_eq!(got.findings.iter().map(|f| f.0).collect::<Vec<_>>(), vec![805], "{:?}", got.findings);
    let mut later = call.input(None);
    later.position = 0;
    assert!(c_call(Some(&later)).findings.is_empty());
}

#[test]
fn a_faulty_input_is_refused_with_its_reason() {
    let dir = tempfile::tempdir().unwrap();
    let path = write(dir.path(), "m.luma", &luma(&[], &[]));
    let call = Call::new(&path, "windows-x64");
    let refused = |input: Option<&LumaValidateInput>, why: &str| {
        let got = c_call(input);
        assert_eq!((got.state, got.findings.len(), got.kept.len()), (-1, 0, 0));
        let reason = got.invalid.unwrap();
        assert!(reason.contains(why), "{reason}");
    };
    refused(None, "null");
    let mut i = call.input(None);
    i.size -= 8;
    refused(Some(&i), "sizeof");
    let mut i = call.input(None);
    i.path = null();
    refused(Some(&i), "path");
    let beos = CString::new("beos-x64").unwrap();
    let mut i = call.input(None);
    i.platform = beos.as_ptr();
    refused(Some(&i), "beos-x64");
    let bad_names = "MR::x\tvoid()\tmaybe\t1\n";
    let mut i = call.input(None);
    i.names = bad_names.as_ptr().cast();
    i.names_size = bad_names.len() as u64;
    refused(Some(&i), "names line 1");
    let mut store = LumaGameStore { path: None, ..call.store };
    store.context = call.store.context;
    let mut i = call.input(None);
    i.store = &store;
    refused(Some(&i), "store");
    let junk = b"not a kept report";
    let file = CString::new("x.luma").unwrap();
    let installed = [LumaInstalledMod { file: file.as_ptr(), on: 1, kept: junk.as_ptr(), kept_size: junk.len() as u64 }];
    let mut i = call.input(None);
    i.installed = installed.as_ptr();
    i.installed_count = 1;
    refused(Some(&i), "installed[0] x.luma");

    // A kept report that does not read is not a fault: every rule runs.
    let got = c_call(Some(&call.input(Some(junk))));
    assert_eq!(got.state, 0);
    // A missing file is a finding, as in Rust.
    let gone = Call::new(&dir.path().join("gone.luma"), "windows-x64");
    let got = c_call(Some(&gone.input(None)));
    assert_eq!((got.state, got.findings.iter().map(|f| f.0).collect::<Vec<_>>()), (0, vec![100]));
}

#[test]
fn the_layout_is_the_header_s() {
    // tests/luma_validate_layout.c asserts the same numbers of the header, for each target:
    //   clang --target=<t> -std=c11 -fsyntax-only tests/luma_validate_layout.c
    // with x86_64-pc-windows-msvc, aarch64-pc-windows-msvc, x86_64-linux-gnu, aarch64-linux-android24,
    // arm64-apple-macos11, x86_64-apple-macos11, arm64-apple-ios14.
    use std::mem::{offset_of, size_of};
    assert_eq!(size_of::<LumaTableColumn>(), 32);
    assert_eq!((size_of::<LumaTable>(), offset_of!(LumaTable, case_insensitive)), (48, 44));
    assert_eq!(size_of::<LumaGameStore>(), 32);
    assert_eq!(size_of::<LumaInstalledMod>(), 32);
    assert_eq!(size_of::<LumaOption>(), 16);
    assert_eq!(size_of::<LumaValidateInput>(), 160);
    assert_eq!(
        (offset_of!(LumaValidateInput, names_size), offset_of!(LumaValidateInput, store), offset_of!(LumaValidateInput, position), offset_of!(LumaValidateInput, kept_size)),
        (64, 96, 124, 152)
    );
    assert_eq!((size_of::<LumaFinding>(), offset_of!(LumaFinding, text)), (48, 40));
}
