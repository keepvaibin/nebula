#pragma once

#include "Game/MapObj/FloaterFloatingForce.hpp"

class FloaterFloatingForceTypeNormal : public FloaterFloatingForce {
public:
    FloaterFloatingForceTypeNormal(LiveActor*, const char*);

    virtual void init(const JMapInfoIter&);
    virtual void start();
    virtual void updateHostTrans(TVec3f*) const;
    virtual const TVec3f& getCurrentVelocity() const {
        return mVelocity;
    }

    void updateVelocity(bool);
    void soundMoveSE();
    void setStateMove();
    void exeWait();
    void exeMoveFirst();
    void exeMove();

    f32 mRotateAngle;
    f32 mRotateSpeed;
    f32 _40;
    f32 _44;
    TVec3f mVelocity;
    f32 _54;
};
