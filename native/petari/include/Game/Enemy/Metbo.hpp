#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ActorMovementUtil.hpp"

class SpinHitController;
class AnimScaleController;
class WalkerStateBindStarPointer;

class Metbo : public LiveActor {
public:
    enum SeVersion {
        SeVersion_NORMAL = 0,
        SeVersion_2D = 1,
    };

    Metbo(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeNonActive();
    void endNonActive();
    void exeWait();
    void exeWalkAround();
    void exeSearch();
    void exeChaseStart();
    void exeChase();
    void endChase();
    void exeGuard();
    void exeTrampleDown();
    void exeBlowDown();
    void exeAttack();
    void exeRest();
    void exeSwoonStart();
    void exeSwoonLand();
    void exeSwoonSpin();
    void exeSwoonWait();
    void exeRecover();
    void exePointerSwoon();
    void endPointerSwoon();
    void moveOrFall(const MR::ActorMoveParam&, const Vec3f*);
    void startSwoon(bool);
    bool tryNonActive();
    bool trySearch();
    bool tryPointerSwoon();
    bool isNrvNormal() const;

    inline bool isNrvDown() const;
    inline bool isNrvSwoon() const;

    SpinHitController* mSpinHitController;
    AnimScaleController* mAnimScaleController;
    WalkerStateBindStarPointer* mStarPointerBind;
    Vec3f mWalkCenter;
    Vec3f mFront;
    Vec3f mWalkDir;
    bool mIsClipped;
};
