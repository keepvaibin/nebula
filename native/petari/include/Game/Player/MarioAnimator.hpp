#pragma once

#include "Game/Animation/XanimePlayer.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioModule.hpp"
#include "Game/Util/NameIndex.hpp"

class MarioActor;
class XanimeResourceTable;
class XanimePlayer;
class HitSensor;

class MarioAnimator : public MarioModule {
public:
    MarioAnimator(MarioActor*);

    void init();
    void update();
    void calc();

    void setHoming();
    bool isAnimationStop() const;
    bool isDefaultAnimationRun(const char*) const;
    void setSpeed(f32);
    void forceSetBlendWeight(const f32*);
    void waterToGround();
    void initCallbackTable();
    void change(const char*);
    void changeDefault(const char*);
    void changeUpper(const char*);
    void changeDefaultUpper(const char*);
    void stopUpper(const char*);
    void setUpperRotateY(f32);
    void entryCallback(const char*);
    f32 getFrame() const;
    f32 getUpperFrame() const;
    u32 getUpperJointID() const;
    void setBlendWeight(const f32*, f32);
    void targetWeight(f32*, f32, f32);
    void setWalkWeight(const f32*);
    void initWalkWeight();
    bool isLandingAnimationRun() const;
    bool isCancelableAnimationRun() const;
    bool isWalkOrWaitingMotion() const;
    void updateWalkBas(const char*, f32);
    void setHand();
    void setTilt();
    void resetTilt();
    void setHipSlidingTilt(f32, f32);
    void setHipSliderTilt();
    void setHipSlipTilt();
    void setWalkMode();
    void updateJointRumble();
    void addRumblePower(f32, u32);
    void clearAllJointTransform();
    bool isMirrorAnimation();
    void switchMirrorMode();
    void changePickupAnimation(const HitSensor*);
    void updateTakingAnimation(const HitSensor*);
    void changeThrowAnimation(const HitSensor*);
    void stopWaitAnimation();
    void controlWaitAnimation();
    void runningCallback();
    void closeCallback();
    void spinEntry();
    void spinUpdate();
    void spinClose();
    void stageInCheck();
    void throwCheck();
    void throwEntry();
    void throwClose();
    void squatSpinCheck();
    void walkinClose();

    inline bool isTeresaClear() const {
        return !isPlayerModeTeresa();
    }

    inline XanimePlayer* getXanimePlayer() {
        return mXanimePlayer;
    }

    XanimeResourceTable* mResourceTable;
    XanimePlayer* mXanimePlayer;
    XanimePlayer* mXanimePlayerUpper;
    u8 mWalkState;  // the walk animation's state (from mTargetWalkSpeedIndex)
    u8 mPrevWalkState;  // the walk state last applied (0xFF: apply again)
    u8 mWaitPhase;  // the wait animation's phase (1, 2)
    f32 mWalkWeights[4];  // the four walk tracks' weights
    Mtx34f mTiltMtx;  // the upper body's tilt (setTilt)
    f32 mTiltSide;  // the tilt to the side, smoothed
    f32 mTiltDown;  // the look down, smoothed
    Vec3f mTiltPrevFront;  // the front at the last tilt
    bool mUpperRunning;  // an upper-body animation is running
    f32 mRumblePower;  // the joint rumble's strength
    u32 mRumbleTimer;  // frames of the joint rumble
    u16 mWaitHoldTimer;  // frames the wait animation holds (controlWaitAnimation)
    Mtx34f mHomingMtx;  // the homing turn (setHoming)
    Mtx34f mHipMtx;  // the hip's rumble, or the upper body's turn
    Mtx34f mMirrorMtx;  // the mirror's matrix (switchMirrorMode)
    bool mMirrorChecked;  // the mirror test ran for this animation
    bool mMirror;  // the animation is mirrored
    bool mUpperDefaultSet;
    bool mCallbackEnded;
    f32 mHomingAngle;  // the homing's horizontal angle, smoothed
    const char* mCurrBck;
    f32 mRumbleHipRot;  // last frame's hip rumble

    s32 mCallbackId;
    MR::NameIndex mCallbackTable;  // an animation's name to its index in marioCallbackTable
};
