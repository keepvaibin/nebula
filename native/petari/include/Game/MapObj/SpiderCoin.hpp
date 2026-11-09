#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class PartsModel;

class SpiderCoin : public LiveActor {
public:
    SpiderCoin(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void calcAndSetBaseMtx() override;

    void exeWait();
    void exeTouch();
    void exeTouchAndApart();
    bool tryRub(s32, Vec2f*);

    const Vec3f* mPointPos;
    const Vec3f* mNeutralPos;
    const Vec3f* mUp;
    f32 mThreadZ;
    Vec2f mPad0Velocity;
    Vec2f mPad1Velocity;
    PartsModel* mCocoonModel;

    Pos3f mBaseMtx;
};
