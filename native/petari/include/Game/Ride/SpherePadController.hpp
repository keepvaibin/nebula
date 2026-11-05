#pragma once

#include "Game/Ride/SphereController.hpp"

class SpherePadController : public SphereController {
public:
    SpherePadController();

    virtual f32 calcMoveVector(Vec3f*, const Vec3f&);
    virtual f32 calcJumpPower() const;
    virtual void update(const Vec3f&);
    virtual void clacXY(f32*, f32*);

    f32 calcDirSphereMove(Vec3f*, const Vec3f&, u32);
};
