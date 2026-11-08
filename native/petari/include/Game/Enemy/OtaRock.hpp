#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class AnimScaleController;
class CocoNutBall;
class FireBall;
class FixedPosition;

class OtaRock : public LiveActor {
public:
    OtaRock(const char* pName);

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);


    void initModel();
    void initSensor();
    void updateBaseMtx();
    CocoNutBall* getDisappearedCocoNut();
    FireBall* getDisappearedFireBall();
    s32 getDisappearedCocoNutNum() const;
    s32 getDisappearedFireBallNum() const;
    bool isValidThrowCocoNut() const;
    bool isValidThrowFireBall() const;
    void throwCocoNut();
    void throwFireBall();
    bool tryToHide();

    void exeWait();
    void exeThrowCocoNut();
    void exeThrowFireBall();
    void exeDown();
    void exeHide();
    void exeHideWait();
    void exeShow();

private:
    bool mNoThrowCocoNut;
    CocoNutBall* mCocoNutArray;
    FireBall* mFireBallArray;
    FixedPosition* mFixedPosition;
    s32 mThrowCocoNutCounter;
    u32 mWaitFrame;
    Pos3f mBaseMtx;
    Vec3f mFront;
    AnimScaleController* mAnimScaleController;
};
