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

    LiveActor* _8C;
    Vec3f _90;
    Vec3f _9C;
    Quat4f _A8;
    SpinDriverShootPath* _B8;
    SpinDriverCamera* mSpinDriverCamera;
    Vec3f _C0;
    Vec3f _CC;
    Vec3f _D8;
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
    Vec3f _10C;
    Vec3f _118;
    bool _124;
};
