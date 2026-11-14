//! The validator on libraries and rows from a real build, when they are given (the other tests need nothing outside
//! the repository). Set LUMA_ROWS to a kernel build's rows folder (`build/<game>/luma`) and LUMA_LIBRARIES to a folder
//! of built mod libraries; each library is checked as a mod's entry library for the platform its format and machine
//! say, and its findings are printed. With LUMA_EXPECT, a file of lines `<library file> <codes, comma-separated or
//! "none">`, the codes found must be those.

use luma_validate::names::Names;
use luma_validate::semver::Version;
use luma_validate::{message, validate, Game, Input, Source};

mod common;
use common::*;

fn platform_of(data: &[u8], file: &str) -> Option<(&'static str, &'static str)> {
    let lower = file.to_lowercase();
    if data.starts_with(b"MZ") {
        let pe = u32::from_le_bytes(data.get(0x3c..0x40)?.try_into().ok()?) as usize;
        let machine = u16::from_le_bytes(data.get(pe + 4..pe + 6)?.try_into().ok()?);
        return Some(if machine == 0xaa64 { ("windows-arm64", "mod.dll") } else { ("windows-x64", "mod.dll") });
    }
    if data.starts_with(b"\x7fELF") {
        let machine = u16::from_le_bytes(data.get(18..20)?.try_into().ok()?);
        let android = lower.contains("android");
        return Some(match (machine, android) {
            (0xb7, true) => ("android-arm64", "mod.so"),
            (0xb7, false) => ("linux-arm64", "mod.so"),
            (_, true) => ("android-x64", "mod.so"),
            _ => ("linux-x64", "mod.so"),
        });
    }
    if data.starts_with(&[0xcf, 0xfa, 0xed, 0xfe]) {
        let cpu = u32::from_le_bytes(data.get(4..8)?.try_into().ok()?);
        return Some(if cpu == 0x0100_000c { ("macos-arm64", "mod.dylib") } else { ("macos-x64", "mod.dylib") });
    }
    None
}

#[test]
fn real_libraries_against_real_rows() {
    let (Ok(rows), Ok(libs)) = (std::env::var("LUMA_ROWS"), std::env::var("LUMA_LIBRARIES")) else {
        eprintln!("LUMA_ROWS and LUMA_LIBRARIES not set: nothing to check");
        return;
    };
    let names = Names::from_rows_folder(std::path::Path::new(&rows), 1).expect("rows");
    let rows_count: usize = names.functions.values().map(Vec::len).sum();
    println!("rows: {} names, {rows_count} rows", names.functions.len());
    let expect: Vec<(String, String)> = std::env::var("LUMA_EXPECT")
        .ok()
        .map(|p| std::fs::read_to_string(p).expect("LUMA_EXPECT"))
        .unwrap_or_default()
        .lines()
        .filter_map(|l| l.split_once(' ').map(|(a, b)| (a.to_string(), b.trim().to_string())))
        .collect();
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: Some(&names), store: None };
    let mut entries: Vec<_> = std::fs::read_dir(&libs).expect("LUMA_LIBRARIES").filter_map(|e| e.ok()).map(|e| e.path()).collect();
    entries.sort();
    let mut wrong = Vec::new();
    for path in entries {
        let file = path.file_name().unwrap().to_string_lossy().to_string();
        let Ok(data) = std::fs::read(&path) else { continue };
        let Some((platform, entry)) = platform_of(&data, &file) else { continue };
        let code = format!(r#"{{"platforms": ["{platform}"]}}"#);
        let bytes = luma(&[("code", code.as_str())], &[file_entry(&format!("code/{platform}/{entry}"), &data)]);
        let platforms = [platform];
        let report = validate(&Input { source: Source::Bytes(&bytes, &file), game: Some(&game), installed: &[], platforms: &platforms, options: None, position: None });
        let mut codes: Vec<String> = report.findings.iter().map(|f| f.code.to_string()).collect();
        codes.dedup();
        let got = if codes.is_empty() { "none".to_string() } else { codes.join(",") };
        println!("{file} as {platform}: {got}");
        for f in &report.findings {
            println!("    {}", message(f, "UsEnglish"));
        }
        let left: Vec<String> = report.not_checked.iter().map(|n| format!("L{} ({})", n.code, n.because)).collect();
        println!("    not checked: {}", left.join(", "));
        if let Some((_, want)) = expect.iter().find(|(f, _)| *f == file) {
            if *want != got {
                wrong.push(format!("{file}: expected {want}, got {got}"));
            }
        }
    }
    assert!(wrong.is_empty(), "\n{}", wrong.join("\n"));
}

fn file_entry(name: &str, data: &[u8]) -> E {
    file(name, data)
}

#[test]
fn speed_of_a_large_luma() {
    // docs/modding/format/validator.md section 7: LUMA_SPEED names a large .luma; the full check and a start's check
    // are timed.
    let Ok(path) = std::env::var("LUMA_SPEED") else {
        eprintln!("LUMA_SPEED not set: nothing to time");
        return;
    };
    let path = std::path::Path::new(&path);
    let game = Game { nebula: Version::parse("2.0.0").unwrap(), region: Some("RMGE01"), extra_languages: &[], device_features: None, names: None, store: None };
    let platforms = ["windows-x64"];
    let input = Input { source: Source::File(path), game: Some(&game), installed: &[], platforms: &platforms, options: None, position: None };
    for round in 0..3 {
        let t = std::time::Instant::now();
        let full = luma_validate::recheck(&input, None, "g");
        let full_time = t.elapsed();
        let saved = luma_validate::Saved::from_report(&full, "g");
        let bytes = saved.to_bytes();
        let t = std::time::Instant::now();
        let back = luma_validate::Saved::parse(&bytes).unwrap();
        let start = luma_validate::recheck(&input, Some(&back), "g");
        let start_time = t.elapsed();
        assert!(full.full && !start.full);
        assert_eq!(start.findings, full.findings);
        println!(
            "round {round}: full {:.3} s, start {:.4} s (kept report {} bytes), {} findings, {} not checked",
            full_time.as_secs_f64(),
            start_time.as_secs_f64(),
            bytes.len(),
            full.findings.len(),
            full.not_checked.len()
        );
    }
}
