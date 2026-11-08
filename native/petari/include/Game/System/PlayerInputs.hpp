#pragma once

#include <nebula_geometry.hpp>
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
    Vec3f mAccel;  // the main hand's tilt, in g
    Vec3f mSubTilt;
    Vec2f mStick;
    Vec2f mPointerPos;
    Vec2f mHorizon;
    f32 mDistance;
};

class PlayerInputReader {
public:
    PlayerInputReader();

    void read(s32 chan);
    void requestReset();
    u32 takeSpinPresses(s32 hand);
    PlayerInputSample* getSample();

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

    PlayerInput* mPad[2];
    PlayerInputReader* mReadDataInfoArray;
    u32 mMode;
};

namespace MR {
    PlayerInput* getPlayerInput(s32);
    void resetPlayerInput();
    void setPlayerInputsModeHome();
    void setPlayerInputsModeGame();
    // How strongly the controller rumbles, 0 to 255.
    void setInputRumble(s32, u8);
};  // namespace MR
