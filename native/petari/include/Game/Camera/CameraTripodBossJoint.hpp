#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTripodBossJoint : public Camera {
public:
    CameraTripodBossJoint(const char* pName = "三脚ボス関節カメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual bool isEnableToReset() const override {
        return true;
    }
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(const Vec2f& angle, f32 dist, s32 jointID, const Vec3f& axis) {
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
    Vec3f mAxis;
    bool mIsRounding;
    f32 mRoundTarget;
    f32 mRoundAngle;
};
