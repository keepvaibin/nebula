//! The save corpus (tests/data/saves, made by make.py): the validator's port of the game's save check gives the answer
//! expected.txt names for every file, the answer the game's own check gives (its runner reads the same files), and
//! no changed byte of a good save makes it panic.

use luma_validate::save_problem;

/// The game's NebulaSaveFormat::Error name for the validator's phrase.
fn answer(phrase: Option<&str>) -> &str {
    match phrase {
        None => "None",
        Some("Save.Short") => "Short",
        Some("Save.Encrypted") => "EncryptedExport",
        Some("Save.Version") => "Version",
        Some("Save.Length") => "Length",
        Some("Save.Table") => "Table",
        Some("Save.Chunk") => "Chunk",
        Some("Save.Checksum") => "Checksum",
        Some(other) => other,
    }
}

fn corpus() -> Vec<(String, Vec<u8>, String)> {
    let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/data/saves");
    let expected = std::fs::read_to_string(dir.join("expected.txt")).expect("expected.txt (run make.py)");
    expected
        .lines()
        .map(|l| {
            let (file, want) = l.split_once(' ').unwrap();
            (file.to_string(), std::fs::read(dir.join(file)).unwrap(), want.to_string())
        })
        .collect()
}

#[test]
fn every_save_of_the_corpus_gets_the_game_s_answer() {
    let corpus = corpus();
    assert_eq!(corpus.len(), 66);
    let mut wrong = Vec::new();
    for (file, data, want) in &corpus {
        let got = answer(save_problem(data));
        if got != want {
            wrong.push(format!("{file}: expected {want}, got {got}"));
        }
    }
    assert!(wrong.is_empty(), "\n{}", wrong.join("\n"));
}

#[test]
fn no_changed_byte_of_a_good_save_makes_it_panic() {
    for (_, data, want) in corpus() {
        if want != "None" {
            continue;
        }
        for at in 0..data.len() {
            for v in [0x00u8, 0xff, data[at] ^ 0x80] {
                let mut bad = data.clone();
                bad[at] = v;
                let _ = save_problem(&bad);
            }
        }
        for cut in 0..data.len() {
            let _ = save_problem(&data[..cut]);
        }
    }
}
