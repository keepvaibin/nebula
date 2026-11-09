#pragma once

#include "Game/Screen/LayoutActor.hpp"

struct NebulaAchievementIcon;

namespace Ui {
    class Pane;
    class Picture;
    class TexMap;
}  // namespace Ui

// Nebula's achievement pop-up: the game's information window (InformationWindow) in the screen's bottom-right corner,
// rising from below its edge with the achievement's icon, "Achievement Get!" and its name. It shows the achievements
// NebulaAchievements has waiting, one at a time. GameScene runs and draws it only in play (not paused, not on the map,
// not in a star's get sequence or a movie), after every other 2D element, so the HUD never covers it.
class NebulaAchievementToast : public LayoutActor {
public:
    NebulaAchievementToast();

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;

    // GameScene: one frame of play, and the drawing at the end of its 2D.
    void update();
    void drawOnTop() const;

private:
    void loadTextures();
    void start(const char16_t* pName, const NebulaAchievementIcon* pIcon);
    void setIcon(const NebulaAchievementIcon* pIcon, f32* pWidth, f32* pHeight);
    void place(f32 rise, f32 alpha, f32 iconScale);

    Ui::Pane* mToast;
    Ui::Pane* mWindow;
    Ui::Pane* mText;
    Ui::Pane* mGlyph;
    Ui::Pane* mIcon;  // holds the glyph or the picture, centred on the icon's place
    Ui::Picture* mPicture;
    Ui::TexMap** mTextures;  // NebulaAchievements::getIconTexture's order; null when not loaded
    s32 mStep;
    s32 mFrame;
    s32 mHoldFrames;
    f32 mWidth;
    f32 mHeight;
};
