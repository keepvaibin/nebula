#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Butterfly : public LiveActor {
public:
    Butterfly(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;

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
    Vec3f mHomePos;
    Quat4f mQuat;
    s32 mColorIndex;
    HitSensor* mPerchTarget;
    bool mStarPieceAppeared;
};
