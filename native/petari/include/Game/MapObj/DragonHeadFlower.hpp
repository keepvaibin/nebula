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

    void initParabola(const Vec3f&);

    void exeWait();
    void exeSetCenter();
    void exeLaunchReady();
    void exeRailMove();

    HitSensor* mPlayerSensor;
    f32 mParabolaProgress;
    Vec3f mParabolaStartPos;
    Vec3f mTargetVelocity;
    Vec3f mCurrentGravity;
    Vec3f mEndGravity;
    Vec3f mParabolaDirection;
    u32 mParabolaSteps;
    f32 mParabolaDistance;
    f32 mParabolaAccel;
    f32 mParabolaVel;
};
