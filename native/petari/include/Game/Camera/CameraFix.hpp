#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTargetMtx;

class CameraFix : public Camera {
public:
    CameraFix(const char* pName = "\212\256\221\123\214\305\222\350\203\112\203\201\203\211" /* 完全固定カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const Vec3f&, const Vec3f&, const Vec3f&);

    Vec3f mWPoint;
    Vec3f mAxis;
    Vec3f mUp;
    CameraTargetMtx* mTarget;
};
