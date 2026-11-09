#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FootPrint;
class MultiEventCamera;
class ParabolicPath;
class RabbitStateWaitStart;
class RabbitStateCaught;
class RailGraph;
class RailGraphIter;
class RailGraphNodeSelecter;
class SpotMarkLight;
class TalkMessageCtrl;

class TrickRabbit : public LiveActor {
public:
    TrickRabbit(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initModel();
    void initSensor();
    void initBind();
    void initRoute(const TableIter& rIter);
    void initDemoCamera(const TableIter& rIter);
    void initState();
    bool receiveMsgTrample();
    bool requestCaught();
    bool tryStartDemoRunnaway();
    void endDemoRunnaway();
    bool tryRunaway();
    bool tryBrakeTurn();
    bool tryBrakeTurnEnd();
    bool tryStop();
    bool tryJumpStart();
    bool tryJump();
    void endJump();
    bool tryJumpEnd();
    bool tryEndFallDown();
    bool tryEndFallDownLand();
    bool tryComebackRouteStart();
    bool tryComebackRoute();
    void endComebackRoute();
    bool tryEndTumble();
    bool tryEndGetUp();
    bool tryDemoJumpStart();
    bool tryDemoJump();
    void endDemoJump();
    bool tryDemoJumpEnd();
    bool tryEndPowerStarDemo();
    void exeWaitStart();
    void exeTryStartDemoRunnaway();
    void exeDemoRunaway();
    void exeDemoJumpStart();
    void exeDemoJump();
    void exeDemoEnd();
    void exeWait();
    void endWait();
    void exeRunaway();
    void exeBrakeTurn();
    void exeJumpStart();
    void exeJump();
    void exeFallDown();
    void exeFallDownLand();
    void exeGetUpFromFallDown();
    void exeComebackRouteStart();
    void exeComebackRoute();
    void exeTumble();
    void exeGetUp();
    void exeCaught();
    void exeWaitPowerStarDemo();
    void exeGiveUp();
    void doWait();
    void doRunaway();
    void doJumpStart();
    void doJump();
    void tearDownWait();
    bool checkDivingThicket(const Vec3f& rStart, const Vec3f& rSegment);
    bool selectEdgeStartEvent(RailGraphIter* pIter);
    void selectEdgeRunnaway(RailGraphIter* pIter, const Vec3f& rAway, f32 minDot);
    void startRouteLevelControl(s32 level, s32 delaySteps);
    void controlRouteLevel();
    void addMovingAccel(const Vec3f& rToNode, f32 accel);
    void addKeepRouteRange(f32 rate, f32 nearDist, f32 farDist);
    void updateFootPrint();
    bool isCaughtable() const;
    bool isTumbable() const;
    bool isNextEdgeJump() const;
    void setUpJumpParam();
    void setUpJumpParamFromPlacement();
    s32 calcDefaultJumpTime() const;
    s32 calcJumpStartTime() const;
    s32 calcJumpTime() const;
    f32 calcJumpHeight() const;

    Quat4f mPoseQuat;
    Vec3f mFront;
    RabbitStateWaitStart* mRabbitStateWaitStart;
    RabbitStateCaught* mRabbitStateCaught;
    MultiEventCamera* mMultiEventCamera;
    TalkMessageCtrl* mTalkMessageCtrl;
    FootPrint* mFootPrint;
    RailGraph* mRailGraph;
    RailGraphIter* mRailGraphIter;
    ParabolicPath* mParabolicPath;
    SpotMarkLight* mSpotMarkLight;
    s32 mPhaseSteps;        // steps of the current crouch or jump
    f32 mNextNodeDistance;  // off the gravity axis
    s32 mRouteLevelStep;    // steps until mRouteLevelTarget applies
    s32 mRouteLevel;        // edges of other levels (edge arg 7) are passed over while running
    s32 mRouteLevelTarget;
    bool mIsRouteLevelControl;
    bool mIsGhost;
};
