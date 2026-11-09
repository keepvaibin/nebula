#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class KabokuriFire : public LiveActor {
public:
    KabokuriFire(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual MtxPtr getBaseMtx() const override {
        return const_cast< Pos3f* >(&mBaseMtx)->toMtxPtr();
    }
    virtual void control() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void start(const Vec3f&, bool);
    void exeWait();

private:
    Pos3f mBaseMtx;
};
