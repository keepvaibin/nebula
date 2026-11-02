#pragma once

#include "Game/Ride/SwingRopePoint.hpp"

class Binder;

class SledRopePoint : public SwingRopePoint {
public:
    SledRopePoint(const TVec3f&, f32);

    virtual void updatePos(f32);

    bool bindToMapCollision(f32);

    Binder* mBinder;
    bool mNoBind;
};
