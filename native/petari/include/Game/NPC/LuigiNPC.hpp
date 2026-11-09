#pragma once

#include "Game/NPC/NPCActor.hpp"

class TakeOutStar;

class LuigiNPC : public NPCActor {
public:
    enum Type {
        Type_Invalid = -1,
        Type_Afraid,
        Type_Normal,
        Type_Event,
        Type_OnTree,
        Type_Arrested,
        Type_Count,
    };

    LuigiNPC(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;


    void setShadowDrop(const char*);
    bool branchFunc(u32);
    bool eventFunc(u32);
    bool trySetNerveAfraid();
    void setWaitAction();
    void setAfraidAction();
    bool trySetNerveArrested();
    bool isDeclarePowerStarType();

    void exeWait();
    void exeReaction();
    void exeTakeOutStar();
    void exeAfraidWait();
    void exeAfraidSquat();
    void exeAfraidRise();
    void exeArrestedWait();
    void exeArrestedJump();
    void exeOnTreeWait();
    void exeOnTreeTouch();
    void exeOnTreeFall();
    void exeOnTreeLand();

    TakeOutStar* mTakeOutStar;
    s32 mType;
};
