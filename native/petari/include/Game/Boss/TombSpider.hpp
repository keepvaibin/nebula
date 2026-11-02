#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class TombSpiderAction1st;
class TombSpiderAction2nd;
class TombSpiderActionCocoon;
class TombSpiderDemo;
class TombSpiderEnvironment;
class TombSpiderParts;
class TombSpiderSensorCtrl;

class TombSpider : public LiveActor {
public:
    TombSpider(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void updateHitSensor(HitSensor* pSensor);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

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

    inline const f32 getRotate() const {
        return mRotation.z;
    }

    TombSpiderSensorCtrl* mSensorCtrl;
    TombSpiderParts* mParts;
    TombSpiderEnvironment* mEnvironment;
    TombSpiderActionCocoon* mActionCocoon;
    TombSpiderAction1st* mAction1st;
    TombSpiderAction2nd* mAction2nd;
    TombSpiderDemo* mDemo;
    bool mNoctilucaActive;
    ActorCameraInfo* mCameraInfo;
};
