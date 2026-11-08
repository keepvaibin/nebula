#pragma once

#include "Game/Camera/CameraMan.hpp"
#include <nebula_geometry.hpp>

class CameraManPause : public CameraMan {
public:
    CameraManPause(const char*);

    virtual void init(const TableIter&);

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
    Vec2f mAngle;
    Vec3f mBasePos;
};
