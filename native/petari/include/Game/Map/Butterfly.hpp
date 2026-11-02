#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Butterfly : public LiveActor {
public:
    Butterfly(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void updatePosture();
    void addRunAwayVelocity();
    bool tryRunAway();
    bool tryHive();
    bool tryPerchOnSleepingMario();
    bool tryAppearStarPeace(s32 port);

    void exeWait();
    void exeRunAway();
    void exeHive();
    void exePerchOn();
    void exeGotoSleepingMario();
    void exeReadyToPerchOnSleepingMario();
    void exePerchOnSleepingMario();

private:
    TVec3f mHomePos;
    TQuat4f mQuat;
    s32 mColorIndex;
    HitSensor* mPerchTarget;
    bool mStarPieceAppeared;
};
