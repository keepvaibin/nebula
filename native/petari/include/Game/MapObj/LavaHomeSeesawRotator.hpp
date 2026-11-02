#pragma once

#include "Game/MapObj/MapPartsRotator.hpp"

class LavaHomeSeesawRotator : public MapPartsRotatorBase {
public:
    LavaHomeSeesawRotator(LiveActor*, const char*, f32);

    virtual ~LavaHomeSeesawRotator();
    virtual void init(const JMapInfoIter&);
    virtual bool isWorking() const {
        return true;
    }

    virtual void start();
    virtual void end();
    virtual bool receiveMsg(u32);
    virtual const TMtx34f& getRotateMtx() const {
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
    void calcRotatedAngle(f32*, const TPos3f&) const;
    bool tryHipDrop();
    bool tryStay();
    bool tryRebound();
    void soundMoveSE();

    f32 mAngularSpeedMax;
    f32 mInertia;
    f32 mRotateAngle;
    f32 mRestoreForce;
    TPos3f mRotateMtx;
    TQuat4f mRotateQuat;
    bool mIsHipDropped;
    TVec3f mRotateAxis;
    f32 mAngularVelocity;
    TVec3f mInitialUp;
    const char* mMoveSound;
    f32 mMoveSoundSpeed;
};
