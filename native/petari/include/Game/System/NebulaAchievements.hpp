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

// The save file's numbers: the achievement board's progress bars and its file page.
struct NebulaFileProgress {
    bool mMario;
    s32 mPowerStars, mPowerStarsMax;
    s32 mStarBits;  // in the Observatory's bank
    s32 mLives;
    s32 mChapters;  // storybook chapters read
    s32 mComets, mCometsMax;  // Prankster Comet stars, all kinds
    s32 mDaredevil, mDaredevilMax;
    s32 mCosmic, mCosmicMax;
    s32 mSpeedy, mSpeedyMax;
    s32 mPurpleCoins, mPurpleCoinsMax;
    s32 mHidden, mHiddenMax;
    s32 mGreenStars;  // of 3
    s32 mTrials;      // of 3
    s32 mLumasFed, mLumasFedMax;  // Hungry Lumas that became a galaxy
};

// Nebula's all-time counters across save files (the host keeps them: galaxy::stats).
enum NebulaStat {
    NebulaStat_Jumps,
    NebulaStat_Spins,
    NebulaStat_Coins,
    NebulaStat_StarBits,
    NebulaStat_LivesLost,
    NebulaStat_GameOvers,
    NebulaStat_OneUps,
    NebulaStat_PlaySeconds,
    NebulaStat_Num
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
    // Its name in the played language: Achievement.<id>, or Achievement.<id>.Luigi on a Luigi file when there is one.
    s32 getName(wchar_t* pDst, s32 capacity, const char* pId);
    s32 getGroup(s32 index);
    // Whether the board lists it while locked: a secret never; the 120-star goals once Halfway There is unlocked.
    bool isShownLocked(s32 index);
    // GameScene: a Grand Star touched (Cosmic Immortal: the rematch with Bowser).
    void onGrandStarGet();
    const NebulaAchievementIcon* getIcon(s32 index);

    // The textures the icons use, each once (the pop-up loads them when its scene starts).
    s32 getIconTextureNum();
    const NebulaAchievementIcon* getIconTexture(s32 index);
    s32 findIconTexture(const NebulaAchievementIcon* pIcon);

    // The day it was unlocked (year * 10000 + month * 100 + day, the player's time zone); 0 while locked; 1 unlocked
    // on a day that cannot be read.
    s32 getUnlockDate(s32 index);
    void getFileProgress(NebulaFileProgress* pProgress);
    // The file's count toward a goal that has one (60 Power Stars, every Prankster Comet...); false otherwise.
    bool getProgress(s32 index, const NebulaFileProgress& rFile, s32* pCurrent, s32* pGoal);

    // Counted as they happen; handed to the host every few seconds and when the board opens.
    void countStat(s32 stat, s32 num);
    void flushStats();
    u64 getStat(s32 stat);
};  // namespace NebulaAchievements
