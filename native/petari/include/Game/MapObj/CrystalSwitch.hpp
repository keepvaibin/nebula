#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CrystalSwitch : public LiveActor {
public:
    CrystalSwitch(const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    bool trySwitchDown();
    bool tryOn();
    bool tryOff();
    void exeOff();
    void exeSwitchDown();
    void exeOn();
    void exeSwitchUp();
    void calcRotSpeed();

    f32 mRotateSpeed;
    u8 mIsHit;
};
