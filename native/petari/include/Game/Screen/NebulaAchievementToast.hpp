#pragma once

#ifdef TARGET_PC
#include "Game/Screen/LayoutActor.hpp"

struct NebulaAchievementIcon;

namespace nw4r {
    namespace lyt {
        class Pane;
        class Picture;
        class TexMap;
    };  // namespace lyt
};  // namespace nw4r

// Nebula's achievement pop-up: the game's information window (InformationWindow) in the screen's bottom-right corner,
// rising from below its edge with the achievement's icon, "Achievement Get!" and its name. It shows the achievements
// NebulaAchievements has waiting, one at a time. GameScene runs and draws it only in play (not paused, not on the map,
// not in a star's get sequence or a movie), after every other 2D element, so the HUD never covers it.
class NebulaAchievementToast : public LayoutActor {
public:
    NebulaAchievementToast();

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();

    // GameScene: one frame of play, and the drawing at the end of its 2D.
    void update();
    void drawOnTop() const;

private:
    void loadTextures();
    void start(const wchar_t* pName, const NebulaAchievementIcon* pIcon);
    void setIcon(const NebulaAchievementIcon* pIcon, f32* pWidth, f32* pHeight);
    void place(f32 rise, f32 alpha, f32 iconScale);

    /* 0x20 */ nw4r::lyt::Pane* mToast;
    /* 0x24 */ nw4r::lyt::Pane* mWindow;
    /* 0x28 */ nw4r::lyt::Pane* mText;
    /* 0x2C */ nw4r::lyt::Pane* mGlyph;
    /* 0x30 */ nw4r::lyt::Pane* mIcon;  // holds the glyph or the picture, centred on the icon's place
    /* 0x34 */ nw4r::lyt::Picture* mPicture;
    /* 0x38 */ nw4r::lyt::TexMap** mTextures;  // NebulaAchievements::getIconTexture's order; null when not loaded
    /* 0x3C */ s32 mStep;
    /* 0x40 */ s32 mFrame;
    /* 0x44 */ s32 mHoldFrames;
    /* 0x48 */ f32 mWidth;
    /* 0x4C */ f32 mHeight;
};
#endif
