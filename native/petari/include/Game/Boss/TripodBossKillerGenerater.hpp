#pragma once

#include "Game/Boss/TripodBossFixPartsBase.hpp"

class HomingKiller;

class TripodBossKillerGenerater : public TripodBossFixPartsBase {
public:
    TripodBossKillerGenerater(const char* pName = "三脚ボスキラー発生源");

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

    virtual void calcTripodLocalMatrix(Pos3f*) override;
    virtual void activateTripodBoss() override;

    void setLocalMatrix(const Pos3f&);
    void setActiveLebel(s32);
    void setHasCollision(bool);
    bool tryShootSetting();
    bool tryShoot();
    bool tryCoolDown();
    bool tryReady();
    bool tryBreak();
    bool tryAbort();
    bool requestBreak();

    void exeNonActive();
    void exeHide();
    void exeShowDemo();
    void exeReady();
    void exeShootSetting();
    void exeShoot();
    void exeCoolDown();
    void exeStop();
    void exeRestart();
    void exeBreak();

    Pos3f mLocalMtx;
    HomingKiller* mKiller;
    f32 mCanShootSettingPlayerDistance;
    s32 mActiveLabel;
    bool mHasCollision;
    bool mIsWaitPlayerAway;
};
