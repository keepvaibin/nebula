#pragma once

#include "Game/LiveActor/ModelObj.hpp"
#include <nebula_geometry.hpp>

class PoltaArm : public ModelObj {
public:
    PoltaArm(const char* pName, const char* pModelName, MtxPtr pMtx);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void makeActorDead() override;
    virtual void control() override;

    void initPartsModel();
    void exeDamage();
    void exeBreak();
    void exeBreakBody();
    void exeRepair();
    void endRepair();
    void exeBroken();
    void exeControlled();
    void exeWaitDamageEnd();
    void exeWaitRepairEnd();
    bool isEnableHitSensor() const;
    bool isEnableAttack() const;
    bool isBroken() const;
    void start();
    bool requestBreakBody();
    bool requestDamage();
    bool requestStartControll();
    bool requestControlled(const char*);
    void appearBreakModel();

    ModelObj* mFormationModel;
    ModelObj* mBreakModel;
    Pos3f mBreakModelMtx;
    s32 mHitPoint;
    s32 mHitPointMax;
    s32 mBrokenCounter;
    bool mIsLeftArm;
};
