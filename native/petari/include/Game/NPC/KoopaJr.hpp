#pragma once

#include "Game/NPC/NPCActor.hpp"

class KoopaJr : public NPCActor {
public:
    KoopaJr(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();

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
