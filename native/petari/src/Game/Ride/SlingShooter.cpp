#include "Game/Ride/SlingShooter.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/SpiderThread.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_math.h>
#include "Game/System/NebulaInput.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 210.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAim_f1, "exeAim.f1", 100.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeAttackSuccess_1, "exeAttackSuccess.1", 2);

namespace {
    static const s32 sStepFreeInvalid = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sNpcSensorPosOffsetZ, 200.0f);
    NEBULA_TUNABLE(f32, sAimFollowRate, 0.1f);
    NEBULA_TUNABLE(f32, sAimDistanceMin, 100.0f);
    NEBULA_TUNABLE(f32, sAimDistanceMax, 300.0f);
    NEBULA_TUNABLE(s32, sAimAnimInterpole, 10);
    NEBULA_TUNABLE(f32, sAimDistanceToStretch, 300.0f);
    NEBULA_TUNABLE(f32, sAimDistanceToWait, 200.0f);
    NEBULA_TUNABLE(f32, sBindAttackSpeed, 45.0f);
    NEBULA_TUNABLE(f32, sBindAttackFrictionRate, 1.0f);
    static const s32 sStepBindAttack = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(s32, sBindAttackInvalidTime, 10);
    NEBULA_TUNABLE(s32, sStepToStopScene, 1);
    NEBULA_TUNABLE(s32, sStepStopSceneAttackSuccess, 15);
    static const s32 sStepStopSceneLastDamage = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, sStepToEnableBindCancel, 10);

    void endBindAndPlayerNoJump(LiveActor* pActor) {
        MR::endBindAndPlayerJump(pActor, Vec3f(0.0f, 0.0f, 0.0f), 0);
    }
};  // namespace

namespace NrvSlingShooter {
    NEW_NERVE(SlingShooterNrvFree, SlingShooter, Free);
    NEW_NERVE(SlingShooterNrvFreeInvalid, SlingShooter, FreeInvalid);
    NEW_NERVE(SlingShooterNrvLand, SlingShooter, Land);
    NEW_NERVE(SlingShooterNrvWait, SlingShooter, Wait);
    NEW_NERVE(SlingShooterNrvAim, SlingShooter, Aim);
    NEW_NERVE(SlingShooterNrvAttack, SlingShooter, Attack);
    NEW_NERVE(SlingShooterNrvAttackSuccess, SlingShooter, AttackSuccess);
};  // namespace NrvSlingShooter

SlingShooter::SlingShooter(const char* pName)
    : LiveActor(pName), mIsKinopioCameraFocused(), mPlayerZ(), mIsKinopioAttached(), mIsKinopioTalkEnabled(true), mIsLastDamage(), mBasePos(),
      mNeutralPos(), mUp(), mPointerPos(0.0f, 0.0f, 0.0f), mPadChannel(-1), mPassThroughEnable(), mAttackVelocity(0.0f, 0.0f, 0.0f), mRider(),
      mCameraTargetMtx(), mCameraInfo() {
    lumacore::setClass(this, lumacore::kClassId< SlingShooter >);
    mBaseMtx.identity();
}

void SlingShooter::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::getPlacementArg3NoInit(rIter, &mIsKinopioCameraFocused);

    initModelManagerWithAnm("SpiderThreadAttachPoint", nullptr, false);

    MR::connectToSceneRide(this);

    initHitSensor(3);
    MR::addHitSensorCallbackBinder(this, "bind", 8, cNum_init_f1);
    MR::addHitSensorCallback(this, "npc", ATYPE_SPRING_ATTACKER_KINOPIO_BIND, 8, cNum_init_f2);
    MR::addHitSensor(this, "attack", ATYPE_SPRING_ATTACKER, 8, cNum_init_f3, Vec3f(0.0f, 0.0f, 0.0f));

    initBinder(cNum_init_f4, cNum_init_f5, 8);
    MR::offBind(this);

    initEffectKeeper(2, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    MR::addEffect(this, "InvalidHitMark");
    initSound(8, false);

    if (mIsKinopioCameraFocused) {
        MR::useStageSwitchWriteA(this, rIter);
    }

    MR::calcGravity(this);
    MR::initShadowVolumeSphere(this, cNum_init_f6);

    MR::initStarPointerTarget(this, cNum_init_f7, Vec3f(0.0f, cNum_init_f8, 0.0f));

    initNerve(GET_NERVE(SlingShooter, SlingShooterNrvFree));

    mCameraTargetMtx = new CameraTargetMtx("カメラターゲットダミー");
    mCameraInfo = MR::createActorCameraInfo(rIter);

    MR::declareEventCamera(mCameraInfo, "プレーヤー射出者[ウェイト]");  // player launcher [wait]
    MR::declareEventCamera(mCameraInfo, "プレーヤー射出者[狙い中]");    // player launcher [aim]
    MR::declareEventCamera(mCameraInfo, "プレーヤー射出者[攻撃中]");    // player launcher [attack]
    MR::declareEventCamera(mCameraInfo, "キノピオ射出者[狙い中]");      // kinopio launcher [aim]

    makeActorAppeared();
}

void SlingShooter::initAfterPlacement() {
    MR::startActorBindToSpiderThread(this, &mBasePos, &mNeutralPos, &mUp, mPosition, Vec3f(0.0f, 0.0f, 0.0f));
}

void SlingShooter::calcAnim() {
    if (mIsKinopioAttached && mRider != nullptr) {
        updateActorMtx();
        MR::setBaseTRMtx(mRider, mBaseMtx);
        mRider->mPosition.set(mPosition);
    }
    LiveActor::calcAnim();
}

void SlingShooter::exeFree() {
    mPosition.set(*mBasePos);
    if (MR::isPlayerInBind() && !mIsKinopioCameraFocused) {
        setNerve(GET_NERVE(SlingShooter, SlingShooterNrvFreeInvalid));
    }
}

void SlingShooter::exeFreeInvalid() {
    exeFree();
    if (MR::isGreaterStep(this, ::sStepFreeInvalid) && (mIsKinopioAttached || MR::isOnGroundPlayer())) {
        mIsKinopioAttached = false;
        setNerve(GET_NERVE(SlingShooter, SlingShooterNrvFree));
    }
}

void SlingShooter::exeLand() {
    if (MR::isFirstStep(this)) {
        if (!mIsKinopioAttached) {
            MR::startBckPlayer("SpiderCocoonLand", 1);
            MR::startEventCamera(mCameraInfo, "プレーヤー射出者[ウェイト]", CameraTargetArg(mCameraTargetMtx));
        } else {
            MR::startBckWithInterpole(mRider, "CocoonLand", 1);
        }
        mVelocity.zero();
    }

    if (mIsKinopioAttached) {
        setNerve(GET_NERVE(SlingShooter, SlingShooterNrvWait));
        return;
    }

    if (updateWait()) {
        return;
    }

    if (MR::isBckStopped(mRider)) {
        setNerve(GET_NERVE(SlingShooter, SlingShooterNrvWait));
        return;
    }

    if (MR::isGreaterStep(this, ::sStepToEnableBindCancel)) {
        if (tryCancel()) {
            return;
        }
    }
}

void SlingShooter::exeWait() {
    if (MR::isFirstStep(this)) {
        if (!mIsKinopioAttached) {
            MR::startBckPlayer("SpiderCocoonWait");
        } else {
            MR::startBck(mRider, "SpiderCocoonWait");
            if (mIsKinopioTalkEnabled) {
                MR::sendSimpleMsgToActor(ACTMES_NPC_EVENT_TALK_ENABLE, mRider);
            }
        }
    }

    if (!updateWait()) {
        if (tryCancel()) {
            return;
        }
    }
}

void SlingShooter::exeAim() {
    if (MR::isFirstStep(this)) {
        if (!mIsKinopioAttached) {
            MR::startSound(mRider, "SE_PV_CATCH");
        } else {
            MR::sendSimpleMsgToActor(ACTMES_NPC_EVENT_TALK_DISABLE, mRider);
        }

        MR::onBind(this);
        MR::startSound(this, "SE_OJ_SPACE_COCOON_ATTACH");
        if (mPadChannel == PLAYER_2) {
            MR::startCSSound2P("CS_DPD_HIT", nullptr);
        }
    }

    f32 dist = mPosition.distance(*mNeutralPos);
    if (dist >= ::sAimDistanceMin) {
        MR::startLevelSound(this, "SE_OJ_LV_SPACE_COCOON_DRAG", ((dist - ::sAimDistanceMin) / (::sAimDistanceMax - ::sAimDistanceMin)) * cNum_exeAim_f1);
    }

    if (tryRelease()) {
        return;
    }

    if (!MR::isHeldJump(mPadChannel)) {
        endCommandStream();

        if (!mIsKinopioAttached) {
            MR::endEventCamera(mCameraInfo, "プレーヤー射出者[狙い中]", true);
            MR::startEventCamera(mCameraInfo, "プレーヤー射出者[ウェイト]", CameraTargetArg(mCameraTargetMtx));
        } else if (mIsKinopioCameraFocused) {
            MR::endEventCamera(mCameraInfo, "キノピオ射出者[狙い中]", true);
        }

        MR::offBind(this);
        MR::sendMsgToSpiderThread(ACTMES_SLING_SHOOT_ACTOR_HANG_END, getSensor("bind"));
        setNerve(GET_NERVE(SlingShooter, SlingShooterNrvWait));
        return;
    }

    updateHang();
}

void SlingShooter::exeAttack() {
    mVelocity.mult(::sBindAttackFrictionRate);

    if (tryAttackMap() || MR::isStep(this, ::sStepBindAttack)) {
        if (!mIsKinopioAttached) {
            MR::endEventCamera(mCameraInfo, "プレーヤー射出者[攻撃中]", true);
            ::endBindAndPlayerNoJump(this);
        }
        endBind();
    }
}

void SlingShooter::exeAttackSuccess() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
    }

    mVelocity.mult(::sBindAttackFrictionRate);

    if (MR::isStep(this, ::sStepToStopScene)) {
        if (mIsLastDamage) {
            MR::stopScene(::sStepStopSceneLastDamage);
        } else {
            MR::stopScene(::sStepStopSceneAttackSuccess);
        }
        mIsLastDamage = false;
    }

    if (MR::isStep(this, cStep_exeAttackSuccess_1)) {
        if (mPassThroughEnable) {
            mVelocity.set(mAttackVelocity);
            mPassThroughEnable = false;
            setNerve(GET_NERVE(SlingShooter, SlingShooterNrvAttack));
            return;
        }

        if (!mIsKinopioAttached) {
            MR::endEventCamera(mCameraInfo, "プレーヤー射出者[攻撃中]", true);
            MR::startBckPlayer("AirRotation");
            ::endBindAndPlayerNoJump(this);
        }
        endBind();
    }
}

void SlingShooter::updateHitSensor(HitSensor* pSensor) {
    if (MR::isSensorAutoRush(pSensor)) {
        pSensor->mPosition.x = mPosition.x;
        pSensor->mPosition.y = mPosition.y;
        pSensor->mPosition.z = MR::getSpiderThreadPosZ();
    }

    if (pSensor->isType(ATYPE_SPRING_ATTACKER_KINOPIO_BIND)) {
        pSensor->mPosition.x = mPosition.x;
        pSensor->mPosition.y = mPosition.y;
        pSensor->mPosition.z = MR::getSpiderThreadPosZ() - ::sNpcSensorPosOffsetZ;
    }
}

void SlingShooter::emitNullEffect(HitSensor* pSender, HitSensor* pReceiver) {
    Vec3f vecBetween;
    MR::calcPosBetweenSensors(&vecBetween, pSender, pReceiver, 0.0f);
    MR::emitEffectHit(this, vecBetween, nullptr);
}

void SlingShooter::emitHitEffect(HitSensor* pSender, HitSensor* pReceiver) {
    Vec3f vecBetween;
    MR::calcPosBetweenSensors(&vecBetween, pSender, pReceiver, 0.0f);
    MR::emitEffectHit(this, vecBetween, "InvalidHitMark");
}

void SlingShooter::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mIsKinopioCameraFocused && mRider == nullptr && !isNerve(GET_NERVE(SlingShooter, SlingShooterNrvFreeInvalid)) && MR::isSensorNpc(pReceiver) &&
        pReceiver->receiveMessage(ACTMES_NPC_EVENT_START, pSender)) {
        mIsKinopioAttached = true;

        startBind(pReceiver->mHost);
        getSensor("npc")->invalidate();
        mRider->mVelocity.zero();
        return;
    }

    if (mRider != nullptr && !MR::tryGetItem(pSender, pReceiver) &&
        (pSender->isType(ATYPE_SPRING_ATTACKER) || pSender->isType(ATYPE_SPRING_ATTACKER_KINOPIO)) &&
        isNerve(GET_NERVE(SlingShooter, SlingShooterNrvAttack)) && pReceiver->receiveMessage(ACTMES_SLING_SHOOT_ATTACK, pSender)) {
        if (pReceiver->receiveMessage(ACTMES_IS_BROKEN, pSender)) {
            emitNullEffect(pSender, pReceiver);

            mAttackVelocity.set(mVelocity);
            mVelocity.zero();

            mPassThroughEnable = pReceiver->receiveMessage(ACTMES_SLING_SHOOT_PASS_THROUGH_ENABLE, pSender);
            mIsLastDamage = pReceiver->receiveMessage(ACTMES_SLING_SHOOT_IS_LAST_DAMAGE, pSender);
            setNerve(GET_NERVE(SlingShooter, SlingShooterNrvAttackSuccess));
            return;
        }

        emitHitEffect(pSender, pReceiver);

        if (!mIsKinopioAttached || !pReceiver->isType(ATYPE_TOMB_SPIDER_COCOON)) {
            MR::startSound(this, "SE_BM_TSPIDER_BOUND");
        }

        if (!mIsKinopioAttached) {
            MR::startBckPlayer("WallHit");
            MR::endEventCamera(mCameraInfo, "プレーヤー射出者[攻撃中]", true);
            ::endBindAndPlayerNoJump(this);
        }
        endBind();
    }
}

bool SlingShooter::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return MR::isMsgStarPieceReflect(msg);
}

bool SlingShooter::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(SlingShooter, SlingShooterNrvLand)) || isNerve(GET_NERVE(SlingShooter, SlingShooterNrvWait)) ||
        isNerve(GET_NERVE(SlingShooter, SlingShooterNrvAim)) || isNerve(GET_NERVE(SlingShooter, SlingShooterNrvAttack))) {
        if (!mIsKinopioAttached) {
            if (isNerve(GET_NERVE(SlingShooter, SlingShooterNrvAim))) {
                MR::sendMsgToSpiderThread(ACTMES_SLING_SHOOT_ACTOR_HANG_END, getSensor("bind"));
            }

            endCommandStream();
            MR::endEventCamera(mCameraInfo, "プレーヤー射出者[ウェイト]", true);
            MR::endEventCamera(mCameraInfo, "プレーヤー射出者[狙い中]", true);
            MR::endEventCamera(mCameraInfo, "プレーヤー射出者[攻撃中]", true);

            mPosition.z = mPlayerZ;
            MR::setPlayerPos(mPosition);

            MR::emitEffect(this, "Stick");
            MR::endBindAndPlayerDamage(this, Vec3f(0.0f, 0.0f, 0.0f));
            endBind();
        }
        return true;
    }

    return false;
}

bool SlingShooter::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_AUTORUSH_BEGIN) {
        if (isNerve(GET_NERVE(SlingShooter, SlingShooterNrvFreeInvalid))) {
            return false;
        }

        if (mIsKinopioAttached) {
            if (MR::touchActorToSpiderThread(*MR::getPlayerPos(), *MR::getPlayerVelocity())) {
                MR::startSound(this, "SE_OJ_SPIDER_THREAD_BOUND_L");
                if (mRider != nullptr) {
                    MR::startSound(mRider, "SE_SV_KINOPIO_POINTING");
                }
            }
            return false;
        }

        MR::touchActorToSpiderThread(*MR::getPlayerPos(), *MR::getPlayerVelocity());
        mIsKinopioAttached = false;
        startBind(pSender->mHost);
        return true;
    }

    if (msg == ACTMES_UPDATE_BASEMTX) {
        updateActorMtx();
        MR::setBaseTRMtx(mRider, mBaseMtx);
        return true;
    }

    // remove the camera area on battle start
    if (msg == ACTMES_TOMB_SPIDER_BATTLE_START && mIsKinopioCameraFocused && MR::isValidSwitchA(this)) {
        mIsKinopioCameraFocused = false;
        MR::onSwitchA(this);
        return true;
    }

    if (msg == ACTMES_TOMB_SPIDER_DEMO_START || msg == ACTMES_RUSH_CANCEL) {
        if (mIsKinopioAttached) {
            if (msg == ACTMES_TOMB_SPIDER_DEMO_START && isNerve(GET_NERVE(SlingShooter, SlingShooterNrvAim))) {
                endCommandStream();
                MR::offBind(this);
                MR::sendMsgToSpiderThread(ACTMES_SLING_SHOOT_ACTOR_HANG_END, getSensor("bind"));
                setNerve(GET_NERVE(SlingShooter, SlingShooterNrvWait));
                return true;
            }
            return false;
        }

        if (isNerve(GET_NERVE(SlingShooter, SlingShooterNrvLand)) || isNerve(GET_NERVE(SlingShooter, SlingShooterNrvWait))) {
            startCancelWait();
            return true;
        }

        if (isNerve(GET_NERVE(SlingShooter, SlingShooterNrvAim))) {
            MR::offBind(this);
            startCancelAim();
            MR::startBckPlayer("Wait");

            ::endBindAndPlayerNoJump(this);
            endBind();
            return true;
        }

        if (msg == ACTMES_RUSH_CANCEL && mRider != nullptr &&
            (isNerve(GET_NERVE(SlingShooter, SlingShooterNrvAttack)) || isNerve(GET_NERVE(SlingShooter, SlingShooterNrvAttackSuccess)))) {
            MR::offBind(this);
            endBind();
            return true;
        }

        return true;
    }

    return false;
}

void SlingShooter::calcAndSetBaseMtx() {
    if (isNerve(GET_NERVE(SlingShooter, SlingShooterNrvLand)) || isNerve(GET_NERVE(SlingShooter, SlingShooterNrvWait)) ||
        isNerve(GET_NERVE(SlingShooter, SlingShooterNrvAim))) {
        MR::setBaseTRMtx(this, mBaseMtx);
        return;
    }

    if (mUp == nullptr || mBasePos == nullptr) {
        return;
    }

    Pos3f mtx;
    calcBaseMtx(&mtx);
    MR::setBaseTRMtx(this, mtx);
}

void SlingShooter::calcBaseMtx(Pos3f* pBaseMtx) {
    Vec3f up = *mUp * -1.0f;
    MR::normalize(&up);
    Vec3f front(0.0f, 0.0f, -1.0f);
    Vec3f side = up.cross(front);
    if (MR::isNearZero(side)) {
        front.set< f32 >(1.0f, 0.0f, 0.0f);
        side = up.cross(front);
    }
    MR::normalize(&side);
    front.cross(side, up);
    MR::normalize(&front);
    pBaseMtx->setXYZDir(side, up, front);
    pBaseMtx->setTrans(*mBasePos);
}

bool SlingShooter::updateWait() {
    mPosition.set(*mBasePos);
    MR::reportStarPointerGrabTarget(this);

    if (MR::isStarPointerPointing(this, PLAYER_1, true, "弱")) {
        MR::requestStarPointerModeBlueStarReady(this);
    }

    if (MR::isStarPointerPointing(this, PLAYER_1, true, "弱") && MR::isPressedJump(PLAYER_1) &&
        MR::tryStartStarPointerCommandStream(this, &mPosition, PLAYER_1, false)) {
        mPadChannel = PLAYER_1;
    }

    if (mPadChannel >= 0) {
        if (!MR::sendMsgToSpiderThread(ACTMES_SLING_SHOOT_ACTOR_HANG_START, getSensor("bind"))) {
            endCommandStream();
            return false;
        }

        MR::calcStarPointerPosOnPlane(&mPointerPos, *mNeutralPos, Vec3f(0.0f, 0.0f, 1.0f), mPadChannel, false);

        if (!mIsKinopioAttached) {
            MR::endEventCamera(mCameraInfo, "プレーヤー射出者[ウェイト]", true);
            MR::startEventCamera(mCameraInfo, "プレーヤー射出者[狙い中]", CameraTargetArg(mCameraTargetMtx));
        } else if (mIsKinopioCameraFocused) {
            MR::startEventCameraNoTarget(mCameraInfo, "キノピオ射出者[狙い中]");
        }
        setNerve(GET_NERVE(SlingShooter, SlingShooterNrvAim));
        return true;
    }

    return false;
}

void SlingShooter::updateHang() {
    f32 dist = mPosition.distance(*mNeutralPos);

    if (MR::isStarPointerInScreen(mPadChannel)) {
        MR::calcStarPointerPosOnPlane(&mPointerPos, *mNeutralPos, Vec3f(0.0f, 0.0f, 1.0f), mPadChannel, false);
    }

    Vec3f followPos = mPointerPos;

    if (followPos.distance(*mNeutralPos) > ::sAimDistanceMax) {
        followPos.sub(*mNeutralPos);
        MR::normalize(&followPos);
        followPos.scale(::sAimDistanceMax);
        followPos.add(*mNeutralPos);
    }

    Vec3f pos = followPos * ::sAimFollowRate + mPosition * (1.0f - ::sAimFollowRate);

    mVelocity = pos - mPosition;

    if (mRider == nullptr) {
        return;
    }

    if (dist < ::sAimDistanceToWait) {
        if (mIsKinopioAttached) {
            if (!MR::isBckPlaying(mRider, "CocoonStretch")) {
                MR::startBckWithInterpole(mRider, "CocoonStretch", ::sAimAnimInterpole);
            }
        } else {
            if (!MR::isBckPlaying(mRider, "SpiderCocoonWait")) {
                MR::startBckPlayer("SpiderCocoonWait", ::sAimAnimInterpole);
            }
        }
    }

    if (dist > ::sAimDistanceToStretch) {
        if (mIsKinopioAttached) {
            if (!MR::isBckPlaying(mRider, "CocoonStretch")) {
                MR::startBckWithInterpole(mRider, "CocoonStretch", ::sAimAnimInterpole);
            }
        } else {
            if (!MR::isBckPlaying(mRider, "SpiderCocoonStretch")) {
                MR::startBckPlayer("SpiderCocoonStretch", ::sAimAnimInterpole);
            }
        }
    }
}

void SlingShooter::updateActorMtx() {
    if (isNerve(GET_NERVE(SlingShooter, SlingShooterNrvAttack)) || isNerve(GET_NERVE(SlingShooter, SlingShooterNrvAttackSuccess))) {
        mBaseMtx.setTrans(mPosition);
        return;
    }

    Vec3f up;
    if (isNerve(GET_NERVE(SlingShooter, SlingShooterNrvLand)) || isNerve(GET_NERVE(SlingShooter, SlingShooterNrvWait))) {
        up.set(*mUp);
        up.scale(-1.0f);
    } else {
        up.set(*mNeutralPos);
        up.sub(mPosition);
    }

    if (!MR::isNearZero(up)) {
        MR::normalize(&up);
        Vec3f front;
        mBaseMtx.getZDir(front);
        Vec3f side = up.cross(front);
        if (!MR::isNearZero(side)) {
            MR::normalize(&side);
            up.cross(front, side);
            MR::normalize(&up);
            mBaseMtx.setXYZDir(side, up, front);
        }
    }
    mBaseMtx.setTrans(mPosition);
}

bool SlingShooter::tryCancel() {
    if (!mIsKinopioAttached && MR::isSpin(PLAYER_1) && MR::isGreaterStep(this, ::sStepToEnableBindCancel)) {
        MR::endEventCamera(mCameraInfo, "プレーヤー射出者[ウェイト]", true);
        MR::startBckPlayer("Spin2nd");
        MR::emitEffect(this, "Stick");
        MR::startSound(mRider, "SE_PV_JUMP_S");
        MR::startSound(mRider, "SE_PM_JUMP_M");
        startCancelWait();
        return true;
    }

    return false;
}

bool SlingShooter::tryRelease() {
    if (MR::isHeldJump(mPadChannel)) {
        return false;
    }

    if (mPosition.distance(*mNeutralPos) < ::sAimDistanceMin) {
        MR::sendMsgToSpiderThread(ACTMES_SLING_SHOOT_ACTOR_HANG_END, getSensor("bind"));
        endCommandStream();

        if (mIsKinopioAttached) {
            MR::endEventCamera(mCameraInfo, "キノピオ射出者[狙い中]", true);
        } else {
            MR::endEventCamera(mCameraInfo, "プレーヤー射出者[狙い中]", true);
            MR::startEventCamera(mCameraInfo, "プレーヤー射出者[ウェイト]", CameraTargetArg(mCameraTargetMtx));
        }
        setNerve(GET_NERVE(SlingShooter, SlingShooterNrvWait));
        return true;
    }

    startCancelAim();

    mIsKinopioTalkEnabled = false;

    mVelocity.set(*mNeutralPos);
    mVelocity.sub(mPosition);
    mVelocity.z = 0.0f;

    if (MR::isNearZero(mVelocity)) {
        mVelocity.set(*mUp);
    }

    MR::normalize(&mVelocity);

    Vec3f up = mVelocity;
    mVelocity.mult(::sBindAttackSpeed);
    up.scale(-1.0f);

    Vec3f front;
    mBaseMtx.getZDir(front);
    Vec3f side = up.cross(front);
    if (!MR::isNearZero(side)) {
        MR::normalize(&side);
        mBaseMtx.setXYZDir(side, up, front);
    }

    MR::emitEffect(mRider, "SpaceCocoonBlur");
    MR::emitEffect(this, "Stick");
    MR::startSound(this, "SE_OJ_SPACE_COCOON_LAUNCH");

    if (!mIsKinopioAttached) {
        MR::startBckPlayer("CocoonFly");
        MR::endEventCamera(mCameraInfo, "プレーヤー射出者[狙い中]", true);
        MR::startEventCameraNoTarget(mCameraInfo, "プレーヤー射出者[攻撃中]");
        MR::startSound(mRider, "SE_PV_JUMP_JOY");
        MR::validatePlayerSensor();
    } else {
        MR::startSound(mRider, "SE_SV_KINOPIO_TALK_GLAD_FLY");
        if (mIsKinopioCameraFocused) {
            MR::endEventCamera(mCameraInfo, "キノピオ射出者[狙い中]", true);
        }
    }

    if (mIsKinopioCameraFocused) {
        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }
        mIsKinopioCameraFocused = false;
    }

    setNerve(GET_NERVE(SlingShooter, SlingShooterNrvAttack));
    return true;
}

bool SlingShooter::tryAttackMap() {
    if (!MR::isBinded(this)) {
        return false;
    }

    if (MR::isLessStep(this, ::sBindAttackInvalidTime)) {
        return false;
    }

    bool attackSuccess = true;
    bool isAnyHit = false;
    for (s32 plane = 0; plane < MR::getBindedPlaneNum(this); plane++) {
        if (!(mVelocity.dot(*MR::getBindedPlaneNormal(this, plane)) > 0.0f)) {
            HitSensor* planeSensor = MR::getBindedPlaneSensor(this, plane);
            MR::shakeCameraNormal();
            if (!MR::sendMsgEnemyAttack(planeSensor, getSensor("attack"))) {
                if (!planeSensor->receiveMessage(ACTMES_IS_BROKEN, getSensor("attack"))) {
                    if (mIsKinopioAttached) {
                        MR::startSound(mRider, "SE_SV_KINOPIO_TRAMPLED");
                    } else {
                        MR::startSound(mRider, "SE_PV_DAMAGE_S");
                    }
                    MR::startSound(mRider, "SE_PM_LAND_HEAVY");
                    attackSuccess = false;
                    continue;
                }
            }
            isAnyHit = true;
            MR::emitEffectHit(this, mPosition, nullptr);
        }
    }

    if (attackSuccess) {
        if (isAnyHit) {
            setNerve(GET_NERVE(SlingShooter, SlingShooterNrvAttackSuccess));
        }
        return false;
    }

    return true;
}

void SlingShooter::startBind(LiveActor* pRider) {
    mRider = pRider;
    mPlayerZ = pRider->mPosition.z;

    MR::invalidateClipping(this);

    if (!mIsKinopioAttached) {
        Pos3f mtx;
        calcBaseMtx(&mtx);
        mCameraTargetMtx->mMatrix.setInline(mtx);
        mCameraTargetMtx->mMatrix.setTrans(*mNeutralPos);
        getSensor("attack")->setType(ATYPE_SPRING_ATTACKER);
        MR::startSound(mRider, "SE_PV_CATCH");
    } else {
        getSensor("attack")->setType(ATYPE_SPRING_ATTACKER_KINOPIO);
    }

    if (!mIsKinopioCameraFocused) {
        MR::startSound(this, "SE_OJ_SPACE_COCOON_ATTACH");
    }

    updateActorMtx();
    setNerve(GET_NERVE(SlingShooter, SlingShooterNrvLand));
}

void SlingShooter::startCancelWait() {
    mPosition.z = mPlayerZ;
    MR::setPlayerPos(Vec3f(MR::getPlayerPos()->x, MR::getPlayerPos()->y, mPosition.z));

    ::endBindAndPlayerNoJump(this);
    endBind();
    endCommandStream();
}

void SlingShooter::startCancelAim() {
    mPosition.z = MR::getSpiderThreadPosZ();
    MR::setPlayerPos(Vec3f(MR::getPlayerPos()->x, MR::getPlayerPos()->y, mPosition.z));

    MR::sendMsgToSpiderThread(ACTMES_SLING_SHOOT_ACTOR_HANG_END, getSensor("bind"));
    endCommandStream();
}

void SlingShooter::endBind() {
    if (mIsKinopioAttached) {
        mRider->mPosition.z = MR::getSpiderThreadPosZ() - ::sNpcSensorPosOffsetZ;
        mRider->mVelocity.zero();
        mRider->receiveMessage(ACTMES_NPC_EVENT_END, MR::getMessageSensor(), MR::getMessageSensor());
    }

    MR::deleteEffect(mRider, "SpaceCocoonBlur");
    mVelocity.zero();
    mRider = nullptr;
    MR::offBind(this);
    mPosition.set(*mBasePos);
    MR::validateClipping(this);
    setNerve(GET_NERVE(SlingShooter, SlingShooterNrvFreeInvalid));
}

void SlingShooter::endCommandStream() {
    if (mPadChannel >= 0 && MR::isStarPointerCommandStream(this, mPadChannel)) {
        MR::tryEndStarPointerCommandStream(this, mPadChannel);
        mPadChannel = -1;
    }
}
