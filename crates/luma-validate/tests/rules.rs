//! One broken mod per rule: every finding the validator gives is produced here by a mod made to break that rule, its
//! message is filled in all 14 languages, a correct mod passes with nothing to say, the same input gives the same
//! report, and no damaged byte makes the validator panic. The rules on libraries are in `libraries.rs`.

use luma_validate::semver::Version;
use luma_validate::{
    message, recheck, validate, ColumnType, Saved, Game, Input, Installed, Report, Source, Store, Table, Touches,
};
use std::collections::{BTreeSet, HashSet};

mod common;
use common::*;

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
            ..Default::default()
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

fn recheck_with(bytes: &[u8], s: &Setup, saved: Option<&Saved>, game: &str) -> Report {
    let input = Input { source: Source::Bytes(bytes, "test.luma"), game: s.game, installed: s.installed, platforms: s.platforms, options: None, position: s.position };
    recheck(&input, saved, game)
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
        // The kept report reads back as written, and a start's check on it gives the full check's findings.
        let saved = Saved::from_report(&report, "game");
        match Saved::parse(&saved.to_bytes()) {
            Some(back) if back == saved => {}
            Some(back) => failures.push(format!("case {i}: kept report reads back differently: {:?}", back.findings)),
            None => failures.push(format!("case {i}: kept report does not read back")),
        }
        let start = recheck_with(bytes, setup, Some(&saved), "game");
        if start.findings != report.findings || start.not_checked != report.not_checked || start.touches != report.touches {
            failures.push(format!("case {i}: the start's check differs: {:?} {:?} {:?}
  full: {:?} {:?} {:?}", start.findings, start.not_checked, start.touches, report.findings, report.not_checked, report.touches));
        }
        if start.full != report.fingerprint.is_none() {
            failures.push(format!("case {i}: the start's check ran in full: {}", start.full));
        }
    }
    // The rules no broken mod here can reach yet, each with why (`libraries.rs` covers the code rules on libraries).
    let elsewhere: [(u16, &str); 12] = [
        (304, "what a source may stand in for is each content kind's contract (3.2 to 3.10)"),
        (407, "table_keys.rs"),
        (503, "libraries.rs"),
        (504, "libraries.rs"),
        (505, "libraries.rs"),
        (506, "needs LUMA_IMPORT_SERVICE (1.1)"),
        (508, "libraries.rs"),
        (510, "libraries.rs"),
        (511, "the iOS signing rules (8.3)"),
        (512, "libraries.rs"),
        (805, "libraries.rs"),
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

#[test]
fn a_start_takes_the_whole_file_findings_from_the_kept_report_and_runs_the_rest() {
    let store = FakeStore { case_insensitive: false };
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], names: None, store: Some(&store) };
    let bad_row = br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "Omega"}, "values": {"count": 3}}]}"#;
    let no_file = br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "Alpha"}, "values": {"count": 3}}]}"#;
    let mut broken = file("layers/base/files/ObjectData/Kuribo.arc", &[7; 64]);
    broken.crc = Some(0x1234_5678);
    let entries = [
        file("layers/base/patches/stage/table.bcsv.json", bad_row),
        file("layers/base/patches/stage/none.bcsv.json", no_file),
        broken,
    ];
    let over = [("layers", r#"[{"name": "base"}]"#), ("needs", r#"[{"id": "other.mod", "version": ">=1.0.0"}]"#)];
    let bytes = luma(&over, &entries);
    let alone = Setup { game: Some(&game), ..NO_GAME };
    let full = recheck_with(&bytes, &alone, None, "game");
    let codes = |r: &Report| -> Vec<u16> { r.findings.iter().map(|f| f.code).collect::<BTreeSet<_>>().into_iter().collect() };
    assert!(full.full);
    assert_eq!(codes(&full), vec![106, 302, 402, 800]);
    let saved = Saved::parse(&Saved::from_report(&full, "game").to_bytes()).unwrap();

    // The needed mod is installed now: L800 goes, the whole-file findings stay, and nothing was read in full.
    let with_other = [other("other.mod")];
    let start = recheck_with(&bytes, &Setup { installed: &with_other, ..alone }, Some(&saved), "game");
    assert!(!start.full);
    assert_eq!(codes(&start), vec![106, 302, 402]);
    assert_eq!(start.touches, full.touches);

    // Another game, another .luma, or a kept report that cannot be read: every rule runs.
    assert!(recheck_with(&bytes, &alone, Some(&saved), "another game").full);
    let changed = luma(&over, &entries[..2]);
    let r = recheck_with(&changed, &alone, Some(&saved), "game");
    assert!(r.full);
    assert_eq!(codes(&r), vec![302, 402, 800]);
    assert!(Saved::parse(b"luma-saved\t1\nvalidator\t1.0\n").is_none());
    assert!(Saved::parse(b"not a kept report").is_none());
    // Cut at any byte, or any byte changed: it does not read.
    let kept = Saved::from_report(&full, "game").to_bytes();
    for n in 0..kept.len() {
        assert!(Saved::parse(&kept[..n]).is_none(), "cut at {n}");
        let mut changed = kept.clone();
        changed[n] ^= 0x01;
        assert!(Saved::parse(&changed).is_none(), "byte {n} changed");
    }
}

/// The mod higher in the list wins a file and a cell (F, INBOX-G 13): installed mods come top first, and `position`
/// is how many are above the mod checked.
#[test]
fn the_mod_higher_in_the_list_wins() {
    let store = FakeStore { case_insensitive: false };
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], names: None, store: Some(&store) };
    let layer = [("layers", r#"[{"name": "base"}]"#)];
    let mut replacing = other("other.mod");
    replacing.touches.replaces = vec!["objectdata/kuribo.arc".into()];
    replacing.touches.cells = vec!["stage/table.bcsv\u{1f}Alpha\u{1f}count".into()];
    let bytes = luma(&layer, &[
        file("layers/base/files/ObjectData/Kuribo.arc", b"x"),
        file("layers/base/patches/stage/table.bcsv.json", br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "Alpha"}, "values": {"count": 1}}]}"#),
    ]);
    let winners = |position: Option<usize>| -> Vec<(u16, String)> {
        let report = check(&bytes, &Setup { game: Some(&game), installed: std::slice::from_ref(&replacing), position, ..NO_GAME });
        report.findings.iter().filter(|f| f.code == 305 || f.code == 404).map(|f| (f.code, message(f, "UsEnglish"))).collect()
    };
    let below = winners(None);
    let above = winners(Some(0));
    assert_eq!(below.len(), 2, "{below:?}");
    assert_eq!(above.len(), 2, "{above:?}");
    assert!(below.iter().all(|(_, m)| m.ends_with("other.mod's is used.") || m.ends_with("other.mod's change is used.")), "{below:?}");
    assert!(above.iter().all(|(_, m)| m.ends_with("test.mod's is used.") || m.ends_with("test.mod's change is used.")), "{above:?}");
}
