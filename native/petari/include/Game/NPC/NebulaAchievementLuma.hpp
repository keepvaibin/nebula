#pragma once

#ifdef TARGET_PC
#include "Game/NPC/Tico.hpp"

// Nebula's Achievement Luma: a Luma on the Observatory's walkway, a little ahead of where Mario arrives and to the
// left, from the time the Gateway's Grand Star is won. It talks as the Observatory's Lumas do (its words are
// NebulaText's LumaGreeting and LumaBoard, through the game's own talk balloon) and, when the talk ends, opens the
// achievement board.
class NebulaAchievementLuma : public Tico {
public:
    NebulaAchievementLuma();

    // Whether this stage has the Luma (and so the board): the Observatory, once the Gateway is won.
    static bool isHere();
    // GameScene, after the stage's own actors are placed: the Luma, when isHere.
    static void tryCreate();

    virtual void init(const JMapInfoIter& rIter);

    void exeNebulaWait();
    void exeNebulaOpen();
};
#endif
