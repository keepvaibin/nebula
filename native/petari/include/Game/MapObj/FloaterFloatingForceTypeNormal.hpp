#pragma once

#include "Game/MapObj/FloaterFloatingForce.hpp"

class FloaterFloatingForceTypeNormal : public FloaterFloatingForce {
public:
    FloaterFloatingForceTypeNormal(LiveActor*, const char*);

    virtual void init(const TableIter&) override;
    virtual void start() override;
    virtual void updateHostTrans(Vec3f*) const override;
    virtual const Vec3f& getCurrentVelocity() const override {
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
