#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTowerBase : public Camera {
public:
    CameraTowerBase(const char*);
    virtual ~CameraTowerBase();

    virtual void reset();
    virtual CameraTargetObj* calc();

    virtual void resetBegin() {
    }
    virtual void resetPreVPan() {
    }
    virtual void resetPostVPan() {
    }
    virtual void arrangePreVPan() {
    }
    virtual void arrangePostVPan() {
    }

    void calcIdealPoseOfTowerBase();

    TVec3f mWPoint;
    TVec3f mAxis;
    f32 mAngleX;
    f32 mDist;
};
