#pragma once

#include "Game/NPC/NPCActor.hpp"

class CollisionParts;
class ModelObj;

class HoneyQueen : public NPCActor {
public:
    HoneyQueen(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void calcAnim() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void fadeOut();
    void fadeIn();
    void talkEntry();
    void switchFunc();

    void exeWait();
    void exeReady();
    void exeDemo();
    void exeFade();
    void exeTalk();
    void exeItch();
    void exeEvent();
    void exeAfter();

    ModelObj* mWing;
    CollisionParts* mCollisionParts[10];
};
