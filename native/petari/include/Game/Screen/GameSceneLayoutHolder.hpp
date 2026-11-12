#pragma once

#include "Game/NameObj/NameObj.hpp"

class CameraInfo;
class CounterLayoutController;
class InformationMessage;
class MarioMeter;
class MarioSubMeter;
class MissLayout;
class NebulaAchievementBoard;
class NebulaAchievementToast;
class NoteCounter;
class OneUpBoard;
class PurpleCoinCounter;
class YesNoLayout;

class GameSceneLayoutHolder : public NameObj {
public:
    /// @brief Creates a new `GameSceneLayoutHolder`.
    GameSceneLayoutHolder();

    virtual void movement() override;

    void createPurpleCoinCounter();
    PurpleCoinCounter* getPurpleCoinCounter() const;
    void initLifeCount(u32);
    void setLifeCount(u32);
    void setOxygenRatio(f32);
    void setBeePowerRatio(f32);
    void changeLifeMeterModeGround();
    void changeLifeMeterModeWater();
    void changeLifeMeterModeBee();
    void requestOneUp(s32);
    void requestPowerUpHPMeter();
    void requestForceAppearHPMeter();
    void startMiss();
    bool isMissAnimEnd() const;
    void activateDefaultGameLayout();
    void deactivateDefaultGameLayout();
    void invalidateStatusLayout();

    CameraInfo* mCameraInfo;
    InformationMessage* mInformationMessage;
    MarioMeter* mMarioMeter;
    MarioSubMeter* mMarioSubMeter;
    MissLayout* mMissLayout;
    NoteCounter* mNoteCounter;
    OneUpBoard* mOneUpBoard;
    YesNoLayout* mYesNoLayout;
    PurpleCoinCounter* mPurpleCoinCounter;
    bool mIsStatusInvalidatePending;  // movement() invalidates the status layout once
    CounterLayoutController* mCounterLayoutCtrl;
    bool mIsActiveDefaultGameLayout;
    // Nebula's achievement pop-up: GameScene runs and draws it (only in play, above everything else in 2D).
    NebulaAchievementToast* mAchievementToast;
    // Plugins' lines of text over the game (draw_text), drawn with the pop-up; null while no mod's code runs.
    // Nebula's achievement board, in the Observatory (its Luma opens it); null elsewhere.
    NebulaAchievementBoard* mAchievementBoard;
};

namespace MR {
    GameSceneLayoutHolder* getGameSceneLayoutHolder();
};  // namespace MR
