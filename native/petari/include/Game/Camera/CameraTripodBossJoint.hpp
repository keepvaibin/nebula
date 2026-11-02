#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTripodBossJoint : public Camera {
public:
    CameraTripodBossJoint(const char* pName = "\216\117\213\162\203\173\203\130\212\326\220\337\203\112\203\201\203\211" /* 三脚ボス関節カメラ */);
    virtual ~CameraTripodBossJoint();

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isEnableToReset() const {
        return true;
    }
    virtual CamTranslatorBase* createTranslator();

    void setParam(const TVec2f& angle, f32 dist, s32 jointID, const TVec3f& axis) {
        mAngleX = angle.x;
        mAngleY = angle.y;
        mDist = dist;
        mJointID = jointID;
        mAxis.set(axis);
    }

    void calcIdealPose();

    f32 mAngleX;
    f32 mAngleY;
    f32 mDist;
    s32 mJointID;
    TVec3f mAxis;
    bool mIsRounding;
    f32 mRoundTarget;
    f32 mRoundAngle;
};
