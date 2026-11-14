//! Placements of classes only code can make (references 3): a placement row whose class neither the game's factory
//! list nor its planet table names is kept as an `after_start` touch (class, file, line) for the game's check after
//! the mods' start, and counted as not checked (L403, Missing.AfterStart).

use luma_validate::names::Names;
use luma_validate::semver::Version;
use luma_validate::{validate, ColumnType, Game, Input, Report, Saved, Source, Store, Table};
use std::collections::HashSet;

mod common;
use common::*;

const OBJ_INFO: &str = "StageData/Zone/Zone.arc/jmp/Placement/Common/ObjInfo";
const ZONES: &str = "StageData/Zone/Zone.arc/jmp/Placement/Common/StageObjInfo";
const PLANETS: &str = "ObjectData/PlanetMapDataTable.arc/PlanetMapDataTable.bcsv";

struct Stage;

impl Store for Stage {
    fn path(&self, lower: &str) -> Option<String> {
        [OBJ_INFO, ZONES, PLANETS].iter().find(|p| p.to_lowercase() == lower).map(|p| p.to_string())
    }
    fn table(&self, path: &str) -> Option<Table> {
        if path == PLANETS {
            let cells = vec![vec!["GoldPlanet".to_string()]];
            return Some(Table { key: vec!["#".into()], columns: vec![("PlanetName".into(), ColumnType::Text)], cells: Some(cells), ..Default::default() });
        }
        let int = ColumnType::Int { min: 0, max: 65535 };
        Some(Table {
            key: vec!["l_id".into()],
            columns: vec![("l_id".into(), int), ("name".into(), ColumnType::Text), ("ShapeModelNo".into(), int)],
            rows: Some(HashSet::from(["1".to_string()])),
            ..Default::default()
        })
    }
    fn may_be_new(&self, path: &str) -> bool {
        path.starts_with("stagedata/")
    }
    /// A stand-in for the host's table reader: "table:column,column;value,value;..." (the real readers are
    /// nebula-import's).
    fn read_table(&self, _path: &str, bytes: &[u8]) -> Option<Table> {
        let text = std::str::from_utf8(bytes).ok()?.strip_prefix("table:")?;
        let mut rows = text.split(';');
        let columns: Vec<(String, ColumnType)> = rows.next()?.split(',').map(|c| (c.to_string(), ColumnType::Text)).collect();
        let cells: Vec<Vec<String>> = rows.map(|r| r.split(',').map(str::to_string).collect()).collect();
        Some(Table { key: vec!["#".into()], columns, cells: Some(cells), ..Default::default() })
    }
}

fn names(factory: &[&str]) -> Names {
    Names { factory: Some(factory.iter().map(|n| n.to_lowercase()).collect()), ..Names::default() }
}

fn check(bytes: &[u8], names: Option<&Names>) -> Report {
    let store = Stage;
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names, store: Some(&store) };
    validate(&Input { source: Source::Bytes(bytes, "test.luma"), game: Some(&game), installed: &[], platforms: &["windows-x64"], options: None, position: None })
}

fn waiting_for(r: &Report, because: &str) -> u32 {
    r.not_checked.iter().filter(|n| n.code == 403 && n.because == because).map(|n| n.places).sum()
}

fn waiting(r: &Report) -> u32 {
    waiting_for(r, "Missing.AfterStart")
}

const PATCH: &str = "layers/base/patches/StageData/Zone/Zone.arc/jmp/Placement/Common/ObjInfo.json";

/// Rows on their own lines: Kuribo (the factory's, in another case), ModThing (no list's), a shaped row (made by its
/// shape), GoldPlanet (the planet table's), and a set of row 1's class to ModOther.
fn patch() -> Vec<u8> {
    concat!(
        "{\"patch\": 1, \"key\": [\"l_id\"],\n",
        " \"add\": [\n",
        "  {\"l_id\": 5, \"name\": \"kuribo\"},\n",
        "  {\"l_id\": 6, \"name\": \"ModThing\"},\n",
        "  {\"l_id\": 7, \"name\": \"FlexShape\", \"ShapeModelNo\": 2},\n",
        "  {\"l_id\": 8, \"name\": \"GoldPlanet\"}\n",
        " ],\n",
        " \"set\": [{\"where\": {\"l_id\": 1},\n",
        "   \"values\": {\"name\": \"ModOther\"}}]}\n"
    )
    .as_bytes()
    .to_vec()
}

#[test]
fn a_placement_of_a_class_no_list_names_waits_for_the_check_after_start() {
    let layer = [("layers", r#"[{"name": "base"}]"#)];
    let r = check(&luma(&layer, &[file(PATCH, &patch())]), Some(&names(&["Kuribo"])));
    assert!(r.findings.is_empty(), "{:?}", r.findings);
    assert_eq!(r.touches.after_start, vec![format!("ModThing\u{1f}{PATCH}\u{1f}4"), format!("ModOther\u{1f}{PATCH}\u{1f}9")]);
    assert_eq!(waiting(&r), 2);

    // Kept in the report the game keeps, as the host reads it at each start.
    let kept = Saved::parse(&Saved::from_report(&r, "game").to_bytes()).unwrap();
    assert_eq!(kept.touches.after_start, r.touches.after_start);

    // Without the factory list no class is known: every placement waits, the planet's and the factory's too; the
    // shaped row still does not.
    let r = check(&luma(&layer, &[file(PATCH, &patch())]), Some(&Names::default()));
    let classes: Vec<&str> = r.touches.after_start.iter().map(|row| row.split('\u{1f}').next().unwrap()).collect();
    assert_eq!(classes, vec!["kuribo", "ModThing", "GoldPlanet", "ModOther"]);
    assert_eq!((waiting(&r), waiting_for(&r, "Missing.Classes")), (0, 4));
    let r = check(&luma(&layer, &[file(PATCH, &patch())]), None);
    assert_eq!(r.touches.after_start.len(), 4);

    // A zone list names zones, not classes; a table outside jmp/Placement and jmp/MapParts names no class.
    let zones = br#"{"patch": 1, "key": ["l_id"], "add": [{"l_id": 5, "name": "NewZone"}]}"#;
    let r = check(&luma(&layer, &[file("layers/base/patches/StageData/Zone/Zone.arc/jmp/Placement/Common/StageObjInfo.json", zones)]), Some(&names(&[])));
    assert!(r.touches.after_start.is_empty(), "{:?}", r.touches.after_start);
    assert_eq!(waiting(&r), 0);
}

#[test]
fn a_whole_placement_table_gives_each_row_by_its_number() {
    let layer = [("layers", r#"[{"name": "base"}]"#)];
    let whole = "layers/base/files/StageData/New/New.arc/jmp/MapParts/Common/MapPartsInfo";
    let table = b"table:l_id,name,ShapeModelNo;0,Kuribo,-1;1,ModRail,-1;2,FlexShape,3;3,,-1";
    let r = check(&luma(&layer, &[file(whole, table)]), Some(&names(&["Kuribo"])));
    assert_eq!(r.touches.after_start, vec![format!("ModRail\u{1f}{whole}\u{1f}2")]);
    assert_eq!(waiting(&r), 1);
    // Its source form too.
    let r = check(&luma(&layer, &[file(&format!("{whole}.json"), table)]), Some(&names(&["Kuribo"])));
    assert_eq!(r.touches.after_start, vec![format!("ModRail\u{1f}{whole}.json\u{1f}2")]);
}

#[test]
fn the_factory_list_is_read_from_the_placement_facts_beside_the_rows() {
    let dir = std::env::temp_dir().join(format!("luma-after-start-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("smg_game.luma-placement.json"), br#"{"placement": 1, "factory": [{"name": "SwitchCube", "class": "SwitchArea", "creator": "c"}, {"name": "Kuribo", "class": "Kuribo", "creator": "c"}]}"#).unwrap();
    let names = Names::from_rows_folder(&dir, 1).unwrap();
    assert_eq!(names.factory, Some(HashSet::from(["switchcube".to_string(), "kuribo".to_string()])));
    // Only the file at the folder's top is read; one without its list is an error naming it.
    std::fs::create_dir_all(dir.join("old")).unwrap();
    std::fs::write(dir.join("old").join("smg_game.luma-placement.json"), br#"{"factory": []}"#).unwrap();
    assert_eq!(Names::from_rows_folder(&dir, 1).unwrap().factory.map(|f| f.len()), Some(2));
    std::fs::write(dir.join("smg_game.luma-placement.json"), br#"{"placement": 1}"#).unwrap();
    assert!(Names::from_rows_folder(&dir, 1).is_err());
    std::fs::remove_dir_all(&dir).unwrap();
}
