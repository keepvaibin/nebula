#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class ProjmapEffectMtxSetter;

class OtaKingMagma : public PartsModel {
public:
    OtaKingMagma(LiveActor*, s32);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;

    void appearDemo();
    void attack();
    void fireAttack();
    void hitBack();
    void damage();
    void down();

    void exeWait();
    void exeAppearDemo();
    void exeAttack();
    void exeFireAttack();
    void exeHitBack();
    void exeDamage();
    void exeDown();
    void exeEndWait();

    void startBckThisAndBloomModel(const char*);

    PartsModel* mBloomModel;
    ProjmapEffectMtxSetter* mProjmapSetter;
    ProjmapEffectMtxSetter* mBloomProjmapSetter;
};
