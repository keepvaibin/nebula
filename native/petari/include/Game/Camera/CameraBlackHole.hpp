#pragma once

#include "Game/Camera/Camera.hpp"

class CameraBlackHole : public Camera {
public:
    CameraBlackHole(const char* pName = "\203\165\203\211\203\142\203\116\203\172\201\133\203\213\203\112\203\201\203\211" /* ブラックホールカメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const Vec3f rWPoint, const Vec3f rBasePos) {
        mWPoint.set(rWPoint);
        mBasePos.set(rBasePos);
    }

    f32 mFovy;
    f32 mRoll;
    Vec3f mWPoint;
    Vec3f mBasePos;
};
