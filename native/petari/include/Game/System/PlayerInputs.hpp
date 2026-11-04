#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

class PlayerInput;

// A player's controller this frame, as the game's PlayerInput classes read it.
struct PlayerInputSample {
    u32 mHold;      // NEBULA_BUTTON_*
    u32 mTrigger;
    u32 mRelease;
    bool mHasMoveStick;
    bool mIsPointerOn;
    TVec3f mAccel;  // the main hand's tilt, in g
    TVec3f mSubTilt;
    TVec2f mStick;
    TVec2f mPointerPos;
    TVec2f mHorizon;
    f32 mDistance;
};

class PlayerInputReader {
public:
    PlayerInputReader();

    void read(s32 chan);
    void requestReset();
    u32 takeSpinPresses(s32 hand);
    PlayerInputSample* getSample(u32);

    NebulaInputDevice mDevice;

private:
    void resetState();
    void updateConnection(s32 chan, const NebulaInputDevice& rDevice);

    PlayerInputSample mState;
    bool mValid;
    bool mResetRequested;
    u32 mSpinPresses[2];  // the main hand's and the sub hand's, not yet taken (InputSwing)
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
