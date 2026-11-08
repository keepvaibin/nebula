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
    /// @brief Creates a new `TrickRabbit`.
    /// @param pName A pointer to the null-terminated name of the object.
    TrickRabbit(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

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
    bool checkDivingThicket(const Vec3f& rVec1, const Vec3f& rVec2);
    bool selectEdgeStartEvent(RailGraphIter* pIter);
    void selectEdgeRunnaway(RailGraphIter* pIter, const Vec3f& rVec, f32 f);
    void startRouteLevelControl(s32 l1, s32 l2);
    void controlRouteLevel();
    void addMovingAccel(const Vec3f& rVec, f32 f);
    void addKeepRouteRange(f32 f1, f32 f2, f32 f3);
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
    s32 mJumpStartStep;
    f32 mMoveSpeed;
    s32 mRouteLevelStep;
    s32 mRouteLevel;
    s32 mRouteLevelTarget;
    bool mIsRouteLevelControl;
    bool mIsGhost;
};
