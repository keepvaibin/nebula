#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>


class FlexibleSphere : public LiveActor {
public:
    FlexibleSphere(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void kill();
    virtual void control();
    virtual void draw() const;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    u16 mShakeTimer;
    bool mGrowDirection;  // true = grow, false = shrink
    bool mVanishOnShrinkEnd;
    bool _90;  // this goes "unused" functionally, despite being "disabled" by arg4
    bool mEnabled;
    f32 mMaxScale;
    f32 mMinScale;
    f32 mScaleRate;
    s32 mTimer;
};
