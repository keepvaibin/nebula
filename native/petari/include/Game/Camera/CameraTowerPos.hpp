#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTowerPos : public Camera {
public:
    CameraTowerPos(const char* pName = "塔カメラ（サブターゲット付き）");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual bool isEnableToReset() const override {
        return true;
    }
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(const Vec3f& wPoint, const Vec3f& axis, f32 dist, const Vec2f& angle, f32 angleXRate) {
        mWPoint.set(wPoint);
        mAxis.set(axis);
        mDist = dist;
        mAngleYMaxSpeed = angle.y;
        mAngleX = angle.x;
        mAngleXRate = angleXRate;
    }

    void calcIdealPose(bool);
    void arrangeHorizontalPose(const Vec3f&, Vec3f&);
    void arrangeWatchPos(Vec3f&, const Vec3f&);

    Vec3f mWPoint;
    Vec3f mAxis;
    f32 mAngleYMaxSpeed;
    f32 mAngleX;
    f32 mDist;
    f32 mAngleXRate;
    Vec3f mPos;
};
