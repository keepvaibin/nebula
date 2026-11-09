#pragma once

#include "Game/NPC/NPCActor.hpp"

class KoopaJr : public NPCActor {
public:
    KoopaJr(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void setStateShipBattleAppear();
    void setStateShipBattlePowerUp();
    void setStateShipBattleEscape();
    void setStateShipBattleShipDamage();
    void endShipBattleTalk();
    void startShipBattleTalk();

    void exeWait();
    void exeReaction();
    void exeReactionEnd();
    void exeShipBattleAppear();
    void exeShipBattlePowerUp();
    void exeShipBattleEscape();
    void exeShipBattleShipDamage();
    void exeShipBattleDemoTalkStart();
    void exeShipBattleDemoTalkWait();

private:
    bool mIsInvalidAppearStarPiece;
    bool mIsShipBattleTalk;
};
