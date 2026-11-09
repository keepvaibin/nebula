#pragma once

#include "Game/Camera/Camera.hpp"
#include <nebula_geometry.hpp>

class CameraMedianPlanet : public Camera {
public:
    CameraMedianPlanet(const char* pName = "中点注目プラネットカメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void getPrevFront(Vec3f*);
    void constrainDipAngle(Vec3f*);
    void makeTargetMtx(Pos3f*);
    void peepMtx(Pos3f*);
    void rotate33(Pos3f*, const Vec3f&, f32);
    void constrainLength(Vec3f*);
    f32 getMaxRate();
    void getRegisterUpVec(Vec3f*);
    void calcWatchPos(Vec3f*);
    bool calcUpVec(Vec3f*, const Vec3f&, const Vec3f&);

    void setParam(const char* pName, f32 distMin, f32 distMax, f32 angleMin, f32 constrainRate, f32 peepAngle, f32 dist, f32 partialRate) {
        mString = pName;
        mDistMin = distMin;
        mDistMax = distMax;
        mAngleMin = angleMin;
        mPeepAngle = peepAngle;
        mDist = dist;
        mAngleConstrainRate = constrainRate;
        mPartialRate = partialRate;
    }

    const char* mString;
    f32 mDistMin;
    f32 mDistMax;
    f32 mAngleMin;
    f32 mPeepAngle;
    f32 mDist;
    f32 mAngleConstrainRate;
    f32 mPartialRate;
};
