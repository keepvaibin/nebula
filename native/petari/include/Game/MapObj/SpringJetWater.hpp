#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpringJetWater : public LiveActor {
public:
    SpringJetWater(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void exeWait();
    void exeJet();
    void exeEnd();

    const char* mObjName;
    Vec3f mUp;
};
