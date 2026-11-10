#include "Game/Enemy/KoopaJrShipCannonShell.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeDown_reboundVelocityFromCollision_1, "exeDown.reboundVelocityFromCollision.1", 1.0f);


namespace {
    NEBULA_TUNABLE(f32, cBodySensorRadius, 75.0f);
    NEBULA_TUNABLE(f32, cAttackSensorRadius, 60.0f);
    NEBULA_TUNABLE(f32, cShadowRadius, 60.0f);
    NEBULA_TUNABLE(f32, cBinderRadius, 75.0f);
    NEBULA_TUNABLE(f32, cStarWandRadius, 150.0f);
    const s32 cLifeTime = NebulaFrameClock::stepsOfSeconds(6);
    NEBULA_TUNABLE(f32, cCameraShakeDistance, 1000.0f);
    // Frames a stomped shell rolls before it misfires.
    NEBULA_TUNABLE(s32, cDownTime, 45);
    NEBULA_TUNABLE(f32, cGravity, 1.0f);
    NEBULA_TUNABLE(f32, cReboundRate, 0.55f);
    NEBULA_TUNABLE(f32, cReboundMinSpeed, 2.0f);
    NEBULA_TUNABLE(f32, cAirFriction, 0.99f);
    NEBULA_TUNABLE(f32, cGroundFriction, 0.95f);
    NEBULA_TUNABLE(s32, cFreezeFrame, 20);
    // Degrees of the freeze wobble per frozen frame, and its width in units.
    NEBULA_TUNABLE(f32, cFreezeRumbleSpeed, 75.0f);
    NEBULA_TUNABLE(f32, cFreezeRumbleWidth, 5.0f);
    NEBULA_TUNABLE(s32, cWallHitInvalidTime, 10);
}  // namespace

namespace NrvKoopaJrShipCannonShell {
    NEW_NERVE(HostTypeFly, KoopaJrShipCannonShell, Fly);
    NEW_NERVE(HostTypeDown, KoopaJrShipCannonShell, Down);
    NEW_NERVE(HostTypeHipDropDown, KoopaJrShipCannonShell, Down);
    NEW_NERVE(HostTypeFreeze, KoopaJrShipCannonShell, Freeze);
}  // namespace NrvKoopaJrShipCannonShell

KoopaJrShipCannonShell::KoopaJrShipCannonShell(const char* pName)
    : CannonShellBase(pName), mFreezePos(gZeroVec), mFreezeVelocity(gZeroVec), mFreezeStep() {
    lumacore::setClass(this, lumacore::kClassId< KoopaJrShipCannonShell >);
    mPoseQuat.set< f32 >(0.0f, 0.0f, 0.0f, 1.0f);
    mPosition.set(gZeroVec);
}

void KoopaJrShipCannonShell::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("KoopaJrShipCannonShell", nullptr, false);
    MR::startBck(this, "KoopaJrShipCannonShell");
    initSound(4, false);
    initHitSensor(2);
    MR::addHitSensorEnemy(this, "body", 8, ::cBodySensorRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemyAttack(this, "attack", 8, ::cAttackSensorRadius * getBaseScale(), Vec3f(0.0f, 0.0f, 0.0f));

    initBinder(::cBinderRadius * getBaseScale(), 0.0f, 0);
    initEffectKeeper(0, nullptr);
    initNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeFly));

    MR::connectToSceneEnemy(this);
    MR::invalidateClipping(this);
    MR::initStarPointerTarget(this, ::cStarWandRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initShadowVolumeSphere(this, ::cShadowRadius * getBaseScale());
    MR::offCalcGravity(this);
    MR::declareCoin(this, 1);
    makeActorDead();
}

void KoopaJrShipCannonShell::kill() {
    mVelocity.zero();
    MR::deleteEffect(this, "LocusSmoke");
    LiveActor::kill();
}

void KoopaJrShipCannonShell::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);
}

void KoopaJrShipCannonShell::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemyAttack(pSender)) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver) && MR::sendMsgEnemyAttackExplosion(pReceiver, pSender)) {
        explosion();
        return;
    }

    if (MR::isSensorEnemy(pReceiver) && isStateEnableExplosion() && MR::sendMsgEnemyAttackExplosion(pReceiver, pSender)) {
        explosion();
        return;
    }

    // Rides take the blast; the shell carries on.
    if (MR::isSensorRide(pReceiver)) {
        MR::sendMsgEnemyAttackExplosion(pReceiver, pSender);
    }
}

bool KoopaJrShipCannonShell::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isSensorEnemyAttack(pReceiver)) {
        return false;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        if (isNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeDown))) {
            return false;
        }

        MR::tryRumblePad(this, "中", PLAYER_1);
        setNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeDown));
        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        MR::tryRumblePad(this, "中", PLAYER_1);
        setNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeHipDropDown));
        return true;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        MR::appearCoinPop(this, mPosition, 1);
        explosion();
        return true;
    }

    if (MR::isMsgJetTurtleAttack(msg)) {
        explosion();
        return true;
    }

    return false;
}

bool KoopaJrShipCannonShell::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (!MR::isMsgExplosionAttack(msg) || !MR::isSensorEnemyAttack(pSender) || !isStateEnableExplosion()) {
        return false;
    }

    explosion();
    return true;
}

bool KoopaJrShipCannonShell::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (!MR::isSensorPlayer(pSender) || !MR::isMsgInvalidHit(msg)) {
        return false;
    }

    explosion();
    return true;
}

void KoopaJrShipCannonShell::launch(const Vec3f& rStartPos, const Vec3f& rVelocity) {
    appear();

    mPosition.set(rStartPos);

    Vec3f front(rVelocity);
    MR::normalize(&front);

    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, front);
    mtx.getQuat(mPoseQuat);
    mVelocity.set(rVelocity);
    MR::startSound(this, "SE_BM_KOOPAJR_SHIP_SHOOT_NORMAL");
    setNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeFly));
}

bool KoopaJrShipCannonShell::tryFreeze() {
    if (!MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        return false;
    }

    mFreezePos.set(mPosition);
    mFreezeVelocity.set(mVelocity);
    mFreezeStep = 0;
    setNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeFreeze));
    return true;
}

// A shell on its launch frame (step -1) does not explode on enemies yet.
bool KoopaJrShipCannonShell::isStateEnableExplosion() const {
    return (isNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeFly)) && MR::isGreaterEqualStep(this, 0)) ||
           isNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeFreeze)) || isNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeDown)) ||
           isNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeHipDropDown));
}

void KoopaJrShipCannonShell::explosion() {
    MR::startRumbleWithShakeCameraWeak(this, "強", "中", ::cCameraShakeDistance, ::cCameraShakeDistance * 2);
    MR::emitEffect(this, "Explosion");
    MR::startSound(this, "SE_EM_KILLER_EXPLOSION");
    kill();
}

void KoopaJrShipCannonShell::misfire() {
    MR::shakeCameraWeak();
    MR::tryRumblePad(this, "弱", PLAYER_1);
    MR::emitEffect(this, "MisFire");
    MR::startSound(this, "SE_EM_KILLER_MISS_FIRE");
    kill();
}

void KoopaJrShipCannonShell::exeFly() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "LocusSmoke");
    }
    MR::startLevelSound(this, "SE_EM_LV_KILLER_FLY");

    if (tryFreeze()) {
        return;
    }

    if (MR::isGreaterStep(this, ::cWallHitInvalidTime) && MR::isBinded(this)) {
        explosion();
    } else if (MR::isStep(this, ::cLifeTime)) {
        kill();
    }
}

void KoopaJrShipCannonShell::exeDown() {
    if (MR::isFirstStep(this)) {
        MR::onCalcGravity(this);
        MR::startSound(this, "SE_EM_STOMPED_S");
        if (isNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeHipDropDown))) {
            MR::jumpPlayer(-mGravity);
        }
    }

    mVelocity.add(mGravity * ::cGravity);
    if (MR::reboundVelocityFromCollision(this, ::cReboundRate, ::cReboundMinSpeed, cNum_exeDown_reboundVelocityFromCollision_1)) {
        MR::deleteEffect(this, "LocusSmoke");
    }

    mVelocity.scale(MR::isBindedGround(this) ? ::cGroundFriction : ::cAirFriction);

    if (MR::isStep(this, ::cDownTime)) {
        misfire();
    }
}

// Pointing again restarts the freeze; mFreezeStep keeps counting so the wobble phase and the hit sound (first freeze only) carry over.
void KoopaJrShipCannonShell::exeFreeze() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
        if (mFreezeStep == 0) {
            MR::startPointerHitSound();
        }
    }

    mFreezeStep++;
    MR::startPointerFreezeLevelSound(this);
    f32 width = ::cFreezeRumbleWidth * MR::cosDegree(MR::repeatDegree(mFreezeStep * ::cFreezeRumbleSpeed)) *
                static_cast< f32 >(::cFreezeFrame - getNerveStep()) / ::cFreezeFrame;

    Vec3f offset;
    offset.set(MR::getCamXdir());
    offset.scale(width);
    editPosition()->add(mFreezePos, offset);

    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeFreeze));
        return;
    }

    if (MR::isStep(this, ::cFreezeFrame)) {
        mPosition.set(mFreezePos);
        mVelocity.set(mFreezeVelocity);
        setNerve(GET_NERVE(KoopaJrShipCannonShell, HostTypeFly));
    }
}

namespace CannonShellUtil {
    void registerKoopaJrShipCannonShell(CannonShellHolder* pHolder, int shellNum) {
        for (s32 i = 0; i < shellNum; i++) {
            KoopaJrShipCannonShell* pShell = new KoopaJrShipCannonShell("砲弾");
            pShell->initWithoutIter();
            pShell->makeActorDead();
            pHolder->registerCannonShell(pShell);
        }
    }
}  // namespace CannonShellUtil
