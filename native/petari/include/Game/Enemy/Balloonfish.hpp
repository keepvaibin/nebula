#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;

/// @brief Spiny Cheep Cheep
class Balloonfish : public LiveActor {
public:
    Balloonfish(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeDash();
    void exeDashEnd();
    void exeStarPointerBind();
    void endStarPointerBind();

    AnimScaleController* mAnimScaleController;
    const Nerve* mNerveBeforeBind;
    s32 mNotBoundStep;
    TQuat4f mQuat;
    f32 mRotateAngle;
};
