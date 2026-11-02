#pragma once

#include "Game/Camera/Camera.hpp"

class CameraGround : public Camera {
public:
    CameraGround(const char* pName = "\222\156\226\312\203\112\203\201\203\211" /* 地面カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const TVec2f& angle, f32 dist, const TVec3f& rUp) {
        mAngleA = angle.x;
        mAngleB = angle.y;
        mDist = dist;
        mUp.set(rUp);
    }

    f32 mAngleA;
    f32 mAngleB;
    f32 mDist;
    TVec3f mUp;
    TVec3f mLastWatchPoint;
    bool mIsNotInitialized;
};
