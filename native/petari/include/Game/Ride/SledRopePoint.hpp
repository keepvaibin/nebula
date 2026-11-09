#pragma once

#include "Game/Ride/SwingRopePoint.hpp"

class Binder;

// A rope point that moves through map collision.
class SledRopePoint : public SwingRopePoint {
public:
    SledRopePoint(const Vec3f&, f32);

    // Hide SwingRopePoint's: the move goes through the binder.
    void updatePos(f32);
    void updatePosAndAxis(const Vec3f&, f32);

    void bindToMapCollision(f32);

    Binder* mBinder;
};
