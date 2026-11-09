#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class TombSpiderGland : public PartsModel {
public:
    TombSpiderGland(LiveActor*, const char* pName, const Vec3f& rPos, const Vec3f& rRotate, const char* pJointName);

    virtual void init(const TableIter& rIter) override;

    void startActive();
    void startDamage();
    bool isActive() const;
    bool isDamage() const;

    void exeWait();
    void exeActiveWait();
    void exeDamageStart();
    void exeDamageWait();
};
