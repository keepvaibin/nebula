#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class DashRing : public LiveActor {
public:
    DashRing(const char* pName) : LiveActor(pName) {
    }

    virtual ~DashRing();
    virtual void init(const JMapInfoIter& rIter) {
        initCommon(rIter, "DashRing");
    }
    virtual void draw() const;
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);

    void initCommon(const JMapInfoIter&, const char*);
    void calcAxis();
    void calcSubAxis();

    TVec3f mAxis;
    TVec3f mSubAxis;
    u16 mActiveTime;
    u16 mRevivalTime;
    s32 mBoostTime;
    s32 mMaxDuration;
    f32 mSpeedScale;
    bool mDoLoop;
    bool mDoTurn;
    TVec3f mInitialPosition;
};
