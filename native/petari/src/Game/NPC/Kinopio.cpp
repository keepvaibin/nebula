#include "Game/NPC/Kinopio.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjConnector.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_Param_f1, "Param.f1", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Param_f2, "Param.f2", 1.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_Param_f3, "Param.f3", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f9, "init.f9", 122.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f10, "init.f10", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f11, "init.f11", 400.0f);

namespace NrvKinopio {
    NEW_NERVE(KinopioNrvReaction, Kinopio, Reaction);
    NEW_NERVE(KinopioNrvTakeOutStar, Kinopio, TakeOutStar);
    NEW_NERVE(KinopioNrvFar, Kinopio, Jump);
    NEW_NERVE(KinopioNrvNear, Kinopio, Jump);
    NEW_NERVE(KinopioNrvWait, Kinopio, Wait);
    NEW_NERVE(KinopioNrvStop0, Kinopio, Stop);
    NEW_NERVE(KinopioNrvStop1, Kinopio, Stop);
    NEW_NERVE(KinopioNrvAction, Kinopio, Action);
    NEW_NERVE(KinopioNrvAppear, Kinopio, Appear);
    NEW_NERVE(KinopioNrvEscape, Kinopio, Escape);
    NEW_NERVE(KinopioNrvStarWait, Kinopio, StarWait);
    NEW_NERVE(KinopioNrvMount, Kinopio, Mount);
    NEW_NERVE(KinopioNrvEvent, Kinopio, Event);
    NEW_NERVE(KinopioNrvEventAndTalk, Kinopio, EventAndTalk);
    NEW_NERVE(KinopioNrvEventTalking, Kinopio, EventAndTalk);
    NEW_NERVE(KinopioNrvEventFall, Kinopio, EventFall);
};  // namespace NrvKinopio

namespace {
    class Param : public AnimScaleParam {
    public:
        Param();
    };

    Param::Param() {
        mPointerTime = cNum_Param_f1;
        mPointerBase = cNum_Param_f2;
        mHitReactionVelocity = cNum_Param_f3;
    }

    static Param sParam;

    NEBULA_TUNABLE(f32, sDistancePlayerIsNear, 200.0f);
    NEBULA_TUNABLE(f32, sDistancePlayerIsFar, 300.0f);
    NEBULA_TUNABLE(f32, sDistancePlayerIsEvent, 450.0f);
    NEBULA_TUNABLE(f32, sTurnLimit, 0.997f);
    NEBULA_TUNABLE(f32, sUpVecBlendRate, 1.0f);
    NEBULA_TUNABLE(f32, sDistanceEventPlayerIsNear, 600.0f);
    static const s32 sTimeToCancelEvent = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(f32, sFallGravity, 0.6f);
    NEBULA_TUNABLE(f32, sFallFrictionRate, 0.98f);
    NEBULA_TUNABLE(s32, sLandingTime, 3);
    NEBULA_TUNABLE(s32, sPickelIndex, 2);
    NEBULA_TUNABLE(f32, sDefaultScale, 1.2f);
    NEBULA_TUNABLE(f32, sWalkSpeed, 0.83f);
    NEBULA_TUNABLE(f32, sRunSpeed, 3.32f);
};  // namespace

Kinopio::Kinopio(const char* pName)
    : NPCActor(pName), mObjArg0(), mBehavior(-1), mAppearBehavior(-1), mGoodsIndex(-1), mConnector(), mTakeOutStar(), mIsSpawnPowerStarMode(), mIsBgmStarted() {
    mConnectQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
}

void Kinopio::init(const TableIter& rIter) {
    MR::initDefaultPosAndQuat(this, rIter);

    Vec3f rotation(mRotation);
    rotation.scale(MR::toRadian(1.0f));
    mPoseQuat.setEuler(rotation);

    MR::getPlacementArg1NoInit(rIter, &mObjArg0);
    MR::getPlacementArg2NoInit(rIter, &mBehavior);
    MR::getPlacementArg3NoInit(rIter, &mIsSpawnPowerStarMode);
    MR::getPlacementArg4NoInit(rIter, &mAppearBehavior);
    MR::getPlacementArg7NoInit(rIter, &mGoodsIndex);
    mScale.set(::sDefaultScale);
    initModelManagerWithAnm("Kinopio", nullptr, false);
    MR::connectToSceneNpc(this);
    MR::initLightCtrl(this);
    initSound(4, false);
    initHitSensor(2);
    MR::addHitSensorNpc(this, "body", 8, cNum_init_f1 * ::sDefaultScale, Vec3f(0.0f, cNum_init_f2 * ::sDefaultScale, 0.0f));
    MR::addHitSensorNpc(this, "Pickel", 8, cNum_init_f3 * ::sDefaultScale, Vec3f(0.0f, cNum_init_f4 * ::sDefaultScale, cNum_init_f5 * ::sDefaultScale));
    MR::invalidateHitSensor(this, "Pickel");
    s32 isInvalidateSensor = -1;
    MR::getPlacementArg6NoInit(rIter, &isInvalidateSensor);
    if (isInvalidateSensor != -1) {
        MR::invalidateHitSensor(this, "body");
    }

    initBinder(cNum_init_f6 * ::sDefaultScale, cNum_init_f7 * ::sDefaultScale, 0);
    MR::offBind(this);
    MR::initShadowVolumeSphere(this, cNum_init_f8 * ::sDefaultScale);
    MR::onCalcShadowOneTime(this, nullptr);
    initEffectKeeper(0, nullptr);
    initNerve(GET_NERVE(Kinopio, KinopioNrvWait));
    mMsgCtrl = MR::createTalkCtrl(this, rIter, "Kinopio", Vec3f(0.0f, cNum_init_f9 * ::sDefaultScale, 0.0f), nullptr);
    MR::setDistanceToTalk(mMsgCtrl, ::sDistancePlayerIsNear);
    MR::onRootNodeAutomatic(mMsgCtrl);
    MR::registerEventFunc(mMsgCtrl, TalkMessageFuncM< Kinopio*, bool (Kinopio::*)(u32) >(this, &Kinopio::eventFunc));
    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        MR::moveCoordAndTransToRailStartPoint(this);
        MR::onCalcShadow(this, nullptr);
    }

    MR::initStarPointerTarget(this, cNum_init_f10, Vec3f(0.0f));
    MR::addToAttributeGroupSearchTurtle(this);
    mLodCtrl = MR::createLodCtrlNPC(this, rIter);

    NPCActorItem item("Kinopio");
    MR::getNPCItemData(&item, mGoodsIndex);
    equipment(item, false);
    ::sParam = Param();  // again, with a mod's numbers
    mScaleController = new AnimScaleController(&::sParam);
    mDelegator = MR::createJointDelegatorWithNullChildFunc< Kinopio >(this, &Kinopio::calcJointScale, "body");

    MR::useStageSwitchReadA(this, rIter);
    MR::useStageSwitchReadB(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);
    MR::tryRegisterDemoCast(this, rIter);
    setNerve(GET_NERVE(Kinopio, KinopioNrvWait));
    setDefaults();
    mSwingReactDist = ::sDistancePlayerIsEvent;
    mParam.setDefaultAction();
    mParam.setTalkAction("Talk", "Talk");
    setTalkAction("Walk");
    mIsRailOnGround = true;
    mRailSpeed = ::sWalkSpeed;
    switch (mBehavior) {
    case -1:
        if (mGoodsIndex == ::sPickelIndex) {
            mParam.setMoveAction("WaitPickel", "WaitPickel");
            mParam.setTalkAction("TalkPickel", "TalkPickel");
            setTalkAction("WalkPickel");
            break;
        }

        setNerve(GET_NERVE(Kinopio, KinopioNrvFar));
        break;
    case 0:
        mParam.setNoTurnAction("SpinWait1");
        break;
    case 1:
        mParam.setNoTurnAction("SpinWait2");
        break;
    case 2:
        mParam.setNoTurnAction("SpinWait3");
        break;
    case 3:
        mUseReactionActions = false;
        MR::setDistanceToTalk(mMsgCtrl, cNum_init_f11);
        break;
    case 5:
        mUseReactionActions = false;
        mParam.setSingleAction("SwimWait");
        break;
    case 7:
        mUseReactionActions = false;
        mParam.setNoTurnAction("Sleep");
        break;
    case 9:
        mParam.setMoveAction("KinopioGoodsWeapon", "KinopioGoodsWeaponTurn");
        mParam.setTalkAction("KinopioGoodsWeapon", "KinopioGoodsWeaponTurn");
        setTalkAction("KinopioGoodsWeaponWalk");
        break;
    case 10:
        mParam.setSingleAction("Joy");
        break;
    case 11:
        mParam.setNoTurnAction("Rightened");
        break;
    case 12: {
        mParam.setSingleAction("StarPieceWait");
        const char* pWaitAction = "StarPieceWait";
        mMoveAction = "KinopioGoodsStarPieceWalk";
        mMoveTalkAction = pWaitAction;
        break;
    }
    case 13:
        setTalkAction("Getaway");
        mRailSpeed = ::sRunSpeed;
        break;
    case 8:
        mIsSpawnPowerStarMode = true;
        break;
    case 6:
        mParam.setNoTurnAction("Pickel");
        MR::validateHitSensor(this, "Pickel");
        break;
    case 4:
        mConnector = new MapObjConnector(this);
        MR::invalidateClipping(this);
        mParam.mTurnWhenNear = false;
        mParam.mTurnOnTalk = false;
        MR::onBind(this);
        MR::onCalcShadow(this, nullptr);
        MR::onCalcGravity(this);
        setNerve(GET_NERVE(Kinopio, KinopioNrvMount));
        break;
    }

    if (mIsSpawnPowerStarMode) {
        mTakeOutStar = new TakeOutStar(this, "TakeOutStar", "TakeOutStar", GET_NERVE(Kinopio, KinopioNrvTakeOutStar));
        MR::declarePowerStar(this);
        setNerve(GET_NERVE(Kinopio, KinopioNrvStarWait));
    }

    MR::useStageSwitchSleep(this, rIter);
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        if (mBehavior != 4) {
            if (mAppearBehavior == 0) {
                pushNerve(GET_NERVE(Kinopio, KinopioNrvEscape));
            } else {
                pushNerve(GET_NERVE(Kinopio, KinopioNrvAppear));
            }
        }

        makeActorDead();
    } else {
        makeActorAppeared();
    }

    MR::startBrk(this, "ColorChange");
    MR::setBrkFrameAndStop(this, mObjArg0);
}

void Kinopio::exeReaction() {
    if (mBehavior == 7) {
        if (mIsTrampledStart) {
            MR::startSound(this, "SE_SM_NPC_TRAMPLED");
            MR::startSound(this, "SE_SV_KINOPIO_POINTING_SLEEP");
        }

        if (isPointingSe()) {
            MR::startPointerHitSound();
            MR::startSound(this, "SE_SV_KINOPIO_POINTING_SLEEP");
        }

        if (mIsSpinAttackedStart) {
            MR::startSound(this, "SE_SM_NPC_TRAMPLED");
            MR::startSound(this, "SE_SV_KINOPIO_POINTING_SLEEP");
        }

        if (mIsReactionStart) {
            MR::startSound(this, "SE_SM_NPC_TRAMPLED");
            MR::startSound(this, "SE_SV_KINOPIO_POINTING_SLEEP");
        }
    } else {
        if (mIsTrampledStart) {
            MR::startSound(this, "SE_SM_NPC_TRAMPLED");
            MR::startSound(this, "SE_SV_KINOPIO_TRAMPLED");
        }

        if (isPointingSe()) {
            MR::startPointerHitSound();
            MR::startSound(this, "SE_SV_KINOPIO_POINTING");
        }

        if (mIsSpinAttackedStart) {
            MR::startSound(this, "SE_SV_KINOPIO_SPIN");
        }

        if (mIsReactionStart) {
            MR::startSound(this, "SE_SV_KINOPIO_STAR_PIECE_HIT");
        }
    }

    if (MR::tryStartReactionAndPopNerve(this)) {
        return;
    }
}

bool Kinopio::isPlayerNotNearKinopio() {
    return !MR::isNearPlayer(this, ::sDistancePlayerIsFar);
}

void Kinopio::exeJump() {
    s32 nextRailPoint;
    if (MR::isExistRail(this)) {
        nextRailPoint = MR::getNextRailPointNo(this);
    } else {
        nextRailPoint = 0;
    }

    if (MR::isFirstStep(this) && !MR::isExistRail(this)) {
        MR::onCalcShadowOneTime(this, nullptr);
    }

    if (MR::tryStartReactionAndPushNerve(this, GET_NERVE(Kinopio, KinopioNrvReaction))) {
        return;
    }

    if (MR::tryTalkNearPlayerAtEnd(mMsgCtrl)) {
        MR::startAction(this, "Notice");
    }

    if (MR::isActionContinuous(this)) {
        if (!isNerve(GET_NERVE(Kinopio, KinopioNrvNear))) {
            return;
        }

        if (MR::isActionStart(this, "Notice")) {
            turnToPlayer(mParam.mTurnSpeed);
        }

        return;
    }

    if (isNerve(GET_NERVE(Kinopio, KinopioNrvFar))) {
        if (MR::isNearPlayer(this, ::sDistancePlayerIsNear)) {
            MR::startAction(this, "Notice");
            setNerve(GET_NERVE(Kinopio, KinopioNrvNear));
            return;
        }

        MR::tryStartMoveTalkAction(this);
    } else {
        if (isPlayerNotNearKinopio()) {
            setNerve(GET_NERVE(Kinopio, KinopioNrvFar));
        }

        MR::tryStartTalkAction(this);
    }

    if (MR::isExistRail(this)) {
        if (MR::getNextRailPointNo(this) != nextRailPoint) {
            tryStartArgs();
        }
    }
}

void Kinopio::exeStop() {
    if (MR::isFirstStep(this)) {
        mRailSpeed = 0.0f;
    }

    if (!(MR::tryStartReactionAndPushNerve(this, GET_NERVE(Kinopio, KinopioNrvReaction)) || MR::tryStartMoveTalkAction(this))) {
        if (isNerve(GET_NERVE(Kinopio, KinopioNrvStop0))) {
            if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
                setDefaultNerve();
            }
        } else if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
            setDefaultNerve();
        }
    }
}

void Kinopio::exeAction() {
    if (MR::isFirstStep(this)) {
        mRailSpeed = 0.0f;
    }

    MR::startMoveAction(this);
    if (!MR::isActionContinuous(this)) {
        if (MR::isEqualStringCase(MR::getPlayingBckName(this), "Tumble")) {
            MR::startAction(this, "Getup");
            setNerve(GET_NERVE(Kinopio, KinopioNrvAction));
        } else {
            setDefaultNerve();
        }
    }
}

void Kinopio::exeWait() {
    s32 nextRailPoint;
    if (MR::isExistRail(this)) {
        nextRailPoint = MR::getNextRailPointNo(this);
    } else {
        nextRailPoint = 0;
    }

    if (MR::isFirstStep(this) && !MR::isExistRail(this)) {
        MR::onCalcShadowOneTime(this, nullptr);
    }

    if (!MR::tryStartReactionAndPushNerve(this, GET_NERVE(Kinopio, KinopioNrvReaction))) {
        MR::tryTalkNearPlayerAndStartMoveTalkAction(this);
        if (MR::isExistRail(this) && MR::getNextRailPointNo(this) != nextRailPoint) {
            tryStartArgs();
        }
    }
}

void Kinopio::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Change");
        MR::invalidateClipping(this);
        MR::onBind(this);
        MR::onCalcShadow(this, nullptr);
    }

    MR::calcGravity(this);

    Vec3f fallSpeed(mGravity);
    fallSpeed.scale(::sFallGravity);
    mVelocity.add(fallSpeed);
    mVelocity.mult(::sFallFrictionRate);

    faceToPlayer(::sTurnLimit);

    if (MR::isBinded(this)) {
        mVelocity.zero();
        if (MR::isBckStopped(this)) {
            MR::validateClipping(this);
            popNerve();
        }
    }
}

void Kinopio::exeEscape() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Escape");
        MR::startSound(this, "SE_SV_KINOPIO_PUHA");
        MR::startSound(this, "SE_SM_NPC_FLY_OUT");
        MR::invalidateClipping(this);
    }

    if (MR::isAnyAnimOneTimeAndStopped(this, "Escape")) {
        MR::validateClipping(this);
        popNerve();
    }
}

void Kinopio::exeMount() {
    if (MR::isFirstStep(this) && mConnector->mParts == nullptr) {
        MR::calcGravity(this);
        MR::setBaseTRMtx(this, mPoseQuat);
        mConnector->attachToUnder();
        mInitPoseQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
        mPoseQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
    }

    if (!MR::tryStartReactionAndPushNerve(this, GET_NERVE(Kinopio, KinopioNrvReaction))) {
        if (MR::tryTalkNearPlayerAndStartTalkAction(this)) {
            faceToPlayer(::sTurnLimit);
        } else {
            faceToDefault(::sTurnLimit);
        }
    }
}

void Kinopio::exeStarWait() {
    exeWait();
    if (MR::isTalkTalking(mMsgCtrl) && !mIsBgmStarted) {
        MR::startStageBGM("MBGM_KINOPIO_TANKEN_B", false);
        mIsBgmStarted = true;
    }
}

void Kinopio::exeEventAndTalk() {
    MR::tryTalkRequest(mMsgCtrl);
    if (MR::isNearPlayer(mMsgCtrl, ::sDistanceEventPlayerIsNear)) {
        MR::tryTalkNearPlayer(mMsgCtrl);
    }
}

void Kinopio::exeEventFall() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::onBind(this);
        MR::onCalcShadow(this, nullptr);
    }

    if (MR::isBindedWall(this) || MR::isBindedRoof(this)) {
        mVelocity.zero();
    }

    MR::calcGravity(this);

    Vec3f fallSpeed(mGravity);
    fallSpeed.scale(::sFallGravity);
    mVelocity.add(fallSpeed);
    mVelocity.mult(::sFallFrictionRate);

    faceToPlayer(::sTurnLimit);

    if (!tryCancelEvent() && MR::isBindedGround(this) && MR::isGreaterStep(this, ::sLandingTime)) {
        mVelocity.zero();
        MR::validateClipping(this);
        MR::turnQuatYDirRate(&mPoseQuat, mPoseQuat, -mGravity, ::sUpVecBlendRate);
        setInitPose();
        MR::tryForwardNode(mMsgCtrl);
        setNerve(GET_NERVE(Kinopio, KinopioNrvWait));
    }
}

void Kinopio::exeEvent() {
}

void Kinopio::exeTakeOutStar() {
}

void Kinopio::calcAndSetBaseMtx() {
    if (isNerve(GET_NERVE(Kinopio, KinopioNrvEvent)) || isNerve(GET_NERVE(Kinopio, KinopioNrvEventAndTalk)) ||
        isNerve(GET_NERVE(Kinopio, KinopioNrvEventTalking))) {
        return;
    }

    if (mConnector != nullptr) {
        mConnector->connect();
        Pos3f mtx(getBaseMtx());
        mtx.getQuat(mConnectQuat);

        Quat4f rotation;
        rotation.mult(mConnectQuat, mPoseQuat);
        rotation.normalize();
        MR::setBaseTRMtx(this, rotation);
    } else {
        NPCActor::calcAndSetBaseMtx();
    }
}

bool Kinopio::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensor(pReceiver, "Pickel")) {
        return false;
    }

    return NPCActor::receiveMsgPlayerAttack(msg, pSender, pReceiver);
}

bool Kinopio::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_NPC_EVENT_START) {
        MR::offRootNodeAutomatic(mMsgCtrl);
        MR::onCalcAnim(this);
        setNerve(GET_NERVE(Kinopio, KinopioNrvEvent));
        return true;
    }

    if (msg == ACTMES_NPC_EVENT_END) {
        setNerve(GET_NERVE(Kinopio, KinopioNrvEventFall));
        return true;
    }

    if (msg == ACTMES_NPC_EVENT_TALK_ENABLE) {
        if (!isNerve(GET_NERVE(Kinopio, KinopioNrvEvent))) {
            return false;
        }

        setNerve(GET_NERVE(Kinopio, KinopioNrvEventAndTalk));
        return true;
    }

    if (msg == ACTMES_NPC_EVENT_TALK_DISABLE) {
        if (!(isNerve(GET_NERVE(Kinopio, KinopioNrvEventAndTalk)) || isNerve(GET_NERVE(Kinopio, KinopioNrvEventTalking)))) {
            return false;
        }

        setNerve(GET_NERVE(Kinopio, KinopioNrvEvent));
        return true;
    }

    if (msg == ACTMES_NPC_TALK_NEXT) {
        MR::tryForwardNode(mMsgCtrl);
        return true;
    }

    return false;
}

void Kinopio::faceToVector(const Vec3f& rDirection, f32 turnLimit) {
    Vec3f direction(rDirection);
    Vec3f up(-mGravity);
    Vec3f currentUp;
    Vec3f currentFront;
    Quat4f inverse;
    f32 length = mConnectQuat.squared();

    if (length <= FloatUtil::epsilon()) {
        inverse.set< f32 >(0.0f, 0.0f, 0.0f, 1.0f);
    } else {
        inverse.x = -mConnectQuat.x / length;
        inverse.y = -mConnectQuat.y / length;
        inverse.z = -mConnectQuat.z / length;
        inverse.w = mConnectQuat.w / length;
    }

    mPoseQuat.getYDir(currentUp);
    mPoseQuat.getZDir(currentFront);
    MR::normalize(&direction);
    inverse.transform(up);
    inverse.transform(direction);
    MR::turnVecToVecCosOnPlane(&currentFront, direction, currentUp, turnLimit);
    MR::blendQuatUpFront(&mPoseQuat, up, currentFront, ::sUpVecBlendRate, 1.0f);
}

void Kinopio::faceToPlayer(f32 turnLimit) {
    Vec3f direction(*MR::getPlayerPos());
    direction.sub(mPosition);
    faceToVector(direction, turnLimit);
}

void Kinopio::faceToDefault(f32 turnLimit) {
    Vec3f defaultUp;
    Vec3f defaultFront;
    Vec3f front;
    mInitPoseQuat.getYDir(defaultUp);
    mInitPoseQuat.getZDir(defaultFront);
    mPoseQuat.getZDir(front);
    MR::turnVecToVecCosOnPlane(&front, defaultFront, defaultUp, turnLimit);
    MR::blendQuatUpFront(&mPoseQuat, defaultUp, front, ::sUpVecBlendRate, 1.0f);
}

void Kinopio::setDefaultNerve() {
    if (mBehavior == 13) {
        mRailSpeed = ::sRunSpeed;
    } else {
        mRailSpeed = ::sWalkSpeed;
    }

    if (mBehavior == -1) {
        if (MR::isNearPlayer(this, ::sDistancePlayerIsNear)) {
            setNerve(GET_NERVE(Kinopio, KinopioNrvNear));
        } else {
            setNerve(GET_NERVE(Kinopio, KinopioNrvFar));
        }
    } else {
        setNerve(GET_NERVE(Kinopio, KinopioNrvWait));
    }
}

bool Kinopio::tryStartArgs() {
    s32 pointArg0;
    MR::getCurrentRailPointArg0WithInit(this, &pointArg0);
    switch (pointArg0) {
    case 0:
        setNerve(GET_NERVE(Kinopio, KinopioNrvStop0));
        break;
    case 1:
        setNerve(GET_NERVE(Kinopio, KinopioNrvStop1));
        break;
    case 2:
        MR::startAction(this, "Notice");
        setNerve(GET_NERVE(Kinopio, KinopioNrvAction));
        break;
    case 3:
        MR::startAction(this, "Tumble");
        setNerve(GET_NERVE(Kinopio, KinopioNrvAction));
        break;
    default:
        return false;
    }

    return true;
}

bool Kinopio::tryCancelEvent() {
    if (MR::isGreaterStep(this, ::sTimeToCancelEvent)) {
        kill();
        return true;
    }

    return false;
}

bool Kinopio::eventFunc(u32) {
    if (mTakeOutStar->takeOut()) {
        if (!MR::isEqualStageName("FishTunnelGalaxy")) {
            MR::startLastStageBGM();
        }

        return true;
    }

    return false;
}
