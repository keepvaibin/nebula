#pragma once

#include "Game/MapObj/FloaterFloatingForce.hpp"

class FloaterFloatingForceTypeNormal : public FloaterFloatingForce {
public:
    FloaterFloatingForceTypeNormal(LiveActor*, const char*);

    virtual void init(const TableIter&);
    virtual void start();
    virtual void updateHostTrans(Vec3f*) const;
    virtual const Vec3f& getCurrentVelocity() const {
        return mVelocity;
    }

    void updateVelocity(bool);
    void soundMoveSE();
    void setStateMove();
    void exeWait();
    void exeMoveFirst();
    void exeMove();

    f32 mRiseAccel;  // the placement's rotate-angle argument
    f32 mSinkAccel;  // the placement's rotate-speed argument
    f32 mSinkDepth;
    f32 mSinkSpeed;
    Vec3f mVelocity;
};
