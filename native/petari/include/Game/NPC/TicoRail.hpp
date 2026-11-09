#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LodCtrl;

class TicoRail : public LiveActor {
public:
    TicoRail(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();
    void exeLookAround();
    void exeMoveSign();
    void exeMove();
    void exeStop();
    void exeTalkStart();
    void exeTalk();
    void exeTalkCancel();
    void exeGoodBye();
    bool isGreaterEqualStepAndRandom(s32 step) const;
    bool isNrvTalking() const;
    bool isSameRailActor(const LiveActor* pActor) const;

    Vec3f mFront;
    LiveActor* mTalkPartner;
    LodCtrl* mLodCtrl;
};
