#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinDriverCamera;
class SpinDriverShootPath;
class ScenarioTitle;
class SimpleLayout;

class ScenarioStarter : public LiveActor {
public:
    ScenarioStarter(const char*);

    virtual ~ScenarioStarter();
    virtual void init(const JMapInfoIter& rIter);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initShootPath(const JMapInfoIter&);
    void exeWaitScenarioCameraEnd();
    void exeWaitToStart();
    void exeCinemaFrameBlank();
    void exeRailMove();
    void exeRailMoveCanceled();
    void exeShowWelcomeLayout();
    bool isStartBgmOnWelcome();
    void calcShootMotionTime();
    void turnBindHead(const TVec3f&, f32);
    void updateBindPosition();
    void updateShootMotion();
    bool trySkipTrigger() const;
    void updateBindActorMtx();

    LiveActor* _8C;
    TVec3f _90;
    TVec3f _9C;
    TQuat4f _A8;
    SpinDriverShootPath* _B8;
    SpinDriverCamera* mSpinDriverCamera;
    TVec3f _C0;
    TVec3f _CC;
    TVec3f _D8;
    f32 _E4;
    f32 _E8;
    f32 _EC;
    s32 mFlightTime;
    s32 mFlyMotionStartStep;
    s32 _F8;
    s32 mFallMotionStartStep;
    f32 mInitalPlayerRotation;
    SimpleLayout* mWelcomeLayout;
    ScenarioTitle* mTitle;
    TVec3f _10C;
    TVec3f _118;
    bool _124;
};
