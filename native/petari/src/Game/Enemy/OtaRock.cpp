#include "Game/Enemy/OtaRock.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/CocoNutBall.hpp"
#include "Game/Enemy/FireBall.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/CocoNut.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initModel_f1, "initModel.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initModel_f2, "initModel.f2", 90.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeDown_1, "exeDown.1", 2);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDown_2, "exeDown.2", 3);

namespace {
    const s32 cCocoNutNum = 4;
    const s32 cFireBallNum = 3;
    NEBULA_TUNABLE_RECORD(Vec, cSensorOffset, {0.0f, 180.0f, 0.0f});
    NEBULA_TUNABLE(f32, cSensorRadius, 300.0f);
    const s32 cWaitFrameMin = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 cWaitFrameMax = NebulaFrameClock::stepsOfMilliseconds(1500);
    NEBULA_TUNABLE(f32, cAppearDistance, 3800.0f);
    NEBULA_TUNABLE(f32, cHideDistance, 4000.0f);
    NEBULA_TUNABLE(s32, cThrowBallStep, 40);
    NEBULA_TUNABLE(f32, cThrowAngleMin, 5.0f);
    NEBULA_TUNABLE(f32, cThrowAngleMax, 10.0f);
    NEBULA_TUNABLE(f32, cThrowFireBallSpeed, 15.0f);
    const s32 cWaitFrameMinNoThrowCocoNut = NebulaFrameClock::stepsOfSeconds(2);
    const s32 cWaitFrameMaxNoThrowCocoNut = NebulaFrameClock::stepsOfSeconds(3);
};  // namespace

namespace NrvOtaRock {
    NEW_NERVE(OtaRockNrvWait, OtaRock, Wait);
    NEW_NERVE(OtaRockNrvThrowCocoNut, OtaRock, ThrowCocoNut);
    NEW_NERVE(OtaRockNrvThrowFireBall, OtaRock, ThrowFireBall);
    NEW_NERVE(OtaRockNrvDown, OtaRock, Down);
    NEW_NERVE(OtaRockNrvHide, OtaRock, Hide);
    NEW_NERVE(OtaRockNrvHideWait, OtaRock, HideWait);
    NEW_NERVE(OtaRockNrvShow, OtaRock, Show);
};  // namespace NrvOtaRock

OtaRock::OtaRock(const char* pName)
    : LiveActor(pName), mNoThrowCocoNut(false), mCocoNutArray(nullptr), mFireBallArray(nullptr), mFixedPosition(nullptr),
      mThrowCocoNutCounter(0), mWaitFrame(0), mBaseMtx(), mFront(0.0f, 0.0f, 1.0f), mAnimScaleController(0) {
    lumacore::setClass(this, lumacore::kClassId< OtaRock >);
    mBaseMtx.identity();
}

void OtaRock::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::useStageSwitchReadA(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mNoThrowCocoNut);
    MR::calcGravity(this);
    initModel();
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initSensor();
    initEffectKeeper(0, nullptr);
    initSound(8, false);
    MR::addToAttributeGroupSearchTurtle(this);
    mAnimScaleController = new AnimScaleController(nullptr);
    initNerve(GET_NERVE(OtaRock, OtaRockNrvWait));
    appear();
}

void OtaRock::appear() {
    LiveActor::appear();
    if (mNoThrowCocoNut) {
        setNerve(GET_NERVE(OtaRock, OtaRockNrvWait));
    } else {
        setNerve(GET_NERVE(OtaRock, OtaRockNrvHideWait));
    }
}

void OtaRock::kill() {
    MR::emitEffect(this, "OtaRockDeath");
    MR::startSound(this, "SE_EM_OTAROCK_DIE");
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
    LiveActor::kill();
}

void OtaRock::control() {
    mAnimScaleController->updateNerve();
}

void OtaRock::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mBaseMtx);
    Vec3f scale = mAnimScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void OtaRock::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if (MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
        mAnimScaleController->startHitReaction();
    } else {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool OtaRock::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgStarPieceReflect(msg)) {
        mAnimScaleController->startHitReaction();
        return true;
    }

    if (MR::isMsgJetTurtleAttack(msg)) {
        setNerve(GET_NERVE(OtaRock, OtaRockNrvDown));
        return true;
    }

    return true;
}

bool OtaRock::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    setNerve(GET_NERVE(OtaRock, OtaRockNrvDown));
    return true;
}

void OtaRock::initModel() {
    initModelManagerWithAnm("OtaRock", nullptr, false);

    if (!mNoThrowCocoNut) {
        mCocoNutArray = new CocoNutBall[::cCocoNutNum];
        for (int i = 0; i < ::cCocoNutNum; i++) {
            mCocoNutArray[i].initWithoutIter();
            mCocoNutArray[i].mHost = this;
        }
    }

    mFireBallArray = new FireBall[::cFireBallNum];
    for (int i = 0; i < ::cFireBallNum; i++) {
        mFireBallArray[i].initWithoutIter();
        mFireBallArray[i].mHost = this;
    }

    mFixedPosition = new FixedPosition(this, "Mouth", Vec3f(0.0f, 0.0f, cNum_initModel_f1), Vec3f(0.0f, 0.0f, -cNum_initModel_f2));

    Pos3f mtxTR;
    MR::makeMtxTR(mtxTR.toMtxPtr(), this);
    mtxTR.getZDir(mFront);
}

void OtaRock::initSensor() {
    initHitSensor(1);
    MR::addHitSensorMtxEnemy(this, "body", 8, ::cSensorRadius, getBaseMtx(), static_cast< Vec3f >(::cSensorOffset));
}

void OtaRock::updateBaseMtx() {
    Vec3f up = -mGravity;
    Vec3f front;
    if (MR::isStageStateScenarioOpeningCamera()) {
        front.set(mFront);
    } else {
        front.sub(*MR::getPlayerPos(), mPosition);
        MR::normalize(&front);
    }
    MR::makeMtxUpFrontPos(&mBaseMtx, up, front, mPosition);
}

CocoNutBall* OtaRock::getDisappearedCocoNut() {
    for (s32 i = 0; i < ::cCocoNutNum; i++) {
        if (MR::isDead(&mCocoNutArray[i])) {
            return &mCocoNutArray[i];
        }
    }

    return nullptr;
}

FireBall* OtaRock::getDisappearedFireBall() {
    for (s32 i = 0; i < ::cFireBallNum; i++) {
        if (MR::isDead(&mFireBallArray[i])) {
            return &mFireBallArray[i];
        }
    }
    return nullptr;
}

s32 OtaRock::getDisappearedCocoNutNum() const {
    s32 num = 0;
    for (s32 i = 0; i < ::cCocoNutNum; i++) {
        if (MR::isDead(&mCocoNutArray[i])) {
            num++;
        }
    }
    return num;
}

s32 OtaRock::getDisappearedFireBallNum() const {
    s32 num = 0;
    for (s32 i = 0; i < ::cFireBallNum; i++) {
        if (MR::isDead(&mFireBallArray[i])) {
            num++;
        }
    }
    return num;
}

bool OtaRock::isValidThrowCocoNut() const {
    return getDisappearedCocoNutNum() > 0;
}

bool OtaRock::isValidThrowFireBall() const {
    return getDisappearedFireBallNum() >= 0;
}

void OtaRock::throwCocoNut() {
    CocoNutBall* pCocoNut = OtaRock::getDisappearedCocoNut();
    Vec3f fixedTrans;
    mFixedPosition->calc();
    mFixedPosition->copyTrans(&fixedTrans);
    f32 f = MR::getRandom(::cThrowAngleMin, ::cThrowAngleMax);
    pCocoNut->appearAndThrow(fixedTrans, MR::isHalfProbability() ? f : -f);
    mThrowCocoNutCounter++;
}

void OtaRock::throwFireBall() {
    FireBall* fireBall = getDisappearedFireBall();
    Vec3f fixedTrans;
    mFixedPosition->calc();
    mFixedPosition->copyTrans(&fixedTrans);
    fireBall->appearAndThrow(fixedTrans, ::cThrowFireBallSpeed, 0.0f);
}

bool OtaRock::tryToHide() {
    bool canHide;

    if (MR::isValidSwitchA(this)) {
        canHide = !MR::isOnSwitchA(this);
    } else {
        canHide = !MR::isNearPlayer(this, ::cHideDistance);
    }

    if (!mNoThrowCocoNut && getDisappearedCocoNutNum() == 4 && canHide) {
        setNerve(GET_NERVE(OtaRock, OtaRockNrvHide));
        return true;
    }
    return false;
}

void OtaRock::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        if (mNoThrowCocoNut) {
            mWaitFrame = MR::getRandom(::cWaitFrameMinNoThrowCocoNut, ::cWaitFrameMaxNoThrowCocoNut);
        } else {
            mWaitFrame = MR::getRandom(::cWaitFrameMin, ::cWaitFrameMax);
        }
    }
    updateBaseMtx();
    if (!tryToHide() && MR::isGreaterStep(this, mWaitFrame)) {
        if (!mNoThrowCocoNut && mThrowCocoNutCounter < 2) {
            if (isValidThrowCocoNut()) {
                setNerve(GET_NERVE(OtaRock, OtaRockNrvThrowCocoNut));
            }
        } else {
            if (isValidThrowFireBall()) {
                mThrowCocoNutCounter = 0;
                setNerve(GET_NERVE(OtaRock, OtaRockNrvThrowFireBall));
            }
        }
    }
}

void OtaRock::exeThrowCocoNut() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Attack");
        MR::startSound(this, "SE_EV_OTAROCK_PRE_RALLYBALL");
    }
    OtaRock::updateBaseMtx();
    if (MR::isStep(this, ::cThrowBallStep)) {
        throwCocoNut();
    }
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(OtaRock, OtaRockNrvWait));
    }
}

void OtaRock::exeThrowFireBall() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Attack");
        MR::startSound(this, "SE_EV_OTAROCK_PRE_HOTBALL");
    }
    OtaRock::updateBaseMtx();
    if (MR::isStep(this, ::cThrowBallStep)) {
        MR::startSound(this, "SE_EM_OTAROCK_FIRE_OUT");
        throwFireBall();
    }
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(OtaRock, OtaRockNrvWait));
    }
}

void OtaRock::exeDown() {
    if (MR::isFirstStep(this)) {
        MR::startBckNoInterpole(this, "Death");
        MR::startSound(this, "SE_EM_OTAROCK_LAST_DAMAGE");
        MR::startSound(this, "SE_EV_OTAROCK_DIE");
        MR::invalidateHitSensors(this);
        MR::tryRumblePadVeryStrong(this, PLAYER_1);

        if (!mNoThrowCocoNut) {
            for (s32 i = 0; i < ::cCocoNutNum; i++) {
                if (!MR::isDead(&mCocoNutArray[i]))
                    mCocoNutArray[i].kill();
            }
        }
        for (s32 i = 0; i < ::cFireBallNum; i++) {
            if (!MR::isDead(&mFireBallArray[i]))
                mFireBallArray[i].kill();
        }
    }

    if (MR::isStep(this, cStep_exeDown_1)) {
        MR::stopScene(16);
    }

    if (MR::isStep(this, cStep_exeDown_2)) {
        MR::shakeCameraNormal();
    }

    if (MR::isBckStopped(this)) {
        kill();
    }
}

void OtaRock::exeHide() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Hide");
        MR::startSound(this, "SE_EM_OTAROCK_HIDE");
    }
    updateBaseMtx();
    MR::setNerveAtBckStopped(this, GET_NERVE(OtaRock, OtaRockNrvHideWait));
}

void OtaRock::exeHideWait() {
    if (MR::isFirstStep(this)) {
        MR::startBckNoInterpole(this, "HideWait");
    }
    updateBaseMtx();

    bool show;

    if (MR::isValidSwitchA(this)) {
        show = MR::isOnSwitchA(this);
    } else {
        show = MR::isNearPlayer(this, ::cAppearDistance);
    }

    if (show) {
        setNerve(GET_NERVE(OtaRock, OtaRockNrvShow));
    }
}

void OtaRock::exeShow() {
    if (MR::isFirstStep(this)) {
        MR::startBckNoInterpole(this, "Show");
        MR::startSound(this, "SE_EM_OTAROCK_APPEAR");
    }
    updateBaseMtx();
    MR::setNerveAtBckStopped(this, GET_NERVE(OtaRock, OtaRockNrvWait));
}
