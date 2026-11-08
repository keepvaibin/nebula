#pragma once

#include "Game/Boss/DinoPackunTailNode.hpp"

class DinoPackunTailPart : public DinoPackunTailNode {
public:
    DinoPackunTailPart(const char*, DinoPackun*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    virtual void requestLockPosition();
    virtual void requestUnLockPosition();
    virtual JointController* createJointControllerOwn(LiveActor*, const char*);

    void exeWait();
    void exeLockPosition();

    f32 mRadius;
};
