#pragma once

#include "Game/Enemy/JumpGuarder.hpp"

class RingBeam;

class JumpBeamer : public JumpEmitter {
public:
    static const s32 sBeamCount = 3;

    JumpBeamer(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    bool isHeldBySwitchB() const;
    void syncSwitchOffB();
    void exeHide();
    void exeUp();
    void exeWait();
    void exeDown();
    void exeHopStart();
    void exeHopWait();
    void exeHopJump();
    void exeHopEnd();
    void exePreOpen();
    void exeOpen();
    void exeClose();
    void exeInter();

    RingBeam* mBeams[sBeamCount];
};
