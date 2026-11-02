#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FixedPosition;

class PartsModel : public LiveActor {
public:
    PartsModel(LiveActor*, const char*, const char*, MtxPtr, int, bool);

    virtual void init(const JMapInfoIter&) override;
    virtual void movement() override;
    virtual void calcAnim() override;
    virtual void calcViewAndEntry() override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void calcAndSetBaseMtx() override;

    void initFixedPosition(const TVec3f&, const TVec3f&, const char*);
    void initFixedPosition(const char*);
    void initFixedPosition(MtxPtr, const TVec3f&, const TVec3f&);
    void loadFixedPosition(const char*);
    void offFixedPosNormalizeScale();

    bool isInitFixedPosition() const {
        return mFixedPosition != nullptr;
    }

    LiveActor* mHost;
    FixedPosition* mFixedPosition;
    MtxPtr mMtx;
    bool mIsCalcOwnMtx;
    bool _99;
    bool mIsDead;
    bool _9B;
};
