#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include "Game/System/NebulaInput.hpp"

class GamePadAcceleration;
class GamePadButton;
class GamePadHVSwing;
class GamePadInfoChecker;
class GamePadLeaveWatcher;
class GamePadPointer;
class GamePadReadDataInfo;
class GamePadRumble;
class GamePadStick;
struct GamePadSample;

class GamePad {
public:
    // `pReadInfo`: the reader of this pad's samples, which its parts set up as they are made.
    GamePad(s32, GamePadReadDataInfo* pReadInfo);

    void update();
    GamePadRumble* getRumbleInstance() const;
    void startLeaveWatcher();
    void stopLeaveWatcher();
    void restartLeaveWatcher();
    s32 getBattery() const;
    void resetPad();
    void connected();
    void disconnected();
    static void connectCallback(s32, s32);
    static void extensionCallback(s32, s32);
    GamePadSample* getSample(u32) const;
    s32 getValidStatusCount() const;
    s32 getEnableAccelPastCount(u32) const;
    bool getAcceleration(TVec3f*, u32) const;
    bool getPastAcceleration(TVec3f*, s32, u32) const;

    /* 0x00 */ s32 mChannel;
    /* 0x04 */ GamePadReadDataInfo* mReadInfo;
    /* 0x08 */ GamePadButton* mButton;
    /* 0x0C */ GamePadPointer* mPointer;
    /* 0x10 */ GamePadAcceleration* mCorePadAccel;
    /* 0x14 */ GamePadHVSwing* mCorePadSwing;
    /* 0x18 */ GamePadRumble* _18;
    /* 0x1C */ GamePadRumble* _1C;
    /* 0x20 */ GamePadStick* mStick;
    /* 0x24 */ GamePadAcceleration* mSubPadAccel;
    /* 0x28 */ GamePadHVSwing* mSubPadSwing;
    /* 0x2C */ GamePadLeaveWatcher* mLeaveWatcher;
    /* 0x30 */ GamePadInfoChecker* mInfoChecker;
    /* 0x34 */ bool _34;
    /* 0x35 */ bool mIsConnected;
    /* 0x36 */ bool mIsSubPadConnected;
};

namespace MR {
    bool isDeviceFreeStyle(const GamePadSample*);
};  // namespace MR
