#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"
#include <JSystem/JGeometry.hpp>

class MapPartsRailRotator : public MapPartsFunction {
public:
    enum AxisType { AxisType_X, AxisType_Y, AxisType_Z };

    MapPartsRailRotator(LiveActor*);

    virtual ~MapPartsRailRotator() {
    }
    virtual void init(const JMapInfoIter&) override;
    virtual bool isWorking() const override;
    virtual void start() override;
    virtual void end() override;
    virtual f32 getJMapArgAngleFactor() const {
        return 1.0f;
    }

    bool hasRotation(s32) const;
    void rotateAtPoint(s32);
    bool hasRotationBetweenPoints(s32) const;
    void rotateBetweenPoints(s32, f32);
    void updateHostRotateMtx();
    void updateInfo(s32);
    bool isReachedTargetAngle() const;
    void calcRotateAxisDir(AxisType, TVec3f*) const;
    void updateRotateMtx(AxisType, f32);

    void initWithRotateMtx(const JMapInfoIter&, MtxPtr);

    void exeRotate();
    void exeWait() {
    }

    void exeDone() {
    }

    s32 mRotateAxis;
    s32 mRotateType;
    f32 mRotateSpeed;
    f32 mTargetAngle;
    f32 mAngle;
    TPos3f _2C;
    TPos3f _5C;
    MtxPtr mHostRotateMtx;
};
