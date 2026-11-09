#pragma once

#include "Game/MapObj/MapPartsRotator.hpp"

class LavaHomeSeesawRotator : public MapPartsRotatorBase {
public:
    LavaHomeSeesawRotator(LiveActor*, const char*, f32);

    virtual void init(const TableIter&) override;
    virtual bool isWorking() const override {
        return true;
    }

    virtual void start() override;
    virtual void end() override;
    virtual bool receiveMsg(u32) override;
    virtual const Mtx34f& getRotateMtx() const override {
        return mRotateMtx;
    }

    void exeWait();
    void exeMoveStart();
    void exeMove();
    void exeStay();
    void exeHipDrop();
    void updateRotateMtx();
    void updateVelocity();
    void updateRestoreForce();
    void clampAngularSpeed();
    f32 getDistanceFromRotAxis() const;
    void addForceHipDrop();
    bool isAngleLimited() const;
    bool isGoingToReachTargetAngle() const;
    void calcRotatedAngle(f32*, const Pos3f&) const;
    bool tryHipDrop();
    bool tryStay();
    bool tryRebound();
    void soundMoveSE();

private:
    void updateRotateAxis();
    f32 calcPlayerTorqueSign() const;
    f32 calcTiltFromRest() const;

public:

    f32 mAngularSpeedMax;
    f32 mInertia;
    f32 mRotateAngle;
    f32 mRestoreForce;
    Pos3f mRotateMtx;
    Quat4f mRotateQuat;
    bool mIsHipDropped;
    Vec3f mRotateAxis;
    f32 mAngularVelocity;
    Vec3f mInitialUp;
    const char* mMoveSound;
    f32 mMoveSoundSpeed;
};
