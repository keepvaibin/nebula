#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include "Game/System/NebulaInput.hpp"

class InputTilt;
class InputButtons;
class InputSwing;
class InputInfoChecker;
class InputLeaveWatcher;
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
    void startLeaveWatcher();
    void stopLeaveWatcher();
    void restartLeaveWatcher();
    s32 getBattery() const;
    void resetPad();
    void connected();
    void disconnected();
    static void connectCallback(s32, s32);
    static void extensionCallback(s32, s32);
    PlayerInputSample* getSample(u32) const;
    s32 getValidStatusCount() const;
    s32 getEnableAccelPastCount(u32) const;
    bool getAcceleration(TVec3f*, u32) const;
    bool getPastAcceleration(TVec3f*, s32, u32) const;

    /* 0x00 */ s32 mChannel;
    /* 0x04 */ PlayerInputReader* mReadInfo;
    /* 0x08 */ InputButtons* mButton;
    /* 0x0C */ InputPointer* mPointer;
    /* 0x10 */ InputTilt* mMainTilt;
    /* 0x14 */ InputSwing* mMainSwing;
    /* 0x18 */ InputRumble* _18;
    /* 0x1C */ InputRumble* _1C;
    /* 0x20 */ InputStick* mStick;
    /* 0x24 */ InputTilt* mSubTilt;
    /* 0x28 */ InputSwing* mSubSwing;
    /* 0x2C */ InputLeaveWatcher* mLeaveWatcher;
    /* 0x30 */ InputInfoChecker* mInfoChecker;
    /* 0x34 */ bool _34;
    /* 0x35 */ bool mIsConnected;
    /* 0x36 */ bool mHasMoveStick;
};

namespace MR {
    bool hasMoveStick(const PlayerInputSample*);
};  // namespace MR
