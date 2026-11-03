#pragma once

#include "Game/MapObj/MapPartsRotator.hpp"

class MapPartsSeesaw1AxisRotator : public MapPartsRotatorBase {
public:
    MapPartsSeesaw1AxisRotator(LiveActor*, const char*, f32);

    virtual ~MapPartsSeesaw1AxisRotator();
    virtual void init(const JMapInfoIter&) override;
    virtual bool isWorking() const override;
    virtual void start() override;
    virtual void end() override;
    virtual bool receiveMsg(u32) override;
    virtual const TMtx34f& getRotateMtx() const override {
        return mRotateMtx;
    }

    virtual bool isMoving() const override {
        return isWorking();
    }

    void exeWait() {
    }

    void exeMove();
    void exeStay();
    void exeHipDrop();
    void rotate();
    bool isAngleLimited() const {
        return 0.0f < mRotateAngle;
    }

    void updateVelocity();
    void updateRestoreForce();
    void clampAngularSpeed();
    f32 getDistanceFromRotAxis() const;
    void addForceHipDrop();
    bool isGoingToReachTargetAngle() const;
    void calcRotatedAngle(f32*, const TPos3f&) const;
    bool tryHipDrop();

    f32 mAngularSpeedMax;
    f32 mInertiaConst;
    f32 mRotateAngle;
    f32 mRestoreForce;
    TPos3f mRotateMtx;
    bool mHipDrop;
    TVec3f mRotateAxis;
    f32 mAngularVelocity;
    f32 mAngularAccel;
    TVec3f mInitialUp;
    const char* mMoveSound;
    f32 mMoveSoundSpeed;
};
