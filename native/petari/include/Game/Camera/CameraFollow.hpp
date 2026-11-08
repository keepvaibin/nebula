#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFollow : public Camera {
public:
    CameraFollow(const char* pName = "フォローカメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isEnableToRoundLeft() const;
    virtual bool isEnableToRoundRight() const;
    virtual bool isEnableToReset() const {
        return true;
    }
    virtual void setParamFromChunk(const CameraParamChunk*);

    void setParam(f32, f32, f32, f32, f32, bool);
    void calcIdealPos();
    void calcRotPos();
    void constrainDistance(Vec3f*, const Vec3f&);
    void constrainAngleX(f32*);
    void constrainAngleY(f32*, const Vec3f&, f32);
    f32 getMagnitudeOfMovingRound() const;
    void checkShiftStateToRound(const Vec3f&, const Vec3f&);
    void arrangeHeight();
    void reflectRotOffset();
    bool isWallForward() const;
    void arrangeLookUp();
    bool checkAscent(Vec3f*) const;
    void arrangeLookDown();
    bool checkDescent(Vec3f*) const;
    void startRound(f32, f32);
    void checkValidityOfRound(const Vec3f&, const Vec3f&);
    bool isUpsideDownByRoll() const;

    f32 mDistMax;
    f32 mDistMin;
    f32 mAngleXMin;
    f32 mAngleXRange;
    Vec3f mPos;
    Vec3f mWatchPos;
    f32 mAngleYRoundSpeed;  // moving round magnitude (rate?)
    f32 mAngleXSpeed;
    bool mCheckWall;
    bool mIsRounding;
    f32 mAngleYTarget;
    f32 mAngleYSpeed;
    bool mIsRoundLeftValid;
    bool mIsRoundRightValid;
};
