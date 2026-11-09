#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class TombSpiderActionBattle;
class TombSpiderActionCocoon;
class TombSpiderDemo;
class TombSpiderEnvironment;
class TombSpiderParts;
class TombSpiderSensorCtrl;

class TombSpider : public LiveActor {
public:
    TombSpider(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void updateHitSensor(HitSensor* pSensor) override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWaitPlayer();
    void exeDemoGateOpen();
    void exeActionCocoon();
    void exeDemoCocoonBreak();
    void exeDemoBattle1stStart();
    void exeDemoBattle1stStartJumpToPlayer();
    void exeAction1st();
    void exeDemoBattle1stEnd();
    void exeDemoBattle2ndStart();
    void exeAction2nd();
    void exeDemoDeath();
    void exeWaitDemo();

    inline f32 getRotate() const {
        return mRotation.z;
    }

    TombSpiderSensorCtrl* mSensorCtrl;
    TombSpiderParts* mParts;
    TombSpiderEnvironment* mEnvironment;
    TombSpiderActionCocoon* mActionCocoon;
    TombSpiderActionBattle* mAction1st;
    TombSpiderActionBattle* mAction2nd;
    TombSpiderDemo* mDemo;
    bool mNoctilucaActive;
    ActorCameraInfo* mCameraInfo;
};
