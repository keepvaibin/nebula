#pragma once

#include <nebula_geometry.hpp>

class CameraTargetObj;
class Binder;

class CameraViewInterpolator {
public:
    enum CalcState {
        CalcState_Ready,
        CalcState_Invalid,
    };

    CameraViewInterpolator();

    void updateCameraMtx(MtxPtr, const Vec3f&, const CameraTargetObj*, f32);
    void setInterpolation(u32);
    bool isInterpolating() const;
    bool isInterpolatingNearlyEnd() const;
    void lookAtCenter();
    bool calcCollision(const Pos3f&);
    bool calcBinder(Vec3f*, const Vec3f&, const Vec3f&);
    void reduceOscillation();
    void updateCalcState(const CameraTargetObj*);
    void interpolateCameraSwitching(MtxPtr, const Vec3f&, f32);
    void checkNearlyEnd(MtxPtr);
    bool translateByRepulsion();

    void resetGravity() {
        Vec3f grav(0.0f, -1.0f, 0.0f);
        mGravity.set(grav);
    }

    u32 mInterpolateTimer;
    u32 mInterpolateTime;
    bool mIsAntiOscillation;
    bool mIsForceCameraChange;
    bool mIsInterpolationOff;
    Pos3f mTargetMtx;
    Pos3f mMtx;
    Vec3f mTargetPosition;
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
    Vec3f mPosition;
    Vec3f mGravity;
    bool mIsCollided;
};
