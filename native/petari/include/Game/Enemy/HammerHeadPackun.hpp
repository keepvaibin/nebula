#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Table.hpp"
#include <nebula_geometry.hpp>

class PartsModel;
class JointRumbler;

class HammerHeadPackun : public LiveActor {
public:
    HammerHeadPackun(const char*);

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeSearch();
    void exeAttack();
    void exeRest();
    void exeRecover();
    void exeDamage();
    void exeRise();
    void exeHit();
    void exeCrow();
    void exeSmashKill();
    void exeBlowKill();
    void exeNumb();
    void endNumb();
    void initPosture();
    void initSensor();
    void initShadow();
    void initLeaf(const TableIter&);
    bool isTargetInRange() const;
    bool isInFieldOfView(const Vec3f&) const;
    bool isAttackSensorValid() const;
    bool isChance() const;
    bool calcPlayerDir(Vec3f*) const;
    void turnTo(const Vec3f&, f32);
    void turnToAttackVec(s32);
    void verticalizeFrontVec();
    bool tryShiftNumb();
    bool receiveMsgPlayerAttackChance(u32, HitSensor*, HitSensor*);

    inline bool checkBlowAttack() const;

    PartsModel* mLeafModel;
    s32 mImmunityTime;
    Vec3f mUp;
    Vec3f mFront;
    Vec3f mAttackDir;
    JointRumbler* mJointRumbler;
    s32 mNumbTime;
};
