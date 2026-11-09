#pragma once

#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Table.hpp"
#include <nebula_geometry.hpp>

class ItemGenerator;
class WalkerStateStagger;
class WalkerStateChase;
class WalkerStateWander;
class WalkerStateFindPlayer;

class Kuribo : public LiveActor {
public:
    enum SeVersion {
        SeVersion_NORMAL = 0,
        SeVersion_2D = 1,
    };

    Kuribo(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initState();
    void initSensor();
    void initAppearState(const TableIter&);
    bool requestDead();
    bool requestFlatDown(HitSensor*, HitSensor*);
    bool requestHipDropDown(HitSensor*, HitSensor*);
    bool requestPressDown();
    bool requestStagger(HitSensor*, HitSensor*);
    bool requestBlowDown(HitSensor*, HitSensor*);
    bool requestAttackSuccess();
    void onNoGravitySupport();
    void setUp(const Vec3f& rPosition, const Quat4f& rPose, const Vec3f& rVelocity);
    void appearBlowed(const Vec3f& rPosition, const Quat4f& rPose, const Vec3f& rVelocity);
    void appearHipDropped(const Vec3f& rPosition, const Quat4f& rPose);
    bool tryNonActive();
    bool tryActive();
    bool tryEndBlow();
    bool tryEndBlowLand();
    bool tryFind();
    bool tryPointBind();
    bool tryDead();
    void exeWander();
    void exeFindPlayer();
    void exeChase();
    void exeStagger();
    void exeNonActive();
    void endNonActive();
    void exeAppearFromBox();
    void exeBlow();
    void exeAttackSuccess();
    void exeHipDropDown();
    void exeFlatDown();
    void exePressDown();
    void exeBlowDown();
    void exeBindStarPointer();
    void endBindStarPointer();
    void exeBlowLand();
    void calcPassiveMovement();
    bool isEnableKick() const;
    bool isUpsideDown() const;
    bool isEnableDead() const;
    bool isEnablePointBind() const;
    bool isEnableAttack() const;
    bool isEnableStamp() const;
    bool isEnablePanch() const;
    bool isEnablePushMove() const;

    AnimScaleController* mScaleController;
    ItemGenerator* mItemGenerator;
    WalkerStateWander* mStateWander;
    WalkerStateFindPlayer* mStateFindPlayer;
    WalkerStateBindStarPointer* mBindStarPointer;
    WalkerStateStagger* mStateStagger;
    WalkerStateChase* mStateChase;
    Quat4f mPoseQuat;
    Vec3f mFront;
    bool mIsNoGravitySupport;
    bool mHasItem;
};

namespace MR {
    LiveActor* createNoItemKuriboActor(const char*);
};  // namespace MR
