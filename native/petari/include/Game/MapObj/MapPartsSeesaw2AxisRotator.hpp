#pragma once

#include "Game/MapObj/MapPartsRotator.hpp"

class MapPartsSeesaw2AxisRotator : public MapPartsRotatorBase {
public:
    MapPartsSeesaw2AxisRotator(LiveActor*, const char*, f32);

    virtual void init(const TableIter&) override;
    virtual bool isWorking() const override;
    virtual void start() override;
    virtual void end() override;
    virtual bool receiveMsg(u32) override;
    virtual const Mtx34f& getRotateMtx() const override {
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
    void restoreMove();
    f32 getInertiaConst() const;

    f32 mRotateAngle;
    f32 mInertiaConst;
    f32 mRestoreForce;
    Pos3f mBaseMtx;
    Pos3f mBaseMtxInv;
    Pos3f mRotateMtx;
    Vec3f mUp;
    bool mHipDrop;
    f32 mRotateSpeed;
    const char* mMoveSound;
    f32 mMoveSoundSpeed;
};
