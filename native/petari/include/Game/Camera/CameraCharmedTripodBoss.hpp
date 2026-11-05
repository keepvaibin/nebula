#pragma once

#include "Game/Camera/Camera.hpp"

class CameraCharmedTripodBoss : public Camera {
public:
    CameraCharmedTripodBoss(const char* pName = "\216\117\213\162\203\173\203\130\203\127\203\207\203\103\203\223\203\147\222\215\216\213\203\112\203\201\203\211" /* 三脚ボスジョイント注視カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(s32, Vec3f, const Vec3f&, const Vec2f&);

    s32 mJointId;
    Vec3f mUp;
    Vec3f mWPoint;
    f32 mAngleX;
    f32 mAngleY;
};
