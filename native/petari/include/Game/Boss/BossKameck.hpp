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

    virtual void init(const TableIter&) override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

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
    Quat4f mPoseQuat;
    Vec3f mFront;
    BossKameckSequencer* mSequencer;
    KameckHolder* mKameckHolder;
    ActorJointCtrl* mJointCtrl;
    ActiveActorList* mActorList;
    BossKameckBeamEventListener* mBeamEventListener;
    BossKameckMoveRail** mMoveRail;
    s32 mMoveRailNum;
    Vec3f mLastVelocity;
    s32 mObjArg7;
};

class BossKameckBeamEventListener : public KameckBeamEventListener {
public:
    BossKameckBeamEventListener(BossKameck* pBoss) : KameckBeamEventListener(), mBossKameck(pBoss) {
    }

    virtual void hitBeam(s32 type) override {
        mBossKameck->hitBeam(type);
    }

    BossKameck* mBossKameck;
};

namespace MR {
    NameObj* createBossKameck1(const char*);
    NameObj* createBossKameck2(const char*);
};  // namespace MR
