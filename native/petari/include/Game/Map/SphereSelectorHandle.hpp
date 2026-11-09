#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class SphereSelectorHandle : public LiveActor {
public:
    SphereSelectorHandle(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual MtxPtr getBaseMtx() const override {
        return const_cast< MtxPtr >(mBaseMtx.mMtx);
    }

    void validateRotate();
    void invalidateRotate();
    bool isHolding() const;
    bool isPointing() const;
    void rotateAxisX();
    void rotateAxisY();
    void updateBaseMtx();
    void changeBgmRotateState();
    void playRotateSE();
    void resetRotateParam();
    void clearPointerVelocity();
    void setStateConfirmStartAtFirstStep();
    bool tryRelease();
    void stackPointerVelocity();
    Vec2f* getPointerVelocity();

    void exeWait();
    void exeHold();
    void exeSpin();
    void exeDemoRotate();
    void exeDisappear();
    void exeInvalidRotate();
    void exeGalaxyConfirmStart();
    void exeGalaxyConfirmWait();
    void exeGalaxyConfirmCancel();
    void exeIdleForFileSelect();
    void exeIdleEndForFileSelect();

    inline bool isWaitOrSpinOrDemoRotate();

public:
    bool mIsFileSelectMode;
    Pos3f mBaseMtx;
    Vec3f mFront;
    f32 mRotateSpeed;
    f32 mTargetRotateSpeed;
    f32 mRotateSpeedX;
    f32 mPrevRotateSpeedX;
    Vec3f mSide;
    Vec3f mUp;
    Vec2f mPointerVelocities[3];  // the last updates' pointer velocities, newest first; the fastest turns the sphere
    s32 mNotPointingStep;
    bool mIsRotateBgm;
};
