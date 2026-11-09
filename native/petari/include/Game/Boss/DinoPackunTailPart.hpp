#pragma once

#include "Game/Boss/DinoPackunTailNode.hpp"

class DinoPackunTailPart : public DinoPackunTailNode {
public:
    DinoPackunTailPart(const char*, DinoPackun*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    virtual void requestLockPosition() override;
    virtual void requestUnLockPosition() override;
    virtual JointController* createJointControllerOwn(LiveActor*, const char*) override;

    void exeWait();
    void exeLockPosition();

    f32 mRadius;
};
