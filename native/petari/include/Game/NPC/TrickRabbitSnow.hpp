#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FootPrint;
class RabbitStateCaught;
class RabbitStateWaitStart;
class SpotMarkLight;
class TalkMessageCtrl;
class TrickRabbitSnowListener;
class WalkerStateBlowDamage;
class WalkerStateRunaway;

class TrickRabbitSnow : public LiveActor {
    friend class TrickRabbitSnowCollect;

public:
    TrickRabbitSnow(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void setListener(TrickRabbitSnowListener*);
    void initSensor();
    void initState();
    void clearFootPrint();
    bool receiveMsgBlowDamage(HitSensor*, HitSensor*);
    bool requestCaught();
    bool requestStartHideSnow();
    void setFinishPosition();
    void setNotCaughtable();
    bool isGiveUp() const;
    bool isHideSnow() const;
    bool isEnableCaught() const;
    bool isEnableBlowDamage() const;
    void startJumpSound();

    void exeWaitSnow();
    void exeStartHideSnow();
    void exeVanish();
    void exeHideSnow();
    void exeAppearFromSnow();
    void exeRunaway();
    void exeBlowDamage();
    void exeCaught();
    void exeGiveUp();

private:
    Quat4f mFinishQuat;
    Vec3f mFinishPos;
    Quat4f mRotateQuat;
    Vec3f mFrontVec;
    RabbitStateWaitStart* mStateWaitStart;
    WalkerStateRunaway* mStateRunaway;
    WalkerStateBlowDamage* mStateBlowDamage;
    RabbitStateCaught* mStateCaught;
    TalkMessageCtrl* mMsgCtrl;
    FootPrint* mFootPrint;
    SpotMarkLight* mSpotMarkLight;
    TrickRabbitSnowListener* mListener;
    s32 mNotCaughtableTimer;
    bool mIsValidAppearStarPiece;
};

class TrickRabbitSnowListener {
public:
    TrickRabbitSnowListener();

    virtual void noticeStart(TrickRabbitSnow*) {
    }

    virtual void noticeAppear(TrickRabbitSnow*) {
    }

    virtual void noticeCaught(TrickRabbitSnow*) {
    }

    virtual void noticeGiveUp(TrickRabbitSnow*) {
    }
};
