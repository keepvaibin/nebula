#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class FlexibleSphere : public LiveActor {
public:
    FlexibleSphere(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void kill();
    virtual void control();
    virtual void draw() const;

    bool tryStart();
    void updateVanishTimer();
    void startQuake();
    void updateQuake();

    u16 mShakeTimer;
    bool mIsGrowing;
    bool mVanishOnShrinkEnd;
    bool mEnabled;
    f32 mMaxScale;
    f32 mMinScale;
    f32 mScaleRate;
    // Frames left until it vanishes.
    s32 mTimer;
};
