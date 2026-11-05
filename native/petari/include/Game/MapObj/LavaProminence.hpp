#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

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
    Vec3f mRailStartDir;
    Vec3f mRailEndDir;
    Vec3f mRailStartPos;
    Vec3f mRailEndPos;
    Pos3f mEffectStartMtx;
    Pos3f mEffectEndMtx;
    Vec3f _12C;
    PartsModel* mBloomModel;
    Pos3f mBloomModelMtx;
    Quat4f _16C;
    Vec3f mRailDir;
    bool mEmitDropEffect;
    Pos3f _18C;
};
