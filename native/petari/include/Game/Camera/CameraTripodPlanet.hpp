#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTripodPlanet : public Camera {
public:
    CameraTripodPlanet(const char* pName = "\216\117\213\162\230\146\220\257\203\112\203\201\203\211" /* 三脚惑星カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const TVec3f& wPoint, const TVec3f& axis, const TVec2f& angle, f32 dist) {
        mWPoint.set(wPoint);
        mAxis.set(axis);
        mAngleX = angle.x;
        mAngleY = angle.y;
        mDist = dist;
    }

    TVec3f mAxis;
    TVec3f mWPoint;
    f32 mAngleX;
    f32 mAngleY;
    f32 mDist;
    bool _70;
};
