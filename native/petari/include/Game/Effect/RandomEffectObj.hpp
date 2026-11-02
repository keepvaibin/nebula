#pragma once

#include "Game/Effect/SimpleEffectObj.hpp"

class RandomEffectObj : public SimpleEffectObj {
public:
    RandomEffectObj(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void control();

    virtual f32 getClippingRadius() const;
    virtual f32 getFarClipDistance() const;

    s32 getRandomTime() const;

private:
    s32 mStep;
    s32 mSeStep;
    s32 mLifeTime;
    s32 mRandWidth;
};
