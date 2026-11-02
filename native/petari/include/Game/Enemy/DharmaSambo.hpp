#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/Util/Array.hpp"

class AnimScaleController;
class AnimScaleParam;
class DharmaSamboParts;
class FixedPosition;
class SpinHitController;

class DharmaSambo : public LiveActor {
public:
    DharmaSambo(const char*);

    virtual ~DharmaSambo();
    virtual void init(const JMapInfoIter&) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void exeWaitUnderGround();
    void exeAppear();
    void exePursue();
    void endPursue();
    void exeAttack();
    void exeSpinHitted();
    void exeStarPointerBind();
    void endStarPointerBind();
    void tearDownStarPointerBind();
    void exeEscapeRun();
    void exeEscapeLand();
    void exeTrampleDeath();
    void exeHide();
    void exeHideAppear();
    void exeKilled();
    DharmaSamboParts* getHeadParts();
    bool isActionEndHead();
    void startActionHead(const char*);
    void startActionAllParts(const char*);
    void startBtkAllParts(const char*);
    bool decCountAndTryToHide(bool);
    bool isStarPointerPointingParts();

    const Nerve* mStarPointerBindEndNerve;
    TVec3f mInitialPos;
    s32 mHideTimer;
    s32 mPartsCount;
    MR::Vector< MR::AssignableArray< DharmaSamboParts* > > mParts;
    MR::Vector< MR::AssignableArray< FixedPosition* > > mFixedPositions;
    HitSensor* mHitSensor;
    AnimScaleController* mAnimScaleController;
    AnimScaleParam* mAnimScaleParam;
};

class DharmaSamboParts : public ModelObj {
public:
    DharmaSamboParts(DharmaSambo*, const char*, const char*, MtxPtr, int, int, int);

    virtual ~DharmaSamboParts();
    virtual void init(const JMapInfoIter&) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void exeDoNothing();
    void exeBlow();
    void exeStarPointerBind();
    void endStarPointerBind();
    void setHead(bool);

    DharmaSambo* mHost;
    s32 mOriginalIndex;
    s32 mIndex;
    SpinHitController* mSpinHitController;
    f32 mHitDistance;
    bool mIsHead;
};
