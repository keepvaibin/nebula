#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class DinoPackun;
class HitSensor;

// A phase of the Dino Piranha fight; the sequencer passes it the boss's sensor messages (the Tail ones from the tail).
class DinoPackunAction : public ActorStateBase< DinoPackun > {
public:
    DinoPackunAction(const char* pName, DinoPackun* pHost);

    virtual void attackSensor(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    }

    virtual bool receiveMsgPlayerAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    virtual bool receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    virtual bool receiveMsgPush(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    virtual bool receiveOtherMsg(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    virtual void attackSensorTail(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    }

    virtual bool receiveMsgPlayerAttackTail(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    virtual bool receiveOtherMsgTail(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    bool sendBlowAttackMessage(HitSensor* pSender, HitSensor* pReceiver, bool isFire);
    bool sendHitAttackMessage(HitSensor* pSender, HitSensor* pReceiver, bool isFire);
    bool updateTurn(s32 turnSteps, f32 turnDegree);
    bool updateWalk(s32 walkSteps, f32 speed, s32 footPrintPeriod);
    bool updateChase(s32 chaseSteps, f32 faceDegree, f32 turnDegree, f32 speed, s32 accelSteps, s32 footPrintPeriod);
    void selectTurnDirection();
    bool updateStart();
    bool updateFind(s32 landStep, f32 turnDegree);
    bool updateCoolDown(s32 coolDownSteps);
    bool updateAttackHit();

    // 1 turns right, -1 left.
    f32 mSide;
};
