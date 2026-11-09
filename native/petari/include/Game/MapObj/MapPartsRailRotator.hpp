#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"
#include <nebula_geometry.hpp>

class MapPartsRailRotator : public MapPartsFunction {
public:
    enum AxisType { AxisType_X, AxisType_Y, AxisType_Z };

    MapPartsRailRotator(LiveActor*);

    virtual ~MapPartsRailRotator() override {
    }
    virtual void init(const TableIter&) override;
    virtual bool isWorking() const override;
    virtual void start() override;
    virtual void end() override;
    virtual f32 getPlacementArgAngleFactor() const {
        return 1.0f;
    }

    bool hasRotation(s32) const;
    void rotateAtPoint(s32);
    bool hasRotationBetweenPoints(s32) const;
    void rotateBetweenPoints(s32, f32);
    void updateHostRotateMtx();
    void updateInfo(s32);
    bool isReachedTargetAngle() const;
    void calcRotateAxisDir(AxisType, Vec3f*) const;
    void updateRotateMtx(AxisType, f32);

    void initWithRotateMtx(const TableIter&, MtxPtr);

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
    Pos3f mBaseRotateMtx;
    Pos3f mRotateMtx;
    MtxPtr mHostRotateMtx;
};
