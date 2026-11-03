#pragma once

#include "Game/Camera/Camera.hpp"

class CameraParallel : public Camera {
public:
    CameraParallel(const char* pName = "\225\275\215\163\203\112\203\201\203\211" /* 平行カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();
    virtual bool isEnableToRoundLeft() const;
    virtual bool isEnableToRoundRight() const;
    virtual bool isEnableToReset() const;

    void calcIdealPose();
    void calcRound();

    void setParam(const TVec2f& angle, f32 dist, bool enabled) {
        mAngleX = angle.x;
        mAngleY = angle.y;
        mDist = dist;
        mIsEnableValid = enabled;
    }

    f32 mAngleX;
    f32 mAngleY;
    f32 mDist;
    bool mIsEnableValid;
    bool mIsRounding;
    f32 mRoundTarget;
    f32 mRoundAngle;
    f32 mRoundAddition;
};
