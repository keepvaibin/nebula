#pragma once

#include "Game/Boss/BossKameckSequencer.hpp"

class BossKameckBarrier;
class BossKameckStateBattle;

class BossKameckVs2 : public BossKameckSequencer {
public:
    BossKameckVs2();

    virtual void start() override;
    virtual void init(BossKameck*, const TableIter&) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void exeOpeningDemo();
    void exeBattleLv1();
    void exeBattleLv2();
    void exePowerUpDemo();
    void exeBattleLv3();
    void exeBattleLv4();
    void exeEndDemo();
    bool isBattle() const;

    BossKameckStateBattle* mStateBattle;
    BossKameckBarrier* mBarrier;
};
