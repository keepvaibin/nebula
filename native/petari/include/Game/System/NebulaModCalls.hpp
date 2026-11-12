#pragma once

#include <nebula_mod_api.h>
#include "Game/Util.hpp"

// Calls that reach into the game's save, story and scene, exported by name (the test driver calls them), and
// what the settings' galaxy select asks of the scene with them.
extern "C" {
int32_t nebula_mod_give_star(const char* pGalaxy, int32_t star, int32_t grand);
int32_t nebula_mod_give_grand_stars(int32_t count);
int32_t nebula_mod_set_player_character(int32_t luigi);
int32_t nebula_mod_list_materials(const char* pModel, NebulaMaterialCallback callback, void* pUser);
int32_t nebula_mod_water_point(int32_t index, float* pXyz);
int32_t nebula_mod_circle_wipe(int32_t open, int32_t frames);
int32_t nebula_mod_grant_spin_and_pointer(void);
}

namespace NebulaModCalls {
    // GameScene::update, each frame: a star given waits until the stage is in play (`inPlay`: its SceneAction nerve).
    void update(bool inPlay);
    // The save's Grand Stars 1 to `count`, and the stage `pStage` (null: the one played, its mission) built again with
    // them.
    void setGrandStars(s32 count, const char* pStage);
    // A stage change the game keeps the name of.
    void changeStage(const char* pStage, s32 scenario);
}  // namespace NebulaModCalls
