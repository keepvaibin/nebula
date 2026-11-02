#pragma once

#include "Game/Camera/Camera.hpp"

class CameraInwardTower : public Camera {
public:
    CameraInwardTower(const char* pName = "\223\203\223\340\225\224\203\112\203\201\203\211" /* 塔内部カメラ */);
    virtual ~CameraInwardTower();

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isEnableToReset() const {
        return true;
    }
    virtual CamTranslatorBase* createTranslator();

    void calcIdealPose();

    void setParam(const TVec3f& rWPoint, const TVec3f& rAxis, const TVec2f& angle, f32 dist) {
        mWPoint.set(rWPoint);
        mAxis.set(rAxis);
        mAngleX = angle.x;
        mAngleMax = angle.y;
        mDist = dist;
    }

    TVec3f mWPoint;
    TVec3f mAxis;
    f32 mAngleX;
    f32 mAngleMax;
    f32 mDist;
    TVec3f mPos;
    f32 mAngleY;
    bool mIsResetting;
};
