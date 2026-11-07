#pragma once

#include "Game/Enemy/KameckBeam.hpp"

class ActiveActorList;
class ActorJointCtrl;
class BossKameck;
class BossKameckBeamEventListener;
class BossKameckMoveRail;
class BossKameckSequencer;
class BossKameckVs2;
class KameckHolder;

class BossKameck : public LiveActor {
public:
    BossKameck(const char*, const char*);

    virtual void init(const TableIter&);
    virtual void makeActorDead();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    BossKameckMoveRail* getMoveRail(s32);
    void startSequence();
    void startDemoAppearKameck();
    void endDemoAppearKameck();
    void appearKameck();
    void deadKameck();
    s32 getLivingKameckNum() const;
    void appearStarPieceToPlayer(s32);
    void endDemo();
    void setPose(MtxPtr);
    void killAllBeam();
    void updatePose();
    void initKameckHolder(const TableIter&);
    void initMoveRail(const TableIter&);
    void appearStarPieceToUp(s32);
    void startDemo();
    void hitBeam(s32);

    const char* mModelName;
    Quat4f _90;
    Vec3f _A0;
    BossKameckSequencer* mSequencer;
    KameckHolder* mKameckHolder;
    ActorJointCtrl* mJointCtrl;
    ActiveActorList* mActorList;
    BossKameckBeamEventListener* mBeamEventListener;
    BossKameckMoveRail** mMoveRail;
    s32 mMoveRailNum;
    Vec3f _C8;
    s32 mObjArg7;
};

class BossKameckBeamEventListener : public KameckBeamEventListener {
public:
    BossKameckBeamEventListener(BossKameck* pBoss) : KameckBeamEventListener(), mBossKameck(pBoss) {
    }

    virtual void hitBeam(s32 type) {
        mBossKameck->hitBeam(type);
    }

    BossKameck* mBossKameck;
};

namespace MR {
    NameObj* createBossKameck1(const char*);
    NameObj* createBossKameck2(const char*);
};  // namespace MR
