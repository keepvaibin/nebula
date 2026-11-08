#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Dossun : public LiveActor {
public:
    Dossun(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;

    void initMapToolInfo(const TableIter&);
    void initShadow();
    void calcParameters();
    s32 getUpperFrame() const;
    void exeReady();
    void exeUpper();
    void exeFallSign();
    void exeFalling();
    void exeOnGround();
    void exeRising();

    Vec3f mUpperPos;
    Vec3f mLowerPos;
    s32 mFallingTime;
    s32 mHoldTime;
    f32 mMovementDist;
    s32 mRisenWaitTime;
    s32 mShadowType;
    bool mHasShadow;
};
