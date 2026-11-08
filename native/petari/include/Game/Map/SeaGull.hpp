#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SeaGullGroup;

class SeaGull : public LiveActor {
public:
    SeaGull(SeaGullGroup* pGroup);

    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void exeHoverFront();
    void exeHoverLeft();
    void exeHoverRight();
    void updateHover();

    SeaGullGroup* mSeaGullGroup;
    s32 mPointIndex;
    bool mIsReverse;
    s32 mTargetUpdateTimer;
    Vec3f* mTargetPosition;
    Vec3f mUp;
    Vec3f mFront;
    Vec3f mBankedUp;
    Vec3f mSide;
    s32 mHoverTimer;
    f32 mBankAngle;
    s32 mGlideTimer;
    s32 mLiftTimer;
    s32 mChirpTimer;
};

class SeaGullGroup : public LiveActor {
public:
    SeaGullGroup(const char* pName);

    virtual void init(const TableIter& rIter);

    Vec3f* updatePosInfo(s32* pPointIndex, bool isReverse) const;

    s32 mPointCount;
    Vec3f* mPoints;
};
