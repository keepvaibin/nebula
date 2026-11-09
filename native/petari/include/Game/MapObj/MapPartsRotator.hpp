#pragma once

#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapPartsFunction.hpp"
#include <nebula_geometry.hpp>

class MapPartsRotatorBase : public MapPartsFunction {
public:
    inline MapPartsRotatorBase(LiveActor* pActor, const char* pName) : MapPartsFunction(pActor, pName) {
    }

    virtual const Mtx34f& getRotateMtx() const = 0;

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

    virtual void init(const TableIter&) override;
    virtual bool isWorking() const override;
    virtual void start() override;
    virtual void end() override;
    virtual const Mtx34f& getRotateMtx() const override {
        return mRotateMtx;
    }
    virtual bool isMoving() const override;
    virtual f32 getRotateSpeed() const override {
        return mRotateSpeed;
    }
    virtual bool isOnReverse() const override {
        return mIsOnReverse;
    }

    void startWithSignalMotion();
    void cancelSignalMotion();
    void updateBaseHostMtx();
    void updateVelocity();
    void updateAngle();
    void updateTargetAngle();
    void restartAtEnd();
    void initRotateSpeed(const TableIter&);
    bool isReachedTargetAngle() const;
    void updateRotateMtx(AxisType, f32);
    void calcRotateAxisDir(AxisType, Vec3f*) const;

    void exeNeverMove();
    void exeWait();
    void exeRotateStart();
    void exeRotate();
    void exeStopAtEnd();

    bool isAccelTypeConstantSpeed() {
        return mRotateAccelType == 0 || mRotateAccelType == 2;
    }

    bool isRotateClockwise() const {
        return 0.0f < mTargetRotateSpeed;
    }

    f32 mTargetRotateSpeed;
    f32 mRotateAngle;
    s32 mRotateStopTime;
    f32 mTargetAngle;
    f32 mRotateSpeed;
    f32 mAngle;
    s32 mRotateAxis;
    s32 mRotateAccelType;
    s32 mRotateType;
    s32 mSignMotionType;
    Pos3f mBaseHostMtx;
    Pos3f mRotateMtx;
    f32 mSignAngle;
    bool mIsOnReverse;
};
