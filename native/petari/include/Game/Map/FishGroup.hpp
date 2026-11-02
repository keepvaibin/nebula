#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FishGroup;

class Fish : public LiveActor {
public:
    Fish(const char*, FishGroup*, int, bool);

    virtual ~Fish();

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void exeWander();
    void exeApproach();
    void exeEscape();

    bool tryEscapeFromPlayer();
    void updateFollowPointPos();

    FishGroup* mGroup;
    int mFishType;
    bool mCanEscape;
    TVec3f mFollowPos;
    TVec3f mFollowPosOffset;
    TVec3f mFront;
    f32 mDistanceToApproach;
    s32 mTimeToSearchPlayer;
    s32 mStepToMove;
    TVec3f* mEscapePos;
};

class FishGroup : public LiveActor {
public:
    FishGroup(const char*);

    virtual ~FishGroup();

    virtual void init(const JMapInfoIter& rIter);
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();

    TVec3f mUp;
    s32 mFishNum;
    Fish** mFish;
    f32 mSpeed;
    TVec3f mClippingCenter;
};
