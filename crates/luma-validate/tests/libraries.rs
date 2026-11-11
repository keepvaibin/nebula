//! The rules on a mod's libraries (L502-L512), on real libraries: `data/lib.c` built by `data/build.sh` for each
//! format the game loads (PE x64 and arm64; ELF x64 with RELA and with RELR, arm64; Mach-O arm64 with rebase opcodes
//! and with chained fixups, x64). A correct library passes on every one, which proves `luma_mod` and every import row
//! are read through the format's relocations; each broken variant gives its finding.

use luma_validate::names::Names;
use luma_validate::semver::Version;
use luma_validate::{message, validate, Game, Input, Report, Source};

mod common;
use common::*;

fn lib(path: &str) -> Vec<u8> {
    let full = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/data").join(path);
    std::fs::read(&full).unwrap_or_else(|e| panic!("{}: {e} (run tests/data/build.sh)", full.display()))
}

/// The game's names the libraries ask for, as the kernel's `.rows` files give them.
fn names() -> Names {
    let mut n = Names { api: 1, ..Names::default() };
    n.add_rows("MR::getPlayerPos\tVec3f *()\thook\t1\nGameSystem::update\tvoid()\thook\t2\nLiveActor::LiveActor\t(const char *)\ttell\t3\n")
        .unwrap();
    n
}

fn entry_name(platform: &str) -> &'static str {
    if platform.starts_with("windows") {
        "mod.dll"
    } else if platform.starts_with("macos") || platform.starts_with("ios") {
        "mod.dylib"
    } else {
        "mod.so"
    }
}

/// A mod with `entry` as its library for `platform`, and extra libraries listed under code.libraries.
fn code_mod(platform: &str, entry: &[u8], extras: &[(&str, Vec<u8>)], native: bool) -> Vec<u8> {
    let mut code = format!(r#"{{"platforms": ["{platform}"]"#);
    if !extras.is_empty() {
        let list: Vec<String> = extras.iter().map(|(n, _)| format!("\"{n}\"")).collect();
        code += &format!(r#", "libraries": {{"{platform}": [{}]}}"#, list.join(", "));
    }
    code += "}";
    let mut over = vec![("code", code.as_str())];
    if native {
        over.push(("permissions", r#"[{"name": "native", "why": "Loads its own helper library."}]"#));
    }
    let mut files = vec![file(&format!("code/{platform}/{}", entry_name(platform)), entry)];
    for (n, data) in extras {
        files.push(deflated(&format!("code/{platform}/{n}"), data));
    }
    luma(&over, &files)
}

fn check(bytes: &[u8], platform: &str, with_names: bool) -> Report {
    let names = names();
    let game = Game {
        nebula: Version::parse("2.0.0").unwrap(),
        region: Some("RMGE01"),
        extra_languages: &[],
        names: with_names.then_some(&names),
        store: None,
    };
    let platforms = [platform];
    let input = Input { source: Source::Bytes(bytes, "test.luma"), game: Some(&game), installed: &[], platforms: &platforms, options: None, position: None };
    let report = validate(&input);
    // The same input gives the same report, and every message is whole in every language.
    let again = validate(&input);
    assert_eq!(format!("{:?}", report.findings), format!("{:?}", again.findings));
    for f in &report.findings {
        assert_eq!(unfilled(f), None);
    }
    report
}

fn codes(r: &Report) -> Vec<u16> {
    r.findings.iter().map(|f| f.code).collect()
}

fn english(r: &Report, code: u16) -> String {
    let f = r.findings.iter().find(|f| f.code == code).unwrap_or_else(|| panic!("L{code} not given: {:?}", codes(r)));
    message(f, "UsEnglish")
}

/// Every built form of the correct library, with the platform that loads it.
const GOOD: [(&str, &str); 9] = [
    ("pe-x64/good.dll", "windows-x64"),
    ("pe-arm64/good.dll", "windows-arm64"),
    ("elf-x64/good.so", "linux-x64"),
    ("elf-x64-relr/good.so", "linux-x64"),
    ("elf-arm64/good.so", "linux-arm64"),
    ("elf-arm64/good.so", "android-arm64"),
    ("macho-arm64/good.dylib", "macos-arm64"),
    ("macho-arm64-chained/good.dylib", "macos-arm64"),
    ("macho-x64/good.dylib", "macos-x64"),
];

#[test]
fn a_correct_library_passes_in_every_format() {
    for (path, platform) in GOOD {
        let report = check(&code_mod(platform, &lib(path), &[], false), platform, true);
        assert!(report.findings.is_empty(), "{path} as {platform}: {:?}", report.findings);
        // Services are asked for at run time; nothing else is left unchecked.
        let left: Vec<u16> = report.not_checked.iter().map(|n| n.code).collect();
        assert_eq!(left, vec![506], "{path} as {platform}");
    }
}

#[test]
fn every_import_row_is_read_in_every_format() {
    // A name the game lacks is named from the row read through the format's relocations: the row's name and
    // signature pointers resolve, in the rebase-opcode, chained, RELA and RELR forms alike.
    for (path, platform) in [
        ("pe-x64/missing.dll", "windows-x64"),
        ("elf-x64/missing.so", "linux-x64"),
        ("macho-arm64/missing.dylib", "macos-arm64"),
    ] {
        let report = check(&code_mod(platform, &lib(path), &[], false), platform, true);
        assert_eq!(codes(&report), vec![505], "{path}");
        assert_eq!(
            english(&report, 505),
            format!("test.mod: code/{platform}/{}: the game has no function \"MR::noSuchFunction\" with the signature \"void()\".", entry_name(platform)),
            "{path}"
        );
    }
}

#[test]
fn the_variants_that_load_pass() {
    for v in ["optional_missing", "constructor_after", "no_imports"] {
        let report = check(&code_mod("windows-x64", &lib(&format!("pe-x64/{v}.dll")), &[], false), "windows-x64", true);
        assert!(report.findings.is_empty(), "{v}: {:?}", report.findings);
    }
}

#[test]
fn each_broken_library_gives_its_finding() {
    let w = "windows-x64";
    let one = |v: &str| check(&code_mod(w, &lib(&format!("pe-x64/{v}.dll")), &[], false), w, true);

    let r = one("wrong_signature");
    assert_eq!(codes(&r), vec![505]);
    assert!(english(&r, 505).contains("\"int()\""), "{}", english(&r, 505));

    let r = one("constructor_wrap");
    assert_eq!(codes(&r), vec![512]);
    assert!(english(&r, 512).contains("LiveActor::LiveActor"), "{}", english(&r, 512));

    let r = one("bad_row");
    assert_eq!(codes(&r), vec![512]);

    let r = one("short_info");
    assert_eq!(codes(&r), vec![512]);

    let r = one("newer");
    assert_eq!(codes(&r), vec![508]);
    assert!(english(&r, 508).contains('2') && english(&r, 508).contains('1'), "{}", english(&r, 508));

    let r = one("no_export");
    assert_eq!(codes(&r), vec![503]);
    let r = check(&code_mod("linux-x64", &lib("elf-x64/no_export.so"), &[], false), "linux-x64", true);
    assert_eq!(codes(&r), vec![503]);

    // Not a library of the platform: a program, another machine, another format, no format at all.
    for (bytes, why) in [
        (lib("pe-x64/program.exe"), "program"),
        (lib("pe-arm64/good.dll"), "arm64 in x64"),
        (lib("elf-x64/good.so"), "ELF on Windows"),
        (b"not a library at all".to_vec(), "text"),
    ] {
        let r = check(&code_mod(w, &bytes, &[], false), w, true);
        assert_eq!(codes(&r), vec![502], "{why}");
    }
}

#[test]
fn a_library_the_system_runs_code_from_at_load_is_refused() {
    // Loader contract D2-6: the system's loader would run it before LumaCore's `begin`, so a C++ constructor there
    // finds no core. Each form names what the format calls it.
    for (path, platform, what) in [
        ("pe-x64/runs_at_load.dll", "windows-x64", "AddressOfEntryPoint"),
        ("elf-x64/runs_at_load.so", "linux-x64", "DT_INIT_ARRAY"),
        ("macho-arm64/runs_at_load.dylib", "macos-arm64", "__DATA_CONST,__mod_init_func"),
        // macOS 13 and later: the linker turns the constructor pointers into 32-bit offsets.
        ("macho-arm64-chained/runs_at_load.dylib", "macos-arm64", "__TEXT,__init_offsets"),
    ] {
        let r = check(&code_mod(platform, &lib(path), &[], false), platform, true);
        assert_eq!(codes(&r), vec![509], "{path}: {:?}", r.findings);
        let text = english(&r, 509);
        assert!(text.contains(what) && text.contains("luma.h"), "{path}: {text}");
        assert!(text.starts_with(&format!("test.mod: code/{platform}/{}: the system would run its ", entry_name(platform))), "{text}");
    }
}

#[test]
fn a_library_the_system_runs_code_from_when_it_closes_it_is_refused() {
    // F's audit of R3-A, A-1: the kernel closes the library after `finish`, when the mod's memory is freed.
    for (path, platform, what) in [
        ("elf-x64/runs_at_close.so", "linux-x64", "DT_FINI_ARRAY"),
        ("elf-x64/fini.so", "linux-x64", "DT_FINI"),
        ("macho-arm64/runs_at_close.dylib", "macos-arm64", "__mod_term_func"),
    ] {
        let r = check(&code_mod(platform, &lib(path), &[], false), platform, true);
        assert_eq!(codes(&r), vec![509], "{path}: {:?}", r.findings);
        let text = english(&r, 509);
        assert!(text.contains(what) && text.contains("when it closes it") && text.contains("luma.h"), "{path}: {text}");
    }
}

#[test]
fn tls_data_alone_loads_and_a_tls_callback_is_refused() {
    // A-3: the directory alone runs nothing; its callbacks run at load and close.
    let w = "windows-x64";
    let r = check(&code_mod(w, &lib("pe-x64/tls_data.dll"), &[], false), w, true);
    assert!(r.findings.is_empty(), "{:?}", r.findings);
    let r = check(&code_mod(w, &lib("pe-x64/tls_callback.dll"), &[], false), w, true);
    assert_eq!(codes(&r), vec![509], "{:?}", r.findings);
    let text = english(&r, 509);
    assert!(text.contains("TLS callbacks") && text.contains("when it loads it"), "{text}");
}

#[test]
fn a_mach_o_segment_with_more_sections_than_it_holds_is_not_read_in_part() {
    // A-2: a segment command whose sections run past its size is not a readable library (no partial read).
    let mut d = lib("macho-arm64/good.dylib");
    let u32_at = |d: &[u8], at: usize| u32::from_le_bytes(d[at..at + 4].try_into().unwrap());
    let mut at = 32;
    let mut done = false;
    for _ in 0..u32_at(&d, 16) {
        let (cmd, size) = (u32_at(&d, at), u32_at(&d, at + 4) as usize);
        if cmd == 0x19 && u32_at(&d, at + 64) > 0 {
            let more = ((size - 72) / 80 + 1) as u32;
            d[at + 64..at + 68].copy_from_slice(&more.to_le_bytes());
            done = true;
            break;
        }
        at += size;
    }
    assert!(done);
    let m = "macos-arm64";
    let r = check(&code_mod(m, &d, &[], false), m, true);
    assert_eq!(codes(&r), vec![502], "{:?}", r.findings);
}

#[test]
fn without_the_game_s_names_the_name_rules_say_they_were_not_checked() {
    let report = check(&code_mod("windows-x64", &lib("pe-x64/good.dll"), &[], false), "windows-x64", false);
    assert!(report.findings.is_empty(), "{:?}", report.findings);
    let left: Vec<u16> = report.not_checked.iter().map(|n| n.code).collect();
    assert_eq!(left, vec![505, 506, 508]);
}

#[test]
fn a_library_that_needs_another_names_it_unless_the_mod_ships_it() {
    // Windows names the library a symbol comes from; ELF names only the libraries needed.
    for (platform, entry, extra) in [("windows-x64", "pe-x64/uses_extra.dll", "pe-x64/extra.dll"), ("linux-x64", "elf-x64/uses_extra.so", "elf-x64/extra.so")] {
        let extra_name = extra.rsplit('/').next().unwrap();
        let r = check(&code_mod(platform, &lib(entry), &[], false), platform, true);
        assert!(!r.findings.is_empty() && codes(&r).iter().all(|&c| c == 504), "{entry}: {:?}", r.findings);
        assert!(r.findings.iter().any(|f| message(f, "UsEnglish").contains(extra_name)), "{entry}: {:?}", r.findings);
        assert!(r.findings.iter().any(|f| message(f, "UsEnglish").contains("extra_value")), "{entry}: {:?}", r.findings);

        // Shipped as an extra library, with the native permission: nothing to say.
        let r = check(&code_mod(platform, &lib(entry), &[(extra_name, lib(extra))], true), platform, true);
        assert!(r.findings.is_empty(), "{entry} with {extra_name}: {:?}", r.findings);

        // Shipped without the permission: L509.
        let r = check(&code_mod(platform, &lib(entry), &[(extra_name, lib(extra))], false), platform, true);
        assert_eq!(codes(&r), vec![509], "{entry}");
    }
}

#[test]
fn an_extra_library_lists_what_it_needs() {
    let w = "windows-x64";
    let r = check(&code_mod(w, &lib("pe-x64/uses_extra.dll"), &[("helper.dll", lib("pe-x64/helper.dll")), ("extra.dll", lib("pe-x64/extra.dll"))], true), w, true);
    assert_eq!(codes(&r), vec![510], "{:?}", r.findings);
    let text = english(&r, 510);
    assert!(text.contains("helper.dll") && text.contains("extra.dll") && text.contains("extra_value"), "{text}");
}

#[test]
fn no_damaged_byte_of_a_library_makes_it_panic() {
    // Every byte of each form's headers and luma_mod changed three ways; the validator reports or passes, never
    // panics or loops.
    for (path, platform) in [GOOD[0], GOOD[2], GOOD[3], GOOD[6], GOOD[7]] {
        let good = lib(path);
        let span = good.len().min(4096);
        for at in 0..span {
            for v in [0x00u8, 0xff, good[at] ^ 0x80] {
                let mut bad = good.clone();
                bad[at] = v;
                let _ = check(&code_mod(platform, &bad, &[], false), platform, true);
            }
        }
        for cut in (0..good.len()).step_by(7) {
            let _ = check(&code_mod(platform, &good[..cut], &[], false), platform, true);
        }
    }
}

#[test]
fn two_mods_replacing_one_function_refuse_the_later() {
    let w = "windows-x64";
    let bytes = code_mod(w, &lib("pe-x64/replace.dll"), &[], false);
    let alone = check(&bytes, w, true);
    assert!(alone.findings.is_empty(), "{:?}", alone.findings);
    assert_eq!(alone.touches.functions, vec!["GameSystem::update\u{1f}void()".to_string()]);

    let names = names();
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], names: Some(&names), store: None };
    let other = luma_validate::Installed {
        id: "other.mod".into(),
        version: Version::parse("1.0.0").unwrap(),
        on: true,
        file: "other.luma".into(),
        conflicts: Vec::new(),
        touches: alone.touches.clone(),
    };
    let installed = [other];
    let run = |position: Option<usize>, on: bool| {
        let mut list = installed.clone();
        list[0].on = on;
        let platforms = [w];
        let r = validate(&Input { source: Source::Bytes(&bytes, "test.luma"), game: Some(&game), installed: &list, platforms: &platforms, options: None, position });
        r.findings.iter().map(|f| (f.code, message(f, "UsEnglish"))).collect::<Vec<_>>()
    };
    // Below the other in the list: refused, naming the function and the other mod.
    assert_eq!(
        run(None, true),
        vec![(805, "test.mod: code/windows-x64/mod.dll replaces \"GameSystem::update\" (void()), which other.mod replaces already; only one mod may replace a function.".to_string())]
    );
    // At the top, or the other off: nothing (the other mod's own check names the clash).
    assert!(run(Some(0), true).is_empty());
    assert!(run(None, false).is_empty());
}
