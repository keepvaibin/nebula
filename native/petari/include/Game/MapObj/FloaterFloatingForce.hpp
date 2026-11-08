#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"
#include <nebula_geometry.hpp>

class FloaterFloatingForce : public MapPartsFunction {
public:
    FloaterFloatingForce(LiveActor*, const char*);

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

    const char* mObjName;
    Vec3f mBasePos;
    Vec3f mUp;
    f32 mMaxSinkDepth;  // the placement's move-condition argument
};
