#pragma once

#include "Game/System/NerveExecutor.hpp"

class Polta;
class PoltaActionBase;
class PoltaDemo;
class PoltaWaitStart;
class PoltaBattleLv1;
class PoltaBattleLv2;
class TableIter;
class HitSensor;

class PoltaActionSequencer : public NerveExecutor {
public:
    PoltaActionSequencer(Polta*, const TableIter&);

    void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    bool updateAction();
    bool startAction();

    void exeWaitStart();
    void exeOpeningDemo();
    void exeBattleLv1();
    void exePowerUpDemo();
    void exeBattleLv2();
    void exeDownDemo();

    Polta* mPoltaPtr;
    PoltaDemo* mActionDemo;
    PoltaActionBase* mCurrentAction;
    PoltaWaitStart* mActionWaitStart;
    PoltaBattleLv1* mActionPoltaBattleLv1;
    PoltaBattleLv2* mActionPoltaBattleLv2;
};
