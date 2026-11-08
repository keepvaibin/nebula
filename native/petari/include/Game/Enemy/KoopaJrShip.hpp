#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class CannonShellHolder;
class HomingKiller;
class TableIter;
class Kameck;
class KoopaJr;
class ModelObj;

class KoopaJrShip : public LiveActor {
public:
    KoopaJrShip(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void initShells();
    void initKoopaJr(const TableIter&);
    void initKameck(const TableIter&);
    void initKiller();
    bool receiveMsgJetTurtleAttack(HitSensor*, HitSensor*);
    s32 getNumShootShells() const;
    f32 getPropellerRotSpeed() const;
    void updateCoordSpeed();
    void calcLauncherInfo(Vec3f*, Vec3f*, s32) const;
    void calcLauncherInfoKiller(Vec3f*, Vec3f*, s32) const;
    void shootShell(s32);
    bool tryShootAllKillers();
    void shootKiller(s32);
    void shootKillersAfterDamage();
    bool isExistActiveKiller() const;
    bool isExistActiveKameck() const;
    void shootMainShells();
    void emitDamageSmokeEffect();
    void emitDamageHitEffect();
    void updateKoopaJrPos();
    void updateKillerPos();
    void killAllSubModels();
    void setStateTurnFront();
    void exeAppear();
    void exeMove();
    void exeMoveFrontAttack();
    void exeShoot();
    void exeShootMain();
    void exeStopAtEnd();
    void exePowerUp();
    void exeDamage();
    void exeBreakStart();
    void exeBreak();
    void exeBreakEnd();
    void exeTurnFront();


    inline bool isNextStateDamage(s32& x) {
        return --x <= 0;
    }

    inline bool isStateBreak() const;

    CannonShellHolder* mShellHolder;
    CannonShellHolder* mMainShellHolder;
    KoopaJr* mJr;
    ModelObj* mShipBreakModel;
    ModelObj* mPodModel;
    MR::Vector< MR::FixedArray< Kameck*, 2 > > mKamecks;
    MR::Vector< MR::FixedArray< Kameck*, 2 > > mKamecksAtTurn;
    MR::FixedArray< HomingKiller*, 6 > mKillers;
    s32 mHitPoint;
    Vec3f mHitPos;
    Vec3f mHitDir;
    f32 mPropAngle;
    f32 mPropRotateSpeed;
    Pos3f mScrew00Mtx;
    Pos3f mScrew01Mtx;
    Rot3f mPropellerMtx;
    f32 mCoordSpeed;
    s32 mSpeedEaseStep;
    Mtx34f mShipMtx;
    Mtx34f mPodMtx;
    Vec3f mJrLocalPos;
    Vec3f mInitPos;
};
