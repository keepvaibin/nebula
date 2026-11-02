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

    s32 mChannel;
    GamePadReadDataInfo* mReadInfo;
    GamePadButton* mButton;
    GamePadPointer* mPointer;
    GamePadAcceleration* mCorePadAccel;
    GamePadHVSwing* mCorePadSwing;
    GamePadRumble* _18;
    GamePadRumble* _1C;
    GamePadStick* mStick;
    GamePadAcceleration* mSubPadAccel;
    GamePadHVSwing* mSubPadSwing;
    GamePadLeaveWatcher* mLeaveWatcher;
    GamePadInfoChecker* mInfoChecker;
    bool _34;
    bool mIsConnected;
    bool mIsSubPadConnected;
};

namespace MR {
    bool isDeviceFreeStyle(const GamePadSample*);
};  // namespace MR
