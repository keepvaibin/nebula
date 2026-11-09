#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class PartsModel;

class LavaProminence : public LiveActor {
public:
    LavaProminence(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void startClipped() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;

    void moveOnRail();
    void updateEffectClipping(bool isMoving);

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
    Vec3f mClipCenter;
    PartsModel* mBloomModel;
    Pos3f mBloomModelMtx;
    Quat4f mRotateQuat;
    Vec3f mRailDir;
    bool mEmitDropEffect;
};
