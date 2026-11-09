#pragma once

#include "Game/Boss/DinoPackunBattleEggVs2.hpp"
#include "Game/Boss/DinoPackunBattleVs2Lv1.hpp"
#include "Game/Boss/DinoPackunDemo.hpp"
#include "Game/Boss/DinoPackunFire.hpp"
#include "Game/Boss/DinoPackunSequencer.hpp"
#include "Game/Boss/DinoPackunTrackFire.hpp"

class DinoPackunVs2 : public DinoPackunSequencer {
public:
    DinoPackunVs2(DinoPackun*);

    virtual void start() override;
    virtual void init() override;
    virtual bool isUseEggShell() const override {
        return true;
    };
    virtual s32 getVsCount() const override {
        return 2;
    };

    void exeWaitStart();
    void exeOpeningDemo();
    void exeBattleEgg();
    void exeCryDemo();
    void exeBattleLv1();
    void exeAngryDemo();
    void exeBattleLv2();
    void exeBattleLv3();
    void exeDownDemo();

    DinoPackunDemo* mDemo;
    DinoPackunBattleEggVs2* mEgg;
    DinoPackunBattleVs2Lv1* mBattleLv1;
    DinoPackunTrackFireHolder* mTrackFireHolder;
    DinoPackunFireHolder* mFireHolder;
};
