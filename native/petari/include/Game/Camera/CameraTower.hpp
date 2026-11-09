#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTower : public Camera {
public:
    CameraTower(const char* pName = "塔カメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual bool isEnableToRoundLeft() const override;
    virtual bool isEnableToRoundRight() const override;
    virtual bool isEnableToReset() const override {
        return true;
    }
    virtual void setParamFromChunk(const CameraParamChunk*) override;
    virtual void arrangeRound();

    void setParam(const Vec3f& wPoint, const Vec3f& axis, f32 dist, const Vec2f& angle) {
        mWPoint.set(wPoint);
        mAxis.set(axis);
        mDist = dist;
        mAngleX = angle.x;
        mAngleYRoundSpeed = angle.y;
    }

    void calcIdealPos();
    void arrangeRoundingPose();
    void arrangeHorizontalPose();
    bool isUpsideDownByRoll() const;

    Vec3f mWPoint;
    Vec3f mAxis;
    f32 mAngleX;
    f32 mAngleYRoundSpeed;
    f32 mDist;
    Vec3f mPos;
    bool mIsRounding;
    s32 mRoundIntervalNum;
    f32 mAngleYSpeed;
    bool mIsRoundLeftValid;
    bool mIsRoundRightValid;
};
