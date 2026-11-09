#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class Mogucchi;
class MogucchiHill;

class MogucchiHillPiece : public LiveActor {
public:
    MogucchiHillPiece(const char* pName = "モグッチ塚ピース");

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initWithModelName(const char* pModelName, const char* pJointName, const char* pAutoEffectName, bool hasLightCtrl);
    void crumble();
    void destroy();
    void setSize(f32 size);
    bool isTargetGoingAway(HitSensor* pSender, HitSensor* pReceiver) const;

    void exeAppear();
    void exeWait();
    void exeCrumble();

    MogucchiHill* mHost;
    const char* mModelName;
    const char* mJointName;
    const char* mAutoEffectName;
    Pos3f mMtx;
    bool mIsHostLeft;
    bool mIsHostTouching;
    bool mHasLightCtrl;
};

class MogucchiHill : public LiveActor {
public:
    MogucchiHill(LiveActor* pHost, s32 pieceCount, const char* pName);

    virtual void init(const TableIter& rIter) override;

    virtual bool notifyAppearAttackToPlayer(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    virtual bool notifyWaitAttackToPlayer(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    void start();
    void end();
    void startNaturally();
    void endNaturally();
    void killPieces(MogucchiHillPiece* pIter);
    void pauseOff();
    void killAllPieces();
    void setAppearNum(s32 appearNum);
    void createPieces();
    void appearPiece();
    void killPieceOverAppear();
    void killPieceIfAlive();
    f32 getDistanceFromReserveMtx() const;
    void reserveCurrentMtx();

    void exeMove();
    void exeWait();

    LiveActor* mHost;
    MogucchiHillPiece* mPieces;
    s32 mPieceNum;
    s32 mPieceIndex;
    Pos3f mReserveMtx;
    bool mHasReserveMtx;
    f32 mAppearDistance;
    f32 mAppearDistanceMax;
    u32 mModelNum;
    const char* const* mModelNameTable;
    const char* mJointName;
    const char* mAutoEffectName;
    bool mHasLightCtrl;
    s32 mSizeAnimState;
    s32 mSizeStep;
    s32 mAppearNum;
};
