#pragma once

#include "Game/Camera/Camera.hpp"

class CameraMtxRegParallel : public Camera {
public:
    CameraMtxRegParallel(const char* pName = "\203\175\203\147\203\212\203\116\203\130\203\214\203\127\203\130\203\136\225\300\215\163\203\112\203\201\203\211" /* マトリクスレジスタ並行カメラ */);
    virtual ~CameraMtxRegParallel();

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const char* str, f32 dist, const TVec2f& angle, const TVec3f& wPoint) {
        mString = str;
        mAngleX = angle.x;
        mAngleY = angle.y;
        mDist = dist;
        mWPoint = wPoint;
    }

    void calcIdealPose();

    const char* mString;
    f32 mAngleX;
    f32 mAngleY;
    f32 mDist;
    TVec3f mWPoint;
};
