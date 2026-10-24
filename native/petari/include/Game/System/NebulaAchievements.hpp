#pragma once

#include <revolution/types.h>

// An achievement's icon: a picture-font glyph, or a texture from the game's files (a layout archive's TPL in
// LayoutData, or an object archive's BTI in ObjectData), the part [u0, u1] x [v0, v1] of it.
struct NebulaAchievementIcon {
    s32 mGlyph;             // the picture font's code (MR::addPictureFontTag); 0: the texture
    const char* mArchive;   // "MapDomeIcon.arc"
    bool mLayoutArchive;    // LayoutData (else ObjectData)
    const char* mTexture;   // "mymapdome1.tpl", "PictureBookCoverFront.bti"
    f32 mU0, mV0, mU1, mV1;
    f32 mWidth, mHeight;    // the drawn part's proportions
    f32 mFill;              // how much larger it draws than the others (0: the same), for room around the picture
};

// Nebula's achievements (galaxy::achievements has the catalogue): each stage start checks every goal against the save
// data and asks the host to unlock what is met; a newly unlocked one waits for its pop-up (NebulaAchievementToast).
namespace NebulaAchievements {
    void evaluate();  // GameScene::start
    void update();    // GameScene::update: the goals that change mid-stage (lives, Star Bits), once a second

    // The oldest unlocked achievement not yet shown: its name in the played language and its icon; null when none
    // waits.
    const wchar_t* getWaiting(const NebulaAchievementIcon** ppIcon);
    // Its pop-up was on screen: the next one waits.
    void popWaiting();
    bool isMoreWaiting();

    // Tests (GALAXY_TEST_EVENT_FLAGS=Achievement:<id>): its pop-up, whether unlocked or not.
    void showForTest(const char* pId);

    // The catalogue: each achievement's id, group (0 story, 1 challenge, 2 extra) and icon.
    s32 getNum();
    const char* getId(s32 index);
    s32 getGroup(s32 index);
    const NebulaAchievementIcon* getIcon(s32 index);

    // The textures the icons use, each once (the pop-up loads them when its scene starts).
    s32 getIconTextureNum();
    const NebulaAchievementIcon* getIconTexture(s32 index);
    s32 findIconTexture(const NebulaAchievementIcon* pIcon);
};  // namespace NebulaAchievements
