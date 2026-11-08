#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinDriverCamera;
class SpinDriverShootPath;
class ScenarioTitle;
class SimpleLayout;

class ScenarioStarter : public LiveActor {
public:
    ScenarioStarter(const char*);

    virtual void init(const TableIter& rIter);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initShootPath(const TableIter&);
    void exeWaitScenarioCameraEnd();
    void exeWaitToStart();
    void exeCinemaFrameBlank();
    void exeRailMove();
    void exeRailMoveCanceled();
    void exeShowWelcomeLayout();
    bool isStartBgmOnWelcome();
    void calcShootMotionTime();
    void turnBindHead(const Vec3f&, f32);
    void updateBindPosition();
    void updateShootMotion();
    bool trySkipTrigger() const;
    void updateBindActorMtx();

    LiveActor* mBindActor;
    Vec3f mHeadDir;
    Vec3f mJumpVec;
    Quat4f mBindQuat;
    SpinDriverShootPath* mShootPath;
    SpinDriverCamera* mSpinDriverCamera;
    Vec3f mInitUp;
    Vec3f mInitFront;
    f32 mSpinAngle;
    f32 mSpinAngleMax;
    f32 mFallAngle;
    s32 mFlightTime;
    s32 mFlyMotionStartStep;
    s32 mFlyEndStep;
    s32 mFallMotionStartStep;
    f32 mInitalPlayerRotation;
    SimpleLayout* mWelcomeLayout;
    ScenarioTitle* mTitle;
    Vec3f mStartPos;
    Vec3f mStartRotate;
    bool mIsSkipped;
};
