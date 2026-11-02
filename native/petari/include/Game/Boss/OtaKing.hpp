#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"

class ActorCameraInfo;
class AnimScaleController;
class CocoNutBall;
class FireBall;
class FireBubble;
class FixedPosition;
class NameObjArchiveListCollector;
class OtaKingMagma;
class OtaKingLongFoot;
class PartsModel;

class OtaKing : public LiveActor {
public:
    OtaKing(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void makeActorAppeared();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void startAppearDemo();
    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);
    void initMapToolInfo(const JMapInfoIter& rIter);
    void initModel(const JMapInfoIter& rIter);
    void initSensor();
    void dirToPlayer();
    CocoNutBall* getDisappearedCocoNut();
    FireBall* getDisappearedFireBall();
    FireBubble* getDisappearedBubble();
    s32 getDisappearedCocoNutNum() const;
    s32 getDisappearedFireBallNum() const;
    s32 getDisappearedFireBubbleNum() const;
    bool isValidThrowCocoNut() const;
    bool isValidThrowFireBall() const;
    bool isValidBubbleAttack() const;
    void damage();
    void throwCocoNut();
    void throwFireBall();
    void startBckWaitIfNotPlaying();
    void startBckWithFrontFoot(const char*);
    bool isNerveValidNearAttack() const;
    void appearBubble();
    void startDemo();
    void appearStarPiece();
    void initLongFoot(const JMapInfoIter& rIter);
    bool tryThrowCocoNutOrFireBallIfWait(s32);

    void exeWaitOnSwitch();
    void exeAppearDemo();
    void exeWait();
    void exeThrowCocoNutWait();
    void exeThrowCocoNut();
    void exeThrowFireBallWait();
    void exeThrowFireBall();
    void exeDamage();
    void exePowerUp();
    void exeDown();
    void exeDownDemo();
    void exeAppearStar();
    void exeDead();
    void exeHitBackStart();
    void exeHitBack();
    void exeNearAttack();
    void exeBubbleAttack();
    void exeWaitStartDemo();

    bool isSensorBodyOrFace(HitSensor*);
    bool isDamageNerve();
    bool isOneHP() const;

    void invalidateClippingAndStartDemo(const char* demoStr, const Nerve* pNerve1, const Nerve* pNerve2) {
        MR::invalidateClipping(this);
        MR::requestStartDemo(this, demoStr, pNerve1, pNerve2);
    }

    void validateClippingAndEndDemo(const char* demoStr) {
        MR::validateClipping(this);
        MR::endDemo(this, demoStr);
    }

    PartsModel* mFeet[2];
    OtaKingLongFoot* mLongFeet[4];
    OtaKingMagma* mMagma;
    TPos3f mLongFootMtx;
    CocoNutBall* mCocoNutBallArray;
    FireBall* mFireBallArray;
    FireBubble* mFireBubbleArray;
    FixedPosition* mThrowPos;
    s32 mHits;
    s32 _EC;
    f32 mTurnSpeed;
    AudAnmSoundObject* mSoundObj;
    TVec3f mSoundObjPos;
    FixedPosition* mBubbleAppearPos;
    CocoNutBall* mDownCocoNut;
    ActorCameraInfo* mAnimCamera;
    char mBckLBuffer[0x20];
    char mBckRBuffer[0x20];
    bool mIsLv2;
    AnimScaleController* mScaleController;
};
