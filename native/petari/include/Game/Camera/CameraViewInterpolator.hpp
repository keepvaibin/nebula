#pragma once

#include <JSystem/JGeometry.hpp>

class CameraTargetObj;
class Binder;

class CameraViewInterpolator {
public:
    enum CalcState {
        CalcState_Ready,
        CalcState_Invalid,
    };

    CameraViewInterpolator();

    void updateCameraMtx(MtxPtr, const TVec3f&, const CameraTargetObj*, f32);
    void setInterpolation(u32);
    bool isInterpolating() const;
    bool isInterpolatingNearlyEnd() const;
    void lookAtCenter();
    bool calcCollision(const TPos3f&);
    bool calcBinder(TVec3f*, const TVec3f&, const TVec3f&);
    void reduceOscillation();
    void updateCalcState(const CameraTargetObj*);
    void interpolateCameraSwitching(MtxPtr, const TVec3f&, f32);
    void checkNearlyEnd(MtxPtr);
    bool translateByRepulsion();

    void resetGravity() {
        TVec3f grav(0.0f, -1.0f, 0.0f);
        mGravity.set(grav);
    }

    u32 mInterpolateTimer;
    u32 mInterpolateTime;
    bool mIsAntiOscillation;
    bool mIsForceCameraChange;
    bool mIsInterpolationOff;
    TPos3f mTargetMtx;
    TPos3f mMtx;
    TVec3f mTargetPosition;
    f32 mRate;
    bool mIsCollisionOff;
    bool mIsRepulsionOff;
    f32 mTargetFovy;
    f32 mFovy;
    bool mIsInterpolatingNearlyEnd;
    bool mIsInterpolating;
    bool mIsCorrectErpPositionOn;
    u32 mCalcState;
    const CameraTargetObj* mTargetObj;
    Binder* mBinder;
    TVec3f mPosition;
    TVec3f mGravity;
    bool mIsCollided;
    TVec3f mLErpOffset;  // TODO: is this name correct?
};
