#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class FlexibleSphere : public LiveActor {
public:
    FlexibleSphere(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void draw() const override;

    bool tryStart();
    void updateVanishTimer();
    void startQuake();
    void updateQuake();

    s32 mShakeTimer;
    bool mIsGrowing;
    bool mVanishOnShrinkEnd;
    bool mEnabled;
    f32 mMaxScale;
    f32 mMinScale;
    f32 mScaleRate;
    // Frames left until it vanishes.
    s32 mTimer;
};
