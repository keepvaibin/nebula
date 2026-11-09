#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class TombSpiderVitalSpot : public PartsModel {
public:
    TombSpiderVitalSpot(LiveActor*, const char* pName, const Vec3f& rPos, const Vec3f& rRotate, const char* pJointName);

    virtual void init(const TableIter& rIter) override;

    void startChance();
    void endChance();
    void startDamage();
    void recover();
    bool isDamage() const;

    void exeWait();
    void exeChanceWait();
    void exeDamageStart();
    void exeDamageWait();
    void exeRecover();
};
