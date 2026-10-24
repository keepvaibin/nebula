#pragma once

#ifdef TARGET_PC
#include "Game/Screen/LayoutActor.hpp"

class GalaxyMapBackground;
struct NebulaBoardParts;

// Nebula's achievement board, opened by the Observatory's Achievement Luma: the galaxy map's starry backdrop with a
// board of tabs over it (all, story, challenges, extras, stats). Each achievement has a card with its icon, its name,
// what earns it, and the day it was unlocked or the file's progress toward it; the stats tab has the save file's
// numbers and Nebula's all-time counts (jumps, lives lost, time played...). Built from the game's own windows, font and
// icons (NebulaPaneKit) and laid out for the screen's shape every frame, 1:1 to 32:9. The pointer, the D-pad and the
// stick all work: - and + change tabs, B or Back closes it. GameScene runs and draws it in its own state,
// the world held.
class NebulaAchievementBoard : public LayoutActor {
public:
    NebulaAchievementBoard();

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();

    void open();
    bool isOpen() const;
    // GameScene: one frame, and the drawing at the end of its 2D.
    void update();
    void drawOnTop() const;

    void exeAppear();
    void exeWait();
    void exeDisappear();

private:
    /* 0x20 */ NebulaBoardParts* mParts;
    /* 0x24 */ GalaxyMapBackground* mBackground;
    /* 0x28 */ bool mIsOpen;
};
#endif
