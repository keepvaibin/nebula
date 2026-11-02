#pragma once

#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapPartsFunction.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class MapPartsRotatorBase : public MapPartsFunction {
public:
    inline MapPartsRotatorBase(LiveActor* pActor, const char* pName) : MapPartsFunction(pActor, pName) {
    }

    virtual ~MapPartsRotatorBase();

    virtual const TMtx34f& getRotateMtx() const = 0;

    virtual bool isMoving() const {
        return false;
    }

    virtual bool isOnReverse() const {
        return false;
    }

    virtual f32 getRotateSpeed() const {
        return 1.0f;
    }
};

class MapPartsRotator : public MapPartsRotatorBase {
public:
    MapPartsRotator(LiveActor*);

    enum AxisType { AxisType_XAxis, AxisType_YAxis, AxisType_ZAxis };

    virtual void init(const JMapInfoIter&);
    virtual bool isWorking() const;
    virtual void start();
    virtual void end();
    virtual const TMtx34f& getRotateMtx() const {
        return mRotateMtx;
    }
    virtual bool isMoving() const;
    virtual f32 getRotateSpeed() const {
        return mRotateSpeed;
    }
    virtual bool isOnReverse() const {
        return mIsOnReverse;
    }

    void startWithSignalMotion();
    void cancelSignalMotion();
    void updateBaseHostMtx();
    void updateVelocity();
    void updateAngle();
    void updateTargetAngle();
    void restartAtEnd();
    void initRotateSpeed(const JMapInfoIter&);
    bool isReachedTargetAngle() const;
    void updateRotateMtx(AxisType, f32);
    void calcRotateAxisDir(AxisType, TVec3f*) const;

    void exeNeverMove();
    void exeWait();
    void exeRotateStart();
    void exeRotate();
    void exeStopAtEnd();

    bool isAccelTypeConstantSpeed() {
        return mRotateAccelType == 0 || mRotateAccelType == 2;
    }

    bool isRotateClockwise() const {
        return 0.0f < _18;
    }

    f32 _18;
    f32 mRotateAngle;
    s32 mRotateStopTime;
    f32 mTargetAngle;
    f32 mRotateSpeed;
    f32 mAngle;
    s32 mRotateAxis;
    s32 mRotateAccelType;
    s32 mRotateType;
    s32 mSignMotionType;
    TPos3f mBaseHostMtx;
    TPos3f mRotateMtx;
    f32 _A0;
    bool mIsOnReverse;
};
