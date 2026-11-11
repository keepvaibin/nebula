#include "Game/Enemy/Petari.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/FootPrint.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initHitSensors_f1, "initHitSensors.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initHitSensors_f2, "initHitSensors.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeApproach_f1, "exeApproach.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeApproach_f2, "exeApproach.f2", 0.92f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeApproach_f3, "exeApproach.f3", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscape_f1, "exeEscape.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscape_f2, "exeEscape.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscape_f3, "exeEscape.f3", 0.45f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscape_f4, "exeEscape.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscape_f5, "exeEscape.f5", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscape_f6, "exeEscape.f6", 0.97f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwoonStart_f1, "exeSwoonStart.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLand_f1, "exeLand.f1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLand_f2, "exeLand.f2", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLand_f3, "exeLand.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinOut_f1, "exeSpinOut.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinOut_f2, "exeSpinOut.f2", 1.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFreeze_f1, "exeFreeze.f1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgEnemyAttack_f1, "receiveMsgEnemyAttack.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f1, "receiveOtherMsg.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initFootPrint_f1, "initFootPrint.f1", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initFootPrint_f2, "initFootPrint.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initFootPrint_f3, "initFootPrint.f3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initFootPrint_f4, "initFootPrint.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receivePlayerAttackAtBody_f1, "receivePlayerAttackAtBody.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receivePlayerAttackAtBody_f2, "receivePlayerAttackAtBody.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveTowardTargetDirection_f1, "moveTowardTargetDirection.f1", 1.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_avoidPlayer_f1, "avoidPlayer.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_avoidPlayer_f2, "avoidPlayer.f2", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_avoidPlayer_f3, "avoidPlayer.f3", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_avoidPlayer_f4, "avoidPlayer.f4", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_avoidWall_f1, "avoidWall.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_avoidWall_f2, "avoidWall.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCenter_f1, "calcCenter.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_meander_f1, "meander.f1", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_meander_f2, "meander.f2", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_meander_f3, "meander.f3", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBoost_f1, "calcBoost.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearStarPieceGradually_f1, "appearStarPieceGradually.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearStarPieceGradually_f2, "appearStarPieceGradually.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearStarPieceAll_f1, "appearStarPieceAll.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearStarPieceAll_f2, "appearStarPieceAll.f2", 40.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwoonEnd_checkPassBckFrame_1, "exeSwoonEnd.checkPassBckFrame.1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPush_addVelocityFromPush_1, "receiveMsgPush.addVelocityFromPush.1", 3.0f);

namespace {
    NEBULA_TUNABLE_RECORD(Vec, sBodySensorOffset, {0.0f, 60.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, sSpinSensorOffset, {0.0f, 0.0f, 0.0f});
    const char* sBodySensorName = "body";
    const char* sSpinSensorName = "spin";

    // Standing turns in place (no move speed) toward mTargetDir.
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, sStandParam, {0.0f, 1.3f, 0.92f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, sSwoonStartParam, {0.0f, 1.3f, 0.985f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, sSwoonParam, {0.0f, 1.3f, 0.0f, 3.0f});

    // To the player: hiding closer than sJumpOutDistance on screen, chasing past sApproachStartDistance until
    // sApproachEndDistance, running from closer than sEscapeStartDistance until sEscapeEndDistance.
    NEBULA_TUNABLE(f32, sJumpOutDistance, 1500.0f);
    NEBULA_TUNABLE(f32, sApproachStartDistance, 700.0f);
    NEBULA_TUNABLE(f32, sApproachEndDistance, 600.0f);
    NEBULA_TUNABLE(f32, sEscapeStartDistance, 500.0f);
    NEBULA_TUNABLE(f32, sEscapeEndDistance, 710.0f);
    NEBULA_TUNABLE(f32, sWallLookDistance, 600.0f);
};  // namespace

namespace {
    NEW_NERVE(PetariNrvLurk, Petari, Lurk);
    NEW_NERVE(PetariNrvJumpOut, Petari, JumpOut);
    NEW_NERVE_ONEND(PetariNrvWait, Petari, Wait, Wait);
    NEW_NERVE_ONEND(PetariNrvApproach, Petari, Approach, Approach);
    NEW_NERVE_ONEND(PetariNrvEscape, Petari, Escape, Escape);
    NEW_NERVE(PetariNrvSwoonStart, Petari, SwoonStart);
    NEW_NERVE(PetariNrvSwoon, Petari, Swoon);
    NEW_NERVE(PetariNrvSwoonEnd, Petari, SwoonEnd);
    NEW_NERVE(PetariNrvLand, Petari, Land);
    NEW_NERVE(PetariNrvSmash, Petari, Smash);
    NEW_NERVE(PetariNrvSpinOut, Petari, SpinOut);
    NEW_NERVE(PetariNrvKickOut, Petari, KickOut);
    NEW_NERVE_ONEND(PetariNrvFreeze, Petari, Freeze, Freeze);
};  // namespace

Petari::Petari(const char* pName)
    : LiveActor(pName), mAnimScaleCtrl(nullptr), mFootPrint(nullptr), mFootprintYOffs(0.0f), mRequestSmoke(true), mFront(0.0f, 0.0f, 1.0f),
      mTargetDir(0.0f, 0.0f, 1.0f), mMeanderStep(0), mMeanderAngle(0.0f), mStarPieceTotal(20), mStarPieceRemaining(20) {
    lumacore::setClass(this, lumacore::kClassId< Petari >);
    mHeadMtx.identity();
}

inline void Petari::initHitSensors() {
    initHitSensor(2);
    MR::addHitSensorEnemy(this, ::sBodySensorName, 32, cNum_initHitSensors_f1, Vec3f(::sBodySensorOffset));
    MR::addHitSensorEnemy(this, ::sSpinSensorName, 32, cNum_initHitSensors_f2, Vec3f(::sSpinSensorOffset));
}

void Petari::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("Petari", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initHitSensors();
    initBinder(cNum_init_f1, cNum_init_f2, 0);
    initEffectKeeper(1, nullptr);
    MR::addEffect(this, "PointerTouchManual");
    MR::setEffectHostMtx(this, "PointerTouchManual", mHeadMtx);
    initSound(4, false);
    MR::initShadowFromCSV(this, "Shadow");
    mAnimScaleCtrl = new AnimScaleController(nullptr);
    initFootPrint(rIter);
    initNerve(GET_NERVE_ANON(PetariNrvLurk));
    initFrontVec();
    MR::onCalcGravity(this);
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
    }
    MR::useStageSwitchWriteDead(this, rIter);
    initStarPiece(rIter);
    MR::initStarPointerTarget(this, cNum_init_f3, Vec3f(0.0f, cNum_init_f4, 0.0f));
    makeActorAppeared();
}

void Petari::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void Petari::makeActorAppeared() {
    MR::hideModel(this);
    setNerve(GET_NERVE_ANON(PetariNrvLurk));
    LiveActor::makeActorAppeared();
    mMeanderStep = 0;
}

void Petari::kill() {
    MR::startSound(this, "SE_EM_EXPLODE_S");
    MR::deleteEffect(this, "InnerLight");
    LiveActor::kill();
    mFootPrint->clear();
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void Petari::calcAnim() {
    LiveActor::calcAnim();
    if (!MR::isDead(this) && !MR::isHiddenModel(this)) {
        mHeadMtx.set(MR::getJointMtx(this, "Head1"));
    }
}

void Petari::exeLurk() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
    }
    tryShiftJumpOut();
}

void Petari::exeJumpOut() {
    setNerve(GET_NERVE_ANON(PetariNrvWait));
}

void Petari::exeWait() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
    }

    MR::moveAndTurnToDirection(this, &mFront, mTargetDir, sStandParam);

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        if (MR::isHiddenModel(this)) {
            MR::showModel(this);
            MR::emitEffect(this, "LeaveAppearanceSmoke");
            MR::startBckNoInterpole(this, "Wait");
        } else {
            MR::startBckWithInterpole(this, "Wait", 30);
        }
        MR::startSound(this, "SE_EM_PETARI_WAIT_APPEAR");
    }
    if (!reflectStarPointer2P() && (tryShiftApproach() || tryShiftEscape()) && !MR::isHiddenModel(this)) {
        MR::emitEffect(this, "LeaveAppearanceSmoke");
        MR::forceDeleteEffect(this, "LeaveAppearanceInnerLight");
        MR::startSound(this, "SE_EM_PETARI_WAIT_HIDE");
    }
}

void Petari::exeApproach() {
    if (MR::isFirstStep(this)) {
        if (!MR::isHiddenModel(this)) {
            MR::hideModel(this);
            MR::emitEffect(this, "StartSmoke");
        }
        tryEmitFirstFootPrintEffect();
    }
    calcCenter();
    calcApproachDirection();
    moveTowardTargetDirection(cNum_exeApproach_f1, cNum_exeApproach_f2, cNum_exeApproach_f3);
    updateFootPrint();

    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "WalkSmoke");
    }

    MR::startLevelSound(this, "SE_EM_LV_PETARI_RUN");

    if (!reflectStarPointer2P()) {
        tryApproachEnd();
    }
}

void Petari::exeEscape() {
    if (MR::isFirstStep(this)) {
        if (!MR::isHiddenModel(this)) {
            MR::hideModel(this);
            MR::emitEffect(this, "StartSmoke");
        }

        mMeanderStep = 0;
        MR::emitEffect(this, "RunSmoke");
        tryEmitFirstFootPrintEffect();
    }
    calcCenter();
    calcEscapeDirection();
    meander();
    avoidPlayer();
    avoidWall();

    // The first step turns all the way round.
    f32 turnDegree = MR::isFirstStep(this) ? cNum_exeEscape_f1 : cNum_exeEscape_f2;
    f32 boost = calcBoost(cNum_exeEscape_f3, cNum_exeEscape_f4, cNum_exeEscape_f5);
    moveTowardTargetDirection(boost, cNum_exeEscape_f6, turnDegree);
    updateFootPrint();
    MR::startLevelSound(this, "SE_EM_LV_PETARI_RUN");

    if (!reflectStarPointer2P()) {
        tryEscapeEnd();
    }
}

void Petari::exeSwoonStart() {
    if (MR::isFirstStep(this)) {
        calcSpinOutVelocity(cNum_exeSwoonStart_f1);
        MR::showModel(this);
        MR::startBck(this, "Appearance");
        MR::startBlowHitSound(this);
        MR::startSound(this, "SE_EM_PETARI_APPEAR");
        mRequestSmoke = true;
    }
    calcCenter();
    MR::moveAndTurnToDirection(this, &mFront, mTargetDir, sSwoonStartParam);
    MR::setNerveAtBckStopped(this, GET_NERVE_ANON(PetariNrvSwoon));
}

void Petari::exeSwoon() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
        MR::startBck(this, "Swoon");
    }
    calcCenter();
    MR::moveAndTurnToDirection(this, &mFront, mTargetDir, sSwoonParam);
    MR::startLevelSound(this, "SE_EM_LV_SWOON_S");
    MR::setNerveAtStep(this, GET_NERVE_ANON(PetariNrvSwoonEnd), NebulaFrameClock::stepsOfSeconds(3));
}

void Petari::exeSwoonEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Hide");
        MR::startSound(this, "SE_EM_PETARI_WAKE_UP");
    }
    if (MR::checkPassBckFrame(this, cNum_exeSwoonEnd_checkPassBckFrame_1)) {
        MR::startSound(this, "SE_EM_PETARI_JUMP");
    }
    calcCenter();
    MR::moveAndTurnToDirection(this, &mFront, mTargetDir, sSwoonParam);

    if (MR::isBckOneTimeAndStopped(this)) {
        MR::startSound(this, "SE_EM_CHANGE_SMOKE");
        MR::hideModel(this);
        setNerve(GET_NERVE_ANON(PetariNrvLand));
    }
}

void Petari::exeLand() {
    if (MR::isFirstStep(this)) {
        mMeanderStep = 0;
    }
    calcCenter();
    calcEscapeDirection();
    meander();
    avoidPlayer();
    avoidWall();
    moveTowardTargetDirection(1.0f, cNum_exeLand_f1, MR::isFirstStep(this) ? cNum_exeLand_f2 : cNum_exeLand_f3);

    MR::setNerveAtStep(this, GET_NERVE_ANON(PetariNrvEscape), 45);
}

void Petari::exeSmash() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::startBck(this, "Press");
        MR::startSound(this, "SE_EM_STOMPED_S");
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    }

    calcCenter();
    appearStarPieceGradually();

    if (MR::isBckOneTimeAndStopped(this)) {
        appearStarPieceAll();
        kill();
    }
}

void Petari::exeSpinOut() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::startBck(this, "Damage");
        MR::startBlowHitSound(this);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    }

    MR::turnDirectionDegree(this, &mFront, -mVelocity, cNum_exeSpinOut_f1);

    // Slides along the ground (or falls), pressed down at 1.3.
    Vec3f down;
    if (MR::isOnGround(this)) {
        down.set(-*MR::getGroundNormal(this));
    } else {
        down.set(mGravity);
    }

    const Vec3f& vel = mVelocity;
    mVelocity.scaleAdd(-down.dot(vel), down, vel);
    mVelocity.add(down * cNum_exeSpinOut_f2);

    calcCenter();
    appearStarPieceGradually();
    if (MR::isBckOneTimeAndStopped(this)) {
        appearStarPieceAll();
        kill();
    }
}

void Petari::exeKickOut() {
    exeSpinOut();
}

void Petari::exeFreeze() {
    if (MR::isFirstStep(this)) {
        MR::startPointerHitSound();
        MR::emitEffectWithScale(this, "PointerTouchManual", cNum_exeFreeze_f1, -1);
        mVelocity.zero();
        mAnimScaleCtrl->startPointerHitVibration();
    }

    MR::moveAndTurnToDirection(this, &mFront, mTargetDir, sStandParam);
    MR::startPointerFreezeLevelSound(this);

    if (!MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE_ANON(PetariNrvWait));
    }
}

void Petari::endApproach() {
    MR::deleteEffect(this, "WalkSmoke");
}

void Petari::endWait() {
}

void Petari::endFreeze() {
    mAnimScaleCtrl->startAnim();
    MR::deleteEffect(this, "PointerTouchManual");
}

void Petari::endEscape() {
    MR::deleteEffect(this, "RunSmoke");
}

void Petari::control() {
    Vec3f upVec(getBaseMtx()[0][1], getBaseMtx()[1][1], getBaseMtx()[2][1]);

    if (MR::isOnGround(this)) {
        Quat4f groundQuat;
        groundQuat.setRotate(upVec, *MR::getGroundNormal(this), cNum_control_f1);
        groundQuat.transform(upVec);
    } else {
        Quat4f gravityQuat;
        gravityQuat.setRotate(upVec, -mGravity, cNum_control_f2);
        gravityQuat.transform(upVec);
    }
    Pos3f baseMtx;
    MR::makeMtxUpFrontPos(&baseMtx, upVec, mFront, mPosition);
    MR::setBaseTRMtx(this, baseMtx);
    mAnimScaleCtrl->updateNerve();

    mHeadMtx.set(getBaseMtx());
}

void Petari::calcAndSetBaseMtx() {
    Vec3f scale = mAnimScaleCtrl->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void Petari::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensor(pSender, ::sBodySensorName) && isSolidBody()) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool Petari::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensor(pReceiver, ::sBodySensorName)) {
        return receivePlayerAttackAtBody(msg, pSender, pReceiver);
    }

    if (MR::isSensor(pReceiver, ::sSpinSensorName)) {
        return receivePlayerAttackAtSpin(msg, pSender, pReceiver);
    }

    return false;
}

bool Petari::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensor(pReceiver, ::sBodySensorName)) {
        return false;
    }

    if (isSolidBody() && MR::isMsgToEnemyAttackBlow(msg)) {
        MR::setVelocityBlowAttack(this, pSender, pReceiver, cNum_receiveMsgEnemyAttack_f1, 0.0f, 4);
        setNerve(GET_NERVE_ANON(PetariNrvSpinOut));
        return true;
    }

    return false;
}

bool Petari::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensor(pReceiver, ::sBodySensorName) && isSolidBody()) {
        MR::addVelocityFromPush(this, cNum_receiveMsgPush_addVelocityFromPush_1, pSender, pReceiver);
        return true;
    }

    return false;
}

bool Petari::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensor(pReceiver, ::sBodySensorName) && isNerve(GET_NERVE_ANON(PetariNrvSwoon)) && MR::isMsgPlayerKick(msg)) {
        MR::setVelocityBlowAttack(this, pSender, pReceiver, cNum_receiveOtherMsg_f1, 0.0f, 4);
        setNerve(GET_NERVE_ANON(PetariNrvKickOut));
        return true;
    }

    return false;
}

void Petari::initFootPrint(const TableIter& rIter) {
    mFootPrint = new FootPrint("ペタリの足跡", 32);

    ResTIMG* texture = MR::getTexFromArc("Footprint.bti", this);
    mFootPrint->setTexture(texture);
    mFootPrint->mSideOffset = cNum_initFootPrint_f1;
    mFootPrint->mHeight = cNum_initFootPrint_f2;
    mFootPrint->mWidth = cNum_initFootPrint_f3;
    mFootPrint->mMinPrintDistance = cNum_initFootPrint_f4;

    MR::getPlacementArg0NoInit(rIter, &mFootprintYOffs);
}

void Petari::initFrontVec() {
    Pos3f mtx;
    MR::makeMtxTR(mtx, mPosition, mRotation);
    mtx.getZDir(mFront);
}

void Petari::initStarPiece(const TableIter& rIter) {
    MR::getPlacementArg1NoInit(rIter, &mStarPieceTotal);
    MR::declareStarPiece(this, mStarPieceTotal);
    mStarPieceRemaining = mStarPieceTotal;
}

bool Petari::tryShiftJumpOut() {
    Vec3f screenPosition;
    if (MR::calcDistanceToPlayer(mPosition) < sJumpOutDistance && MR::calcScreenPosition(&screenPosition, mPosition)) {
        setNerve(GET_NERVE_ANON(PetariNrvJumpOut));
        return true;
    }

    return false;
}

bool Petari::tryShiftApproach() {
    if (MR::calcDistanceToPlayer(mPosition) > sApproachStartDistance) {
        setNerve(GET_NERVE_ANON(PetariNrvApproach));
        return true;
    }

    return false;
}

bool Petari::tryApproachEnd() {
    if (MR::calcDistanceToPlayer(mPosition) < sApproachEndDistance) {
        setNerve(GET_NERVE_ANON(PetariNrvWait));
        return true;
    }

    return false;
}

bool Petari::tryShiftEscape() {
    if (MR::calcDistanceToPlayer(mPosition) < sEscapeStartDistance) {
        setNerve(GET_NERVE_ANON(PetariNrvEscape));
        return true;
    }

    return false;
}

bool Petari::tryEscapeEnd() {
    if (MR::calcDistanceToPlayer(mPosition) > sEscapeEndDistance) {
        setNerve(GET_NERVE_ANON(PetariNrvWait));
        return true;
    }

    return false;
}

bool Petari::receivePlayerAttackAtBody(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceAttack(msg)) {
        if (isNerve(GET_NERVE_ANON(PetariNrvSwoon))) {
            mAnimScaleCtrl->startHitReaction();
            setNerve(GET_NERVE_ANON(PetariNrvSwoon));
            return true;
        }
        if (isNerve(GET_NERVE_ANON(PetariNrvSwoonStart)) || isNerve(GET_NERVE_ANON(PetariNrvSwoonEnd))) {
            mAnimScaleCtrl->startHitReaction();
            return true;
        }

        if (isNerve(GET_NERVE_ANON(PetariNrvWait)) && !MR::isHiddenModel(this)) {
            mAnimScaleCtrl->startHitReaction();
            return true;
        }
    }

    if (isSolidBody() && (MR::isMsgJetTurtleAttack(msg) || MR::isMsgFireBallAttack(msg) || MR::isMsgInvincibleAttack(msg))) {
        MR::setVelocityBlowAttack(this, pSender, pReceiver, cNum_receivePlayerAttackAtBody_f1, 0.0f, 4);
        setNerve(GET_NERVE_ANON(PetariNrvKickOut));
        return true;
    }

    if (isNerve(GET_NERVE_ANON(PetariNrvFreeze)) && !MR::isHiddenModel(this) && (MR::isMsgPlayerTrample(msg) || MR::isMsgPlayerHipDrop(msg))) {
        setNerve(GET_NERVE_ANON(PetariNrvSmash));
        return true;
    }

    if (!isNerve(GET_NERVE_ANON(PetariNrvSwoon))) {
        return false;
    }

    if (MR::isMsgPlayerSpinAttack(msg) || MR::isMsgFreezeAttack(msg)) {
        MR::setVelocityBlowAttack(this, pSender, pReceiver, cNum_receivePlayerAttackAtBody_f2, 0.0f, 4);
        setNerve(GET_NERVE_ANON(PetariNrvSpinOut));
        return true;
    }
    if (MR::isMsgPlayerTrample(msg) || MR::isMsgPlayerHipDrop(msg)) {
        setNerve(GET_NERVE_ANON(PetariNrvSmash));
        return true;
    }

    return false;
}

bool Petari::receivePlayerAttackAtSpin(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (!MR::isMsgPlayerSpinAttack(msg) && !MR::isMsgFreezeAttack(msg)) {
        return false;
    }

    if (isNerve(GET_NERVE_ANON(PetariNrvWait)) || isNerve(GET_NERVE_ANON(PetariNrvEscape)) || isNerve(GET_NERVE_ANON(PetariNrvFreeze))) {
        setNerve(GET_NERVE_ANON(PetariNrvSwoonStart));
        return true;
    }

    return false;
}

bool Petari::isSolidBody() const {
    if (MR::isHiddenModel(this)) {
        return false;
    }

    return isNerve(GET_NERVE_ANON(PetariNrvSwoonStart)) || isNerve(GET_NERVE_ANON(PetariNrvSwoon)) || isNerve(GET_NERVE_ANON(PetariNrvSwoonEnd)) ||
           isNerve(GET_NERVE_ANON(PetariNrvWait)) || isNerve(GET_NERVE_ANON(PetariNrvFreeze));
}

// Moves only on the ground.
void Petari::moveTowardTargetDirection(f32 speed, f32 damp, f32 turnDegree) {
    if (!MR::isBindedGround(this)) {
        speed = 0.0f;
    }
    MR::moveAndTurnToDirection(this, &mFront, mTargetDir, {speed, cNum_moveTowardTargetDirection_f1, damp, turnDegree});
}

void Petari::updateFootPrint() {
    if (MR::isBindedGround(this)) {
        mFootPrint->addPrint(mPosition - *MR::getGroundNormal(this) * mFootprintYOffs, mFront, *MR::getGroundNormal(this), false);
    }
}

void Petari::calcSpinOutVelocity(f32 speed) {
    Vec3f deltaDir = mBodyCenter - *MR::getPlayerCenterPos();
    deltaDir.orthogonalize(mGravity);

    if (!MR::isNearZero(deltaDir)) {
        mFront.set(-deltaDir);
        MR::normalize(&mFront);
    }

    deltaDir.setLength(speed);
    mVelocity.set(deltaDir);
}

void Petari::calcApproachDirection() {
    Vec3f toPlayer(*MR::getPlayerCenterPos());
    toPlayer -= mBodyCenter;
    mTargetDir = toPlayer;
    if (MR::isNearZero(mTargetDir)) {
        mTargetDir = mFront;
    }
    MR::normalize(&mTargetDir);
}

void Petari::calcEscapeDirection() {
    Vec3f fromPlayer(mBodyCenter);
    fromPlayer -= *MR::getPlayerCenterPos();
    mTargetDir = fromPlayer;
    if (MR::isNearZero(mTargetDir)) {
        mTargetDir = mFront;
    }
    MR::normalize(&mTargetDir);
}

void Petari::avoidPlayer() {
    Vec3f crossVec = mTargetDir.cross(mFront);
    f32 turnSign;
    if (crossVec.dot(mGravity) < 0.0f) {
        turnSign = -1.0f;
    } else {
        turnSign = 1.0f;
    }

    Vec3f dirToPlayer = *MR::getPlayerCenterPos() - mBodyCenter;
    if (!MR::isNearZero(dirToPlayer)) {
        f32 avoidWeight = cNum_avoidPlayer_f1 / (dirToPlayer * cNum_avoidPlayer_f2).squared();
        MR::clamp01(&avoidWeight);
        MR::normalize(&dirToPlayer);
        dirToPlayer.negate();
        Vec3f playerVelocity = *MR::getPlayerVelocity();
        MR::normalizeOrZero(&playerVelocity);
        f32 approaching = dirToPlayer.dot(playerVelocity);
        if (approaching < 0.0f) {
            approaching = 0.0f;
        }
        avoidWeight *= approaching;
        Quat4f quat;
        quat.setRotate(mGravity, ((avoidWeight * ((cNum_avoidPlayer_f3 * turnSign) * MR::pi())) / cNum_avoidPlayer_f4));
        quat.transform(mTargetDir);
    }
}

void Petari::avoidWall() {
    Vec3f planarDir = mTargetDir;
    planarDir.orthogonalize(mGravity);
    if (!MR::isNearZero(planarDir)) {
        MR::normalize(&planarDir);
        Triangle rayHitTri;
        Vec3f hitPos;
        if (MR::getFirstPolyOnLineToMapAndMoveLimit(&hitPos, &rayHitTri, mBodyCenter, planarDir * sWallLookDistance)) {
            Vec3f wallNormal = *rayHitTri.getNormal(0);
            wallNormal.orthogonalize(mGravity);
            if (!MR::isNearZero(wallNormal)) {
                MR::normalize(&wallNormal);
                // Bends away from the wall by the square of how near it is.
                f32 proximity = 1.0f - (hitPos.distance(mBodyCenter) / sWallLookDistance);
                MR::clamp01(&proximity);
                f32 falloff = proximity * proximity;

                mTargetDir -= wallNormal * (wallNormal.dot(mTargetDir)) * cNum_avoidWall_f1 * falloff * cNum_avoidWall_f2;
            }
        }
    }
}

void Petari::calcCenter() {
    mBodyCenter = mPosition + mGravity * -cNum_calcCenter_f1;
}

void Petari::meander() {
    if (MR::isStep(this, mMeanderStep)) {
        mMeanderAngle = MR::getRandom(-cNum_meander_f1, cNum_meander_f2);
        mMeanderStep = getNerveStep() + MR::getRandom((s32)60, 120);
    }

    Quat4f turn;
    turn.setRotate(-mGravity, MR::pi() * mMeanderAngle / cNum_meander_f3);
    turn.transform(mTargetDir);
}

f32 Petari::calcBoost(f32 baseSpeed, f32 boostMultiplier, f32 effectiveRange) const {
    f32 distToPlayer = MR::calcDistanceToPlayer(mBodyCenter);

    f32 scaledDist = distToPlayer * cNum_calcBoost_f1;

    f32 distSquared = scaledDist * scaledDist;
    f32 intensity = effectiveRange / distSquared;

    if (intensity > 1.0f) {
        intensity = 1.0f;
    }

    return baseSpeed + (baseSpeed * boostMultiplier * intensity);
}

void Petari::appearStarPieceGradually() {
    if (getNerveStep() % 5 != 0) {
        return;
    }

    s32 numPieces = mStarPieceRemaining < 2 ? mStarPieceRemaining : 2;

    Vec3f mouthPos;
    calcMouthPos(&mouthPos);
    MR::appearStarPiece(this, mouthPos, numPieces, cNum_appearStarPieceGradually_f1, cNum_appearStarPieceGradually_f2, false);
    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    mStarPieceRemaining -= numPieces;
}

void Petari::appearStarPieceAll() {
    if (mStarPieceRemaining > 0) {
        MR::appearStarPiece(this, mPosition, mStarPieceRemaining, cNum_appearStarPieceAll_f1, cNum_appearStarPieceAll_f2, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    }
}

bool Petari::reflectStarPointer2P() {
    if (!MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        return false;
    }

    setNerve(GET_NERVE_ANON(PetariNrvFreeze));
    return true;
}

void Petari::calcMouthPos(Vec3f* pOut) const {
    Pos3f mtx;
    mtx.set(MR::getJointMtx(this, "Head3"));
    mtx.getTrans(*pOut);
}

void Petari::tryEmitFirstFootPrintEffect() {
    if (mRequestSmoke) {
        MR::emitEffect(this, "StartSmoke");
        mRequestSmoke = false;
    }
}
