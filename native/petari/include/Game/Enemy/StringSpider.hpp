#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinHitController;
class AnimScaleController;
class AnimStampController;
class StringSpiderString;
class FixedPosition;

class StringSpider : public LiveActor {
public:
    StringSpider(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void calcAnim();
    virtual void kill();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    s32 getSearchToAttackTime();
    void doSwingAction(f32, bool);
    void reactDPD();
    void reactStringForce();
    bool isOverStringLength(Vec3f*, f32*);
    Vec3f calcToRootVector();
    void calcPosture(f32, bool);
    void tearDownDpdGripped();
    void extendString();
    void shortenString(f32);
    bool reactSpinHit(HitSensor*, HitSensor*);
    void startSwingLevelSound();

    void exeWait();
    void endWait();
    void exeDownStart();
    void exeDownEnd();
    void exeDown();
    void exeUp();
    void exeSearch();
    void exeAttack();
    void exeHit();
    void exeSpinned();
    void exeTrampleFall();
    void exeWallHitDeath();
    void exeBlow();
    void exeDpdGripped();
    void endDpdGripped();

    SpinHitController* mSpinHitController;
    AnimScaleController* mScaleController;
    AnimStampController* mStampController;
    StringSpiderString* mString;
    FixedPosition* mFixedPos;
    Quat4f _A0;
    f32 _B0;
    f32 _B4;
    bool _B8;
    s32 _BC;
    Vec3f* _C0;
    Vec3f* _C4;
};

class StringSpiderString : public LiveActor {
public:
    StringSpiderString(const char*);

    virtual void calcAndSetBaseMtx();

    Quat4f _8C;
    Vec3f _9C;
};
