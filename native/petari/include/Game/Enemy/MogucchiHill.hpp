#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class Mogucchi;
class MogucchiHill;

class MogucchiHillPiece : public LiveActor {
public:
    /// @brief Creates a new `MogucchiHillPiece`.
    /// @param pName A pointer to the null-terminated name of the object.
    MogucchiHillPiece(const char* pName = "\203\202\203\117\203\142\203\140\222\313\203\163\201\133\203\130" /* モグッチ塚ピース */);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

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
    TPos3f _9C;
    bool _CC;
    bool _CD;
    bool mHasLightCtrl;
};

class MogucchiHill : public LiveActor {
public:
    MogucchiHill(LiveActor* pHost, s32 pieceCount, const char* pName);

    virtual void init(const JMapInfoIter& rIter);

    virtual bool notifyAppearAttackToPlayer(HitSensor* pSender, HitSensor* pReceiver) {
        return false;
    }

    virtual bool notifyWaitAttackToPlayer(HitSensor* pSender, HitSensor* pReceiver) {
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
    MogucchiHillPiece* _90;
    s32 _94;
    s32 _98;
    TPos3f mReserveMtx;
    bool _CC;
    f32 _D0;
    f32 _D4;
    u32 _D8;
    const char** mModelNameTable;
    const char* mJointName;
    const char* mAutoEffectName;
    bool mHasLightCtrl;
    s32 _EC;
    s32 _F0;
    s32 mAppearNum;
};
