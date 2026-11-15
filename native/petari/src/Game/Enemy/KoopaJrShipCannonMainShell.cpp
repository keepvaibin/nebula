#include "Game/Enemy/KoopaJrShipCannonMainShell.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 cBaseScale = 1.2f;
    const f32 cAttackSensorRadius = 75.0f;
    const f32 cShadowRadius = 60.0f;
    const f32 cBinderRadius = 75.0f;
    const s32 cLifeTime = NebulaFrameClock::stepsOfSeconds(5);
    const f32 cCameraShakeDistance = 1000.0f;
    // Wall hits are ignored this long after launch (the shell leaves the barrel).
    const s32 cWallHitInvalidTime = NebulaFrameClock::stepsOfSeconds(1);
}  // namespace

namespace NrvKoopaJrShipCannonMainShell {
    NEW_NERVE(HostTypeFly, KoopaJrShipCannonMainShell, Fly);
}  // namespace NrvKoopaJrShipCannonMainShell

KoopaJrShipCannonMainShell::KoopaJrShipCannonMainShell(const char* pName) : CannonShellBase(pName) {
    lumacore::setClass(this, lumacore::kClassId< KoopaJrShipCannonMainShell >);
    mRotateQuat.set< f32 >(0.0f, 0.0f, 0.0f, 1.0f);
}

void KoopaJrShipCannonMainShell::init(const TableIter& /*rIter*/) {
    mScale.setAll< f32 >(getBaseScale());

    initModelManagerWithAnm("KoopaJrShipCannonMainShell", nullptr, false);

    MR::startBtk(this, "KoopaJrShipCannonMainShell");

    initSound(4, false);
    initEffectKeeper(0, "KoopaJrShipCannonShell");
    initHitSensor(1);

    MR::addHitSensorEnemyAttack(this, "attack", 8, ::cAttackSensorRadius * getBaseScale(), Vec3f(0.0f, 0.0f, 0.0f));

    initBinder(::cBinderRadius * getBaseScale(), 0.0f, 0);
    initNerve(GET_NERVE(KoopaJrShipCannonMainShell, HostTypeFly));

    MR::connectToSceneEnemy(this);
    MR::invalidateClipping(this);
    MR::initShadowVolumeSphere(this, ::cShadowRadius * getBaseScale());
    MR::offCalcGravity(this);
    makeActorDead();
}

void KoopaJrShipCannonMainShell::kill() {
    mVelocity.zero();
    MR::deleteEffect(this, "LocusSmoke");
    MR::deleteEffect(this, "LocusFire");
    LiveActor::kill();
}

void KoopaJrShipCannonMainShell::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mRotateQuat);
}

void KoopaJrShipCannonMainShell::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemyAttack(pSender)) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver) && MR::sendMsgEnemyAttackFireStrong(pReceiver, pSender)) {
        explosion();
        return;
    }

    // Other enemies take the blast once a flight has run a step (step -1 is the frame of launch); the shell carries on.
    if (MR::isSensorEnemy(pReceiver) && isNerve(GET_NERVE(KoopaJrShipCannonMainShell, HostTypeFly)) && MR::isGreaterEqualStep(this, 0)) {
        MR::sendMsgEnemyAttackExplosion(pReceiver, pSender);
    }
}

bool KoopaJrShipCannonMainShell::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgJetTurtleAttack(msg)) {
        MR::sendMsgEnemyAttack(pReceiver, pSender);
        return true;
    }

    return MR::isMsgStarPieceReflect(msg);
}

void KoopaJrShipCannonMainShell::launch(const Vec3f& rStartPos, const Vec3f& rVelocity) {
    appear();

    mPosition.set(rStartPos);
    Vec3f dir(rVelocity);
    MR::normalize(&dir);

    Pos3f mtx;
    MR::makeMtxFrontUp(&mtx, dir, -mGravity);
    mtx.getQuat(mRotateQuat);
    mVelocity.set(rVelocity);
    setNerve(GET_NERVE(KoopaJrShipCannonMainShell, HostTypeFly));
}

f32 KoopaJrShipCannonMainShell::getBaseScale() const {
    return ::cBaseScale;
}

void KoopaJrShipCannonMainShell::explosion() {
    MR::startRumbleWithShakeCameraWeak(this, "強", "中", ::cCameraShakeDistance, ::cCameraShakeDistance * 2);
    MR::emitEffect(this, "Explosion");
    MR::startSound(this, "SE_BM_KOOPAJR_SHIP_BREAK_FIRE");
    kill();
}

void KoopaJrShipCannonMainShell::exeFly() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "LocusSmoke");
        MR::emitEffect(this, "LocusFire");
    }

    MR::startLevelSound(this, "SE_BM_LV_KOOPAJR_SHIP_FLY_FIRE");

    if (MR::isGreaterStep(this, ::cWallHitInvalidTime) && MR::isBindedWall(this)) {
        explosion();
    } else if (MR::isStep(this, ::cLifeTime)) {
        kill();
    }
}

namespace CannonShellUtil {
    void registerKoopaJrShipCannonMainShell(CannonShellHolder* pHolder, int shellNum) {
        for (s32 i = 0; i < shellNum; i++) {
            KoopaJrShipCannonMainShell* pShell = new KoopaJrShipCannonMainShell("メイン砲弾");
            pShell->initWithoutIter();
            pShell->makeActorDead();
            pHolder->registerCannonShell(pShell);
        }
    }
}  // namespace CannonShellUtil
