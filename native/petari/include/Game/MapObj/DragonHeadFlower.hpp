#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class DragonHeadFlower : public LiveActor {
public:
    DragonHeadFlower(const char*);

    virtual ~DragonHeadFlower() {
    }

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initParabola(const TVec3f&);

    void exeWait();
    void exeSetCenter();
    void exeLaunchReady();
    void exeRailMove();

    HitSensor* mPlayerSensor;
    f32 mParabolaProgress;
    TVec3f mParabolaStartPos;
    TVec3f mTargetVelocity;
    TVec3f mCurrentGravity;
    TVec3f mEndGravity;
    TVec3f mParabolaDirection;
    u32 mParabolaSteps;
    f32 mParabolaDistance;
    f32 mParabolaAccel;
    f32 mParabolaVel;
};
