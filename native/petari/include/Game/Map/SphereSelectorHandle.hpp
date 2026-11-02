#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class SphereSelectorHandle : public LiveActor {
public:
    SphereSelectorHandle(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void control();
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)&_90;
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
    TVec2f* getPointerVelocity();

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
    TPos3f _90;
    TVec3f _C0;
    f32 mRotateSpeed;
    f32 mTargetRotateSpeed;
    f32 _D4;
    f32 _D8;
    TVec3f _DC;
    TVec3f _E8;
    TVec2f _F4[3];
    s32 _10C;
    TVec3f _110;
    TVec3f _11C;
    bool _128;
};
