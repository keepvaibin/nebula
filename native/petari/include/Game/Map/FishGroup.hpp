#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FishGroup;

class Fish : public LiveActor {
public:
    Fish(const char*, FishGroup*, int, bool);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void exeWander();
    void exeApproach();
    void exeEscape();

    bool tryEscapeFromPlayer();
    void updateFollowPointPos();

    FishGroup* mGroup;
    int mFishType;
    bool mCanEscape;
    Vec3f mFollowPos;
    Vec3f mFollowPosOffset;
    Vec3f mFront;
    f32 mDistanceToApproach;
    s32 mTimeToSearchPlayer;
    s32 mStepToMove;
    Vec3f* mEscapePos;
};

class FishGroup : public LiveActor {
public:
    FishGroup(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;

    Vec3f mUp;
    s32 mFishNum;
    Fish** mFish;
    f32 mSpeed;
    Vec3f mClippingCenter;
};
