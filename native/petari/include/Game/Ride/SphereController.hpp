#pragma once

#include <nebula_geometry.hpp>

class SphereController {
public:
    SphereController();

    virtual f32 calcMoveVector(Vec3f*, const Vec3f&);
    virtual f32 calcJumpPower() const;
    virtual bool doBrake() const;
    virtual void update(const Vec3f&);
    virtual void notifyActivate(){};
    virtual void notifyDeactivate(){};

protected:
};
