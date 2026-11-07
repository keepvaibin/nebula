#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"
#include <nebula_geometry.hpp>

class FloaterFloatingForce : public MapPartsFunction {
public:
    FloaterFloatingForce(LiveActor*, const char*);

    virtual ~FloaterFloatingForce();
    virtual void init(const TableIter&);
    virtual void start();

    virtual void updateHostTrans(Vec3f*) const {
    }

    virtual void updateHostVelocity(Vec3f*) const {
    }

    virtual const Vec3f& getCurrentVelocity() const {
        // Zero.
        static Vec3f zero;
        return zero;
    }

    const char* _18;
    Vec3f _1C;
    Vec3f _28;
    f32 mMoveConditionType;
};
