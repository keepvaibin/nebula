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
class NameObjArchiveListCollector;

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

    static void makeArchiveList(NameObjArchiveListCollector*, const TableIter&);

    inline bool isNextStateDamage(s32& x) {
        return --x <= 0;
    }

    inline bool isStateBreak() const;

    s32 get_D0() const {
        return _D0;
    }

    CannonShellHolder* mShellHolder;
    CannonShellHolder* mMainShellHolder;
    KoopaJr* mJr;
    ModelObj* mShipBreakModel;
    ModelObj* mPodModel;
    MR::Vector< MR::FixedArray< Kameck*, 2 > > mKamecks;
    MR::Vector< MR::FixedArray< Kameck*, 2 > > _AC;
    MR::FixedArray< HomingKiller*, 6 > mKillers;
    s32 _D0;
    Vec3f _D4;
    Vec3f _E0;
    f32 _EC;
    f32 mPropRotateSpeed;
    Pos3f mScrew00Mtx;
    Pos3f mScrew01Mtx;
    Rot3f mPropellerMtx;
    f32 _184;
    s32 _188;
    Mtx34f mShipMtx;
    Mtx34f mPodMtx;
    Vec3f _1EC;
    Vec3f _1F8;
};
