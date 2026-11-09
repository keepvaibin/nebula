#pragma once

#include "Game/Enemy/JumpGuarder.hpp"

class RingBeam;

class JumpBeamer : public JumpEmitter {
public:
    static const s32 sBeamCount = 3;

    JumpBeamer(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
