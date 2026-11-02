#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class PartsModel;

class LavaProminence : public LiveActor {
public:
    /// @brief Creates a new `LavaProminence`.
    /// @param pName A pointer to the null-terminated name of the object.
    LavaProminence(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void kill();
    virtual void startClipped();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void moveOnRail();
    void setGravityAndMakeMtx();
    void updateEffectClipping(bool);

    void exeWaitSwitch();
    void exeWait();
    void exeSign();
    void exeMoveStartExtra();
    void exeMoveStart();
    void exeMoveLoop();
    void exeMoveEnd();
    void exeMoveEndExtra();

    bool isNrvMove();

    s32 mWaitTime;
    f32 mMoveSpeed;
    f32 mMoveRailCoord;
    f32 mMoveRailEndOffset;
    TVec3f mRailStartDir;
    TVec3f mRailEndDir;
    TVec3f mRailStartPos;
    TVec3f mRailEndPos;
    TPos3f mEffectStartMtx;
    TPos3f mEffectEndMtx;
    TVec3f _12C;
    PartsModel* mBloomModel;
    TPos3f mBloomModelMtx;
    TQuat4f _16C;
    TVec3f mRailDir;
    bool mEmitDropEffect;
    TPos3f _18C;
};
