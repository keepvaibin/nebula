#pragma once

#include "Game/Camera/Camera.hpp"

class CameraCharmedTripodBoss : public Camera {
public:
    CameraCharmedTripodBoss(const char* pName = "\216\117\213\162\203\173\203\130\203\127\203\207\203\103\203\223\203\147\222\215\216\213\203\112\203\201\203\211" /* 三脚ボスジョイント注視カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(s32, TVec3f, const TVec3f&, const TVec2f&);

    /* 0x4C */ s32 mJointId;
    /* 0x50 */ TVec3f mUp;
    /* 0x5C */ TVec3f mWPoint;
    /* 0x68 */ f32 mAngleX;
    /* 0x6C */ f32 mAngleY;
};
