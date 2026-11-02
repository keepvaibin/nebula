#pragma once


class GamePad;

class GamePadHVSwing {
public:
    GamePadHVSwing(const GamePad*, u32);

    void updateSwing();
    void updateCentrifugal();
    void update();
    bool updateNebulaShake();

    const GamePad* mPad;
    s32 mChannel;
    f32 mDistanceSwingThreshold;
    bool mIsSwing;
    bool mIsSwingLatched;
    s32 mSwingLatchedFrames;
    s32 mSwingBelowThresholdFrames;
    bool mIsTriggerSwing;
    f32 mSwingThreshold;
    bool mSwingDetected;
    bool mSwingTriggered;
    s32 mSwingHoldFrames;
    s32 mSwingCooldownFrames;
    s32 mNebulaShakePresses;  // shake button presses not yet swung
    s32 mNebulaShakeFrames;   // frames left of the swing a press makes
    bool mIsNebulaShaking;    // the last frame's swing was a press's
};
