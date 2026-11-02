#pragma once

#include "Game/Camera/Camera.hpp"

class CameraBlackHole : public Camera {
public:
    CameraBlackHole(const char* pName = "\203\165\203\211\203\142\203\116\203\172\201\133\203\213\203\112\203\201\203\211" /* ブラックホールカメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const TVec3f rWPoint, const TVec3f rBasePos) {
        mWPoint.set(rWPoint);
        mBasePos.set(rBasePos);
    }

    f32 mFovy;
    f32 mRoll;
    TVec3f mWPoint;
    TVec3f mBasePos;
};
