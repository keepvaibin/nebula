#include "Game/NPC/TrickRabbit.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/SpotMarkLight.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Map/RailGraphIter.hpp"
#include "Game/NPC/RabbitStateCaught.hpp"
#include "Game/NPC/RabbitStateWaitStart.hpp"
#include "Game/NPC/TrickRabbitUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/FootPrint.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/MultiEventCamera.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ParabolicPath.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailGraphUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    class RunnawayNodeSelecter : public RailGraphNodeSelecter {
    public:
        virtual bool isSatisfy(RailGraphIter& rIter);
    };
};  // namespace

namespace NrvTrickRabbit {
    NEW_NERVE(TrickRabbitNrvWaitStart, TrickRabbit, WaitStart);
    NEW_NERVE(TrickRabbitNrvTryStartDemoRunnaway, TrickRabbit, TryStartDemoRunnaway);
    NEW_NERVE(TrickRabbitNrvDemoRunaway, TrickRabbit, DemoRunaway);
    NEW_NERVE(TrickRabbitNrvDemoJumpStart, TrickRabbit, DemoJumpStart);
    NEW_NERVE_ONEND(TrickRabbitNrvDemoJump, TrickRabbit, DemoJump, DemoJump);
    NEW_NERVE(TrickRabbitNrvDemoEnd, TrickRabbit, DemoEnd);
    NEW_NERVE_ONEND(TrickRabbitNrvWait, TrickRabbit, Wait, Wait);
    NEW_NERVE(TrickRabbitNrvRunaway, TrickRabbit, Runaway);
    NEW_NERVE(TrickRabbitNrvJumpStart, TrickRabbit, JumpStart);
    NEW_NERVE_ONEND(TrickRabbitNrvJump, TrickRabbit, Jump, Jump);
    NEW_NERVE(TrickRabbitNrvBrakeTurn, TrickRabbit, BrakeTurn);
    NEW_NERVE(TrickRabbitNrvFallDown, TrickRabbit, FallDown);
    NEW_NERVE(TrickRabbitNrvFallDownLand, TrickRabbit, FallDownLand);
    NEW_NERVE(TrickRabbitNrvGetUpFromFallDown, TrickRabbit, GetUpFromFallDown);
    NEW_NERVE(TrickRabbitNrvComebackRouteStart, TrickRabbit, ComebackRouteStart);
    NEW_NERVE_ONEND(TrickRabbitNrvComebackRoute, TrickRabbit, ComebackRoute, ComebackRoute);
    NEW_NERVE(TrickRabbitNrvTumble, TrickRabbit, Tumble);
    NEW_NERVE(TrickRabbitNrvGetUp, TrickRabbit, GetUp);
    NEW_NERVE(TrickRabbitNrvCaught, TrickRabbit, Caught);
    NEW_NERVE(TrickRabbitNrvWaitPowerStarDemo, TrickRabbit, WaitPowerStarDemo);
    NEW_NERVE(TrickRabbitNrvGiveUp, TrickRabbit, GiveUp);
};  // namespace NrvTrickRabbit

TrickRabbit::TrickRabbit(const char* pName)
    : LiveActor(pName), mPoseQuat(0.0f, 0.0f, 0.0f, 1.0f), mFront(0, 0, 1), mRabbitStateWaitStart(), mRabbitStateCaught(), mMultiEventCamera(),
      mTalkMessageCtrl(), mFootPrint(), mRailGraphIter(), mSpotMarkLight(), mPhaseSteps(), mNextNodeDistance(), mRouteLevelStep(), mRouteLevel(), mRouteLevelTarget(), mIsRouteLevelControl(), mIsGhost() {
    lumacore::setClass(this, lumacore::kClassId< TrickRabbit >);
    mParabolicPath = new ParabolicPath();
}

void TrickRabbit::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    if (MR::isEqualString(mName, "いたずら幽霊ウサギ")) {
        mIsGhost = true;
    } else {
        mIsGhost = false;
    }

    initModel();
    initSound(4, 0);
    initSensor();
    initBind();
    initEffectKeeper(0, 0);
    MR::onEmitEffectSyncClipping(this, "Light");
    MR::onForceDeleteEffectSyncClipping(this, "Light");
    MR::initShadowVolumeSphere(this, mScale.x * 45.0f);

    mFootPrint = TrickRabbitUtil::createRabbitFootPrint(this);

    initRoute(rIter);

    mTalkMessageCtrl = MR::createTalkCtrl(this, rIter, "TrickRabbit", Vec3f(0.0f, 120.0f, 0.0f), 0);

    initDemoCamera(rIter);
    initState();

    mSpotMarkLight = new SpotMarkLight(this, 100.0f, 1500.0f, 0);
    mSpotMarkLight->initWithoutIter();

    MR::declarePowerStar(this);
    MR::addToAttributeGroupSearchTurtle(this);
    MR::useStageSwitchWriteA(this, rIter);

    if (mTalkMessageCtrl == nullptr) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvWait));
    }

    makeActorAppeared();
}

void TrickRabbit::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void TrickRabbit::initModel() {
    initModelManagerWithAnm("TrickRabbit", 0, false);
    MR::connectToSceneNpc(this);
    MR::initLightCtrl(this);
}

void TrickRabbit::initSensor() {
    initHitSensor(2);
    MR::addHitSensorAtJointEnemy(this, "Body", "Spine", 8, 70.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointEnemy(this, "Catch", "Spine", 8, 30.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initStarPointerTargetAtJoint(this, "Spine", 70.0f, Vec3f(0, 0, 0));
}

void TrickRabbit::initBind() {
    initBinder(mScale.x * 60.0f, mScale.x * 60.0f, 0);
    MR::onCalcGravity(this);
}

void TrickRabbit::initRoute(const TableIter& rIter) {
    mRailGraph = MR::createRailGraphFromPlacement(rIter);
    mRailGraphIter = MR::createRailGraphIter(mRailGraph);
}

void TrickRabbit::initDemoCamera(const TableIter& rIter) {
    ActorCameraInfo cameraInfo(rIter);

    if (cameraInfo.mCameraSetID == -1) {
        return;
    }

    mMultiEventCamera = new MultiEventCamera();
    mMultiEventCamera->setUp(mName, new ActorCameraInfo(rIter), 1);
}

void TrickRabbit::initState() {
    initNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvWaitStart));

    mRabbitStateCaught = new RabbitStateCaught(this, mTalkMessageCtrl);
    mRabbitStateCaught->init();

    mRabbitStateWaitStart = new RabbitStateWaitStart(this, &mFront, mTalkMessageCtrl);
    mRabbitStateWaitStart->init();
    mRabbitStateWaitStart->setTalkActionName("StartEvent");
    mRabbitStateWaitStart->mIsKillAfterTalk = true;
}

void TrickRabbit::control() {
    Vec3f up(-mGravity);
    MR::blendQuatUpFront(&mPoseQuat, up, mFront, 0.1f, 0.2f);
    controlRouteLevel();
}

void TrickRabbit::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);
}

void TrickRabbit::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        if (isCaughtable()) {
            if (pSender == getSensor("Catch")) {
                requestCaught();
            }
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }
}

bool TrickRabbit::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isSensorPlayer(pSender) && !isCaughtable()) {
        return true;
    }

    return false;
}

bool TrickRabbit::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver != getSensor("Body")) {
        return false;
    }

    if (isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvWaitStart)) || isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvGiveUp))) {
        return mRabbitStateWaitStart->receiveMsgPlayerAttack(msg, pSender, pReceiver);
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        MR::limitedStarPieceHitSound();
        MR::startSound(this, "SE_SM_RABBIT_STAR_PIECE_HIT");
        MR::startSound(this, "SE_SV_RABBIT_STAR_PIECE_HIT");
        return receiveMsgTrample();
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        return requestCaught();
    }

    if (MR::isMsgPlayerTrample(msg)) {
        MR::startSound(this, "SE_SM_NPC_TRAMPLED");
        MR::startSound(this, "SE_SV_RABBIT_TRAMPLED");
        return receiveMsgTrample();
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        return receiveMsgTrample();
    }

    return false;
}

bool TrickRabbit::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvWaitStart)) || isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvGiveUp))) {
        return mRabbitStateWaitStart->receiveOtherMsg(msg, pSender, pReceiver);
    }

    return false;
}

bool TrickRabbit::receiveMsgTrample() {
    if (!isTumbable()) {
        return false;
    }

    if (isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvJump)) && MR::isGreaterStep(this, 1)) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvFallDown));
    } else {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvTumble));
    }

    return true;
}

bool TrickRabbit::requestCaught() {
    if (isCaughtable()) {
        if (mIsGhost) {
            MR::showModel(this);
            MR::validateShadow(this, 0);
        }

        MR::invalidateClipping(this);
        MR::forwardNode(mTalkMessageCtrl);
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvCaught));
        return true;
    }

    return false;
}

bool TrickRabbit::tryStartDemoRunnaway() {
    if (MR::tryStartDemoMarioPuppetable(this, "逃走")) {
        MR::startEventBGM(MR::EventBgmID_Chase);
        if (mMultiEventCamera != nullptr) {
            mMultiEventCamera->start(CameraTargetArg(this), 0);
        }

        MR::moveNodeNearPosition(mRailGraphIter, mPosition, -1.0f, 0);
        if (selectEdgeStartEvent(mRailGraphIter)) {
            setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvDemoRunaway));
            if (mIsGhost) {
                MR::hideModelAndOnCalcAnim(this);
                MR::invalidateShadow(this, 0);
            }
        } else {
            setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvDemoEnd));
        }

        return true;
    }

    return false;
}

void TrickRabbit::endDemoRunnaway() {
    MR::endDemo(this, "逃走");
    if (mMultiEventCamera != nullptr) {
        mMultiEventCamera->endForce();
    }

    startRouteLevelControl(1, 1200);
    setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvWait));
}

bool TrickRabbit::tryRunaway() {
    if (MR::isNearPlayer(this, 1200.0f)) {
        RunnawayNodeSelecter selecter;
        MR::moveNodeNearPosition(mRailGraphIter, mPosition, -1.0f, &selecter);

        Vec3f fromPlayer(mPosition - *MR::getPlayerPos());
        selectEdgeRunnaway(mRailGraphIter, fromPlayer, -0.3f);
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvRunaway));
        return true;
    }

    return false;
}

bool TrickRabbit::tryBrakeTurn() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1)) && MR::isNearPlayer(this, 600.0f)) {
        // The player is ahead on the route: turn back.
        Vec3f toPlayer(*MR::getPlayerPos() - mPosition);
        Vec3f toNextNode(*MR::getNextNodePosition(mRailGraphIter) - mPosition);
        MR::normalizeOrZero(&toPlayer);
        MR::normalizeOrZero(&toNextNode);
        if (toNextNode.dot(toPlayer) > 0.1f) {
            MR::selectReverseEdge(mRailGraphIter);
            setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvBrakeTurn));
            return true;
        }
    }

    return false;
}

bool TrickRabbit::tryBrakeTurnEnd() {
    if (MR::isGreaterStep(this, 40)) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvRunaway));
        return true;
    }

    return false;
}

bool TrickRabbit::tryStop() {
    if (!MR::isNearPlayer(this, 1500.0f)) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvWait));
        return true;
    }

    return false;
}

bool TrickRabbit::tryJumpStart() {
    if (isNextEdgeJump()) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvJumpStart));
        return true;
    }

    return false;
}

bool TrickRabbit::tryJump() {
    if (MR::isGreaterStep(this, mPhaseSteps)) {
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvJump));
        return true;
    }

    return false;
}

void TrickRabbit::endJump() {
    MR::onBind(this);
}

bool TrickRabbit::tryJumpEnd() {
    if (MR::isGreaterEqualStep(this, mPhaseSteps)) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvRunaway));
        return true;
    }

    return false;
}

bool TrickRabbit::tryEndFallDown() {
    if (MR::isBindedGround(this)) {
        MR::startAction(this, "FallDownLand");
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvFallDownLand));
        return true;
    }

    return false;
}

bool TrickRabbit::tryEndFallDownLand() {
    if (MR::isGreaterStep(this, 20)) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvGetUpFromFallDown));
        return true;
    }

    return false;
}

bool TrickRabbit::tryComebackRouteStart() {
    if (MR::isGreaterStep(this, 20)) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvComebackRouteStart));
        return true;
    }

    return false;
}

bool TrickRabbit::tryComebackRoute() {
    if (MR::isGreaterStep(this, mPhaseSteps)) {
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvComebackRoute));
        return true;
    }

    return false;
}

void TrickRabbit::endComebackRoute() {
    MR::onBind(this);
}

bool TrickRabbit::tryEndTumble() {
    if (MR::isGreaterStep(this, 20) && MR::isBindedGround(this)) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvGetUp));
        return true;
    }

    return false;
}

bool TrickRabbit::tryEndGetUp() {
    if (MR::isGreaterStep(this, 20)) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvRunaway));
        return true;
    }

    return false;
}

bool TrickRabbit::tryDemoJumpStart() {
    if (isNextEdgeJump()) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvDemoJumpStart));
        return true;
    }

    return false;
}

bool TrickRabbit::tryDemoJump() {
    if (MR::isGreaterStep(this, mPhaseSteps)) {
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvDemoJump));
        return true;
    }

    return false;
}

void TrickRabbit::endDemoJump() {
    MR::onBind(this);
}

bool TrickRabbit::tryDemoJumpEnd() {
    if (MR::isGreaterEqualStep(this, mPhaseSteps)) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvDemoRunaway));
        return true;
    }

    return false;
}

bool TrickRabbit::tryEndPowerStarDemo() {
    if (MR::isEndPowerStarAppearDemo(this)) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvGiveUp));
        return true;
    }

    return false;
}

void TrickRabbit::exeWaitStart() {
    if (MR::isFirstStep(this)) {
        mRabbitStateWaitStart->setTalkActionName("StartEvent");
        mRabbitStateWaitStart->mIsKillAfterTalk = true;
    }

    if (MR::updateActorState(this, mRabbitStateWaitStart) && !tryStartDemoRunnaway()) {
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvTryStartDemoRunnaway));
    }
}

void TrickRabbit::exeTryStartDemoRunnaway() {
    tryStartDemoRunnaway();
}

void TrickRabbit::exeDemoRunaway() {
    doRunaway();
    if (mNextNodeDistance < 125.0f) {
        MR::moveNextNode(mRailGraphIter);
        if (!selectEdgeStartEvent(mRailGraphIter)) {
            setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvDemoEnd));
            return;
        }
    }

    tryDemoJumpStart();
}

void TrickRabbit::exeDemoJumpStart() {
    if (MR::isFirstStep(this)) {
        mPhaseSteps = calcJumpStartTime();
    }

    doJumpStart();
    tryDemoJump();
}

void TrickRabbit::exeDemoJump() {
    if (MR::isFirstStep(this)) {
        setUpJumpParamFromPlacement();
    }

    Vec3f lastPos(mPosition);
    doJump();
    if (tryDemoJumpEnd()) {
        mVelocity.set(mPosition - lastPos);
    }
}

void TrickRabbit::exeDemoEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "FollowMe");
        if (mIsGhost) {
            MR::showModel(this);
            MR::validateShadow(this, 0);
        }
    }

    doWait();
    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        endDemoRunnaway();
    }
}

void TrickRabbit::exeWait() {
    if (MR::isFirstStep(this)) {
        if (mIsGhost) {
            MR::showModel(this);
            MR::validateShadow(this, 0);
        }

        MR::startAction(this, "FollowMe");
    }

    doWait();
    tryRunaway();
}

void TrickRabbit::endWait() {
    tearDownWait();
}

void TrickRabbit::exeRunaway() {
    doRunaway();
    if (mNextNodeDistance < 125.0f) {
        MR::moveNextNode(mRailGraphIter);
        Vec3f fromPlayer(mPosition - *MR::getPlayerPos());
        selectEdgeRunnaway(mRailGraphIter, fromPlayer, 0.0f);
    }

    if (!tryStop() && !tryJumpStart()) {
        tryBrakeTurn();
    }
}

void TrickRabbit::exeBrakeTurn() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Brake");
    }

    updateFootPrint();

    if (!MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, 1.0f);
    }

    MR::attenuateVelocity(this, 0.93f);
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    MR::turnVecToVecCosOnPlane(&mFront, *MR::getNextNodePosition(mRailGraphIter) - mPosition, mGravity, -1.0f);
    tryBrakeTurnEnd();
}

void TrickRabbit::exeJumpStart() {
    if (MR::isFirstStep(this)) {
        mPhaseSteps = calcJumpStartTime();
    }

    doJumpStart();
    tryJump();
}

void TrickRabbit::exeJump() {
    if (MR::isFirstStep(this)) {
        setUpJumpParamFromPlacement();
    }

    Vec3f lastPos(mPosition);
    doJump();

    if (tryJumpEnd()) {
        mVelocity.set(mPosition - lastPos);
    }
}

void TrickRabbit::exeFallDown() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Fall");
    }

    if (!MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, 1.0f);
        MR::attenuateVelocity(this, 0.99f);
    } else {
        MR::attenuateVelocity(this, 0.85f);
    }

    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    tryEndFallDown();
}

void TrickRabbit::exeFallDownLand() {
    if (MR::isBckStopped(this)) {
        MR::startAction(this, "FallDownLoop");
    }

    tryEndFallDownLand();
}

void TrickRabbit::exeGetUpFromFallDown() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "FallDownEnd");
    }

    MR::zeroVelocity(this);
    tryComebackRouteStart();
}

void TrickRabbit::exeComebackRouteStart() {
    if (MR::isFirstStep(this)) {
        mPhaseSteps = 8;
    }

    doJumpStart();
    tryComebackRoute();
}

void TrickRabbit::exeComebackRoute() {
    if (MR::isFirstStep(this)) {
        setUpJumpParam();
    }

    Vec3f lastPos(mPosition);
    doJump();

    if (tryJumpEnd()) {
        mVelocity.set(mPosition - lastPos);
    }
}

void TrickRabbit::exeTumble() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "FallDownLoop");
    }

    if (!MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, 1.0f);
        MR::attenuateVelocity(this, 0.99f);
        MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    } else {
        MR::zeroVelocity(this);
    }

    tryEndTumble();
}

void TrickRabbit::exeGetUp() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "FallDownEnd");
    }

    MR::zeroVelocity(this);
    tryEndGetUp();
}

void TrickRabbit::exeCaught() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(60);
        MR::startSystemSE("SE_SY_TOTAL_COMPLETE");
    }

    MR::updateActorStateAndNextNerve(this, mRabbitStateCaught, GET_NERVE(TrickRabbit, TrickRabbitNrvWaitPowerStarDemo));
}

void TrickRabbit::exeWaitPowerStarDemo() {
    if (MR::isFirstStep(this)) {
        MR::endDemo(this, "捕まり");
        MR::startAfterBossBGM();
        Vec3f starPos(mPosition - (mGravity * 200.0f));
        MR::requestAppearPowerStar(this, starPos);
        MR::startAction(this, "Wait");
        mFootPrint->clear();
    }

    MR::zeroVelocity(this);

    if (!MR::isOnGround(this) && !MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, 1.0f);
    }

    // Walked into by the player: pushed along their front.
    if (MR::isNearPlayer(this, 70.0f)) {
        Vec3f playerFront;
        MR::getPlayerFrontVec(&playerFront);
        mVelocity += playerFront;
    }

    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    MR::turnDirectionToPlayerDegreeHorizon(this, &mFront, 30.0f);

    if (tryEndPowerStarDemo()) {
        MR::forwardNode(mTalkMessageCtrl);
    }
}

void TrickRabbit::exeGiveUp() {
    if (MR::isFirstStep(this)) {
        mRabbitStateWaitStart->setTalkActionName("GiveUpTalk");
        mRabbitStateWaitStart->mIsKillAfterTalk = false;
    }

    MR::updateActorState(this, mRabbitStateWaitStart);
}

void TrickRabbit::doWait() {
    if (!MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, 1.0f);
    }

    MR::attenuateVelocity(this, 0.85f);
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    MR::turnDirectionToPlayerDegreeHorizon(this, &mFront, 10.0f);
}

void TrickRabbit::doRunaway() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Run");
    }

    if (MR::checkPassBckFrame(this, 0.0f)) {
        updateFootPrint();
    }

    if (MR::checkPassBckFrame(this, 3.0f)) {
        MR::startSound(this, "SE_SM_RABBIT_JUMP");
    }

    Vec3f nextNodePos(*MR::getNextNodePosition(mRailGraphIter));

    // Edge arg 1: speed in percent; unset is full speed.
    f32 speedRate = MR::getSelectEdgeArg1(mRailGraphIter) / 100.0f;

    if (speedRate < 0.0f) {
        speedRate = 1.0f;
    }

    if (!MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, 1.0f);
    }

    addMovingAccel(nextNodePos - mPosition, 2.4f * speedRate);
    addKeepRouteRange(1.0f, 100.0f, 400.0f);
    MR::attenuateVelocity(this, 0.85f);
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
}

void TrickRabbit::doJumpStart() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "JumpStart");
    }

    updateFootPrint();

    if (MR::isBckOneTimeAndStopped(this)) {
        MR::startAction(this, "JumpStartLoop");
    }

    MR::turnVecToVecCosOnPlane(&mFront, *MR::getNextNodePosition(mRailGraphIter) - mPosition, mGravity, 0.99f);

    if (!MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, 1.0f);
    }

    MR::attenuateVelocity(this, 0.88f);
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
}

void TrickRabbit::doJump() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Jump");
        MR::startSound(this, "SE_SM_RABBIT_JUMP");
        MR::offBind(this);
    }

    MR::turnVecToVecCosOnPlane(&mFront, (*MR::getNextNodePosition(mRailGraphIter) - mPosition), mGravity, 0.9f);
    Vec3f nextPos;
    mParabolicPath->calcPosition(&nextPos, MR::calcNerveRate(this, mPhaseSteps));

    // A thicket crossed this step, going in or coming out.
    if (!checkDivingThicket((mPosition - (mGravity * 70.0f)), nextPos - mPosition)) {
        checkDivingThicket((nextPos - (mGravity * 70.0f)), (mPosition - nextPos));
    }

    mPosition.set(nextPos);
}

void TrickRabbit::tearDownWait() {
    MR::invalidateClipping(this);

    if (mIsGhost) {
        MR::hideModelAndOnCalcAnim(this);
        MR::invalidateShadow(this, 0);
    }
}

bool TrickRabbit::checkDivingThicket(const Vec3f& rStart, const Vec3f& rSegment) {
    Triangle poly = Triangle();
    Pos3f effectMtx;
    Vec3f hitPos;

    if (!MR::getFirstPolyOnLineToMap(&hitPos, &poly, rStart, rSegment)) {
        return false;
    }

    MR::makeMtxUpNoSupportPos(&effectMtx, *poly.getNormal(0), hitPos);
    MR::emitEffectHit(this, effectMtx, "ThicketJump");
    MR::startSound(this, "SE_SM_RABBIT_THICKET");
    return true;
}

// Edge arg 7: route level (-1 any level); below -1 marks the start-event route.
bool TrickRabbit::selectEdgeStartEvent(RailGraphIter* pIter) {
    bool selected = false;
    pIter->watchStartEdge();
    while (!pIter->isWatchEndEdge()) {
        if (!MR::isWatchedPrevEdge(pIter) && MR::getWatchEdgeArg7(pIter) < -1) {
            pIter->selectEdge();
            selected = true;
        }

        pIter->watchNextEdge();
    }
    return selected;
}

// Ranks the edges by: on the current route level, then more than minDot along rAway, then not the edge
// it came by, then the most along rAway. The best one is selected.
void TrickRabbit::selectEdgeRunnaway(RailGraphIter* pIter, const Vec3f& rAway, f32 minDot) {
    Vec3f away;
    MR::normalizeOrZero(rAway, &away);
    f32 bestDot = -FLOAT_MAX;
    bool bestOnLevel = false;
    bool bestAhead = false;
    bool bestNotBack = false;
    pIter->watchStartEdge();

    while (!pIter->isWatchEndEdge()) {
        s32 level = MR::getWatchEdgeArg7(pIter);
        if (level >= -1) {
            Vec3f dir;
            MR::calcWatchEdgeDirection(pIter, &dir);
            f32 dot = dir.dot(away);

            bool onLevel = level == -1 || level == mRouteLevel;
            bool ahead = dot > minDot;
            bool notBack = !MR::isWatchedPrevEdge(pIter);

            bool better;
            if (onLevel != bestOnLevel) {
                better = onLevel;
            } else if (ahead != bestAhead) {
                better = ahead;
            } else if (notBack != bestNotBack) {
                better = notBack;
            } else {
                better = bestDot < dot;
            }

            if (better) {
                bestDot = dot;
                bestOnLevel = onLevel;
                bestAhead = ahead;
                bestNotBack = notBack;
                pIter->selectEdge();
            }
        }

        pIter->watchNextEdge();
    }
}

void TrickRabbit::startRouteLevelControl(s32 level, s32 delaySteps) {
    mRouteLevelTarget = level;
    mRouteLevelStep = delaySteps;
    mIsRouteLevelControl = true;
}

void TrickRabbit::controlRouteLevel() {
    if (!mIsRouteLevelControl) {
        return;
    }

    if (mRouteLevel == mRouteLevelTarget) {
        return;
    }

    if (!mRouteLevelStep) {
        mRouteLevel = mRouteLevelTarget;
        mIsRouteLevelControl = false;
    } else {
        mRouteLevelStep--;
    }
}

// Turns toward rToNode off the gravity axis, keeps its length as the node distance, accelerates along the front.
void TrickRabbit::addMovingAccel(const Vec3f& rToNode, f32 accel) {
    Vec3f gravity = mGravity;
    Vec3f dir = rToNode.killElement(gravity);
    MR::separateScalarAndDirection(&mNextNodeDistance, &dir, dir);

    if (!MR::isNearZero(dir)) {
        MR::turnVecToVecCos(&mFront, mFront, dir, 0.98f, gravity);
    }

    mVelocity += mFront * accel;
}

// Pulls back toward the current edge: nothing within nearDist, full rate from farDist.
void TrickRabbit::addKeepRouteRange(f32 rate, f32 nearDist, f32 farDist) {
    Vec3f* nextNodePos = MR::getNextNodePosition(mRailGraphIter);
    Vec3f* currentNodePos = MR::getCurrentNodePosition(mRailGraphIter);
    Vec3f footPoint;

    f32 edgeRate = MR::getFootPoint(*currentNodePos, *nextNodePos, mPosition, &footPoint);
    if (edgeRate < 0.0f) {
        footPoint.set(*currentNodePos);
    }

    if (edgeRate > 1.0f) {
        footPoint.set(*nextNodePos);
    }

    Vec3f toEdge = MR::killGravity(this, footPoint - mPosition);
    f32 distance;
    MR::separateScalarAndDirection(&distance, &toEdge, toEdge);
    f32 pull = MR::normalize(distance, nearDist, farDist);
    mVelocity += ((toEdge * pull) * rate);
}

void TrickRabbit::updateFootPrint() {
    if (MR::isBindedGround(this)) {
        mFootPrint->addPrint(mPosition, mFront, *MR::getGroundNormal(this), false);
    }
}

bool TrickRabbit::isCaughtable() const {
    if (isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvWait)) || isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvRunaway)) ||
        isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvJumpStart)) || isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvJump)) ||
        isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvBrakeTurn)) || isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvTumble)) ||
        isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvFallDown)) || isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvGetUpFromFallDown)) ||
        isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvComebackRouteStart)) || isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvComebackRoute)) ||
        isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvGetUp))) {
        return true;
    }

    return false;
}

bool TrickRabbit::isTumbable() const {
    if (isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvWait)) || isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvRunaway)) ||
        isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvJumpStart)) || isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvBrakeTurn)) ||
        isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvJump)) || isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvWaitStart)) ||
        isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvGiveUp))) {
        return true;
    }

    return false;
}

bool TrickRabbit::isNextEdgeJump() const {
    return MR::isSelectedEdge(mRailGraphIter) && MR::getSelectEdgeArg0(mRailGraphIter) == 1;
}

void TrickRabbit::setUpJumpParam() {
    Vec3f up(-mGravity);
    mParabolicPath->initFromUpVectorAddHeight(mPosition, *MR::getNextNodePosition(mRailGraphIter), up, 100.0f);
    mPhaseSteps = calcDefaultJumpTime();
}

void TrickRabbit::setUpJumpParamFromPlacement() {
    f32 height = calcJumpHeight();
    Vec3f up(-mGravity);
    mParabolicPath->initFromUpVectorAddHeight(mPosition, *MR::getNextNodePosition(mRailGraphIter), up, height);
    mPhaseSteps = calcJumpTime();
}

// Steps to cover the arc at 22 units a step.
s32 TrickRabbit::calcDefaultJumpTime() const {
    f32 ratePerStep = mParabolicPath->calcPathSpeedFromAverageSpeed(22.0f);

    if (ratePerStep <= 0.0f) {
        return 0;
    }

    return 1.0f / ratePerStep;
}

// On a jump edge, args 1-3 are the crouch steps, the jump steps and the jump height.
s32 TrickRabbit::calcJumpStartTime() const {
    s32 steps = MR::getSelectEdgeArg1(mRailGraphIter);
    return steps >= 0 ? steps : 20;
}

s32 TrickRabbit::calcJumpTime() const {
    s32 steps = MR::getSelectEdgeArg2(mRailGraphIter);
    return steps < 0 ? calcDefaultJumpTime() : steps;
}

f32 TrickRabbit::calcJumpHeight() const {
    f32 height = MR::getSelectEdgeArg3(mRailGraphIter);
    return height < 0.0f ? 100.0f : height;
}

namespace {
    bool RunnawayNodeSelecter::isSatisfy(RailGraphIter& rIter) {
        rIter.watchStartEdge();
        while (!rIter.isWatchEndEdge()) {
            if (MR::getWatchEdgeArg7(&rIter) != -2) {
                return true;
            }

            rIter.watchNextEdge();
        }

        return false;
    }
};  // namespace
