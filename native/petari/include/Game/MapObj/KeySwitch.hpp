#pragma once

#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class KeySwitch : public LiveActor {
public:
    KeySwitch(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initKeySwitchByOwner(const TableIter&);
    void appearKeySwitch(const Vec3f&);
    void exeDemoStart();
    void exeAppear();
    void exeWait();
    bool tryAvoid();

    ActorCameraInfo* mCameraInfo;
    s32 mCurDemoFrame;
};
