#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

class GamePad;

// One moment of a player's controller as the game's GamePad classes read it (GamePadReadDataInfo::read): the buttons of the
// read (every sample of one read has them), the filtered accelerations, the pointer and the Nunchuk stick.
struct GamePadSample {
    u32 mHold;      // NEBULA_BUTTON_*
    u32 mTrigger;
    u32 mRelease;
    bool mRepeat;   // a held button repeats this read (GamePadButton's delay and pulse)
    bool mIsSubPadConnected;
    bool mIsPointerOn;
    TVec3f mAccel;  // the remote's, filtered (GamePadAcceleration's parameters), in g
    TVec3f mSubAccel;
    TVec2f mStick;
    TVec2f mPointerPos;
    f32 mPointerSpeed;
    TVec2f mHorizon;
    f32 mDistance;
};

// A player's samples since the last read (input_read), newest first, and the state they are made from.
class GamePadReadDataInfo {
public:
    GamePadReadDataInfo();

    void read(s32 chan);
    void requestReset();
    void setButtonRepeat(f32 delaySec, f32 pulseSec);
    void setAccelParam(f32 playRadius, f32 sensitivity);
    u32 takeShakePresses(s32 device);
    GamePadSample* getSample(u32) const;
    u32 getValidStatusCount() const;

    /* 0x00 */ GamePadSample* mStatusArray;
    /* 0x04 */ u32 mValidStatusCount;
    NebulaInputDevice mDevice;

private:
    void resetState();
    void updateConnection(s32 chan, const NebulaInputDevice& rDevice);

    GamePadSample mState;        // the newest sample's state, carried into the next read
    bool mResetRequested;
    bool mStateSubPad;        // mState's Nunchuk: a change restarts its filter and stick
    bool mSubResetRequested;
    u8 mShakeHeld;
    u32 mShakePresses[2];     // the remote's and the Nunchuk's, not yet taken (GamePadHVSwing)
    u16 mRepeatTime;          // in 200ths of a second of the setting, one per sample
    u16 mRepeatNext;
    u16 mRepeatDelay;
    u16 mRepeatPulse;
    f32 mAccelPlayRadius;
    f32 mAccelSensitivity;
};

class GamePadHolder {
public:
    GamePadHolder();

    void updateReadDataOnly();
    void updateProjectPadData();
    void updateInGame();
    void update();
    void resetPad();
    GamePad* getGamePad(s32);

    /* 0x00 */ GamePad* mPad[2];
    /* 0x08 */ GamePadReadDataInfo* mReadDataInfoArray;
    /* 0x0C */ u32 mMode;
};

namespace MR {
    GamePad* getGamePad(s32);
    void resetGamePad();
    void setGamePadHolderModeHomeButton();
    void setGamePadHolderModeGame();
    void setAutoSleepTimeWiiRemote(bool);
    // The controller's rumble motor.
    void setGamePadMotor(s32, bool);
};  // namespace MR
