#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class JumpStand : public LiveActor {
public:
    JumpStand(const char*);

    enum JumpType { Jump_Low = 0, Jump_Middle = 1, Jump_High = 2 };

    virtual void init(const TableIter&);
    virtual void control();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeTrampleBound();
    void exeHipDropBound();
    void exeStarPieceBound();
    bool tryToStartBind(HitSensor*);
    void updateBindActorMtx();
    void endBindAndShootUp(f32, JumpType);

    LiveActor* mBindedActor;
    Pos3f _90;
    bool mIsMarioJumpingHigh;
};
