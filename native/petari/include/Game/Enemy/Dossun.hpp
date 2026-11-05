#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Dossun : public LiveActor {
public:
    Dossun(const char*);

    virtual void init(const JMapInfoIter&) override;
    virtual void appear() override;

    void initMapToolInfo(const JMapInfoIter&);
    void initShadow();
    void calcParameters();
    s32 getUpperFrame() const;
    void exeReady();
    void exeUpper();
    void exeFallSign();
    void exeFalling();
    void exeOnGround();
    void exeRising();

    Vec3f _8C;
    Vec3f _98;
    s32 mFallingTime;
    s32 mHoldTime;
    f32 mMovementDist;
    s32 mRisenWaitTime;
    s32 mShadowType;
    bool mHasShadow;
};
