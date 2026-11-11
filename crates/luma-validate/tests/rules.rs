//! One broken mod per rule: every finding the validator gives is produced here by a mod made to break that rule, its
//! message is filled in all 14 languages, a correct mod passes with nothing to say, the same input gives the same
//! report, and no damaged byte makes the validator panic. The rules on libraries are in `libraries.rs`.

use luma_validate::semver::Version;
use luma_validate::{
    message, validate, ColumnType, Finding, Game, Input, Installed, Report, Source, Store, Table, Touches, LANGUAGES,
};
use std::collections::{BTreeSet, HashSet};

// ---------------------------------------------------------------- a zip writer that can also lie

#[derive(Clone)]
struct E {
    name: String,
    data: Vec<u8>,
    method: u16,
    flags: u16,
    unix_mode: Option<u32>,
    crc: Option<u32>,
    size: Option<u64>,
    local_name: Option<String>,
}

fn file(name: &str, data: &[u8]) -> E {
    E { name: name.into(), data: data.to_vec(), method: 0, flags: 0, unix_mode: None, crc: None, size: None, local_name: None }
}

fn deflated(name: &str, data: &[u8]) -> E {
    E { method: 8, ..file(name, data) }
}

fn put16(v: &mut Vec<u8>, x: u16) {
    v.extend_from_slice(&x.to_le_bytes());
}
fn put32(v: &mut Vec<u8>, x: u32) {
    v.extend_from_slice(&x.to_le_bytes());
}

/// A zip of the entries in order. A `size` over 4 GiB is written as Zip64 in the central directory.
fn zip(entries: &[E]) -> Vec<u8> {
    let mut out = Vec::new();
    let mut central = Vec::new();
    for e in entries {
        let packed = if e.method == 8 { miniz_oxide::deflate::compress_to_vec(&e.data, 6) } else { e.data.clone() };
        let crc = e.crc.unwrap_or_else(|| crc32fast::hash(&e.data));
        let size = e.size.unwrap_or(e.data.len() as u64);
        let offset = out.len() as u32;
        let local = e.local_name.as_deref().unwrap_or(&e.name);
        put32(&mut out, 0x0403_4b50);
        put16(&mut out, 20);
        put16(&mut out, e.flags);
        put16(&mut out, e.method);
        put32(&mut out, 0);
        put32(&mut out, crc);
        put32(&mut out, packed.len() as u32);
        put32(&mut out, size.min(0xffff_ffff) as u32);
        put16(&mut out, local.len() as u16);
        put16(&mut out, 0);
        out.extend_from_slice(local.as_bytes());
        out.extend_from_slice(&packed);

        let zip64 = size >= 0xffff_ffff;
        put32(&mut central, 0x0201_4b50);
        put16(&mut central, if e.unix_mode.is_some() { 0x031e } else { 20 });
        put16(&mut central, if zip64 { 45 } else { 20 });
        put16(&mut central, e.flags);
        put16(&mut central, e.method);
        put32(&mut central, 0);
        put32(&mut central, crc);
        put32(&mut central, packed.len() as u32);
        put32(&mut central, if zip64 { 0xffff_ffff } else { size as u32 });
        put16(&mut central, e.name.len() as u16);
        put16(&mut central, if zip64 { 12 } else { 0 });
        put16(&mut central, 0);
        put16(&mut central, 0);
        put16(&mut central, 0);
        put32(&mut central, e.unix_mode.map_or(0, |m| m << 16));
        put32(&mut central, offset);
        central.extend_from_slice(e.name.as_bytes());
        if zip64 {
            put16(&mut central, 1);
            put16(&mut central, 8);
            central.extend_from_slice(&size.to_le_bytes());
        }
    }
    let cd_offset = out.len() as u32;
    out.extend_from_slice(&central);
    put32(&mut out, 0x0605_4b50);
    put16(&mut out, 0);
    put16(&mut out, 0);
    put16(&mut out, entries.len() as u16);
    put16(&mut out, entries.len() as u16);
    put32(&mut out, central.len() as u32);
    put32(&mut out, cd_offset);
    put16(&mut out, 0);
    out
}

// ---------------------------------------------------------------- luma.json

const BASE: [(&str, &str); 7] = [
    ("format", r#""1.0""#),
    ("id", r#""test.mod""#),
    ("version", r#""1.0.0""#),
    ("name", r#""Test""#),
    ("summary", r#""A mod made to break one rule.""#),
    ("authors", r#"["G"]"#),
    ("game", r#"{"id": "RMG"}"#),
];

/// The base luma.json with keys replaced or added; a value of "-" removes the key.
fn manifest(over: &[(&str, &str)]) -> Vec<u8> {
    let mut keys: Vec<(String, String)> = BASE.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect();
    for (k, v) in over {
        match keys.iter_mut().find(|(kk, _)| kk == k) {
            Some(e) => e.1 = v.to_string(),
            None => keys.push((k.to_string(), v.to_string())),
        }
    }
    keys.retain(|(_, v)| v != "-");
    let body: Vec<String> = keys.iter().map(|(k, v)| format!("  \"{k}\": {v}")).collect();
    format!("{{\n{}\n}}\n", body.join(",\n")).into_bytes()
}

fn luma(over: &[(&str, &str)], more: &[E]) -> Vec<u8> {
    let mut entries = vec![file("luma.json", &manifest(over))];
    entries.extend_from_slice(more);
    zip(&entries)
}

// ---------------------------------------------------------------- the game a mod is checked against

const TABLE: &str = "Stage/Table.bcsv";

struct FakeStore {
    case_insensitive: bool,
}

impl Store for FakeStore {
    fn path(&self, lower: &str) -> Option<String> {
        [TABLE, "ObjectData/Kuribo.arc"].iter().find(|p| p.to_lowercase() == lower).map(|p| p.to_string())
    }
    fn table(&self, path: &str) -> Option<Table> {
        (path == TABLE).then(|| Table {
            key: vec!["name".into()],
            columns: vec![("name".into(), ColumnType::Text), ("count".into(), ColumnType::Int { min: 0, max: 10 })],
            rows: Some(HashSet::from(["Alpha".to_string()])),
            case_insensitive: self.case_insensitive,
        })
    }
    fn may_be_new(&self, path: &str) -> bool {
        path.ends_with(".wav")
    }
}

struct Setup<'a> {
    game: Option<&'a Game<'a>>,
    installed: &'a [Installed],
    platforms: &'a [&'a str],
    position: Option<usize>,
}

const NO_GAME: Setup = Setup { game: None, installed: &[], platforms: &["windows-x64"], position: None };

fn check(bytes: &[u8], s: &Setup) -> Report {
    validate(&Input { source: Source::Bytes(bytes, "test.luma"), game: s.game, installed: s.installed, platforms: s.platforms, options: None, position: s.position })
}

fn other(id: &str) -> Installed {
    Installed {
        id: id.into(),
        version: Version::parse("1.0.0").unwrap(),
        on: true,
        file: format!("{id}.luma"),
        conflicts: Vec::new(),
        touches: Touches::default(),
    }
}

/// A message left with a `{name}` unfilled in any language.
fn unfilled(f: &Finding) -> Option<String> {
    for lang in LANGUAGES {
        let text = message(f, lang);
        let mut rest = text.as_str();
        while let Some(open) = rest.find('{') {
            let after = &rest[open + 1..];
            if let Some(close) = after.find('}') {
                let name = &after[..close];
                if !name.is_empty() && name.chars().all(|c| c.is_ascii_lowercase() || c == '_') {
                    return Some(format!("L{} in {lang}: {text}", f.code));
                }
            }
            rest = after;
        }
    }
    None
}

// ---------------------------------------------------------------- the rules

#[test]
fn a_correct_mod_passes_with_nothing_to_say() {
    let bytes = luma(&[], &[]);
    let report = check(&bytes, &NO_GAME);
    assert!(report.findings.is_empty(), "{:?}", report.findings);

    let store = FakeStore { case_insensitive: false };
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], names: None, store: Some(&store) };
    let patch = br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "Alpha"}, "values": {"count": 3}}]}"#;
    let bytes = luma(
        &[("layers", r#"[{"name": "base"}]"#), ("nebula", r#"">=2.0.0""#)],
        &[file("layers/base/patches/stage/table.bcsv.json", patch), deflated("layers/base/files/ObjectData/Kuribo.arc", &[7; 4096])],
    );
    let report = check(&bytes, &Setup { game: Some(&game), ..NO_GAME });
    assert!(report.findings.is_empty(), "{:?}", report.findings);
}

#[test]
fn every_rule_has_a_broken_mod() {
    let store = FakeStore { case_insensitive: false };
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], names: None, store: Some(&store) };
    let layer = [("layers", r#"[{"name": "base"}]"#)];
    let patch_file = "layers/base/patches/stage/table.bcsv.json";

    let mut eocd_multi = luma(&[], &[]);
    let n = eocd_multi.len();
    eocd_multi[n - 18] = 1; // the EOCD's "this disk" number

    let mut big = file("media/big.png", b"x");
    big.size = Some(5 << 30);
    let mut bad_crc = file("media/a.png", b"abc");
    bad_crc.crc = Some(1);
    let mut encrypted = file("media/a.png", b"abc");
    encrypted.flags = 1;
    let mut bzip = file("media/a.png", b"abc");
    bzip.method = 12;
    let mut link = file("media/a.png", b"/etc/passwd");
    link.unix_mode = Some(0o120777);
    let mut local = file("media/a.png", b"abc");
    local.local_name = Some("media/b.png".into());

    let mut conflicting = other("other.mod");
    conflicting.conflicts = vec!["test.mod".into()];
    let mut replacing = other("other.mod");
    replacing.touches.replaces = vec!["objectdata/kuribo.arc".into()];
    let mut setting = other("other.mod");
    setting.touches.cells = vec!["stage/table.bcsv\u{1f}Alpha\u{1f}count".into()];
    let same_id = other("test.mod");

    let set_alpha = br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "Alpha"}, "values": {"count": 3}}]}"#;

    let cases: Vec<(u16, Vec<u8>, Setup)> = vec![
        (100, b"this is not a zip file at all, only words".to_vec(), NO_GAME),
        (101, luma(&[], &[encrypted]), NO_GAME),
        (101, luma(&[], &[bzip]), NO_GAME),
        (101, eocd_multi, NO_GAME),
        (102, luma(&[], &[file("media/CON.png", b"x")]), NO_GAME),
        (102, luma(&[], &[file("media/a\\b.png", b"x")]), NO_GAME),
        (102, luma(&[], &[local]), NO_GAME),
        (103, luma(&[], &[file("media/A.png", b"x"), file("media/a.png", b"x")]), NO_GAME),
        (104, luma(&[], &[link]), NO_GAME),
        (105, luma(&[], &[big]), NO_GAME),
        (106, luma(&[], &[bad_crc]), NO_GAME),
        (107, luma(&[], &[file("readme.txt", b"x")]), NO_GAME),
        (200, zip(&[file("media/a.png", b"x")]), NO_GAME),
        (201, zip(&[file("luma.json", b"{\"id\": \"a\", // no\n}")]), NO_GAME),
        (201, zip(&[file("luma.json", b"\xef\xbb\xbf{}")]), NO_GAME),
        (202, luma(&[("colour", "1")], &[]), NO_GAME),
        (203, luma(&[("name", "-")], &[]), NO_GAME),
        (204, luma(&[("name", "5")], &[]), NO_GAME),
        (204, luma(&[("authors", "[]")], &[]), NO_GAME),
        (205, luma(&[("format", r#""9.0""#)], &[]), NO_GAME),
        (206, luma(&[("id", r#""Bad_Id""#)], &[]), NO_GAME),
        (207, luma(&[("game", r#"{"id": "SB4E01"}"#)], &[]), NO_GAME),
        (208, luma(&[("game", r#"{"id": "RMG", "regions": ["RMGJ01"]}"#)], &[]), Setup { game: Some(&game), ..NO_GAME }),
        (209, luma(&[("icon", r#""media/none.png""#)], &[]), NO_GAME),
        (210, luma(&[("options", r#"[{"key": "a", "kind": "number", "name": "A", "default": 20, "min": 0, "max": 10, "step": 1}]"#)], &[]), NO_GAME),
        (211, luma(&[("permissions", r#"[{"name": "net"}]"#)], &[]), NO_GAME),
        (212, luma(&[("load", r#"{"after": ["test.mod"]}"#)], &[]), NO_GAME),
        (213, luma(&[("takes_over", r#"["test.mod"]"#)], &[]), NO_GAME),
        (214, luma(&[("text", r#"{"Klingon": {"name": "Test"}}"#)], &[]), NO_GAME),
        (215, luma(&[], &[file("media/extra.png", b"x")]), NO_GAME),
        (216, luma(&[("saves", r#"[{"region": "RMGE01", "file": "saves/RMGE01/a.bin", "why": "A finished game."}]"#)], &[]), NO_GAME),
        (216, luma(&[("saves", r#"[{"region": "RMGE01", "file": "saves/RMGE01/a.bin", "why": "A finished game."}]"#)], &[file("saves/RMGE01/a.bin", &[0; 100])]), NO_GAME),
        (217, luma(&[], &[file("saves/RMGE01/b.bin", &[0; 100])]), NO_GAME),
        (300, luma(&layer, &[]), NO_GAME),
        (300, luma(&[], &[file("layers/extra/files/a.wav", b"x")]), NO_GAME),
        (301, luma(&[("layers", r#"[{"name": "base", "when": {}}]"#)], &[file("layers/base/files/a.wav", b"x")]), NO_GAME),
        (301, luma(&[("layers", r#"[{"name": "base", "when": {"option": {"nope": true}}}]"#)], &[file("layers/base/files/a.wav", b"x")]), NO_GAME),
        (302, luma(&layer, &[file("layers/base/files/ObjectData/Nothing.arc", b"x")]), Setup { game: Some(&game), ..NO_GAME }),
        (303, luma(&layer, &[file("layers/base/files/Stage/Table.bcsv", b"x")]), Setup { game: Some(&game), ..NO_GAME }),
        (305, luma(&layer, &[file("layers/base/files/ObjectData/Kuribo.arc", b"x")]), Setup { game: Some(&game), installed: std::slice::from_ref(&replacing), ..NO_GAME }),
        (306, luma(&layer, &[file("layers/base/other/a.wav", b"x")]), NO_GAME),
        (307, luma(&[("layers", r#"[{"name": "base", "when": {"code": true}}]"#)], &[file("layers/base/files/a.wav", b"x")]), NO_GAME),
        (400, luma(&layer, &[file(patch_file, b"{}")]), with_game_ref(&game)),
        (400, luma(&layer, &[file(patch_file, b"{\"patch\": 1, \"key\": [\"name\"], \"colour\": 1}")]), with_game_ref(&game)),
        (401, luma(&layer, &[file(patch_file, br#"{"patch": 1, "key": ["count"]}"#)]), with_game_ref(&game)),
        (402, luma(&layer, &[file(patch_file, br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "Beta"}, "values": {"count": 1}}]}"#)]), with_game_ref(&game)),
        (402, luma(&layer, &[file(patch_file, br#"{"patch": 1, "key": ["name"], "add": [{"name": "Alpha", "count": 1}]}"#)]), with_game_ref(&game)),
        (403, luma(&layer, &[file(patch_file, br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "Alpha"}, "values": {"count": "many"}}]}"#)]), with_game_ref(&game)),
        (403, luma(&layer, &[file(patch_file, br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "Alpha"}, "values": {"colour": 1}}]}"#)]), with_game_ref(&game)),
        (403, luma(&layer, &[file(patch_file, br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "Alpha"}, "values": {"count": 11}}]}"#)]), with_game_ref(&game)),
        (404, luma(&layer, &[file(patch_file, set_alpha)]), Setup { game: Some(&game), installed: std::slice::from_ref(&setting), ..NO_GAME }),
        (405, luma(&layer, &[file("layers/base/patches/@other.mod/stage/table.bcsv.json", set_alpha)]), with_game_ref(&game)),
        (406, luma(&layer, &[file(patch_file, br#"{"patch": 1, "key": ["name"], "select": [{"match": {"name": {"like": "A"}}, "values": {"count": 1}}]}"#)]), with_game_ref(&game)),
        (406, luma(&layer, &[file(patch_file, br#"{"patch": 1, "key": ["name"], "select": [{"match": {"colour": {"equals": "A"}}, "values": {"count": 1}}]}"#)]), with_game_ref(&game)),
        (500, luma(&[("code", r#"{"platforms": ["windows-x64"]}"#)], &[]), NO_GAME),
        (500, luma(&[], &[file("code/windows-x64/mod.dll", b"MZ")]), NO_GAME),
        (501, luma(&[("code", r#"{"platforms": ["windows-x64"]}"#)], &[file("code/beos-x64/mod.dll", b"MZ")]), NO_GAME),
        (501, luma(&[("code", r#"{"platforms": ["windows-x64"]}"#)], &[file("code/windows-x64/other.dll", b"MZ")]), NO_GAME),
        (502, luma(&[("code", r#"{"platforms": ["windows-x64"]}"#)], &[file("code/windows-x64/mod.dll", b"not a library")]), NO_GAME),
        (507, luma(&[("code", r#"{"platforms": ["linux-x64"]}"#)], &[file("code/linux-x64/mod.so", b"\x7fELF")]), NO_GAME),
        (509, luma(&[("code", r#"{"platforms": ["windows-x64"]}"#)], &[file("code/windows-x64/mod.dll", b"MZ"), file("code/windows-x64/extra.dll", b"MZ")]), NO_GAME),
        (509, luma(&[("code", r#"{"platforms": ["windows-x64"], "libraries": {"windows-x64": ["extra.dll"]}}"#)], &[file("code/windows-x64/mod.dll", b"MZ"), file("code/windows-x64/extra.dll", b"MZ")]), NO_GAME),
        (700, luma(&[("kit", r#"{"luma.shop": 1}"#)], &[]), NO_GAME),
        (700, luma(&[], &[file("kit/luma.shop/items.json", b"{}")]), NO_GAME),
        (800, luma(&[("needs", r#"[{"id": "other.mod", "version": ">=1.0.0"}]"#)], &[]), NO_GAME),
        (801, luma(&[], &[]), Setup { installed: std::slice::from_ref(&conflicting), ..NO_GAME }),
        (802, luma(&[], &[]), Setup { installed: std::slice::from_ref(&same_id), ..NO_GAME }),
        (803, luma(&[("nebula", r#"">=9.0.0""#)], &[]), with_game_ref(&game)),
    ];

    let mut failures = Vec::new();
    let mut seen = BTreeSet::new();
    for (i, (code, bytes, setup)) in cases.iter().enumerate() {
        let report = check(bytes, setup);
        let codes: Vec<u16> = report.findings.iter().map(|f| f.code).collect();
        if !codes.contains(code) {
            failures.push(format!("case {i}: L{code} not given; got {codes:?}"));
        }
        for f in &report.findings {
            seen.insert(f.code);
            if let Some(u) = unfilled(f) {
                failures.push(format!("case {i}: unfilled: {u}"));
            }
        }
        // The same input, the same report.
        if check(bytes, setup) != report {
            failures.push(format!("case {i}: a second run differs"));
        }
    }
    // The rules no broken mod here can reach yet, each with why (`libraries.rs` covers the code rules on libraries).
    let elsewhere: [(u16, &str); 11] = [
        (304, "what a source may stand in for is each content kind's contract (3.2 to 3.10)"),
        (407, "matched at load (3.11)"),
        (503, "libraries.rs"),
        (504, "libraries.rs"),
        (505, "libraries.rs"),
        (506, "needs LUMA_IMPORT_SERVICE (1.1)"),
        (508, "libraries.rs"),
        (510, "libraries.rs"),
        (511, "the iOS signing rules (8.3)"),
        (512, "libraries.rs"),
        (701, "the LumaKit schemas (5.x); 702 likewise, and 804 is the cooker's (3.x)"),
    ];
    for code in luma_validate::CODES {
        let skip = elsewhere.iter().any(|(c, _)| c == code) || matches!(code, 702 | 804);
        if !skip && !seen.contains(code) {
            failures.push(format!("L{code}: no case gives it"));
        }
    }
    assert!(failures.is_empty(), "\n{}", failures.join("\n"));
}

fn with_game_ref<'a>(game: &'a Game<'a>) -> Setup<'a> {
    Setup { game: Some(game), ..NO_GAME }
}

#[test]
fn a_table_matched_without_case_finds_its_rows() {
    let store = FakeStore { case_insensitive: true };
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], names: None, store: Some(&store) };
    let patch = br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "ALPHA"}, "values": {"count": 3}}]}"#;
    let bytes = luma(&[("layers", r#"[{"name": "base"}]"#)], &[file("layers/base/patches/stage/table.bcsv.json", patch)]);
    let report = check(&bytes, &with_game_ref(&game));
    assert!(report.findings.is_empty(), "{:?}", report.findings);
    assert_eq!(report.touches.cells, vec!["stage/table.bcsv\u{1f}alpha\u{1f}count".to_string()]);

    let exact = FakeStore { case_insensitive: false };
    let game = Game { store: Some(&exact), ..game };
    let codes: Vec<u16> = check(&bytes, &with_game_ref(&game)).findings.iter().map(|f| f.code).collect();
    assert_eq!(codes, vec![402]);
}

#[test]
fn a_finding_names_the_line_of_luma_json() {
    let report = check(&luma(&[("name", "5")], &[]), &NO_GAME);
    let f = report.findings.iter().find(|f| f.code == 204).expect("L204");
    assert_eq!((f.file.as_str(), f.line, f.mod_id.as_deref(), f.pointer.as_deref()), ("luma.json", Some(5), Some("test.mod"), Some("/name")));
    assert_eq!(message(f, "UsEnglish"), "test.mod: luma.json line 5: \"name\" is 5; it must be text.");
}

#[test]
fn no_damaged_byte_makes_it_panic() {
    let patch = br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "Alpha"}, "values": {"count": 3}}]}"#;
    let good = luma(
        &[("layers", r#"[{"name": "base"}]"#), ("options", r#"[{"key": "a", "kind": "toggle", "name": "A", "default": true}]"#)],
        &[file("layers/base/patches/stage/table.bcsv.json", patch), deflated("layers/base/files/ObjectData/Kuribo.arc", &[7; 512])],
    );
    let store = FakeStore { case_insensitive: false };
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], names: None, store: Some(&store) };
    // Every byte set to three values, and every length cut: none may panic, and every message stays filled.
    for at in 0..good.len() {
        for value in [0x00u8, 0xff, good[at] ^ 0x5a] {
            let mut bad = good.clone();
            bad[at] = value;
            for f in check(&bad, &with_game_ref(&game)).findings {
                assert!(unfilled(&f).is_none(), "byte {at} = {value:#x}: {:?}", unfilled(&f));
            }
        }
    }
    for len in 0..good.len() {
        let _ = check(&good[..len], &with_game_ref(&game));
    }
}
