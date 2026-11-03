#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTower : public Camera {
public:
    CameraTower(const char* pName = "\223\203\203\112\203\201\203\211" /* 塔カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isEnableToRoundLeft() const;
    virtual bool isEnableToRoundRight() const;
    virtual bool isEnableToReset() const {
        return true;
    }
    virtual CamTranslatorBase* createTranslator();
    virtual void arrangeRound();

    void setParam(const TVec3f& wPoint, const TVec3f& axis, f32 dist, const TVec2f& angle) {
        mWPoint.set(wPoint);
        mAxis.set(axis);
        mDist = dist;
        mAngleX = angle.x;
        mAngleYRoundSpeed = angle.y;
    }

    void calcIdealPos();
    void arrangeRoundingPose();
    void arrangeHorizontalPose();
    bool isUpsideDownByRoll() const;

    TVec3f mWPoint;
    TVec3f mAxis;
    f32 mAngleX;
    f32 mAngleYRoundSpeed;
    f32 mDist;
    TVec3f mPos;
    bool mIsRounding;
    s32 mRoundIntervalNum;
    f32 mAngleYSpeed;
    bool mIsRoundLeftValid;
    bool mIsRoundRightValid;
};
