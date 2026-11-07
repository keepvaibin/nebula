#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class TombSpiderGland : public PartsModel {
public:
    TombSpiderGland(LiveActor*, const char*, const Vec3f&, const Vec3f&, const char*);

    virtual void init(const TableIter& rIter);

    void startActive();
    void startDamage();
    bool isActive() const;
    bool isDamage() const;

    void exeWait();
    void exeActiveWait();
    void exeDamageStart();
    void exeDamageWait();
};
