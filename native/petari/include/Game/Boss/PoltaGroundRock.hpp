#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Polta;
class ModelObj;

class PoltaGroundRock : public LiveActor {
public:
    PoltaGroundRock(const char*);

    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initBreakModel();
    void start(LiveActor*, const Vec3f&);
    void requestBreak();
    bool isEnableAttack() const;
    bool isEnableBreak() const;

    void exeSign();
    void exeJut();
    void exeWait();
    void exeBreak();

    LiveActor* mOwner;
    ModelObj* mBreakModel;
    Quat4f _94;
    Vec3f _A4;
};
