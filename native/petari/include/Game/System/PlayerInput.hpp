#pragma once

#include <nebula_geometry.hpp>
#include "Game/System/NebulaInput.hpp"

class InputTilt;
class InputButtons;
class InputSwing;
class InputInfoChecker;
class InputPointer;
class PlayerInputReader;
class InputRumble;
class InputStick;
struct PlayerInputSample;

class PlayerInput {
public:
    // `pReadInfo`: the reader of this pad's samples, which its parts set up as they are made.
    PlayerInput(s32, PlayerInputReader* pReadInfo);

    void update();
    InputRumble* getRumbleInstance() const;
    s32 getBattery() const;
    void resetPad();
    void connected();
    void disconnected();
    static void connectCallback(s32, s32);
    static void extensionCallback(s32, s32);
    // This frame's sample; null while the controller has none.
    PlayerInputSample* getSample() const;

    s32 mChannel;
    PlayerInputReader* mReadInfo;
    InputButtons* mButton;
    InputPointer* mPointer;
    InputTilt* mMainTilt;
    InputSwing* mMainSwing;
    InputRumble* mRumble;
    InputRumble* mMenuRumble;
    InputStick* mStick;
    InputTilt* mSubTilt;
    InputSwing* mSubSwing;
    InputInfoChecker* mInfoChecker;
    bool mIsConnected;
    bool mHasMoveStick;
};

namespace MR {
    bool hasMoveStick(const PlayerInputSample*);
};  // namespace MR
