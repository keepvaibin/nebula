#pragma once

#include "Game/Effect/SimpleEffectObj.hpp"

class RandomEffectObj : public SimpleEffectObj {
public:
    RandomEffectObj(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;

    virtual f32 getClippingRadius() const override;
    virtual f32 getFarClipDistance() const override;

    s32 getRandomTime() const;

private:
    s32 mStep;
    s32 mSeStep;
    s32 mLifeTime;
    s32 mRandWidth;
    // FireworksA plays a shot and an explosion sound.
    bool mIsFireworks;
};
