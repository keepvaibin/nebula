#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

class PlayerInput;

// One moment of a player's controller as the game's PlayerInput classes read it (PlayerInputReader::read): the buttons of the
// read (every sample of one read has them), the filtered accelerations, the pointer and the Nunchuk stick.
struct PlayerInputSample {
    u32 mHold;      // NEBULA_BUTTON_*
    u32 mTrigger;
    u32 mRelease;
    bool mHasMoveStick;
    bool mIsPointerOn;
    TVec3f mAccel;  // the main hand's, filtered (InputTilt's parameters), in g
    TVec3f mSubTilt;
    TVec2f mStick;
    TVec2f mPointerPos;
    TVec2f mHorizon;
    f32 mDistance;
};

// A player's samples since the last read (input_read), newest first, and the state they are made from.
class PlayerInputReader {
public:
    PlayerInputReader();

    void read(s32 chan);
    void requestReset();
    u32 takeShakePresses(s32 device);
    PlayerInputSample* getSample(u32) const;
    u32 getValidStatusCount() const;

    /* 0x00 */ PlayerInputSample* mStatusArray;
    /* 0x04 */ u32 mValidStatusCount;
    NebulaInputDevice mDevice;

private:
    void resetState();
    void updateConnection(s32 chan, const NebulaInputDevice& rDevice);
    void watchActivity(const NebulaInputSample& rSample, bool subPad);

    PlayerInputSample mState;        // the newest sample's state, carried into the next read
    bool mResetRequested;
    bool mStateMoveStick;        // mState's move stick: a change restarts its filter and stick
    bool mSubHandResetRequested;
    u8 mShakeHeld;
    u32 mShakePresses[2];     // the main hand's and the sub hand's, not yet taken (InputSwing)
    NebulaInputSample mActivitySample;  // the previous sample, which activity is measured against
    u16 mAccelSettle;                   // samples the accelerometer's change has held
    u16 mStickSettle;
};

class PlayerInputs {
public:
    PlayerInputs();

    void updateReadDataOnly();
    void updateProjectPadData();
    void updateInGame();
    void update();
    void resetPad();
    PlayerInput* getPlayerInput(s32);

    /* 0x00 */ PlayerInput* mPad[2];
    /* 0x08 */ PlayerInputReader* mReadDataInfoArray;
    /* 0x0C */ u32 mMode;
};

namespace MR {
    PlayerInput* getPlayerInput(s32);
    void resetPlayerInput();
    void setPlayerInputsModeHome();
    void setPlayerInputsModeGame();
    void setInputSleepTime(bool);
    // The controller's rumble motor.
    void setInputRumble(s32, bool);
};  // namespace MR
