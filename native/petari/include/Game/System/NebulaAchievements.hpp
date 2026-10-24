#pragma once

#include <revolution/types.h>

// Nebula's achievements (galaxy::achievements has the catalogue): each stage start checks every goal against the save
// data and asks the host to unlock what is met; a newly unlocked one waits for its pop-up (NebulaAchievementToast).
namespace NebulaAchievements {
    void evaluate();  // GameScene::start
    void update();    // GameScene::update: the goals that change mid-stage (lives, Star Bits), once a second

    // The oldest unlocked achievement not yet shown: its name and its picture font code; null when none waits.
    const wchar_t* getWaiting(s32* pIcon);
    // Its pop-up was on screen: the next one waits.
    void popWaiting();
    bool isMoreWaiting();
};  // namespace NebulaAchievements
