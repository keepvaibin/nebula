use super::*;
use crate::cook::refuse::said;
use serde_json::json;

fn objects() -> ObjectArgs {
    let description = json!({
        "factory": [
            {"name": "IronCannonLauncher", "class": "IronCannonLauncher", "creator": "createNameObj<IronCannonLauncher>"},
            {"name": "Kuribo", "class": "Kuribo", "creator": "createNameObj<Kuribo>"},
            {"name": "SuperSpinDriverPink", "class": null, "creator": "MR::createSuperSpinDriverPink"},
            {"name": "ShareArea", "class": "AreaObj", "creator": "createNameObj<AreaObj>"},
            {"name": "SpinningBlock", "class": "SpinningBlock", "creator": "createNameObj<SpinningBlock>"}
        ],
        "placement": 1,
        "placement_args": {
            // F's form: a base's rows merged with their "from", a helper's read without a target, a column by name.
            "SpinningBlock": [
                {"arg": 0, "field": "Obj_arg0", "target": null, "type": "s32", "from": "SpinningBlock", "via": ["getDomeIdFromArg0"]},
                {"arg": 0, "field": "Obj_arg0", "target": "mDomeId", "type": "s32", "from": "MapObjActor", "via": []},
                {"arg": 1, "field": "Obj_arg1", "target": "mInBase", "type": "f32", "from": "MapObjActor", "via": []},
                {"arg": 1, "field": "Obj_arg1", "target": "mRotateSpeed", "type": "f32", "from": "SpinningBlock", "via": []},
                {"arg": null, "field": "RotateAccel", "target": "mAccel", "type": "f32", "from": "SpinningBlock", "via": []},
                {"arg": null, "field": null, "target": "mAny", "type": "s32", "from": "SpinningBlock", "via": []}
            ],
            "IronCannonLauncher": [
                {"arg": 0, "target": "mShotInterval", "type": "s32", "from": "IronCannonLauncher"},
                {"arg": 1, "target": "mSpeed", "type": "f32", "from": "IronCannonLauncher"},
                {"arg": 2, "target": "this->mIsRotate", "type": "bool", "from": "IronCannonLauncher"},
                {"arg": 3, "target": "arg", "type": "s32", "from": "IronCannonLauncher"}
            ],
            "Kuribo": [
                {"arg": 0, "target": "mSpeed", "type": "f32", "from": "Kuribo"},
                {"arg": 1, "target": "mSpeed", "type": "f32", "from": "Kuribo"}
            ],
            "AreaObj": [
                {"arg": 0, "target": "mObjArg0", "type": "s32", "from": "AreaObj"},
                {"arg": 0, "target": "mObjArg0", "type": "bool", "from": "AreaObj"}
            ]
        }
    });
    ObjectArgs::from_description(description.to_string().as_bytes()).unwrap()
}

#[test]
fn a_fields_name_is_the_modders_name() {
    assert_eq!(modder_name(Some("mShotInterval"), 0), "ShotInterval");
    assert_eq!(modder_name(Some("this->mIsRotate"), 2), "IsRotate");
    assert_eq!(modder_name(Some("m_speed"), 1), "speed");
    assert_eq!(modder_name(Some("pInfo.mValue"), 1), "Value");
    // A local, a lower-case m word, or no target: the argument's number.
    assert_eq!(modder_name(Some("arg"), 3), "Arg3");
    assert_eq!(modder_name(Some("mode"), 4), "Arg4");
    assert_eq!(modder_name(None, 7), "Arg7");
    assert_eq!(modder_name(Some("m"), 5), "Arg5");
}

#[test]
fn the_description_gives_each_objects_names() {
    let o = objects();
    let names = |object: &str| o.of(object).unwrap().iter().map(|a| (a.column.clone(), a.name.clone(), a.kind)).collect::<Vec<_>>();
    let n = |column: &str, name: &str, kind| (column.to_string(), name.to_string(), kind);
    assert_eq!(
        names("ironcannonlauncher"),
        [n("Obj_arg0", "ShotInterval", ArgKind::Int), n("Obj_arg1", "Speed", ArgKind::Float), n("Obj_arg2", "IsRotate", ArgKind::Bool), n("Obj_arg3", "Arg3", ArgKind::Int)]
    );
    // One field read from two arguments names neither; one argument read two ways is a whole number.
    assert_eq!(names("Kuribo"), [n("Obj_arg0", "Arg0", ArgKind::Float), n("Obj_arg1", "Arg1", ArgKind::Float)]);
    assert_eq!(names("ShareArea"), [n("Obj_arg0", "ObjArg0", ArgKind::Int)]);
    // One name per argument: the class's own read with a field, else a base's; a column read by name keeps its name;
    // one named by a variable (field null) is nothing a modder names.
    assert_eq!(
        names("SpinningBlock"),
        [n("Obj_arg0", "DomeId", ArgKind::Int), n("Obj_arg1", "RotateSpeed", ArgKind::Float), n("RotateAccel", "RotateAccel", ArgKind::Float)]
    );
    assert_eq!(o.of("SpinningBlock").unwrap()[2].arg, None);
    assert!(o.of("SuperSpinDriverPink").unwrap().is_empty());
    assert!(o.of("NoSuchThing").is_none());
    assert!(ObjectArgs::from_description(b"{}").is_err());
    assert!(ObjectArgs::from_description(br#"{"factory":[],"placement_args":{"A":[{"arg":8}]}}"#).is_err());
    // A later version of the file is not read as this one.
    assert!(ObjectArgs::from_description(br#"{"placement":2,"factory":[],"placement_args":{}}"#).is_err());
}

#[test]
fn a_column_read_by_its_own_name_takes_the_value_as_given() {
    let o = objects();
    let mut rows = vec![json!({"name": "SpinningBlock", "args": {"RotateAccel": 0.5, "rotatespeed": 3, "DomeId": 2}})];
    expand_rows(&mut rows, "/rows", Some(&o), false).unwrap();
    assert_eq!(rows[0], json!({"name": "SpinningBlock", "RotateAccel": 0.5, "Obj_arg0": 2, "Obj_arg1": 3}));
    // Given twice: by name and as the column.
    let mut rows = vec![json!({"name": "SpinningBlock", "RotateAccel": 1, "args": {"RotateAccel": 0.5}})];
    assert_eq!(said(&expand_rows(&mut rows, "/rows", Some(&o), false).unwrap_err()).0, "Cook.Stage.ArgTwice");
}

#[test]
fn named_arguments_become_the_games_columns() {
    let o = objects();
    let mut rows = vec![
        json!({"name": "IronCannonLauncher", "l_id": 3, "args": {"ShotInterval": 120, "speed": 30.0, "IsRotate": true}}),
        json!({"name": "IronCannonLauncher", "l_id": 4, "args": {"IsRotate": false, "Arg3": 7, "ShotInterval": null}, "Obj_arg5": 2}),
        json!({"name": "Kuribo", "l_id": 5}),
        json!({"name": "SuperSpinDriverPink", "args": {"Arg0": 1}}),
    ];
    assert_eq!(expand_rows(&mut rows, "/rows", Some(&o), true).unwrap(), 3);
    assert_eq!(
        rows[0],
        json!({"name": "IronCannonLauncher", "l_id": 3, "Obj_arg0": 120, "Obj_arg1": 30, "Obj_arg2": 1, "Obj_arg3": -1,
               "Obj_arg4": -1, "Obj_arg5": -1, "Obj_arg6": -1, "Obj_arg7": -1})
    );
    assert_eq!(
        rows[1],
        json!({"name": "IronCannonLauncher", "l_id": 4, "Obj_arg0": -1, "Obj_arg1": -1, "Obj_arg2": -1, "Obj_arg3": 7,
               "Obj_arg4": -1, "Obj_arg5": 2, "Obj_arg6": -1, "Obj_arg7": -1})
    );
    assert_eq!(rows[2], json!({"name": "Kuribo", "l_id": 5}), "a row without args is the mod's as written");
    assert_eq!(rows[3]["Obj_arg0"], json!(1));
}

#[test]
fn a_wrong_argument_is_refused_by_name() {
    let o = objects();
    let cases = [
        (json!({"name": "IronCannonLauncher", "args": {"Interval": 1}}), "Cook.Stage.ArgName", "/rows/0/args/Interval"),
        (json!({"name": "IronCannonLauncher", "args": {"Arg8": 1}}), "Cook.Stage.ArgName", "/rows/0/args/Arg8"),
        (json!({"name": "IronCannonLauncher", "args": {"Speed": 2.5}}), "Cook.Effect.Kind", "/rows/0/args/Speed"),
        (json!({"name": "IronCannonLauncher", "args": {"ShotInterval": true}}), "Cook.Effect.Kind", "/rows/0/args/ShotInterval"),
        (json!({"name": "IronCannonLauncher", "args": {"ShotInterval": 3_000_000_000u64}}), "Cook.Effect.Range", "/rows/0/args/ShotInterval"),
        (json!({"name": "IronCannonLauncher", "args": {"ShotInterval": "fast"}}), "Cook.Effect.Kind", "/rows/0/args/ShotInterval"),
        (json!({"name": "IronCannonLauncher", "args": {"ShotInterval": 1}, "Obj_arg0": 2}), "Cook.Stage.ArgTwice", "/rows/0/args/ShotInterval"),
        (json!({"name": "IronCannonLauncher", "args": {"ShotInterval": 1, "Arg0": 2}}), "Cook.Stage.ArgTwice", "/rows/0/args/ShotInterval"),
        (json!({"name": "IronCannonLauncher", "args": [1, 2]}), "Cook.Effect.Kind", "/rows/0/args"),
        (json!({"name": "Wanwan", "args": {"Arg0": 1}}), "Cook.Stage.NoObject", "/rows/0/args"),
        (json!({"args": {"Arg0": 1}}), "Cook.Stage.NoObject", "/rows/0/args"),
    ];
    for (row, why, at) in cases {
        let mut rows = vec![row.clone()];
        let error = expand_rows(&mut rows, "/rows", Some(&o), true).unwrap_err();
        let (got, args) = said(&error);
        assert_eq!(got, why, "{row}: {args:?}");
        let place = args.iter().find(|(n, _)| *n == "where").map(|(_, v)| v.as_str());
        // ArgTwice for the second key of one object names whichever came second; both are the row's args.
        assert!(place == Some(at) || (why == "Cook.Stage.ArgTwice" && place.is_some_and(|p| p.starts_with("/rows/0/args/"))), "{row}: {args:?}");
    }
    // The list of names is said with the refusal.
    let mut rows = vec![json!({"name": "IronCannonLauncher", "args": {"Interval": 1}})];
    let (_, args) = said(&expand_rows(&mut rows, "/rows", Some(&o), true).unwrap_err());
    assert!(args.iter().any(|(n, v)| *n == "expected" && v == "ShotInterval, Speed, IsRotate, Arg3, Arg0-Arg7"), "{args:?}");
    // Without the game's facts, a row with args cannot be cooked: the refusal names the file.
    let mut rows = vec![json!({"name": "Kuribo", "args": {"Arg0": 1}})];
    assert_eq!(said(&expand_rows(&mut rows, "/rows", None, true).unwrap_err()).0, "Cook.Stage.NoFacts");
}

#[test]
fn a_whole_table_is_expanded_with_the_facts_file_and_only_when_a_row_names_arguments() {
    let _file = TEST_FILE.lock().unwrap_or_else(|e| e.into_inner());
    let dir = tempfile::tempdir().unwrap();
    let plain = br#"{"rows":[{"name":"Kuribo","Obj_arg0":3}]}"#;
    let named = br#"{"rows":[{"name":"IronCannonLauncher","args":{"ShotInterval":90}}]}"#;
    // No facts file: a table without args stands as written, one with args is refused naming the file.
    use_file(&dir.path().join("none.json"));
    assert_eq!(expand_table(plain).unwrap(), None);
    let (why, args) = said(&expand_table(named).unwrap_err());
    assert_eq!(why, "Cook.Stage.NoFacts");
    assert!(args[0].1.ends_with("none.json"), "{args:?}");
    // With the facts (a folder holding smg_game.luma-placement.json).
    let facts = serde_json::json!({"factory": [{"name": "IronCannonLauncher", "class": "IronCannonLauncher"}],
        "placement_args": {"IronCannonLauncher": [{"arg": 0, "target": "mShotInterval", "type": "s32"}]}});
    std::fs::write(dir.path().join(FILE_NAME), facts.to_string()).unwrap();
    use_file(dir.path());
    let out: Value = serde_json::from_slice(&expand_table(named).unwrap().unwrap()).unwrap();
    assert_eq!(out["rows"][0]["Obj_arg0"], json!(90));
    assert_eq!(out["rows"][0]["Obj_arg1"], json!(-1));
    assert!(out["rows"][0].get("args").is_none());
}

#[test]
fn a_set_changes_only_the_arguments_it_names() {
    let mut fields = json!({"args": {"Speed": 4}}).as_object().unwrap().clone();
    assert!(expand_fields(&mut fields, "/set/0/values", "IronCannonLauncher", Some(&objects()), false).unwrap());
    assert_eq!(serde_json::Value::Object(fields), json!({"Obj_arg1": 4}));
    // A column given in another case is the same column.
    let mut fields = json!({"obj_arg1": 2, "args": {"Speed": 4}}).as_object().unwrap().clone();
    let (why, _) = said(&expand_fields(&mut fields, "/add/0", "IronCannonLauncher", Some(&objects()), true).unwrap_err());
    assert_eq!(why, "Cook.Stage.ArgTwice");
}
