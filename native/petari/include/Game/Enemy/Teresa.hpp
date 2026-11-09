#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ActorMovementUtil.hpp"

class KeySwitch;
class PartsModel;
class Triangle;

class Teresa : public LiveActor {
public:
    Teresa(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initDummyModel(const TableIter&);
    void initFromPlacementParam(const TableIter&);
    void initSensor();
    void initBind();
    bool filterBind(const Triangle*);
    bool requestAttack(HitSensor*, HitSensor*);
    bool requestDrift();
    bool requestLoveHit(HitSensor*, HitSensor*);
    bool requestSearchLightDead();
    void setStartNerve();
    bool tryAppearEnd();
    bool tryRailTurn();
    bool tryRailTurnEnd();
    bool tryWalk();
    bool tryWalkEnd();
    bool tryChase();
    bool tryChaseEnd();
    bool tryShay();
    bool tryShayEnd();
    bool tryLoveFind();
    bool tryLoveEnd();
    bool tryLoveFindEnd();
    bool tryLoveChaseEnd();
    bool tryLoveHitEnd();
    bool tryAggressiveEnd();
    bool tryAttackSuccessEnd();
    bool tryDriftEnd();
    bool tryHideWater();
    bool tryHideWall();
    bool tryHideWallEnd();
    bool tryAscension();
    void endAppear();
    void updateAppear(const Vec3f& rDir, const char* pAction);
    void exeAppearFromWall();
    void exeAppearFromGround();
    void exeWait();
    void exeWalk();
    void exeRailWalk();
    void exeRailTurn();
    void exeLoveFind();
    void exeLoveChase();
    void exeLoveHit();
    void exeLoveEnd();
    void exeChase();
    void exeShay();
    void exeAggressive();
    void exeAttackSuccess();
    void exeDrift();
    void exeHideWall();
    void exeHideWater();
    void exeAscension();
    void exeStop();
    void updateNormalVelocity();
    void addDriftVelocity();
    void updateDriftAnimScale();
    void updateDriftTransparency();
    void updateNormalTransparency();
    void setTransparency(f32 val);
    bool isAppearing() const;
    bool isHidden() const;
    bool canAttack() const;
    bool canDrift() const;
    bool canSearchLightDead() const;
    bool isEnableStarPieceStop() const;
    bool isCheckWater() const;
    bool isShay() const;
    void endDrift();
    void endAppearFromGround();
    void endAppearFromWall();

    inline bool isNotNear() const {
        return !MR::isNearPlayer(this, 2000.0f);
    }

    PartsModel* mDisplayModel;
    KeySwitch* mKeySwitch;
    Quat4f mPoseQuat;
    Vec3f mWallNormal;
    Vec3f mWallHitPos;
    Vec3f mInitPos;
    Vec3f mAnimScale;
    Vec3f mWalkTarget;
    f32 mWalkRange;
    f32 mTransparency;
    s32 mAppearanceType;
    bool mHasCoin;
    bool mIsChief;
};
