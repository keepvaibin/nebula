//! The importer's shader ids against the game's enum (native/include/nebula_game_api.h, NebulaShaderId).

use nebula_import::cook::pipeline_list::{SHADER_PARTICLE, SHADER_PARTICLE_INSTANCES};

fn game_id(name: &str) -> Option<u64> {
    let header = include_str!("../../../native/include/nebula_game_api.h");
    header.lines().find_map(|line| {
        let (left, right) = line.trim().split_once('=')?;
        if left.trim() != name {
            return None;
        }
        right.split(',').next()?.trim().parse().ok()
    })
}

#[test]
fn particle_shader_ids_are_the_games() {
    assert_eq!(game_id("NEBULA_SHADER_PARTICLE"), Some(SHADER_PARTICLE));
    assert_eq!(game_id("NEBULA_SHADER_PARTICLE_INSTANCES"), Some(SHADER_PARTICLE_INSTANCES));
}
