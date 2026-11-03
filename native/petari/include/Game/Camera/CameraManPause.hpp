#pragma once

#include "Game/Camera/CameraMan.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class CameraManPause : public CameraMan {
public:
    CameraManPause(const char*);

    virtual void init(const JMapInfoIter&);

    virtual void calc();
    virtual void notifyActivate();
    virtual void notifyDeactivate();
    virtual bool isCollisionOff() const {
        return true;
    }
    virtual bool isZeroFrameMoveOff() const {
        return true;
    }

    f32 mDistBase;
    f32 mDistOffset;
    TVec2f mAngle;
    TVec3f mBasePos;
};
