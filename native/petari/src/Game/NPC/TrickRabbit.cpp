#include "Game/NPC/TrickRabbit.hpp"
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
      mTalkMessageCtrl(), mFootPrint(), mRailGraphIter(), mSpotMarkLight(), mJumpStartStep(), mMoveSpeed(), mRouteLevelStep(), mRouteLevel(), mRouteLevelTarget(), mIsRouteLevelControl(), mIsGhost() {
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
    f32 f = mScale.x;
    MR::initShadowVolumeSphere(this, f * 45.0f);

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
    f32 f = 1.0f;
    mScale.x *= f;
    mScale.y *= f;
    mScale.z *= f;
    initModelManagerWithAnm("TrickRabbit", 0, false);
    MR::connectToSceneNpc(this);
    MR::initLightCtrl(this);
}

void TrickRabbit::initSensor() {
    initHitSensor(2);
    MR::addHitSensorAtJointEnemy(this, "Body", "Spine", 8, 70.0, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointEnemy(this, "Catch", "Spine", 8, 30.0, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initStarPointerTargetAtJoint(this, "Spine", 70.0f, Vec3f(0, 0, 0));
}

void TrickRabbit::initBind() {
    f32 f = mScale.x;
    initBinder(f * 60.0f, f * 60.0f, 0);
    MR::onCalcGravity(this);
}

void TrickRabbit::initRoute(const TableIter& rIter) {
    mRailGraph = MR::createRailGraphFromPlacement(rIter);
    mRailGraphIter = MR::createRailGraphIter(mRailGraph);
}

void TrickRabbit::initDemoCamera(const TableIter& rIter) {
    ActorCameraInfo stack_8 = ActorCameraInfo(rIter);

    if (stack_8.mCameraSetID == -1) {
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
    Vec3f stack_8(-mGravity);
    MR::blendQuatUpFront(&mPoseQuat, stack_8, mFront, 0.1f, 0.2f);
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

bool TrickRabbit::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
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

    bool b = isNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvJump)) && MR::isGreaterStep(this, 1);

    if (b) {
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

    startRouteLevelControl(1, 0x4B0);
    setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvWait));
}

bool TrickRabbit::tryRunaway() {
    if (MR::isNearPlayer(this, 1200.0f)) {
        ::RunnawayNodeSelecter stack_8 = ::RunnawayNodeSelecter();
        MR::moveNodeNearPosition(mRailGraphIter, mPosition, -1.0f, &stack_8);

        Vec3f stack_C(mPosition - *MR::getPlayerPos());
        selectEdgeRunnaway(mRailGraphIter, stack_C, -0.3f);
        setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvRunaway));
        return true;
    }

    return false;
}

bool TrickRabbit::tryBrakeTurn() {
    if (MR::isGreaterStep(this, 60) && MR::isNearPlayer(this, 600.0f)) {
        Vec3f stack_2C;
        Vec3f stack_20;
        Vec3f stack_14(*MR::getPlayerPos() - mPosition);
        stack_2C.set(stack_14);
        Vec3f stack_8(*MR::getNextNodePosition(mRailGraphIter) - mPosition);
        stack_20.set(stack_8);
        MR::normalizeOrZero(&stack_2C);
        MR::normalizeOrZero(&stack_20);
        f32 f = stack_20.dot(stack_2C);
        if (f > 0.1f) {
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
    bool isFar = !MR::isNearPlayer(this, 1500.0f);
    if (isFar) {
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
    if (MR::isGreaterStep(this, mJumpStartStep)) {
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
    if (MR::isGreaterEqualStep(this, mJumpStartStep)) {
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
    if (MR::isGreaterStep(this, mJumpStartStep)) {
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
    if (MR::isGreaterStep(this, mJumpStartStep)) {
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
    if (MR::isGreaterEqualStep(this, mJumpStartStep)) {
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
    if (mMoveSpeed < 125.0f) {
        MR::moveNextNode(mRailGraphIter);
        if (!selectEdgeStartEvent(mRailGraphIter)) {
            setNerve(GET_NERVE(TrickRabbit, TrickRabbitNrvDemoEnd));
            return;
        }
    }

    if (tryDemoJumpStart()) {
        return;
    }
}

void TrickRabbit::exeDemoJumpStart() {
    if (MR::isFirstStep(this)) {
        mJumpStartStep = calcJumpStartTime();
    }

    doJumpStart();

    if (tryDemoJump()) {
        return;
    }
}

void TrickRabbit::exeDemoJump() {
    if (MR::isFirstStep(this)) {
        setUpJumpParamFromPlacement();
    }

    Vec3f stack_14(mPosition);
    doJump();
    if (tryDemoJumpEnd()) {
        Vec3f stack_8(mPosition - stack_14);
        mVelocity.set(stack_8);
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
    if (MR::isStep(this, 30)) {
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

    if (tryRunaway()) {
        return;
    }
}

void TrickRabbit::endWait() {
    tearDownWait();
}

void TrickRabbit::exeRunaway() {
    doRunaway();
    if (mMoveSpeed < 125.0f) {
        MR::moveNextNode(mRailGraphIter);
        Vec3f stack_8(mPosition - *MR::getPlayerPos());
        selectEdgeRunnaway(mRailGraphIter, stack_8, 0.0f);
    }

    if (!tryStop() && !tryJumpStart() && tryBrakeTurn()) {
        return;
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
    Vec3f stack_8(*MR::getNextNodePosition(mRailGraphIter) - mPosition);
    MR::turnVecToVecCosOnPlane(&mFront, stack_8, mGravity, -1.0f);

    if (tryBrakeTurnEnd()) {
        return;
    }
}

void TrickRabbit::exeJumpStart() {
    if (MR::isFirstStep(this)) {
        mJumpStartStep = calcJumpStartTime();
    }

    doJumpStart();

    if (tryJump()) {
        return;
    }
}

void TrickRabbit::exeJump() {
    if (MR::isFirstStep(this)) {
        setUpJumpParamFromPlacement();
    }

    Vec3f stack_14(mPosition);
    doJump();

    if (tryJumpEnd()) {
        Vec3f stack_8(mPosition - stack_14);
        mVelocity.set(stack_8);
    }
}

void TrickRabbit::exeFallDown() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Fall");
    }

    if (!MR::isBindedGround(this)) {
        if (!MR::isBindedGround(this)) {
            MR::addVelocityToGravity(this, 1.0f);
        }

        MR::attenuateVelocity(this, 0.99f);
    } else {
        MR::attenuateVelocity(this, 0.85f);
    }

    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);

    if (tryEndFallDown()) {
        return;
    }
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

    if (tryComebackRouteStart()) {
        return;
    }
}

void TrickRabbit::exeComebackRouteStart() {
    if (MR::isFirstStep(this)) {
        mJumpStartStep = 8;
    }

    doJumpStart();

    if (tryComebackRoute()) {
        return;
    }
}

void TrickRabbit::exeComebackRoute() {
    if (MR::isFirstStep(this)) {
        setUpJumpParam();
    }

    Vec3f stack_14(mPosition);
    doJump();

    if (tryJumpEnd()) {
        Vec3f stack_8(mPosition - stack_14);
        mVelocity.set(stack_8);
    }
}

void TrickRabbit::exeTumble() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "FallDownLoop");
    }

    if (!MR::isBindedGround(this)) {
        if (!MR::isBindedGround(this)) {
            MR::addVelocityToGravity(this, 1.0f);
        }

        MR::attenuateVelocity(this, 0.99f);
        MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    } else {
        MR::zeroVelocity(this);
    }

    if (tryEndTumble()) {
        return;
    }
}

void TrickRabbit::exeGetUp() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "FallDownEnd");
    }

    MR::zeroVelocity(this);

    if (tryEndGetUp()) {
        return;
    }
}

void TrickRabbit::exeCaught() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(60);
        MR::startSystemSE("SE_SY_TOTAL_COMPLETE");
    }

    MR::updateActorStateAndNextNerve(this, mRabbitStateCaught, GET_NERVE(TrickRabbit, TrickRabbitNrvWaitPowerStarDemo));
}

void TrickRabbit::exeWaitPowerStarDemo() {
    Vec3f stack_20;
    if (MR::isFirstStep(this)) {
        MR::endDemo(this, "捕まり");
        MR::startAfterBossBGM();
        Vec3f stack_14(mPosition - (mGravity * 200.0f));
        MR::requestAppearPowerStar(this, stack_14);
        MR::startAction(this, "Wait");
        mFootPrint->clear();
    }

    MR::zeroVelocity(this);

    if (!MR::isOnGround(this) && !MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, 1.0f);
    }

    if (MR::isNearPlayer(this, 70.0f)) {
        MR::getPlayerFrontVec(&stack_20);
        mVelocity += stack_20;
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

    Vec3f stack_14(*MR::getNextNodePosition(mRailGraphIter));
    f32 arg = MR::getSelectEdgeArg1(mRailGraphIter);
    arg = arg / 100.0f;

    if (arg < 0.0f) {
        arg = 1.0f;
    }

    if (!MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, 1.0f);
    }

    Vec3f stack_8(stack_14 - mPosition);
    addMovingAccel(stack_8, 2.4f * arg);
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

    Vec3f stack_8(*MR::getNextNodePosition(mRailGraphIter) - mPosition);
    MR::turnVecToVecCosOnPlane(&mFront, stack_8, mGravity, 0.99f);

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
    Vec3f stack_5C;
    mParabolicPath->calcPosition(&stack_5C, MR::calcNerveRate(this, mJumpStartStep));

    if (!checkDivingThicket((mPosition - (mGravity * 70.0f)), stack_5C - mPosition)) {
        checkDivingThicket((stack_5C - (mGravity * 70.0f)), (mPosition - stack_5C));
    }

    mPosition.set(stack_5C);
}

void TrickRabbit::tearDownWait() {
    MR::invalidateClipping(this);

    if (mIsGhost) {
        MR::hideModelAndOnCalcAnim(this);
        MR::invalidateShadow(this, 0);
    }
}

bool TrickRabbit::checkDivingThicket(const Vec3f& rVec1, const Vec3f& rVec2) {
    Triangle stack_44 = Triangle();
    Pos3f stack_14;
    Vec3f stack_8;

    if (!MR::getFirstPolyOnLineToMap(&stack_8, &stack_44, rVec1, rVec2)) {
        return false;
    }

    MR::makeMtxUpNoSupportPos(&stack_14, *stack_44.getNormal(0), stack_8);
    MR::emitEffectHit(this, stack_14, "ThicketJump");
    MR::startSound(this, "SE_SM_RABBIT_THICKET");
    return true;
}

bool TrickRabbit::selectEdgeStartEvent(RailGraphIter* pIter) {
    bool b = false;
    pIter->watchStartEdge();
    while (!pIter->isWatchEndEdge()) {
        if (!MR::isWatchedPrevEdge(pIter) && MR::getWatchEdgeArg7(pIter) < -1) {
            pIter->selectEdge();
            b = true;
        }

        pIter->watchNextEdge();
    };
    return b;
}

void TrickRabbit::selectEdgeRunnaway(RailGraphIter* pIter, const Vec3f& rVec, f32 f) {
    Vec3f stack_14;
    MR::normalizeOrZero(rVec, &stack_14);
    f32 v10 = -FLOAT_MAX;
    s32 v17;
    s32 r29 = 0;
    s32 r28 = 0;
    s32 r27 = 0;
    pIter->watchStartEdge();

    while (!pIter->isWatchEndEdge()) {
        s32 arg = MR::getWatchEdgeArg7(pIter);
        if (arg >= -1) {
            Vec3f stack_8;
            MR::calcWatchEdgeDirection(pIter, &stack_8);
            f32 dot = stack_8.dot(stack_14);
            v17 = 1;

            if (arg != -1) {
                if (arg != mRouteLevel) {
                    v17 = 0;
                }
            }

            bool v18 = dot > f;
            bool isWatched = !MR::isWatchedPrevEdge(pIter);
            bool v20 = false;

            if (!r29 || v17) {
                if (!r29 && v17) {
                    v20 = true;
                }

                if (!v20) {
                    if (r28 && !v18) {
                        goto endif;
                    }

                    if (!r28 && v18) {
                        v20 = true;
                    }
                }

                if (!v20) {
                    if (r27 && !isWatched) {
                        goto endif;
                    }

                    if (!r27 && isWatched) {
                        v20 = true;
                    }
                }

                if (!v20) {
                    if (v10 < dot) {
                        v20 = true;
                    }
                }

                if (v20) {
                    v10 = dot;
                    r29 = v17;
                    r28 = v18;
                    r27 = isWatched;
                    pIter->selectEdge();
                }
            }
        }

    endif:
        pIter->watchNextEdge();
    }
}

void TrickRabbit::startRouteLevelControl(s32 l1, s32 l2) {
    mRouteLevelTarget = l1;
    mRouteLevelStep = l2;
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

void TrickRabbit::addMovingAccel(const Vec3f& rVec, f32 f) {
    Vec3f stack_20 = mGravity;
    Vec3f stack_14 = rVec.killElement(stack_20);
    MR::separateScalarAndDirection(&mMoveSpeed, &stack_14, stack_14);

    if (!MR::isNearZero(stack_14)) {
        MR::turnVecToVecCos(&mFront, mFront, stack_14, 0.98f, stack_20);
    }

    Vec3f stack_8(mFront * f);
    mVelocity += stack_8;
}

void TrickRabbit::addKeepRouteRange(f32 f1, f32 f2, f32 f3) {
    Vec3f* nextNodePos = MR::getNextNodePosition(mRailGraphIter);
    Vec3f* currentNodePos = MR::getCurrentNodePosition(mRailGraphIter);
    Vec3f stack_3C;

    f32 f = MR::getFootPoint(*currentNodePos, *nextNodePos, mPosition, &stack_3C);
    if (f < 0.0f) {
        stack_3C.set(*MR::getCurrentNodePosition(mRailGraphIter));
    }

    if (f > 1.0f) {
        stack_3C.set(*MR::getNextNodePosition(mRailGraphIter));
    }

    Vec3f stack_30 = MR::killGravity(this, stack_3C - mPosition);
    f32 scalar;
    MR::separateScalarAndDirection(&scalar, &stack_30, stack_30);
    f32 norm = MR::normalize(scalar, f2, f3);
    mVelocity += ((stack_30 * norm) * f1);
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
    Vec3f stack_8(-mGravity);
    mParabolicPath->initFromUpVectorAddHeight(mPosition, *MR::getNextNodePosition(mRailGraphIter), stack_8, 100.0f);
    mJumpStartStep = calcDefaultJumpTime();
}

void TrickRabbit::setUpJumpParamFromPlacement() {
    f32 f = calcJumpHeight();
    Vec3f stack_8(-mGravity);
    mParabolicPath->initFromUpVectorAddHeight(mPosition, *MR::getNextNodePosition(mRailGraphIter), stack_8, f);
    mJumpStartStep = calcJumpTime();
}

s32 TrickRabbit::calcDefaultJumpTime() const {
    f32 f = mParabolicPath->calcPathSpeedFromAverageSpeed(22.0f);

    if (f <= 0.0f) {
        return 0;
    } else {
        return 1.0f / f;
    }
}

s32 TrickRabbit::calcJumpStartTime() const {
    s32 arg = MR::getSelectEdgeArg1(mRailGraphIter);

    if (arg >= 0) {
        return arg;
    } else {
        return 20;
    }
}

s32 TrickRabbit::calcJumpTime() const {
    s32 arg = MR::getSelectEdgeArg2(mRailGraphIter);

    if (arg < 0) {
        return calcDefaultJumpTime();
    } else {
        return arg;
    }
}

f32 TrickRabbit::calcJumpHeight() const {
    f32 arg = MR::getSelectEdgeArg3(mRailGraphIter);

    if (arg < 0.0f) {
        return 100.0f;
    } else {
        return arg;
    }
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
