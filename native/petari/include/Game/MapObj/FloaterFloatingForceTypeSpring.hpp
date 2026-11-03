#pragma once

#include "Game/MapObj/FloaterFloatingForce.hpp"

class SpringValue;

class FloaterSpringForce {
public:
    FloaterSpringForce(f32, f32, f32);

    void addSpringVelocity(f32);
    void setSpringBaseValue(f32);

    f32 _0;
    f32 _4;
    f32 _8;
    SpringValue* mSpringValue;
};

class FloaterFloatingForceTypeSpring : public FloaterFloatingForce {
public:
    FloaterFloatingForceTypeSpring(LiveActor*, const char*);

    virtual ~FloaterFloatingForceTypeSpring();
    virtual void init(const JMapInfoIter&);
    virtual void start();
    virtual void updateHostVelocity(TVec3f*) const;
    virtual const TVec3f& getCurrentVelocity() const {
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
    TVec3f mVelocity;
    f32 _48;
};
