#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TakoHeiInk : public LiveActor {
public:
    TakoHeiInk(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;

    void start(const Vec3f&, const Vec3f&, bool);
    void exeWait();
};
