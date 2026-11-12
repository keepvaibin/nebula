//! One broken mod per rule: every finding the validator gives is produced here by a mod made to break that rule, its
//! message is filled in all 14 languages, a correct mod passes with nothing to say, the same input gives the same
//! report, and no damaged byte makes the validator panic. The rules on libraries are in `libraries.rs`.

use luma_validate::semver::Version;
use luma_validate::{
    message, recheck, validate, Arg, ColumnType, Saved, Game, Input, Installed, Report, Source, Store, Table, Touches,
};
use std::collections::{BTreeSet, HashSet};

mod common;
use common::*;

// ---------------------------------------------------------------- the game a mod is checked against

const TABLE: &str = "Stage/Table.bcsv";

struct FakeStore {
    case_insensitive: bool,
}

/// Two layers of one zone's objects: a link group (table-keys 13).
const COMMON: &str = "StageData/Zone/jmp/Placement/Common/ObjInfo";
const LAYER_A: &str = "StageData/Zone/jmp/Placement/LayerA/ObjInfo";

impl Store for FakeStore {
    fn path(&self, lower: &str) -> Option<String> {
        [TABLE, "ObjectData/Kuribo.arc", COMMON, LAYER_A].iter().find(|p| p.to_lowercase() == lower).map(|p| p.to_string())
    }
    fn table(&self, path: &str) -> Option<Table> {
        if path == COMMON || path == LAYER_A {
            return Some(Table {
                key: vec!["l_id".into()],
                columns: vec![("l_id".into(), ColumnType::Int { min: i64::from(i32::MIN), max: i64::from(i32::MAX) })],
                rows: Some(HashSet::from([if path == COMMON { "1" } else { "2" }.to_string()])),
                ..Default::default()
            });
        }
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
        needs: Vec::new(),
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
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: None, store: Some(&store) };
    let patch = br#"{"patch": 1, "key": ["name"], "set": [{"where": {"name": "Alpha"}, "values": {"count": 3}}]}"#;
    let bytes = luma(
        &[("layers", r#"[{"name": "base"}]"#), ("nebula", r#"">=2.0.0""#)],
        &[
            file("layers/base/patches/stage/table.bcsv.json", patch),
            deflated("layers/base/files/ObjectData/Kuribo.arc", &[7; 4096]),
            // A texture pack's texture, by the game's name or Dolphin's (decision 3 of the one form).
            file("layers/base/textures/Kuribo/KuriboBody.png", b"p"),
            file("layers/base/textures/tex1_32x32_0000000000001111_14.dds", b"d"),
        ],
    );
    let report = check(&bytes, &Setup { game: Some(&game), ..NO_GAME });
    assert!(report.findings.is_empty(), "{:?}", report.findings);

    // A layer of the previous engine's rows only has no folder (a converted Gecko list): L221 notes, no L300.
    let bytes = luma(
        &[("layers", r#"[{"name": "base"}]"#), ("pre_standard", r#"{"layers": {"base": {"tunables": {"player.jumps": 3}}}}"#)],
        &[],
    );
    let report = check(&bytes, &NO_GAME);
    assert!(report.findings.iter().all(|f| f.code == 221), "{:?}", report.findings);
}

/// Table-keys 13: a key added to one layer of a zone must be new to every layer of it, in the game and in the mod;
/// two mods adding one key to a group are L404.
#[test]
fn a_link_key_is_new_to_the_whole_zone() {
    let store = FakeStore { case_insensitive: false };
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: None, store: Some(&store) };
    let layer = [("layers", r#"[{"name": "base"}]"#)];
    let common = "layers/base/patches/StageData/Zone/jmp/Placement/Common/ObjInfo.json";
    let layer_a = "layers/base/patches/StageData/Zone/jmp/Placement/LayerA/ObjInfo.json";
    let add = |id: u32| format!(r#"{{"patch": 1, "key": ["l_id"], "add": [{{"l_id": {id}}}]}}"#).into_bytes();
    let in_group = |r: &Report| r.findings.iter().filter(|f| f.code == 402 && f.args.iter().any(|(n, a)| *n == "why" && *a == Arg::Phrase("Patch.KeyInGroup"))).count();

    // LayerA's game row 2: Common may not add it; 9 is new to the zone.
    let r = check(&luma(&layer, &[file(common, &add(2))]), &Setup { game: Some(&game), ..NO_GAME });
    assert_eq!(in_group(&r), 1, "{:?}", r.findings);
    let f = r.findings.iter().find(|f| f.code == 402).unwrap();
    assert!(f.args.iter().any(|(n, a)| *n == "where" && *a == Arg::Text(LAYER_A.into())), "{:?}", f.args);
    assert!(message(f, "UsEnglish").contains(LAYER_A), "{}", message(f, "UsEnglish"));
    let r = check(&luma(&layer, &[file(common, &add(9))]), &Setup { game: Some(&game), ..NO_GAME });
    assert!(r.findings.is_empty(), "{:?}", r.findings);
    assert_eq!(r.touches.links, vec!["stagedata/zone/jmp/placement/*/objinfo\u{1f}9".to_string()]);

    // The same mod adding 9 to two layers.
    let r = check(&luma(&layer, &[file(common, &add(9)), file(layer_a, &add(9))]), &Setup { game: Some(&game), ..NO_GAME });
    assert_eq!(in_group(&r), 1, "{:?}", r.findings);

    // Another installed mod adding 9 to the zone in another layer, and one adding Beta to a table of no group.
    let mut adding = other("other.mod");
    adding.touches.links = vec!["stagedata/zone/jmp/placement/*/objinfo\u{1f}9".into()];
    adding.touches.adds = vec!["stage/table.bcsv\u{1f}Beta".into()];
    let beta = br#"{"patch": 1, "key": ["name"], "add": [{"name": "Beta"}]}"#;
    let r = check(
        &luma(&layer, &[file(layer_a, &add(9)), file("layers/base/patches/stage/table.bcsv.json", beta)]),
        &Setup { game: Some(&game), installed: std::slice::from_ref(&adding), ..NO_GAME },
    );
    let clashes: Vec<String> = r.findings.iter().filter(|f| f.code == 404).map(|f| message(f, "UsEnglish")).collect();
    assert_eq!(clashes.len(), 2, "{clashes:?}");
    assert!(clashes.iter().any(|m| m.contains("stagedata/zone/jmp/placement/*/objinfo")), "{clashes:?}");
}

/// A zone whose start has MarioNo 0 in LayerB and LayerC, which no scenario turns on together: scenario 1 turns on
/// LayerA and LayerB (mask 3), scenario 2 LayerA and LayerC (mask 5); LayerD is never on (F's dandelionhillzone case).
struct ScenarioStore {
    listed: bool,
}

const START: &str = "StageData/DandelionHillZone/jmp/Start/{}/StartInfo";
const SCENARIOS: &str = "StageData/DandelionHillGalaxyScenario/ScenarioData.bcsv";

impl Store for ScenarioStore {
    fn path(&self, lower: &str) -> Option<String> {
        ["Common", "LayerA", "LayerB", "LayerC", "LayerD"]
            .iter()
            .map(|l| START.replace("{}", l))
            .chain([SCENARIOS.to_string()])
            .find(|p| p.to_lowercase() == lower)
    }
    fn table(&self, path: &str) -> Option<Table> {
        let int = ColumnType::Int { min: 0, max: 65535 };
        if path == SCENARIOS {
            return Some(Table {
                key: vec!["ScenarioNo".into()],
                columns: vec![("ScenarioNo".into(), int), ("DandelionHillZone".into(), int)],
                rows: Some(HashSet::from(["1".to_string(), "2".to_string()])),
                cells: Some(vec![vec!["1".into(), "3".into()], vec!["2".into(), "5".into()]]),
                ..Default::default()
            });
        }
        let layer = ["LayerB", "LayerC"].iter().any(|l| path == START.replace("{}", l));
        path.starts_with("StageData/DandelionHillZone/").then(|| Table {
            key: vec!["MarioNo".into()],
            columns: vec![("MarioNo".into(), int)],
            rows: Some(if layer { HashSet::from(["0".to_string()]) } else { HashSet::new() }),
            ..Default::default()
        })
    }
    fn may_be_new(&self, _path: &str) -> bool {
        false
    }
    fn folder(&self, lower: &str) -> Option<Vec<(String, bool)>> {
        // A file named like a scenario folder is no galaxy (the game lists folders only).
        (self.listed && lower == "stagedata").then(|| {
            vec![("DandelionHillGalaxyScenario".to_string(), true), ("DandelionHillZone".to_string(), true), ("FakeScenario".to_string(), false)]
        })
    }
    /// A stand-in for the host's table reader: "name,name;value,value;..." (the real readers are nebula-import's,
    /// tested there).
    fn read_table(&self, _path: &str, bytes: &[u8]) -> Option<Table> {
        let text = std::str::from_utf8(bytes).ok()?.strip_prefix("table:")?;
        let mut rows = text.split(';');
        let int = ColumnType::Int { min: 0, max: 65535 };
        let columns: Vec<(String, ColumnType)> = rows.next()?.split(',').map(|c| (c.to_string(), int)).collect();
        let cells: Vec<Vec<String>> = rows.map(|r| r.split(',').map(str::to_string).collect()).collect();
        Some(Table { key: vec!["ScenarioNo".into()], columns, rows: Some(cells.iter().map(|r| r[0].clone()).collect()), cells: Some(cells), ..Default::default() })
    }
}

/// Validator revision 4: a new galaxy's own scenario table, brought whole, and the installed mods' scenario cells
/// join the layer sets (table-keys 13.6).
#[test]
fn a_new_galaxy_s_table_and_other_mods_cells_say_which_layers_are_on_together() {
    let store = ScenarioStore { listed: true };
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: None, store: Some(&store) };
    let layer = [("layers", r#"[{"name": "base"}]"#)];
    let at = |l: &str| format!("layers/base/patches/{}.json", START.replace("{}", l));
    let add = |n: u32| format!(r#"{{"patch": 1, "key": ["MarioNo"], "add": [{{"MarioNo": {n}}}]}}"#).into_bytes();
    let in_group = |r: &Report| r.findings.iter().filter(|f| f.code == 402 && f.args.iter().any(|(n, a)| *n == "why" && *a == Arg::Phrase("Patch.KeyInGroup"))).count();
    let new_galaxy = "layers/base/files/StageData/MoonGalaxyScenario/ScenarioData.bcsv";
    let run = |files: &[E], installed: &[luma_validate::Installed], position: Option<usize>| {
        check(&luma(&layer, files), &Setup { game: Some(&game), installed, position, ..NO_GAME })
    };

    // Alone, LayerD meets no MarioNo 0. A new galaxy whose scenario 1 turns on LayerB and LayerD (mask 10) in the same
    // zone puts them together: MarioNo 0 in LayerB is met.
    assert_eq!(in_group(&run(&[file(&at("LayerD"), &add(0))], &[], None)), 0);
    let table = |mask: u32| format!("table:ScenarioNo,DandelionHillZone;1,{mask}").into_bytes();
    let r = run(&[file(new_galaxy, &table(10)), file(&at("LayerD"), &add(0))], &[], None);
    assert_eq!(in_group(&r), 1, "{:?}", r.findings);
    assert!(r.not_checked.is_empty(), "{:?}", r.not_checked);
    assert_eq!(r.touches.scenarios, vec!["moongalaxy\u{1f}1\u{1f}DandelionHillZone\u{1f}10\u{1f}whole".to_string()]);
    // The source form at the same path, `.json` added, reads alike.
    assert_eq!(in_group(&run(&[file(&format!("{new_galaxy}.json"), &table(10)), file(&at("LayerD"), &add(0))], &[], None)), 1);
    // A table that keeps LayerD apart (LayerA and LayerD, mask 9): nothing met.
    assert_eq!(in_group(&run(&[file(new_galaxy, &table(9)), file(&at("LayerD"), &add(0))], &[], None)), 0);
    // A table the store cannot read: said, not guessed.
    let r = run(&[file(new_galaxy, b"not a table"), file(&at("LayerD"), &add(0))], &[], None);
    assert_eq!(in_group(&r), 0);
    assert!(r.not_checked.iter().any(|n| n.code == 402 && n.because == "Missing.NewScenarios"), "{:?}", r.not_checked);

    // Another mod that is on brings that galaxy: its kept cells count for this mod's check, in either place in the list.
    let mut galaxy_mod = other("moon.mod");
    galaxy_mod.touches = run(&[file(new_galaxy, &table(10))], &[], None).touches;
    for position in [Some(0), Some(1), None] {
        let r = run(&[file(&at("LayerD"), &add(0))], std::slice::from_ref(&galaxy_mod), position);
        assert_eq!(in_group(&r), 1, "{position:?}: {:?}", r.findings);
    }
    galaxy_mod.on = false;
    assert_eq!(in_group(&run(&[file(&at("LayerD"), &add(0))], std::slice::from_ref(&galaxy_mod), None)), 0);
    // The higher mod's cell wins: a mod above that sets the galaxy's scenario 1 to LayerA and LayerD (9) over this
    // one's own 10 keeps LayerB and LayerD apart; below it, this mod's 10 stands.
    galaxy_mod.on = true;
    galaxy_mod.touches = run(&[file(new_galaxy, &table(9))], &[], None).touches;
    let mine = [file(new_galaxy, &table(10)), file(&at("LayerD"), &add(0))];
    assert_eq!(in_group(&run(&mine, std::slice::from_ref(&galaxy_mod), Some(1))), 0);
    assert_eq!(in_group(&run(&mine, std::slice::from_ref(&galaxy_mod), Some(0))), 1);


    // V4-1, the cook's rule: the highest whole table is the base, and every mod's patch cells edit it, lowest first,
    // whoever brought the table. A patch on another mod's galaxy is `patches/@<id>/<its path>.json`.
    let patch = |mask: u32| format!(r#"{{"patch": 1, "key": ["ScenarioNo"], "set": [{{"where": {{"ScenarioNo": 1}}, "values": {{"DandelionHillZone": {mask}}}}}]}}"#).into_bytes();
    let on_moon = "layers/base/patches/@moon.mod/StageData/MoonGalaxyScenario/ScenarioData.bcsv.json";
    let on_mine = "layers/base/patches/@this.mod/StageData/MoonGalaxyScenario/ScenarioData.bcsv.json";
    // This mod brings the galaxy (10); a mod below it patches scenario 1 to 9: the patch edits the whole table above.
    let mut patcher = other("patcher.mod");
    patcher.touches = run(&[file(on_mine, &patch(9))], &[], None).touches;
    assert_eq!(patcher.touches.scenarios, vec!["moongalaxy\u{1f}1\u{1f}DandelionHillZone\u{1f}9\u{1f}patch".to_string()]);
    assert_eq!(in_group(&run(&mine, std::slice::from_ref(&patcher), Some(0))), 0);
    assert_eq!(in_group(&run(&mine, std::slice::from_ref(&patcher), Some(1))), 0);
    // A mod above brings it (9); this mod, below, patches it to 10: the patch still edits it.
    let r = run(&[file(on_moon, &patch(10)), file(&at("LayerD"), &add(0))], std::slice::from_ref(&galaxy_mod), Some(1));
    assert_eq!(in_group(&r), 1, "{:?}", r.findings);
    // Two whole tables: the higher's is the base, whatever patch cells there are, and the lower's rows are gone.
    let two = "table:ScenarioNo,DandelionHillZone;2,10".to_string().into_bytes();
    let mine_two = [file(new_galaxy, &two), file(&at("LayerD"), &add(0))];
    assert_eq!(in_group(&run(&mine_two, std::slice::from_ref(&galaxy_mod), Some(1))), 0, "the higher's table has no scenario 2");
    assert_eq!(in_group(&run(&mine_two, std::slice::from_ref(&galaxy_mod), Some(0))), 1);

    // V4-2: two mods bringing one new galaxy whole are named (L305), in either form.
    let r = run(&[file(&format!("{new_galaxy}.json"), &table(10))], std::slice::from_ref(&galaxy_mod), Some(1));
    let named: Vec<String> = r.findings.iter().filter(|f| f.code == 305).map(|f| message(f, "UsEnglish")).collect();
    assert_eq!(named.len(), 1, "{:?}", r.findings);
    assert!(named[0].contains("stagedata/moongalaxyscenario/scenariodata.bcsv\"") && named[0].contains("moon.mod"), "{named:?}");

    // The kept report carries the cells.
    let kept = luma_validate::Saved::from_report(&r, "RMGE01").to_bytes();
    let back = luma_validate::Saved::parse(&kept).expect("kept report reads back");
    assert_eq!(back.touches, r.touches);
    let kept = luma_validate::Saved::from_report(&run(&[file(on_mine, &patch(9))], &[], None), "RMGE01").to_bytes();
    assert_eq!(luma_validate::Saved::parse(&kept).unwrap().touches.scenarios, patcher.touches.scenarios);
}

/// Table-keys T-4: a start or generalpos key is new among Common and the layers a scenario turns on together, by the
/// galaxies' scenario tables with the mod's own scenario patches; two mods adding one key overlap only there.
#[test]
fn a_start_key_is_new_among_the_layers_on_together() {
    let store = ScenarioStore { listed: true };
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: None, store: Some(&store) };
    let layer = [("layers", r#"[{"name": "base"}]"#)];
    let at = |l: &str| format!("layers/base/patches/{}.json", START.replace("{}", l));
    let add = |n: u32| format!(r#"{{"patch": 1, "key": ["MarioNo"], "add": [{{"MarioNo": {n}}}]}}"#).into_bytes();
    let scenario = |mask: u32| format!(r#"{{"patch": 1, "key": ["ScenarioNo"], "add": [{{"ScenarioNo": 3, "DandelionHillZone": {mask}}}]}}"#).into_bytes();
    let scenario_file = format!("layers/base/patches/{SCENARIOS}.json");
    let in_group = |r: &Report| r.findings.iter().filter(|f| f.code == 402 && f.args.iter().any(|(n, a)| *n == "why" && *a == Arg::Phrase("Patch.KeyInGroup"))).count();
    let run = |files: &[E]| check(&luma(&layer, files), &Setup { game: Some(&game), ..NO_GAME });

    // LayerA is on with LayerB (scenario 1): MarioNo 0 is there.
    let r = run(&[file(&at("LayerA"), &add(0))]);
    assert_eq!(in_group(&r), 1, "{:?}", r.findings);
    let f = r.findings.iter().find(|f| f.code == 402).unwrap();
    assert!(f.args.iter().any(|(n, a)| *n == "where" && *a == Arg::Text(START.replace("{}", "LayerB"))), "{:?}", f.args);
    // Common is on with every layer.
    assert_eq!(in_group(&run(&[file(&at("Common"), &add(0))])), 1);
    // LayerD is never on; with a new scenario 3 of LayerA and LayerD (mask 9) it meets no MarioNo 0 either.
    let r = run(&[file(&at("LayerD"), &add(0))]);
    assert!(r.findings.is_empty(), "{:?}", r.findings);
    assert!(r.not_checked.is_empty(), "{:?}", r.not_checked);
    let r = run(&[file(&scenario_file, &scenario(9)), file(&at("LayerD"), &add(0))]);
    assert!(r.findings.is_empty(), "{:?}", r.findings);
    let d = "stagedata/dandelionhillzone/jmp/start/{}/startinfo\u{1f}0";
    assert!(r.touches.links.contains(&d.replace("{}", "layerd")), "{:?}", r.touches.links);
    assert!(r.touches.links.contains(&d.replace("{}", "@dandelionhillgalaxy#3")), "{:?}", r.touches.links);
    // A new scenario 3 of LayerB and LayerD (mask 10): the mod's own scenario patch puts LayerD with LayerB, whichever
    // file comes first.
    assert_eq!(in_group(&run(&[file(&at("LayerD"), &add(0)), file(&scenario_file, &scenario(10))])), 1);

    // The same mod's own adds: LayerB and LayerC never meet; LayerA and LayerB do.
    assert_eq!(in_group(&run(&[file(&at("LayerB"), &add(7)), file(&at("LayerC"), &add(7))])), 0);
    assert_eq!(in_group(&run(&[file(&at("LayerA"), &add(7)), file(&at("LayerB"), &add(7))])), 1);

    // Two installed mods: one adding 7 to LayerC overlaps an add to LayerA (scenario 2) and to Common, not to LayerB.
    let mut adding = other("other.mod");
    adding.touches = run(&[file(&at("LayerC"), &add(7))]).touches;
    let overlaps = |l: &str| {
        check(&luma(&layer, &[file(&at(l), &add(7))]), &Setup { game: Some(&game), installed: std::slice::from_ref(&adding), ..NO_GAME })
            .findings
            .iter()
            .filter(|f| f.code == 404)
            .count()
    };
    assert!(overlaps("LayerA") >= 1);
    assert_eq!(overlaps("LayerB"), 0);
    assert!(overlaps("Common") >= 1);

    // A store that cannot list its galaxies: only Common and the same layer are checked, and the report says so.
    let unlisted = ScenarioStore { listed: false };
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: None, store: Some(&unlisted) };
    let r = check(&luma(&layer, &[file(&at("LayerA"), &add(0))]), &Setup { game: Some(&game), ..NO_GAME });
    assert_eq!(in_group(&r), 0, "{:?}", r.findings);
    assert!(r.not_checked.iter().any(|n| n.code == 402 && n.because == "Missing.Scenarios"), "{:?}", r.not_checked);
}

#[test]
fn every_rule_has_a_broken_mod() {
    let store = FakeStore { case_insensitive: false };
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: None, store: Some(&store) };
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
        (211, luma(&[("permissions", r#"[{"name": "net.internet", "why": "Uploads your times."}]"#)], &[]), NO_GAME),
        (211, luma(&[("permissions", r#"[{"name": "net.internet", "why": "Uploads your times.", "hosts": ["a.org", "A.org"]}]"#)], &[]), NO_GAME),
        (211, luma(&[("permissions", r#"[{"name": "fs.exchange", "why": "Shares a ghost.", "hosts": ["a.org"]}]"#)], &[]), NO_GAME),
        (211, luma(&[("permissions", r#"[{"name": "camera", "why": "Sees you."}]"#)], &[]), NO_GAME),
        (218, luma(&[("permissions", r#"[{"name": "system.links", "why": "Opens the leaderboard page."}]"#)], &[]), NO_GAME),
        (219, luma(&[("permissions", r#"[{"name": "test.secret", "why": "A test."}]"#)], &[]), NO_GAME),
        (220, luma(&[("plugins", r#"[{"binaries": {"windows-x64": "code/windows-x64/mod.dll"}}]"#)], &[]), NO_GAME),
        (221, luma(&[("pre_standard", r#"{"achievements": []}"#)], &[]), NO_GAME),
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
        // The same table in its JSON form (audit 7.1 E-2).
        (303, luma(&layer, &[file("layers/base/files/Stage/Table.bcsv.json", b"{}")]), Setup { game: Some(&game), ..NO_GAME }),
        (305, luma(&layer, &[file("layers/base/files/ObjectData/Kuribo.arc", b"x")]), Setup { game: Some(&game), installed: std::slice::from_ref(&replacing), ..NO_GAME }),
        (306, luma(&layer, &[file("layers/base/other/a.wav", b"x")]), NO_GAME),
        (306, luma(&layer, &[file("layers/base/textures/Kuribo/notes.txt", b"x")]), NO_GAME),
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
        // Said again from its carried parameters (the facts file, the loader's refusals): the same words in every
        // language (audit 3.12 C-1, loader contract 3).
        for f in &report.findings {
            let carried = luma_validate::args_text(f);
            let mut args: Vec<(String, String)> = carried.iter().filter_map(|a| a.split_once('=')).map(|(n, v)| (n.to_string(), v.to_string())).collect();
            args.push(("mod".into(), format!("T:{}", f.mod_id.clone().unwrap_or_else(|| f.file.clone()))));
            args.push(("file".into(), format!("T:{}", f.file)));
            args.extend(f.line.map(|l| ("line".to_string(), l.to_string())));
            args.extend(f.column.map(|c| ("column".to_string(), c.to_string())));
            let args: Vec<(&str, &str)> = args.iter().map(|(n, v)| (n.as_str(), v.as_str())).collect();
            for language in luma_validate::LANGUAGES {
                if luma_validate::message_of(f.code, &args, language) != luma_validate::message(f, language)
                    || luma_validate::reason_of(f.code, &args, language) != luma_validate::reason(f, language)
                {
                    failures.push(format!("case {i}: L{} in {language} differs when said from {carried:?}", f.code));
                }
            }
        }
        if start.findings != report.findings || start.not_checked != report.not_checked || start.touches != report.touches {
            failures.push(format!("case {i}: the start's check differs: {:?} {:?} {:?}
  full: {:?} {:?} {:?}", start.findings, start.not_checked, start.touches, report.findings, report.not_checked, report.touches));
        }
        if start.full != report.fingerprint.is_none() {
            failures.push(format!("case {i}: the start's check ran in full: {}", start.full));
        }
    }
    // The rules no broken mod here can reach yet, each with why (`libraries.rs` covers the code rules on libraries).
    let elsewhere: [(u16, &str); 13] = [
        (304, "what a source may stand in for is each content kind's contract (3.2 to 3.10)"),
        (407, "table_keys.rs"),
        (503, "libraries.rs"),
        (504, "libraries.rs"),
        (505, "libraries.rs"),
        (506, "libraries.rs"),
        (508, "libraries.rs"),
        (510, "libraries.rs"),
        (511, "the iOS signing rules (8.3)"),
        (512, "libraries.rs"),
        (805, "libraries.rs"),
        (806, "renderer.rs"),
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
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: None, store: Some(&store) };
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
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: None, store: Some(&store) };
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
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: None, store: Some(&store) };
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
    // A form 1 report (before `need` lines), whole and signed, does not read: its needs would be read as none.
    let form1 = String::from_utf8(kept_form1(&full)).unwrap();
    assert!(Saved::parse(form1.as_bytes()).is_none());
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

/// `full`'s kept report written in form 1, signed as that form was.
fn kept_form1(full: &Report) -> Vec<u8> {
    let text = String::from_utf8(Saved::from_report(full, "game").to_bytes()).unwrap();
    let body: String = text.lines().filter(|l| !l.starts_with("end\t") && !l.starts_with("need\t")).map(|l| format!("{l}\n")).collect();
    let body = body.replacen("luma-saved\t2\n", "luma-saved\t1\n", 1);
    use sha1::{Digest, Sha1};
    let sum: String = Sha1::digest(body.as_bytes()).iter().map(|b| format!("{b:02x}")).collect();
    format!("{body}end\t{sum}\n").into_bytes()
}

/// L800's cycle (loader D3-3): the game starts a needed mod first, so mods that need each other round a circle cannot
/// start. The check sees this mod's needs and the kept needs of the mods that are on.
#[test]
fn a_cycle_of_needs_through_the_mod_is_refused_and_named() {
    let needing = |id: &str, needs: &[&str]| {
        let mut o = other(id);
        o.needs = needs.iter().map(|n| n.to_string()).collect();
        o
    };
    let bytes = luma(&[("needs", r#"[{"id": "b.mod"}]"#)], &[]);
    let cycles = |installed: &[Installed]| -> Vec<String> {
        let r = check(&bytes, &Setup { installed, ..NO_GAME });
        r.findings.iter().filter(|f| f.code == 800).map(|f| message(f, "UsEnglish")).collect()
    };
    // Two: test.mod -> b.mod -> test.mod. No stray space where the need has no version range.
    let two = [needing("b.mod", &["test.mod"])];
    assert_eq!(
        cycles(&two),
        vec!["test.mod needs b.mod: each needs the next, in a circle (test.mod -> b.mod -> test.mod), so none of them can start first.".to_string()]
    );
    // Three, and the shortest is named when there are two.
    let three = [needing("b.mod", &["c.mod"]), needing("c.mod", &["test.mod"])];
    assert!(cycles(&three)[0].contains("(test.mod -> b.mod -> c.mod -> test.mod)"), "{:?}", cycles(&three));
    let both = [needing("b.mod", &["c.mod", "test.mod"]), needing("c.mod", &["test.mod"])];
    assert!(cycles(&both)[0].contains("(test.mod -> b.mod -> test.mod)"), "{:?}", cycles(&both));
    // None: a chain that does not come back, a cycle not through this mod, and a mod that is off.
    assert!(cycles(&[needing("b.mod", &["c.mod"]), needing("c.mod", &[])]).is_empty());
    assert!(cycles(&[needing("b.mod", &["c.mod"]), needing("c.mod", &["b.mod"])]).is_empty());
    let mut off = needing("b.mod", &["test.mod"]);
    off.on = false;
    let r = check(&bytes, &Setup { installed: std::slice::from_ref(&off), ..NO_GAME });
    assert_eq!(r.findings.iter().filter(|f| f.code == 800).map(|f| message(f, "UsEnglish")).collect::<Vec<_>>(), vec!["test.mod needs b.mod: it is off.".to_string()]);
    // Every language fills it.
    let r = check(&bytes, &Setup { installed: &two, ..NO_GAME });
    let f = r.findings.iter().find(|f| f.code == 800).unwrap();
    for lang in luma_validate::LANGUAGES {
        let text = message(f, lang);
        assert!(!text.contains('{') && text.contains("test.mod -> b.mod -> test.mod"), "{lang}: {text}");
    }
    // The needs go through the kept report, so another mod's check sees them.
    let kept = Saved::parse(&Saved::from_report(&r, "game").to_bytes()).unwrap();
    assert_eq!(kept.installed("test.luma", true).unwrap().needs, vec!["b.mod".to_string()]);
}

/// The mod higher in the list wins a file and a cell (F, INBOX-G 13): installed mods come top first, and `position`
/// is how many are above the mod checked.
#[test]
fn the_mod_higher_in_the_list_wins() {
    let store = FakeStore { case_insensitive: false };
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: None, store: Some(&store) };
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
    assert!(below.iter().all(|(_, m)| m.contains("; other.mod's") && m.ends_with("(it is higher in the list).")), "{below:?}");
    assert!(above.iter().all(|(_, m)| m.contains("; test.mod's") && m.ends_with("(it is higher in the list).")), "{above:?}");
}

/// F's AP-1 (audit 3.1-perm): a revision-1 `net` with hosts is one finding, Perm.NetSplit, not NetSplit and
/// HostsOnlyNet; another name with hosts still gets HostsOnlyNet.
#[test]
fn a_revision_1_net_with_hosts_is_one_finding() {
    let r = check(&luma(&[("permissions", r#"[{"name": "net", "why": "Uploads your times.", "hosts": ["a.org"]}]"#)], &[]), &NO_GAME);
    let got: Vec<(u16, String)> = r.findings.iter().map(|f| (f.code, message(f, "UsEnglish"))).collect();
    assert_eq!(got.len(), 1, "{got:?}");
    assert_eq!(got[0].0, 211);
    assert!(got[0].1.contains("net is two permissions now"), "{}", got[0].1);
    let r = check(&luma(&[("permissions", r#"[{"name": "fs.exchange", "why": "Shares a ghost.", "hosts": ["a.org"]}]"#)], &[]), &NO_GAME);
    assert!(message(&r.findings[0], "UsEnglish").contains("only net.internet takes hosts"), "{:?}", r.findings);
}

#[test]
fn the_previous_engines_keys_name_their_replacement() {
    let codes = |over: &[(&str, &str)]| -> Vec<(u16, String, String)> {
        check(&luma(over, &[]), &NO_GAME)
            .findings
            .iter()
            .map(|f| (f.code, f.pointer.clone().unwrap_or_default(), message(f, "UsEnglish")))
            .collect()
    };
    // K2's luma.json as the previous engine read it: each old key says what the one form writes.
    let got = codes(&[
        ("plugins", r#"[{"binaries": {"windows-x64": "code/windows-x64/mod.dll"}}]"#),
        ("description", r#""Old.""#),
        ("load_after", "[]"),
    ]);
    let said: Vec<(u16, &str)> = got.iter().map(|(c, p, _)| (*c, p.as_str())).collect();
    assert_eq!(said, [(220, "/plugins"), (220, "/description"), (220, "/load_after")], "{got:?}");
    assert!(got[0].2.ends_with("\"plugins\" is the previous engine's; write code (code/<platform>/mod.<ending>)."), "{}", got[0].2);
    assert!(got[1].2.ends_with("write summary."), "{}", got[1].2);
    // A layer by path, its old rows and its old conditions: under pre_standard.layers.<name>.
    let got = codes(&[("layers", r#"[{"name": "base", "path": "layer", "tunables": {}, "when": {"mods": ["a.mod"]}}]"#)]);
    let expected: Vec<String> = got.iter().filter(|(c, _, _)| *c == 220).map(|(_, _, m)| m[m.rfind("write ").unwrap() + 6..].to_string()).collect();
    assert_eq!(expected, ["name (layers/<name>/).", "pre_standard.layers.base.tunables.", "pre_standard.layers.base.when.mods."], "{got:?}");
    // Under pre_standard: a Note per key, naming the plan that replaces it; a key the previous engine never had is L202.
    let got = codes(&[("pre_standard", r#"{"achievements": [], "galaxies": [], "nope": 1, "layers": {"base": {"forms": [], "when": {"not": {}}, "nope": 2}}}"#)]);
    let said: Vec<(u16, &str)> = got.iter().map(|(c, p, _)| (*c, p.as_str())).collect();
    assert_eq!(
        said,
        [
            (202, "/pre_standard/nope"),
            (202, "/pre_standard/layers/base/nope"),
            (221, "/pre_standard/achievements"),
            (221, "/pre_standard/galaxies"),
            (221, "/pre_standard/layers/base/forms"),
            (221, "/pre_standard/layers/base/when/not"),
        ],
        "{got:?}"
    );
    assert!(got[4].2.ends_with("\"pre_standard.layers.base.forms\" is read by the previous engine only, until plan 1.4 replaces it."), "{}", got[4].2);
    let report = check(&luma(&[("pre_standard", r#"{"save": {}}"#)], &[]), &NO_GAME);
    assert_eq!(report.findings.iter().map(|f| f.level).collect::<Vec<_>>(), [luma_validate::Level::Note]);
}

/// Text and key options (director's decision 23:0x: a mod reads them itself, key options are input actions).
#[test]
fn text_and_key_options_are_whole_and_choose_no_layer() {
    let said = |options: &str, layers: Option<&str>| -> Vec<(u16, String, String)> {
        let mut over = vec![("options", options)];
        let mut more = Vec::new();
        if let Some(l) = layers {
            over.push(("layers", l));
            more.push(file("layers/base/files/a.wav", b"x"));
        }
        check(&luma(&over, &more), &NO_GAME)
            .findings
            .iter()
            .map(|f| (f.code, f.pointer.clone().unwrap_or_default(), message(f, "UsEnglish")))
            .collect()
    };
    let text = |default: &str, max: u32| format!(r#"[{{"key": "t", "kind": "text", "name": "T", "default": {default}, "max_length": {max}}}]"#);
    // Within max_length in code points, spaces kept: no finding.
    assert!(said(&text(r#""ééééé""#, 5), None).is_empty());
    assert!(said(&text(r#"" a b ""#, 5), None).is_empty());
    assert!(said(&text(r#""""#, 5), None).is_empty());
    for bad in [r#""éééééé""#, r#""a\nb""#, r#""a\tb""#, r#""a\u0000""#] {
        let got = said(&text(bad, 5), None);
        assert_eq!(got.len(), 1, "{bad}: {got:?}");
        assert_eq!((got[0].0, got[0].1.as_str()), (210, "/options/0/default"), "{bad}");
        assert!(got[0].2.contains("max_length"), "{}", got[0].2);
    }
    // max_length and default are required.
    let got = said(r#"[{"key": "t", "kind": "text", "name": "T", "default": "a"}]"#, None);
    assert!(got.iter().any(|(c, p, m)| *c == 203 && p == "/options/0" && m.contains("max_length")), "{got:?}");
    let got = said(r#"[{"key": "t", "kind": "text", "name": "T", "max_length": 4}]"#, None);
    assert!(got.iter().any(|(c, p, m)| *c == 203 && p == "/options/0" && m.contains("default")), "{got:?}");
    // A key option: a default is required and names one of the game's controls, as the input bindings spell them.
    let key = |default: &str| format!(r#"[{{"key": "k", "kind": "key", "name": "K", "default": {default}}}]"#);
    for good in [r#""two""#, r#""Two""#, r#""dpad_up""#, r#""jump_back""#] {
        assert!(said(&key(good), None).is_empty(), "{good}");
    }
    for bad in [r#""Space""#, r#""2""#, r#""""#] {
        let got = said(&key(bad), None);
        assert_eq!(got.len(), 1, "{bad}: {got:?}");
        assert_eq!((got[0].0, got[0].1.as_str()), (210, "/options/0/default"), "{bad}");
        assert!(got[0].2.contains("is not a control the game names"), "{}", got[0].2);
    }
    let got = said(&key("2"), None);
    assert!(got.iter().any(|(c, p, _)| *c == 204 && p == "/options/0/default"), "{got:?}");
    let got = said(r#"[{"key": "k", "kind": "key", "name": "K"}]"#, None);
    assert!(got.iter().any(|(c, p, m)| *c == 203 && p == "/options/0" && m.contains("default")), "{got:?}");
    // A layer's when may not name a text or key option: its own message, not "a value it can never have".
    let both = r#"[{"key": "t", "kind": "text", "name": "T", "default": "a", "max_length": 4}, {"key": "k", "kind": "key", "name": "K", "default": "two"}, {"key": "on", "kind": "toggle", "name": "On", "default": true}]"#;
    for (named, value) in [("t", r#""a""#), ("k", r#""two""#)] {
        let layers = format!(r#"[{{"name": "base", "when": {{"option": {{"{named}": {value}}}}}}}]"#);
        let got = said(both, Some(&layers));
        assert_eq!(got.len(), 1, "{named}: {got:?}");
        assert_eq!((got[0].0, got[0].1.as_str()), (301, format!("/layers/0/when/option/{named}").as_str()));
        assert!(got[0].2.contains("a text or key option") && got[0].2.contains(&format!("\"{named}\"")), "{}", got[0].2);
    }
    assert!(said(both, Some(r#"[{"name": "base", "when": {"option": {"on": true}}}]"#)).is_empty());
}
