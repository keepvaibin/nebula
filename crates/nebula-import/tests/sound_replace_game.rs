//! A mod's sound effect replacing a game sound by name (docs/modding/content/sound.md section 6), against a real
//! imported game: LUMA_GAME_PAK=<game.pak> and --ignored. A name the game has cooks with the name in the file; one it
//! lacks is refused by name.

use nebula_import::cook::overlay::cook_source;
use nebula_import::cook::{refuse, sound, store};

#[test]
#[ignore = "needs an imported game: LUMA_GAME_PAK"]
fn a_sound_replaces_a_game_sound_the_game_has() {
    let pak = std::env::var("LUMA_GAME_PAK").expect("LUMA_GAME_PAK names a game.pak");
    let pak = std::path::Path::new(&pak);
    let names = sound::sound_names(&store::read_file(pak, "AudioRes/SMR.szs").unwrap()).unwrap();
    assert_eq!(names.len(), 3487);
    let dir = tempfile::tempdir().unwrap();
    let mut wav = b"RIFF\0\0\0\0WAVEfmt ".to_vec();
    for v in [16u32, 0x0001_0001, 48000, 96000, 0x0010_0002] {
        wav.extend_from_slice(&v.to_le_bytes());
    }
    wav.extend_from_slice(b"data");
    wav.extend_from_slice(&200u32.to_le_bytes());
    wav.resize(wav.len() + 200, 0);
    std::fs::write(dir.path().join("jump.wav"), &wav).unwrap();
    let source = dir.path().join("Jump.sound.json");
    let path = "Luma/my.mod/sfx/Jump.sound.json";
    let good = br#"{"kind":"effect","file":"jump.wav","replace":"SE_PM_JUMP_M"}"#;
    assert_eq!(cook_source(path, &source, good, Some(pak), None).unwrap(), 1);
    let facts = refuse::take_facts();
    assert!(facts.iter().any(|f| matches!(f, refuse::CookFact::Sound { facts, .. } if facts.replaces == "SE_PM_JUMP_M")), "{facts:?}");
    let bad = br#"{"kind":"effect","file":"jump.wav","replace":"SE_PM_JUMP_XL"}"#;
    let error = cook_source(path, &source, bad, Some(pak), None).unwrap_err();
    let finding = refuse::refusal_of(&error).expect("a refusal");
    let words = luma_validate::reason(finding, "UsEnglish");
    assert!(words.contains("SE_PM_JUMP_XL") && words.contains("/replace"), "{words}");
}
