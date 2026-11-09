#pragma once

#include "Game/Boss/BossKameckSequencer.hpp"

class BossKameckStateBattle;

class BossKameckVs1 : public BossKameckSequencer {
public:
    BossKameckVs1();

    virtual void start() override;
    virtual void init(BossKameck*, const TableIter&) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void exeOpeningDemo();
    void exeBattleLv1();
    void exeBattleLv2();
    void exePowerUpDemo();
    void exeBattleLv3();
    void exeEndDemo();
    bool isBattle() const;

    BossKameckStateBattle* mStateBattle;
};
