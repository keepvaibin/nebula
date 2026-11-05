#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class KabokuriFire : public LiveActor {
public:
    KabokuriFire(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual MtxPtr getBaseMtx() const {
        return const_cast< Pos3f* >(&_8C)->toMtxPtr();
    }
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void start(const Vec3f&, bool);
    void exeWait();

private:
    Pos3f _8C;
};
