#include "Game/Enemy/Balloonfish.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 110.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 0.02f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f2, "exeWait.f2", 0.1f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeStarPointerBind_setBckRate_1, "exeStarPointerBind.setBckRate.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_endStarPointerBind_setBckRate_1, "endStarPointerBind.setBckRate.1", 1.0f);

namespace {
    static const s32 hWaitTime = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(s32, hAttackBeginTime, 82);
    NEBULA_TUNABLE(f32, hTargetPlayerVelocityMult, 10.0f);
    NEBULA_TUNABLE(f32, hWaitMaxScale, 1.5f);
    static const s32 hDashTime = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(f32, hDashVel, 10.0f);
    NEBULA_TUNABLE(f32, hDashScale, 0.5f);
    NEBULA_TUNABLE(s32, hDashEndTime, 95);
    NEBULA_TUNABLE(f32, hRotateAngle, 0.0f);
};  // namespace

namespace NrvBalloonfish {
    NEW_NERVE(HostTypeNrvWait, Balloonfish, Wait);
    NEW_NERVE(HostTypeNrvDash, Balloonfish, Dash);
    NEW_NERVE(HostTypeNrvDashEnd, Balloonfish, Dash);
    NEW_NERVE_ONEND(HostTypeNrvStarPointerBind, Balloonfish, StarPointerBind, StarPointerBind);
};  // namespace NrvBalloonfish

Balloonfish::Balloonfish(const char* pName)
    : LiveActor(pName), mAnimScaleController(), mNerveBeforeBind(), mQuat(0.0f, 0.0f, 0.0f, 1.0f), mRotateAngle(::hRotateAngle) {
    lumacore::setClass(this, lumacore::kClassId< Balloonfish >);
}

void Balloonfish::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        MR::needStageSwitchReadAppear(this, rIter);
        MR::listenStageSwitchOnAppear(this, MR::Functor(this, &Balloonfish::appear));
    }

    initModelManagerWithAnm("Balloonfish", nullptr, false);
    mQuat.setEuler(mRotation);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);

    f32 scale = mScale.y;
    initBinder(cNum_init_f1 * scale, 0.0f, 0);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "body", 32, cNum_init_f2 * scale, Vec3f(0.0f, cNum_init_f3 * scale, 0.0f));
    initEffectKeeper(0, "Balloonfish");
    initSound(2, false);
    initNerve(GET_NERVE(Balloonfish, HostTypeNrvWait));
    f32 zero = 0.0f;
    MR::initStarPointerTarget(this, cNum_init_f4, Vec3f(zero, cNum_init_f5, zero));
    mAnimScaleController = new AnimScaleController(nullptr);
    MR::onCalcGravity(this);
    MR::initShadowVolumeSphere(this, cNum_init_f6 * mScale.y);
    MR::declareCoin(this, 1);
    MR::invalidateClipping(this);
    makeActorDead();
}

void Balloonfish::appear() {
    LiveActor::appear();
    MR::emitEffect(this, "AppearFromBox");
    MR::startSound(this, "SE_EM_BLNFISH_APPEAR");
}

void Balloonfish::kill() {
    LiveActor::kill();
    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_BLNFISH_EXPLOSION");
}

void Balloonfish::control() {
    mAnimScaleController->updateNerve();
}

void Balloonfish::exeWait() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
        if (mNerveBeforeBind != mSpine->getCurrentNerve()) {
            mNotBoundStep = 0;
            MR::startBck(this, "appearance");
            MR::startSound(this, "SE_EM_BLNFISH_PRE_DASH");
        }

        mNerveBeforeBind = nullptr;
    }

    Vec3f toPredictedPlayerPos(*MR::getPlayerCenterPos());
    toPredictedPlayerPos += *MR::getPlayerVelocity() * ::hTargetPlayerVelocityMult;
    toPredictedPlayerPos -= mPosition;

    f32 ratio = static_cast< f32 >(mNotBoundStep) / ::hWaitTime;
    f32 scale = (1.0f - ratio) + ratio * ::hWaitMaxScale;
    mScale.set(scale, scale, scale);
    MR::blendQuatFrontUp(&mQuat, -mGravity, toPredictedPlayerPos, cNum_exeWait_f1, cNum_exeWait_f2);

    if (mNotBoundStep > ::hWaitTime) {
        setNerve(GET_NERVE(Balloonfish, HostTypeNrvDash));
        return;
    }

    // "weak"
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        mNerveBeforeBind = mSpine->getCurrentNerve();
        setNerve(GET_NERVE(Balloonfish, HostTypeNrvStarPointerBind));
        return;
    }

    mNotBoundStep++;
}

void Balloonfish::exeDash() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(Balloonfish, HostTypeNrvDash))) {
            if (mNerveBeforeBind != mSpine->getCurrentNerve()) {
                mNotBoundStep = 0;
                MR::startBck(this, "Attack");
                MR::emitEffect(this, "SwimBubble");
            }
        } else {
            if (mNerveBeforeBind != mSpine->getCurrentNerve()) {
                mNotBoundStep = 0;
                MR::startBck(this, "AttackEnd");
                MR::emitEffect(this, "SwimBubble");
            }
        }

        Vec3f zDir;
        mQuat.getZDir(zDir);
        mVelocity.set(zDir * ::hDashVel);

        mNerveBeforeBind = nullptr;
    }

    MR::startLevelSound(this, "SE_EM_LV_BLNFISH_DASH");

    s32 nerveMaxDuration;
    if (isNerve(GET_NERVE(Balloonfish, HostTypeNrvDash))) {
        nerveMaxDuration = ::hDashTime;
        f32 ratio = static_cast< f32 >(mNotBoundStep) / nerveMaxDuration;
        mScale.set(::hWaitMaxScale * (1.0f - ratio) + ::hDashScale * ratio);
    } else {
        nerveMaxDuration = ::hDashEndTime;
    }

    if (nerveMaxDuration < mNotBoundStep) {
        if (isNerve(GET_NERVE(Balloonfish, HostTypeNrvDash))) {
            setNerve(GET_NERVE(Balloonfish, HostTypeNrvDashEnd));
            return;
        } else {
            kill();
            return;
        }
    }

    if (!MR::isInWater(getSensor("body")->mPosition)) {
        kill();
        return;
    }

    // "weak"
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        mNerveBeforeBind = mSpine->getCurrentNerve();
        setNerve(GET_NERVE(Balloonfish, HostTypeNrvStarPointerBind));
        return;
    }

    mNotBoundStep++;
}

void Balloonfish::exeStarPointerBind() {
    if (MR::isFirstStep(this)) {
        MR::startPointerHitSound();
        MR::setBckRate(this, cNum_exeStarPointerBind_setBckRate_1);
        mAnimScaleController->startPointerHitVibration();
        MR::emitEffect(this, "Touch");
    }

    MR::startPointerFreezeLevelSound(this);
    mVelocity.zero();

    // "weak"
    if (!MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(mNerveBeforeBind);
    }
}

void Balloonfish::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if ((isNerve(GET_NERVE(Balloonfish, HostTypeNrvWait)) && MR::isLessStep(this, ::hAttackBeginTime)) ||
        isNerve(GET_NERVE(Balloonfish, HostTypeNrvStarPointerBind))) {
        MR::sendMsgPush(pReceiver, pSender);
        return;
    }

    if (!MR::sendMsgEnemyAttackMaximum(pReceiver, pSender)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool Balloonfish::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgStarPieceReflect(msg)) {
        if (!isNerve(GET_NERVE(Balloonfish, HostTypeNrvStarPointerBind))) {
            mAnimScaleController->startHitReaction();
        }
        return true;
    }

    if (MR::isMsgJetTurtleAttack(msg)) {
        kill();
        return true;
    }

    return false;
}

void Balloonfish::calcAndSetBaseMtx() {
    Pos3f mtx;
    mtx.makeQuat(mQuat);

    Pos3f spinMtx;
    spinMtx.makeRotate(Vec3f(0.0f, 0.0f, 1.0f), mRotateAngle);
    mtx.concat(mtx, spinMtx);

    mtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, mtx);

    Vec3f scale(mAnimScaleController->mScale * mScale);
    MR::setBaseScale(this, scale);
}

void Balloonfish::endStarPointerBind() {
    MR::setBckRate(this, cNum_endStarPointerBind_setBckRate_1);
    mAnimScaleController->startAnim();
    MR::deleteEffect(this, "Touch");
}
