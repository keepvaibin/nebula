#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ScrewSwitchReverse : public LiveActor {
public:
    ScrewSwitchReverse(const char*);

    virtual void init(const TableIter&) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void updateBindActorMtx();
    void endBind();

    void exeWait();
    void exeAdjust();
    void exeScrew();

    LiveActor* mHost;
    bool mIsForceJump;  // Obj_arg0
};
