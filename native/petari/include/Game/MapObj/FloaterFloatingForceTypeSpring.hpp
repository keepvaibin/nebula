#pragma once

#include "Game/MapObj/FloaterFloatingForce.hpp"

class SpringValue;

class FloaterSpringForce {
public:
    FloaterSpringForce(f32 baseValue, f32 spring, f32 damp);

    void addSpringVelocity(f32);
    void setSpringBaseValue(f32 baseValue);

    f32 mBaseValue;
    f32 mSpring;
    f32 mDamp;
    SpringValue* mSpringValue;
};

class FloaterFloatingForceTypeSpring : public FloaterFloatingForce {
public:
    FloaterFloatingForceTypeSpring(LiveActor*, const char*);

    virtual void init(const TableIter&) override;
    virtual void start() override;
    virtual void updateHostVelocity(Vec3f*) const override;
    virtual const Vec3f& getCurrentVelocity() const override {
        return mVelocity;
    }

    void updateVelocity();
    void soundMoveSE();
    f32 getCurrentSinkSpeed() const;
    bool tryOn();
    bool tryReturn();
    ;
    void exeWait();
    void exeMove();
    void exeMoveReturn();

    FloaterSpringForce* mSpringForce;
    Vec3f mVelocity;
    f32 mRideSinkSpeed;
};
