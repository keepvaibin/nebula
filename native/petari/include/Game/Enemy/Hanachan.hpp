#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"

class AnimScaleController;
class Hanachan;

class HanachanParts : public LiveActor {
public:
    enum PartsType {
        PartsType_Head,
        PartsType_Body,
        PartsType_BodyS,
    };

    HanachanParts(Hanachan*, s32, const char*, const char*);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    const Vec3f* getCommonGravity() const;
    void exeWalk();
    void exeTrample();
    void exeBecomeAngry();
    void endBecomeAngry();
    void exeAngryPursue();
    void endAngryPursue();
    void exeAngryEnd();
    void exeWallHitEnd();
    void exeOverturn();
    void exeOverturnBound();
    void exeOverturnWait();
    void exeRecover();
    void endRecover();
    void exeHipDropped();
    void exeBlow();
    void exeStarPointerBind();
    void endStarPointerBind();
    bool isHeadHitWall();
    bool isLandedInNerve(const Nerve*);
    bool isHipDroppedLanded();
    void changeHeadAngry();
    void changeHeadCalmDown();

    Hanachan* mHost;
    Vec3f mPushVelocity;
    Vec3f mFallVelocity;
    PartsType mPartsType;
    Quat4f mRotationQuat;
    s32 mPartsIndex;
    s32 mActionStartStep;
    bool mIsLanded;
};

class Hanachan : public LiveActor {
public:
    Hanachan(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();

    void exeWalk();
    void exeTrample();
    void exeBecomeAngry();
    void exeAngryPursue();
    void exeAngryEnd();
    void exeWallHitEnd();
    void exeOverturn();
    void exeOverturnWait();
    void exeOverturnBound();
    void exeRecover();
    void exeHipDropped();
    void exeBlow();
    void exeStarPointerBind();
    void endStarPointerBind();
    s32 calcNearestInfectionId();
    bool isOwnSensor(HitSensor*);
    void setNerveBlow(const Vec3f&);
    void applyPlayerHipDropReaction();
    void setNerveAllParts(const Nerve*);
    void setDelayAllPartsAtId(s32, s32, s32);
    bool isStarPointerPointing();
    void moveHeadAlongRail(f32);
    void moveHeadToPlayer(f32, f32);
    Vec3f calcSensorDirection(const HitSensor*, const HitSensor*) const;
    void moveBodyAlongHead();

    MR::InlineArray< HanachanParts*, 5 > mBodyParts;
    Vec3f mAttackPos;
    Vec3f mFrontDir;
    bool mIsChasePlayer;
    AnimScaleController* mScaleController;
};
