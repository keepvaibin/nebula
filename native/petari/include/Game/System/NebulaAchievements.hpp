#pragma once

#include <revolution/types.h>

// Nebula's achievements (galaxy::achievements has the catalogue): each stage start checks every goal against the save
// data and asks the host to unlock what is met; a newly unlocked one is announced in the game's own information banner
// once the player is free to read it (no cutscene, no pause, the banner unused), for three seconds.
namespace NebulaAchievements {
    void evaluate();  // GameScene::start
    void update();    // GameScene::update
};  // namespace NebulaAchievements
