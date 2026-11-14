//! luma.md revision 4: a mod's `renderer.features` (its form, the game's list of feature names) and L806, a
//! feature this device's graphics lack (start-check.md 3), at import and at a start's check.

use luma_validate::names::{feature_name_ok, renderer_features_from_json, Names, RENDERER_FILE};
use luma_validate::semver::Version;
use luma_validate::{message, recheck, validate, Game, Input, Report, Saved, Source};

mod common;
use common::*;

const LIST: &str = r#"{"renderer": {"features": ["mesh_shaders", "ray_tracing"]}}"#;

fn names(list: Option<&str>) -> Names {
    let mut n = Names { api: 1, ..Names::default() };
    n.renderer_features = list.map(|l| renderer_features_from_json(l.as_bytes()).unwrap());
    n
}

fn input<'a>(bytes: &'a [u8], game: &'a Game<'a>) -> Input<'a> {
    Input { source: Source::Bytes(bytes, "test.luma"), game: Some(game), installed: &[], platforms: &[], options: None, position: None }
}

fn check(bytes: &[u8], list: Option<&str>, device: Option<&[&str]>) -> Report {
    let n = names(list);
    let device: Option<Vec<String>> = device.map(|d| d.iter().map(|s| s.to_string()).collect());
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: device.as_deref(), names: Some(&n), store: None };
    let report = validate(&input(bytes, &game));
    for f in &report.findings {
        assert_eq!(unfilled(f), None);
    }
    report
}

fn codes(r: &Report) -> Vec<u16> {
    r.findings.iter().map(|f| f.code).collect()
}

fn not_checked(r: &Report) -> Vec<(u16, &'static str)> {
    r.not_checked.iter().map(|n| (n.code, n.because)).collect()
}

fn wants(features: &str) -> Vec<u8> {
    luma(&[("renderer", &format!(r#"{{"features": {features}}}"#))], &[])
}

#[test]
fn feature_names_follow_the_grammar() {
    for ok in ["a", "ray_tracing", "mesh_shaders2", "x1_y2_z3", "abcdefghijklmnopqrstuvwxyz012345"] {
        assert!(feature_name_ok(ok), "{ok}");
    }
    for bad in ["", "Ray", "1a", "_a", "a_", "a__b", "a-b", "a.b", "ray tracing", "abcdefghijklmnopqrstuvwxyz0123456", "é"] {
        assert!(!feature_name_ok(bad), "{bad}");
    }
}

#[test]
fn the_game_s_list_reads_strictly() {
    let got = renderer_features_from_json(LIST.as_bytes()).unwrap();
    assert_eq!(got.into_iter().collect::<Vec<_>>(), vec!["mesh_shaders".to_string(), "ray_tracing".to_string()]);
    assert!(renderer_features_from_json(br#"{"renderer": {"features": []}}"#).unwrap().is_empty());
    // The self-description's other keys are not this reader's.
    assert!(renderer_features_from_json(br#"{"self": 1, "renderer": {"features": ["a"]}}"#).is_ok());
    let many: Vec<String> = (0..65).map(|i| format!("\"f{i:03}\"")).collect();
    let too_many = format!(r#"{{"renderer": {{"features": [{}]}}}}"#, many.join(", "));
    for (bad, at) in [
        ("{}", "renderer"),
        (r#"{"renderer": []}"#, "renderer"),
        (r#"{"renderer": {}}"#, "renderer.features"),
        (r#"{"renderer": {"features": {}}}"#, "renderer.features"),
        (r#"{"renderer": {"features": [], "more": 1}}"#, "renderer.more"),
        (r#"{"renderer": {"features": ["Ray"]}}"#, "renderer.features[0]"),
        (r#"{"renderer": {"features": [1]}}"#, "renderer.features[0]"),
        (r#"{"renderer": {"features": ["b", "a"]}}"#, "renderer.features[1]"),
        (r#"{"renderer": {"features": ["a", "a"]}}"#, "renderer.features[1]"),
        (too_many.as_str(), "renderer.features"),
    ] {
        match renderer_features_from_json(bad.as_bytes()) {
            Err(luma_validate::names::TypesError::Row { at: got, .. }) => assert_eq!(got, at, "{bad}"),
            other => panic!("{bad}: {other:?}"),
        }
    }
    assert!(matches!(renderer_features_from_json(b"{"), Err(luma_validate::names::TypesError::Json { .. })));
}

#[test]
fn a_folder_of_names_reads_luma_renderer_json() {
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("renderer-names");
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(dir.join("more")).unwrap();
    std::fs::write(dir.join(RENDERER_FILE), LIST).unwrap();
    let n = Names::from_rows_folder(&dir, 1).unwrap();
    assert_eq!(n.renderer_features.map(|f| f.len()), Some(2));
    // A second one anywhere under the folder is an error, not a choice.
    std::fs::write(dir.join("more").join(RENDERER_FILE), LIST).unwrap();
    assert!(Names::from_rows_folder(&dir, 1).is_err());
    std::fs::remove_file(dir.join("more").join(RENDERER_FILE)).unwrap();
    std::fs::write(dir.join(RENDERER_FILE), r#"{"renderer": {"features": ["a", "a"]}}"#).unwrap();
    assert!(Names::from_rows_folder(&dir, 1).is_err());
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn the_key_s_form_is_checked_with_or_without_the_list() {
    for list in [None, Some(LIST)] {
        for (features, want) in [
            (r#"["ray_tracing"]"#, vec![]),
            ("[]", vec![204]),
            (r#"["Ray"]"#, vec![204]),
            (r#"["a__b"]"#, vec![204]),
            (r#"[5]"#, vec![204]),
            (r#"["ray_tracing", "ray_tracing"]"#, vec![204]),
        ] {
            let r = check(&wants(features), list, None);
            assert_eq!(codes(&r), want, "{list:?} {features}: {:?}", r.findings);
        }
        let r = check(&luma(&[("renderer", r#"["ray_tracing"]"#)], &[]), list, None);
        assert_eq!(codes(&r), vec![204]);
        let r = check(&luma(&[("renderer", "{}")], &[]), list, None);
        assert_eq!(codes(&r), vec![203]);
        let r = check(&luma(&[("renderer", r#"{"features": ["ray_tracing"], "optional": ["mesh_shaders"]}"#)], &[]), list, None);
        assert_eq!(codes(&r), vec![202]);
        // No `renderer` key: nothing said, nothing left unchecked.
        let r = check(&luma(&[], &[]), list, Some(&[]));
        assert!(r.findings.is_empty() && r.not_checked.is_empty(), "{:?} {:?}", r.findings, r.not_checked);
    }
    let r = check(&wants(r#"["Ray"]"#), None, None);
    let text = message(&r.findings[0], "UsEnglish");
    assert!(text.contains("a feature name"), "{text}");
}

#[test]
fn a_name_the_game_does_not_list_is_l204() {
    let r = check(&wants(r#"["ray_tracin"]"#), Some(LIST), Some(&["ray_tracing"]));
    assert_eq!(codes(&r), vec![204]);
    let text = message(&r.findings[0], "UsEnglish");
    assert!(text.contains("ray_tracin") && text.contains("a graphics feature the game's renderer lists"), "{text}");
    assert_eq!(r.findings[0].pointer.as_deref(), Some("/renderer/features/0"));
    // Not L806 as well: one finding per mistake.
    assert!(r.not_checked.is_empty(), "{:?}", r.not_checked);
}

#[test]
fn l806_is_said_for_a_feature_the_device_lacks() {
    let bytes = wants(r#"["mesh_shaders", "ray_tracing"]"#);
    let r = check(&bytes, Some(LIST), Some(&["mesh_shaders"]));
    assert_eq!(codes(&r), vec![806], "{:?}", r.findings);
    let f = &r.findings[0];
    assert_eq!(f.pointer.as_deref(), Some("/renderer/features/1"));
    assert_eq!(f.file, "luma.json");
    assert_eq!(message(f, "UsEnglish"), "test.mod needs ray_tracing, which this device's graphics do not have.");
    assert_eq!(message(f, "JpJapanese"), "test.mod には ray_tracing が必要ですが、このデバイスのグラフィックスにはありません。");
    assert!(r.not_checked.is_empty());
    // An error: the mod is off.
    assert_eq!(luma_validate::level_of(806), luma_validate::Level::Error);
    // Each feature missing is its own finding; a device with every one gives none.
    let r = check(&bytes, Some(LIST), Some(&[]));
    assert_eq!(codes(&r), vec![806, 806]);
    let r = check(&bytes, Some(LIST), Some(&["mesh_shaders", "ray_tracing", "something_newer"]));
    assert!(r.findings.is_empty() && r.not_checked.is_empty(), "{:?} {:?}", r.findings, r.not_checked);
}

#[test]
fn l806_is_not_checked_without_the_list_or_the_device() {
    let bytes = wants(r#"["ray_tracing"]"#);
    // LumaLink: the list, no device.
    let r = check(&bytes, Some(LIST), None);
    assert_eq!((codes(&r), not_checked(&r)), (vec![], vec![(806, "Missing.Device")]));
    // No list (whatever the device says): the rule is not checked, as start-check.md 3 says.
    for device in [None, Some(&[][..]), Some(&["ray_tracing"][..])] {
        let r = check(&bytes, None, device);
        assert_eq!((codes(&r), not_checked(&r)), (vec![], vec![(806, "Missing.RendererFeatures")]), "{device:?}");
    }
    // A key whose names are all wrong leaves nothing for L806.
    let r = check(&wants(r#"["Ray"]"#), None, None);
    assert!(r.not_checked.is_empty(), "{:?}", r.not_checked);
}

#[test]
fn a_start_s_check_reads_the_device_again() {
    let bytes = wants(r#"["ray_tracing"]"#);
    let n = names(Some(LIST));
    let with = vec!["ray_tracing".to_string()];
    let none: Vec<String> = Vec::new();
    let game = |device: &'_ [String]| -> (Version, Option<Vec<String>>) { (Version::parse("2.0.0").unwrap(), Some(device.to_vec())) };
    let (v1, d1) = game(&with);
    let (v2, d2) = game(&none);
    let at_import = Game { nebula: v1, region: Some("RMGE01"), extra_languages: &[], device_features: d1.as_deref(), names: Some(&n), store: None };
    let report = validate(&input(&bytes, &at_import));
    assert!(report.findings.is_empty(), "{:?}", report.findings);
    let kept = Saved::from_report(&report, "key");
    let kept = Saved::parse(&kept.to_bytes()).unwrap();
    // The same game key, another GPU: the manifest's rules run at every start, so L806 is said from the kept report.
    let later = Game { nebula: v2, region: Some("RMGE01"), extra_languages: &[], device_features: d2.as_deref(), names: Some(&n), store: None };
    let r = recheck(&input(&bytes, &later), Some(&kept), "key");
    assert_eq!(codes(&r), vec![806], "{:?}", r.findings);
    // And back.
    let r = recheck(&input(&bytes, &at_import), Some(&Saved::parse(&Saved::from_report(&r, "key").to_bytes()).unwrap()), "key");
    assert!(r.findings.is_empty(), "{:?}", r.findings);
}
