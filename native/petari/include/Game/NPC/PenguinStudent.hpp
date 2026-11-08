#pragma once

#include "Game/NPC/NPCActor.hpp"

class RemovableTurtle;

class PenguinStudent : public NPCActor {
public:
    PenguinStudent(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    bool branchFunc(u32);
    bool tryReleaseTurtle();
    void exeSwim();
    void exeLead();
    void exeSlow();

    s32 mObjArg0;
    RemovableTurtle* mRemovableTurtle;
    LiveActor* mActor;
    f32 mRailOffset;
};
