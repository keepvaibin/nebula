#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;

/// @brief Spiny Cheep Cheep
class Balloonfish : public LiveActor {
public:
    Balloonfish(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeDash();
    void exeDashEnd();
    void exeStarPointerBind();
    void endStarPointerBind();

    AnimScaleController* mAnimScaleController;
    const Nerve* mNerveBeforeBind;
    s32 mNotBoundStep;
    Quat4f mQuat;
    f32 mRotateAngle;
};
