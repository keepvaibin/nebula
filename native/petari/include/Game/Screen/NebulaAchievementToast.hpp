#pragma once

#ifdef TARGET_PC
#include "Game/Screen/LayoutActor.hpp"

namespace nw4r {
    namespace lyt {
        class Pane;
        class TextBox;
    };  // namespace lyt
};  // namespace nw4r

// Nebula's achievement pop-up: the game's information window (InformationWindow) in the bottom-right corner, sliding up
// from the screen's edge with "Achievement Get!" over the achievement's name and its icon from the game's picture font.
// It announces NebulaAchievements' waiting achievements one at a time, outside cutscenes.
class NebulaAchievementToast : public LayoutActor {
public:
    NebulaAchievementToast();

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();

private:
    void start(const wchar_t* pName, s32 icon);
    void place(f32 rise, f32 alpha);

    /* 0x20 */ nw4r::lyt::Pane* mToast;
    /* 0x24 */ nw4r::lyt::Pane* mWindow;
    /* 0x28 */ nw4r::lyt::Pane* mText;
    /* 0x2C */ nw4r::lyt::Pane* mIcon;
    /* 0x30 */ s32 mStep;
    /* 0x34 */ s32 mFrame;
    /* 0x38 */ s32 mHoldFrames;
    /* 0x3C */ f32 mWidth;
    /* 0x40 */ f32 mHeight;
};
#endif
