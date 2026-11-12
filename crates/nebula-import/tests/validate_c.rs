//! The validator's C interface (include/luma_validate.h) against the Rust one: the same findings, field for field and
//! text for text, through a store given as C functions; the kept report at a start; other mods by their kept reports;
//! and every input fault refused with its reason, never a crash.

#[path = "../../luma-validate/tests/common/mod.rs"]
mod common;
use common::*;

use luma_validate::semver::Version;
use luma_validate::{message, validate, Game, Input, Names, Source, Store, Table};
use nebula_import::cook::table::{column_hash, Cell, Column, Kind, TableIr};
use nebula_import::store_view::table_view;
use nebula_import::validate_c::*;
use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr::null;

const TABLE: &str = "Stage/Table.bcsv";
/// The galaxies' folder as the game lists it: two names, one a file named like a galaxy's folder.
const LISTED: [(&str, u32); 3] = [("AstroGalaxyScenario", 1), ("AstroGalaxy", 1), ("OddScenario", 0)];

/// The store's table: `name` (text) and `count` (an 8-bit int), one row "Alpha".
fn table_ir() -> TableIr {
    TableIr {
        columns: vec![
            Column { hash: column_hash("name"), mask: u32::MAX, kind: Kind::Text, width: 32 },
            Column { hash: column_hash("count"), mask: 0xff, kind: Kind::Int, width: 8 },
        ],
        rows: vec![vec![Cell::Text(b"Alpha".to_vec()), Cell::Bits(1)]],
    }
}

// ---------------------------------------------------------------- one store, as Rust and as C functions

struct RustStore;
impl Store for RustStore {
    fn path(&self, lower: &str) -> Option<String> {
        [TABLE, "ObjectData/Kuribo.arc"].iter().find(|p| p.to_lowercase() == lower).map(|p| p.to_string())
    }
    fn table(&self, path: &str) -> Option<Table> {
        (path == TABLE).then(|| table_view(TABLE, &table_ir()))
    }
    fn may_be_new(&self, path: &str) -> bool {
        path.ends_with(".wav")
    }
    fn folder(&self, lower: &str) -> Option<Vec<(String, bool)>> {
        (lower == "stagedata").then(|| {
            let mut names: Vec<(String, bool)> = LISTED.iter().map(|(n, f)| (n.to_string(), *f != 0)).collect();
            names.sort();
            names
        })
    }
}

/// What the C store hands out: the table's cooked bytes, kept for the call.
struct CTable {
    ntbl: Vec<u8>,
    store_path: CString,
}

fn c_table() -> Box<CTable> {
    Box::new(CTable { ntbl: table_ir().to_ntbl(), store_path: CString::new(TABLE).unwrap() })
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
unsafe extern "C" fn c_table_of(context: *mut c_void, path: *const c_char, bytes: *mut *const u8, size: *mut u64, store_path: *mut *const c_char) -> u32 {
    let path = CStr::from_ptr(path).to_str().unwrap();
    if path != TABLE {
        return 0;
    }
    let t = &*(context as *const CTable);
    *bytes = t.ntbl.as_ptr();
    *size = t.ntbl.len() as u64;
    *store_path = t.store_path.as_ptr();
    1
}
/// Lists the galaxies' folder in the store's own order, the first name twice (a store may repeat; one is kept).
unsafe extern "C" fn c_folder(_: *mut c_void, lower: *const c_char, sink: LumaFolderSink, sink_context: *mut c_void) -> i32 {
    if CStr::from_ptr(lower).to_str().unwrap() != "stagedata" {
        return -1;
    }
    let names: Vec<CString> = LISTED.iter().map(|(n, _)| CString::new(*n).unwrap()).collect();
    for (name, (_, is_folder)) in names.iter().zip(LISTED).chain(std::iter::once((&names[0], LISTED[0]))) {
        sink(sink_context, name.as_ptr(), is_folder);
    }
    LISTED.len() as i32
}

fn c_store(context: *mut c_void) -> LumaGameStore {
    LumaGameStore {
        size: std::mem::size_of::<LumaGameStore>() as u32,
        context,
        path: Some(c_path),
        table: Some(c_table_of),
        folder: Some(c_folder),
    }
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
    /// The types file given; empty: none.
    types: Vec<u8>,
    /// The services catalog given; empty: none.
    services: Vec<u8>,
    /// The renderer's feature list given; empty: none.
    renderer: Vec<u8>,
    /// This device's features; None: not given.
    device: Option<Vec<CString>>,
    device_ptrs: Vec<*const c_char>,
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
            store: c_store(std::ptr::null_mut()),
            installed: Vec::new(),
            files: Vec::new(),
            types: Vec::new(),
            services: Vec::new(),
            renderer: Vec::new(),
            device: None,
            device_ptrs: Vec::new(),
        });
        c.store.context = (&*c.table as *const CTable).cast_mut().cast();
        c
    }
    fn set_device(&mut self, features: &[&str]) {
        let list: Vec<CString> = features.iter().map(|f| CString::new(*f).unwrap()).collect();
        self.device_ptrs = list.iter().map(|c| c.as_ptr()).collect();
        self.device = Some(list);
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
            services: if self.services.is_empty() { null() } else { self.services.as_ptr().cast() },
            services_size: self.services.len() as u64,
            store: &self.store,
            store_key: self.store_key.as_ptr(),
            installed: self.installed.as_ptr(),
            installed_count: self.installed.len() as u32,
            position: u32::MAX,
            options: null(),
            option_count: 0,
            kept: kept.map_or(null(), |k| k.as_ptr()),
            kept_size: kept.map_or(0, |k| k.len() as u64),
            types: if self.types.is_empty() { null() } else { self.types.as_ptr().cast() },
            types_size: self.types.len() as u64,
            renderer: if self.renderer.is_empty() { null() } else { self.renderer.as_ptr().cast() },
            renderer_size: self.renderer.len() as u64,
            device_features: if self.device.is_some() { self.device_ptrs.as_ptr() } else { null() },
            device_feature_count: self.device_ptrs.len() as u32,
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
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: Some(&names), store: Some(&RustStore) };
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
    let mut store = c_store(call.store.context);
    store.path = None;
    let mut i = call.input(None);
    i.store = &store;
    refused(Some(&i), "store");
    let mut store = c_store(call.store.context);
    store.folder = None;
    i.store = &store;
    refused(Some(&i), "folder");
    let mut store = c_store(call.store.context);
    store.size -= 8;
    i.store = &store;
    refused(Some(&i), "sizeof(LumaGameStore)");
    let junk = b"not a kept report";
    let file = CString::new("x.luma").unwrap();
    let installed = [LumaInstalledMod { file: file.as_ptr(), on: 1, kept: junk.as_ptr(), kept_size: junk.len() as u64 }];
    let mut i = call.input(None);
    i.installed = installed.as_ptr();
    i.installed_count = 1;
    refused(Some(&i), "installed[0] x.luma");
    // A types file that does not read names where; one given without names is the caller's fault.
    let mut typed = Call::new(&path, "windows-x64");
    typed.types = br#"{"types": [], "globals": [], "left_out": [{"kind": "nerve", "name": "x", "file": "a.hpp", "line": 1, "reason": "r"}]}"#.to_vec();
    refused(Some(&typed.input(None)), "types: left_out[0].kind");
    typed.types = b"{".to_vec();
    refused(Some(&typed.input(None)), "types: line 1");
    let mut i = typed.input(None);
    i.names = null();
    refused(Some(&i), "types given without names");

    // A types file is part of what identifies the game: a kept report made without it checks every rule again.
    let mut typed = Call::new(&path, "windows-x64");
    typed.types = br#"{"types": [], "globals": [], "left_out": []}"#.to_vec();
    let with_types = c_call(Some(&typed.input(None)));
    assert_eq!(with_types.state, 0, "{:?}", with_types.invalid);
    let without = c_call(Some(&call.input(None)));
    assert_eq!(c_call(Some(&typed.input(Some(&with_types.kept)))).state, 1);
    assert_eq!(c_call(Some(&typed.input(Some(&without.kept)))).state, 0);

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
    assert_eq!((size_of::<LumaGameStore>(), offset_of!(LumaGameStore, context), offset_of!(LumaGameStore, folder)), (40, 8, 32));
    assert_eq!(size_of::<LumaInstalledMod>(), 32);
    assert_eq!(size_of::<LumaOption>(), 16);
    assert_eq!(size_of::<LumaValidateInput>(), 208);
    assert_eq!(
        (offset_of!(LumaValidateInput, names_size), offset_of!(LumaValidateInput, store), offset_of!(LumaValidateInput, position), offset_of!(LumaValidateInput, kept_size)),
        (64, 96, 124, 152)
    );
    assert_eq!((offset_of!(LumaValidateInput, types_size), offset_of!(LumaValidateInput, renderer_size), offset_of!(LumaValidateInput, device_feature_count)), (168, 184, 200));
    assert_eq!((size_of::<LumaFinding>(), offset_of!(LumaFinding, text)), (48, 40));
}

/// Validator revision 3: the C store's folder and table give the views the Rust store gives (one code builds them):
/// a folder's names in the game's case with whether each is a folder, repeats merged; -1 is no folder; a table from
/// its cooked bytes; bytes that are no table are no table.
#[test]
fn the_c_store_lists_folders_and_reads_tables_as_the_rust_one() {
    let call = Call::new(std::path::Path::new("m.luma"), "windows-x64");
    let store = nebula_import::validate_c::store_for_tests(&call.store);
    assert_eq!(store.folder("stagedata"), RustStore.folder("stagedata"));
    assert_eq!(store.folder("objectdata"), None);
    let (c, rust) = (store.table(TABLE).unwrap(), RustStore.table(TABLE).unwrap());
    assert_eq!((c.key, c.columns, c.rows, c.cells, c.repeated), (rust.key, rust.columns, rust.rows, rust.cells, rust.repeated));
    assert!(store.table("Stage/Other.bcsv").is_none());

    let junk = Box::new(CTable { ntbl: b"NTBL but not a table".to_vec(), store_path: CString::new(TABLE).unwrap() });
    let bad = c_store((&*junk as *const CTable).cast_mut().cast());
    assert!(nebula_import::validate_c::store_for_tests(&bad).table(TABLE).is_none());

    // Which files may be new is the library's own list, the one LumaLink's PakStore reads (F INBOX-G 46).
    for path in ["ObjectData/Nokotaro.arc", "/objectdata/kuribo.arc/kuribo_new.btk", "StageData/MyGalaxy/MyGalaxyMap.arc", "AudioRes/Stream/new.ast", "ObjectData/new.bcsv"] {
        assert_eq!(store.may_be_new(path), nebula_import::store_view::may_be_new(path), "{path}");
    }
    assert!(store.may_be_new("ObjectData/Nokotaro.arc") && !store.may_be_new("AudioRes/Stream/new.ast"));
}

/// luma_validate_message and luma_validate_reason say a refusal from (code, parameters) as the validator says its own
/// findings: a phrase by `P:`, a literal by `T:` or plain, the place by name; a missing parameter shows as `{name}`;
/// null entries are left out, never read.
#[test]
fn a_refusal_is_said_from_its_code_and_parameters_in_every_language() {
    let names = [c"why", c"row", c"where", c"mod", c"file", c"line"];
    let values = [c"P:Patch.KeyInGroup", c"T:5", c"layerb", c"T:low", c"T:layers/base/patches/x.json", c"3"];
    let n: Vec<*const c_char> = names.iter().map(|s| s.as_ptr()).collect();
    let v: Vec<*const c_char> = values.iter().map(|s| s.as_ptr()).collect();
    let args: Vec<(&str, &str)> = names.iter().zip(&values).map(|(a, b)| (a.to_str().unwrap(), b.to_str().unwrap())).collect();
    for language in luma_validate::LANGUAGES {
        let l = CString::new(language).unwrap();
        let said = unsafe { CStr::from_ptr(luma_validate_message(402, n.as_ptr(), v.as_ptr(), 6, l.as_ptr())) }.to_str().unwrap().to_string();
        assert_eq!(said, luma_validate::message_of(402, &args, language));
        assert!(!said.contains('{') && said.contains("low"), "{language}: {said}");
        let why = unsafe { CStr::from_ptr(luma_validate_reason(402, n.as_ptr(), v.as_ptr(), 6, l.as_ptr())) }.to_str().unwrap().to_string();
        assert_eq!(why, luma_validate::reason_of(402, &args, language));
        assert!(!why.contains('{') && why.contains('5'), "{language}: {why}");
    }
    // A parameter the text needs and the call lacks is seen, not dropped; null lists and entries are no fault.
    let missing = unsafe { CStr::from_ptr(luma_validate_message(402, n.as_ptr(), v.as_ptr(), 1, null())) }.to_str().unwrap().to_string();
    assert!(missing.contains("{mod}"), "{missing}");
    let nulls = [null(), c"x".as_ptr()];
    let none = unsafe { CStr::from_ptr(luma_validate_message(402, nulls.as_ptr(), nulls.as_ptr(), 2, null())) };
    assert!(!none.to_bytes().is_empty());
    assert!(!unsafe { luma_validate_reason(402, null(), null(), 3, null()) }.is_null());
}

#[test]
fn the_required_services_are_read_per_platform_and_after_a_start_s_check() {
    // pe-x64/service.dll imports net 1.0 without LUMA_OPTIONAL (luma-validate's tests/data/lib.c, SERVICE).
    let dir = tempfile::tempdir().unwrap();
    let bytes = luma(&[("code", r#"{"platforms": ["windows-x64"]}"#)], &[file("code/windows-x64/mod.dll", &fixture("pe-x64/service.dll"))]);
    let path = write(dir.path(), "s.luma", &bytes);
    let mut call = Call::new(&path, "windows-x64");
    call.services = br#"{"services": [{"name": "net", "major": 1, "minor": 0, "permission": null}]}"#.to_vec();
    let required = |input: &LumaValidateInput, platform: &str| unsafe {
        let r = luma_validate(input);
        assert_eq!(luma_report_errors(r), 0);
        let p = CString::new(platform).unwrap();
        let mut out = Vec::new();
        for i in 0.. {
            let name = luma_report_required_service(r, p.as_ptr(), i);
            if name.is_null() {
                break;
            }
            out.push(CStr::from_ptr(name).to_str().unwrap().to_string());
        }
        assert!(luma_report_required_service(r, null(), 0).is_null());
        assert!(luma_report_required_service(null(), p.as_ptr(), 0).is_null());
        luma_report_free(r);
        out
    };
    assert_eq!(required(&call.input(None), "windows-x64"), vec!["net".to_string()]);
    assert_eq!(required(&call.input(None), "linux-x64"), Vec::<String>::new());
    // The start's check takes the kept report and gives the same.
    let full = c_call(Some(&call.input(None)));
    assert_eq!(c_call(Some(&call.input(Some(&full.kept)))).state, 1);
    assert_eq!(required(&call.input(Some(&full.kept)), "windows-x64"), vec!["net".to_string()]);
    // The catalog identifies the game: a kept report made against another one checks every rule again.
    let mut other = Call::new(&path, "windows-x64");
    other.services = br#"{"services": [{"name": "net", "major": 1, "minor": 1, "permission": null}]}"#.to_vec();
    assert_eq!(c_call(Some(&other.input(Some(&full.kept)))).state, 0);
    // A catalog not in registry 9's form is the caller's fault, said; one without names likewise.
    other.services = br#"{"services": [{"name": "Net", "major": 1, "minor": 0, "permission": null}]}"#.to_vec();
    let got = c_call(Some(&other.input(None)));
    assert_eq!(got.state, -1);
    assert!(got.invalid.unwrap().contains("services: services[0].name"));
    let mut i = call.input(None);
    i.names = null();
    let got = c_call(Some(&i));
    assert!(got.invalid.unwrap().contains("services given without names"));
}

/// luma-format revision 4 through the C input: the renderer's list and the device's features give L806 (an error, the
/// mod off), both are part of the game's key, and a list or device name not in form is the caller's fault.
#[test]
fn renderer_features_and_the_device_s_reach_l806() {
    let dir = tempfile::tempdir().unwrap();
    let bytes = luma(&[("renderer", r#"{"features": ["ray_tracing"]}"#)], &[]);
    let path = write(dir.path(), "r.luma", &bytes);
    let mut call = Call::new(&path, "windows-x64");
    call.renderer = br#"{"renderer": {"features": ["mesh_shaders", "ray_tracing"]}}"#.to_vec();
    call.set_device(&["mesh_shaders"]);
    let got = c_call(Some(&call.input(None)));
    assert_eq!(got.state, 0);
    assert_eq!(got.errors, 1);
    let f = &got.findings[0];
    assert_eq!((f.0, f.1, f.3.as_str(), f.6.as_deref()), (806, 0, "luma.json", Some("/renderer/features/0")));
    assert_eq!(f.7, "test.mod needs ray_tracing, which this device's graphics do not have.");
    // A device that has it: nothing. The device is not part of the game's key (F's AR-1): another GPU takes the kept
    // report (state 1) and L806 is said from the manifest's rules, which run at every start.
    let mut has = Call::new(&path, "windows-x64");
    has.renderer = call.renderer.clone();
    has.set_device(&["ray_tracing", "mesh_shaders", "ray_tracing"]);
    let full = c_call(Some(&has.input(None)));
    assert_eq!((full.state, full.findings.len()), (0, 0));
    assert_eq!(c_call(Some(&has.input(Some(&full.kept)))).state, 1);
    let again = c_call(Some(&call.input(Some(&full.kept))));
    assert_eq!((again.state, again.findings.iter().map(|f| f.0).collect::<Vec<_>>()), (1, vec![806]));
    // The list too.
    let mut other = Call::new(&path, "windows-x64");
    other.renderer = br#"{"renderer": {"features": ["ray_tracing"]}}"#.to_vec();
    other.set_device(&["ray_tracing", "mesh_shaders"]);
    assert_eq!(c_call(Some(&other.input(Some(&full.kept)))).state, 0);
    // Not given: not checked, no finding.
    let none = Call::new(&path, "windows-x64");
    assert_eq!(c_call(Some(&none.input(None))).findings.len(), 0);
    // The caller's faults, said.
    let mut bad = Call::new(&path, "windows-x64");
    bad.renderer = br#"{"renderer": {"features": ["b", "a"]}}"#.to_vec();
    assert!(c_call(Some(&bad.input(None))).invalid.unwrap().contains("renderer: renderer.features[1]"));
    let mut bad = Call::new(&path, "windows-x64");
    bad.set_device(&["Ray Tracing"]);
    assert!(c_call(Some(&bad.input(None))).invalid.unwrap().contains("device_features[0] \"Ray Tracing\" is not a feature name"));
    let mut i = call.input(None);
    i.names = null();
    i.services = null();
    assert!(c_call(Some(&i)).invalid.unwrap().contains("renderer given without names"));
    let mut nulls = Call::new(&path, "windows-x64");
    nulls.set_device(&["ray_tracing"]);
    nulls.device_ptrs[0] = null();
    assert!(c_call(Some(&nulls.input(None))).invalid.unwrap().contains("device_features[0] is null"));
}

/// start-check F-S2 and references 3: luma_report_read_kept reads a kept report with no rule run (state 2; id,
/// version, required services and after-start rows answer; count 0, kept null); bytes that do not read are -1 with
/// why. luma_report_after_start gives the rows on either kind of report.
#[test]
fn a_kept_report_is_read_without_a_check_and_gives_its_facts() {
    let dir = tempfile::tempdir().unwrap();
    let bytes = luma(&[("code", r#"{"platforms": ["windows-x64"]}"#)], &[file("code/windows-x64/mod.dll", &fixture("pe-x64/service.dll"))]);
    let path = write(dir.path(), "s.luma", &bytes);
    let mut call = Call::new(&path, "windows-x64");
    call.services = br#"{"services": [{"name": "net", "major": 1, "minor": 0, "permission": null}]}"#.to_vec();
    let full = c_call(Some(&call.input(None)));
    let rows = |r: *const LumaReport| unsafe {
        let mut out = Vec::new();
        for i in 0.. {
            let (mut class, mut file, mut line) = (null(), null(), 0u32);
            if luma_report_after_start(r, i, &mut class, &mut file, &mut line) == 0 {
                break;
            }
            out.push((CStr::from_ptr(class).to_str().unwrap().to_string(), CStr::from_ptr(file).to_str().unwrap().to_string(), line));
        }
        out
    };
    unsafe {
        let r = luma_report_read_kept(full.kept.as_ptr(), full.kept.len() as u64);
        assert_eq!(luma_report_state(r), 2);
        assert!(luma_report_invalid(r).is_null());
        assert_eq!(s(luma_report_id(r)).as_deref(), Some("test.mod"));
        assert_eq!(s(luma_report_version(r)).as_deref(), Some("1.0.0"));
        let p = CString::new("windows-x64").unwrap();
        assert_eq!(s(luma_report_required_service(r, p.as_ptr(), 0)).as_deref(), Some("net"));
        assert!(luma_report_required_service(r, p.as_ptr(), 1).is_null());
        assert_eq!((luma_report_count(r), luma_report_errors(r), luma_report_not_checked_count(r)), (0, 0, 0));
        let mut size = 7u64;
        assert!(luma_report_kept(r, &mut size).is_null());
        assert!(luma_report_at(r, 0, null()).is_null());
        assert!(rows(r).is_empty());
        luma_report_free(r);

        // After-start rows, as the placement rules write them into the kept report's touches.
        let mut saved = luma_validate::Saved::parse(&full.kept).unwrap();
        saved.touches.after_start = vec!["MyMod::Lamp\u{1f}layers/a/stage.json\u{1f}12".into(), "Other\u{1f}layers/b/x.json\u{1f}3".into()];
        let kept = saved.to_bytes();
        let r = luma_report_read_kept(kept.as_ptr(), kept.len() as u64);
        assert_eq!(rows(r), vec![("MyMod::Lamp".to_string(), "layers/a/stage.json".to_string(), 12), ("Other".to_string(), "layers/b/x.json".to_string(), 3)]);
        // Null outputs are skipped; null report and an index past the last give 0.
        assert_eq!(luma_report_after_start(r, 1, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()), 1);
        assert_eq!(luma_report_after_start(r, 2, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()), 0);
        assert_eq!(luma_report_after_start(null(), 0, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()), 0);
        luma_report_free(r);
        // The same rows from luma_validate's report at a start that takes those kept bytes.
        let r = luma_validate(&call.input(Some(&kept)));
        assert_eq!(luma_report_state(r), 1);
        assert_eq!(rows(r).len(), 2);
        luma_report_free(r);

        // Bytes that do not read: changed, cut short, empty, null.
        let mut changed = full.kept.clone();
        let at = changed.len() / 2;
        changed[at] ^= 1;
        for (bytes, why) in [(&changed[..], "does not read"), (&full.kept[..full.kept.len() - 3], "does not read"), (&[][..], "does not read")] {
            let r = luma_report_read_kept(bytes.as_ptr(), bytes.len() as u64);
            assert_eq!(luma_report_state(r), -1);
            assert!(s(luma_report_invalid(r)).unwrap().contains(why));
            assert!(luma_report_id(r).is_null());
            luma_report_free(r);
        }
        let r = luma_report_read_kept(null(), 0);
        assert_eq!((luma_report_state(r), s(luma_report_invalid(r)).as_deref()), (-1, Some("kept is null")));
        luma_report_free(r);
    }
}
