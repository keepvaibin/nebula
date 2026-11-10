#include "Game/Enemy/Metbo.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/SpinHitController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_startSwoon_f1, "startSwoon.f1", 180.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_startSwoon_turnDirectionToPlayerDegree_1, "startSwoon.turnDirectionToPlayerDegree.1", 180.0f);

namespace {
    NEBULA_TUNABLE(f32, cSpeedMax, 30.0f);
    NEBULA_TUNABLE(f32, cReceivePushPower, 1.5f);
    const s32 cStepForWait = NebulaFrameClock::stepsOfMilliseconds(2500);
    NEBULA_TUNABLE(s32, cStepForWalkAround, 200);
    const s32 cStepForChaseMin = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(s32, cStepForChaseMax, 220);
    const s32 cStepForSwoonWait = NebulaFrameClock::stepsOfMilliseconds(2500);
    const s32 cStepForRest = NebulaFrameClock::stepsOfSeconds(1);
    // Beyond this from the player Metbo goes inactive; within it it wakes.
    NEBULA_TUNABLE(f32, cActiveRadius, 3000.0f);
    NEBULA_TUNABLE(f32, cWalkAroundRadius, 300.0f);
    NEBULA_TUNABLE(f32, cWalkAroundTurn, 20.0f);
    // Every this many steps the walk direction turns by cWalkAroundTurn either way.
    NEBULA_TUNABLE(s32, cWalkAroundTurnInterval, 10);
    // One chance in this many per step of a chase ending early.
    NEBULA_TUNABLE(s32, cChaseGiveUpChance, 50);
    NEBULA_TUNABLE(f32, cFindRadiusIn, 800.0f);
    NEBULA_TUNABLE(f32, cFindRadiusOut, 1200.0f);
    NEBULA_TUNABLE(s32, cSpinHitStop, 3);
    NEBULA_TUNABLE(f32, cSpinPower, 45.0f);
    // Horizontal share of the swoon launch before it is normalised with the up vector.
    NEBULA_TUNABLE(f32, cSpinPowerHRate, 0.4f);
    // Ledge probe: distance ahead, drop height, ground search depth.
    NEBULA_TUNABLE(f32, cFallCheckAhead, 200.0f);
    NEBULA_TUNABLE(f32, cFallCheckHeight, 100.0f);
    NEBULA_TUNABLE(f32, cFallCheckDepth, 300.0f);
    NEBULA_TUNABLE(f32, cBaseMtxBlendRate, 0.3f);
    NEBULA_TUNABLE(f32, cBodyRadius, 100.0f);
    NEBULA_TUNABLE(f32, cAttackRadius, 80.0f);
    NEBULA_TUNABLE(f32, cShadowRadius, 80.0f);
    NEBULA_TUNABLE(s32, cGroupClippingNum, 32);
    NEBULA_TUNABLE(s32, cStarPieceNum, 3);

    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cFallParam, {0.0f, 3.0f, 0.99f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cDontMoveParam, {0.0f, 2.0f, 0.5f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cSkateMoveParam, {0.0f, 2.0f, 0.98f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cWalkAroundParam, {0.25f, 2.0f, 0.92f, 2.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cSearchParam, {0.0f, 2.0f, 0.5f, 5.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cChaseParam, {0.8f, 2.0f, 0.9f, 2.0f});

    bool isFallNext(const Metbo* pMetbo, const Vec3f& rDir) {
        return MR::isFallNextMove(pMetbo->mPosition, rDir, pMetbo->mGravity, ::cFallCheckAhead, ::cFallCheckHeight, ::cFallCheckDepth, nullptr);
    }
}  // namespace

namespace NrvMetbo {
    NEW_NERVE_ONEND(MetboNrvNonActive, Metbo, NonActive, NonActive);
    NEW_NERVE(MetboNrvWait, Metbo, Wait);
    NEW_NERVE(MetboNrvWalkAround, Metbo, WalkAround);
    NEW_NERVE(MetboNrvSearch, Metbo, Search);
    NEW_NERVE(MetboNrvChaseStart, Metbo, ChaseStart);
    NEW_NERVE_ONEND(MetboNrvChase, Metbo, Chase, Chase);
    NEW_NERVE(MetboNrvGuard, Metbo, Guard);
    NEW_NERVE(MetboNrvTrampleDown, Metbo, TrampleDown);
    NEW_NERVE(MetboNrvBlowDown, Metbo, BlowDown);
    NEW_NERVE(MetboNrvAttack, Metbo, Attack);
    NEW_NERVE(MetboNrvRest, Metbo, Rest);
    NEW_NERVE(MetboNrvSwoonStart, Metbo, SwoonStart);
    NEW_NERVE(MetboNrvSwoonLand, Metbo, SwoonLand);
    NEW_NERVE(MetboNrvSwoonSpin, Metbo, SwoonSpin);
    NEW_NERVE(MetboNrvSwoonWait, Metbo, SwoonWait);
    NEW_NERVE(MetboNrvRecover, Metbo, Recover);
    NEW_NERVE_ONEND(MetboNrvPointerSwoon, Metbo, PointerSwoon, PointerSwoon);
};  // namespace NrvMetbo

Metbo::Metbo(const char* pName)
    : LiveActor(pName), mSpinHitController(nullptr), mAnimScaleController(nullptr), mStarPointerBind(nullptr), mWalkCenter(0.0f, 0.0f, 0.0f),
      mFront(0.0f, 0.0f, 1.0f), mWalkDir(0.0f, 0.0f, 0.0f), mIsClipped(false) {
    lumacore::setClass(this, lumacore::kClassId< Metbo >);
}

void Metbo::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("Metbo", nullptr, false);
    MR::connectToSceneEnemy(this);
    initHitSensor(2);
    MR::addHitSensorEnemy(this, "body", 8, ::cBodyRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemyAttack(this, "attack", 8, ::cAttackRadius, Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(::cBodyRadius, ::cBodyRadius, 0);
    MR::initLightCtrl(this);
    initEffectKeeper(2, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, false);
    MR::setSeVersion(this, MR::isEqualStageName("KoopaBattleVs2Galaxy") ? SeVersion_2D : SeVersion_NORMAL);
    MR::initShadowVolumeSphere(this, ::cShadowRadius);

    s32 clippingGroupId;
    mIsClipped = MR::getPlacementClippingGroupID(rIter, &clippingGroupId);
    MR::setGroupClipping(this, rIter, ::cGroupClippingNum);
    MR::useStageSwitchReadA(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);
    MR::useStageSwitchSleep(this, rIter);
    MR::onCalcGravity(this);
    MR::declareCoin(this, 1);
    MR::declareStarPiece(this, ::cStarPieceNum);
    MR::initStarPointerTarget(this, ::cBodyRadius);
    mAnimScaleController = new AnimScaleController(nullptr);
    mAnimScaleController->setParamTight();
    mStarPointerBind = new WalkerStateBindStarPointer(this, mAnimScaleController);
    MR::calcFrontVec(&mFront, this);
    mSpinHitController = new SpinHitController(this, 20, 20, 4, cNum_init_f1, cNum_init_f2, cNum_init_f3, 3, false);
    mWalkCenter.set(mPosition);
    initNerve(GET_NERVE(Metbo, MetboNrvWait));
    makeActorAppeared();
}

void Metbo::exeNonActive() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
        if (!mIsClipped) {
            MR::offBind(this);
            MR::offCalcShadow(this, nullptr);
            MR::offCalcGravity(this);
            MR::offCalcAnim(this);
        }
        MR::invalidateHitSensors(this);
    }

    if (mIsClipped) {
        moveOrFall(::cDontMoveParam, nullptr);
    }

    if (MR::isNearPlayerAnyTime(this, ::cActiveRadius)) {
        setNerve(GET_NERVE(Metbo, MetboNrvWait));
    }
}

void Metbo::endNonActive() {
    MR::onBind(this);
    MR::onCalcShadow(this, nullptr);
    MR::onCalcAnim(this);
    MR::onCalcGravity(this);
    MR::validateHitSensors(this);
}

void Metbo::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    moveOrFall(::cDontMoveParam, nullptr);
    if ((!MR::isValidSwitchA(this) || MR::isOnSwitchA(this)) && !trySearch() && !tryNonActive() && MR::isStep(this, ::cStepForWait)) {
        setNerve(GET_NERVE(Metbo, MetboNrvWalkAround));
    }
}

void Metbo::exeWalkAround() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Walk");
    }

    if (!MR::isNear(this, mWalkCenter, ::cWalkAroundRadius) || MR::isBindedWallOfMoveLimit(this) || ::isFallNext(this, mFront)) {
        mWalkDir.sub(mWalkCenter, mPosition);
    } else if (getNerveStep() % ::cWalkAroundTurnInterval == 0) {
        Vec3f upVec;
        MR::calcUpVec(&upVec, this);
        const f32 sign = MR::getRandom(0, 2) ? -1.0f : 1.0f;
        MR::rotateVecDegree(&mWalkDir, upVec, ::cWalkAroundTurn * sign);
    }

    Vec3f target;
    target.add(mWalkDir, mPosition);
    moveOrFall(::cWalkAroundParam, &target);
    if (!trySearch() && MR::isStep(this, ::cStepForWalkAround)) {
        if (MR::getRandom(0, 2)) {
            setNerve(GET_NERVE(Metbo, MetboNrvWait));
        } else {
            setNerve(GET_NERVE(Metbo, MetboNrvWalkAround));
        }
    }
}

void Metbo::exeSearch() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Search");
        MR::startSoundSeVer(this, "SE_EM_METBO_FIND");
    }

    moveOrFall(::cSearchParam, nullptr);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Metbo, MetboNrvChaseStart));
    }
}

void Metbo::exeChaseStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "RunStart");
    }

    if (MR::isBindedGround(this)) {
        MR::startLevelSoundSeVer(this, "SE_EM_LV_KURIBO_DASH1");
    }

    moveOrFall(::cDontMoveParam, nullptr);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Metbo, MetboNrvChase));
    }
}

void Metbo::exeChase() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Run");
    }

    if (MR::isBindedGround(this)) {
        MR::startLevelSoundSeVer(this, "SE_EM_LV_KURIBO_DASH1");
    }

    moveOrFall(::cChaseParam, nullptr);
    if (::isFallNext(this, mFront)) {
        setNerve(GET_NERVE(Metbo, MetboNrvRest));
        return;
    }

    if (MR::isLessStep(this, ::cStepForChaseMin)) {
        return;
    }

    // The random draw happens only when every earlier test fails.
    if (!MR::isNearPlayer(this, ::cFindRadiusOut) || MR::isBindedWallOfMoveLimit(this) || MR::isStep(this, ::cStepForChaseMax) ||
        !MR::getRandom(0, ::cChaseGiveUpChance)) {
        setNerve(GET_NERVE(Metbo, MetboNrvRest));
    }
}

void Metbo::endChase() {
    mWalkCenter.set(mPosition);
}

void Metbo::exeGuard() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Guard");
    }

    moveOrFall(::cDontMoveParam, nullptr);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Metbo, MetboNrvWait));
    }
}

void Metbo::exeTrampleDown() {
    if (MR::isFirstStep(this)) {
        MR::startSoundSeVer(this, "SE_EM_STOMPED_S");
        MR::startBck(this, "Press");
        MR::invalidateHitSensors(this);
    }

    moveOrFall(::cDontMoveParam, nullptr);
    if (MR::isBckStopped(this)) {
        MR::appearCoinPop(this, mPosition, 1);
        kill();
    }
}

void Metbo::exeBlowDown() {
    if (MR::isFirstStep(this)) {
        MR::startBlowHitSound(this);
        MR::startBck(this, "Blow");
        MR::invalidateHitSensors(this);
    }
    mSpinHitController->execute(this);
}

void Metbo::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Attack");
    }

    moveOrFall(::cDontMoveParam, nullptr);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Metbo, MetboNrvRest));
    }
}

void Metbo::exeRest() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    moveOrFall(::cDontMoveParam, nullptr);
    if (MR::isStep(this, ::cStepForRest)) {
        setNerve(GET_NERVE(Metbo, MetboNrvWait));
    }
}

void Metbo::exeSwoonStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SwoonStart");
        MR::startSoundSeVer(this, "SE_EM_CRASH_S");
    } else {
        MR::moveAndTurnToPlayer(this, &mFront, ::cFallParam);
        if (MR::isBindedGround(this)) {
            setNerve(GET_NERVE(Metbo, MetboNrvSwoonLand));
        }
    }
}

void Metbo::exeSwoonLand() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SwoonLand");
    }

    MR::startLevelSoundSeVer(this, "SE_EM_LV_METBO_STAGGER");
    moveOrFall(::cSkateMoveParam, nullptr);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Metbo, MetboNrvSwoonSpin));
    }
}

void Metbo::exeSwoonSpin() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SwoonSpin");
    }

    MR::startLevelSoundSeVer(this, "SE_EM_LV_METBO_STAGGER");
    moveOrFall(::cSkateMoveParam, nullptr);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Metbo, MetboNrvSwoonWait));
    }
}

void Metbo::exeSwoonWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SwoonWait");
    }

    MR::startLevelSoundSeVer(this, "SE_EM_LV_METBO_SWOON");
    moveOrFall(::cDontMoveParam, nullptr);
    if (MR::isStep(this, ::cStepForSwoonWait)) {
        setNerve(GET_NERVE(Metbo, MetboNrvRecover));
    }
}

void Metbo::exeRecover() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Recover");
        MR::startSoundSeVer(this, "SE_EM_METBO_SWOON_RECOVER");
    }

    moveOrFall(::cDontMoveParam, nullptr);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Metbo, MetboNrvWait));
    }
}

void Metbo::exePointerSwoon() {
    MR::updateActorStateAndNextNerve(this, mStarPointerBind, GET_NERVE(Metbo, MetboNrvWait));
}

void Metbo::endPointerSwoon() {
    mStarPointerBind->kill();
}

void Metbo::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void Metbo::kill() {
    MR::startSoundSeVer(this, "SE_EM_EXPLODE_S");
    MR::emitEffect(this, "Death");

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
    LiveActor::kill();
}

void Metbo::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, mFront);
    MR::blendMtx(getBaseMtx(), mtx, ::cBaseMtxBlendRate, mtx);
    MR::setBaseTRMtx(this, mtx);
    Vec3f scale = mAnimScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void Metbo::control() {
    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f)) || MR::isInDarkMatter(mPosition) || MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        kill();
        return;
    }

    if (!isNerve(GET_NERVE(Metbo, MetboNrvSwoonStart)) && !isNrvDown()) {
        MR::restrictVelocity(this, ::cSpeedMax);
    }
    mAnimScaleController->updateNerve();
    tryPointerSwoon();
}

void Metbo::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNrvDown()) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver) && MR::isSensorEnemyAttack(pSender)) {
        if (isNrvNormal() && MR::sendMsgEnemyAttack(pReceiver, pSender)) {
            MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);
            setNerve(GET_NERVE(Metbo, MetboNrvAttack));
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }

    if (!MR::isSensorPlayer(pReceiver) && MR::isSensorEnemy(pSender)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool Metbo::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (isNrvDown()) {
        return false;
    }

    if (!MR::isSensorEnemy(pSender) && !MR::isSensorMapObj(pSender)) {
        return false;
    }

    Vec3f away;
    away.sub(mPosition, pSender->mHost->mPosition);
    MR::normalizeOrZero(&away);
    mVelocity.scaleAdd(::cReceivePushPower, away, mVelocity);
    return true;
}

bool Metbo::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceReflect(msg)) {
        MR::startSoundSeVer(this, "SE_EM_METBO_TRAMPLE");
        if (!isNrvSwoon() && !isNerve(GET_NERVE(Metbo, MetboNrvRecover))) {
            setNerve(GET_NERVE(Metbo, MetboNrvGuard));
        }
        return true;
    }

    if (isNrvDown()) {
        return false;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        MR::startSoundSeVer(this, "SE_EM_METBO_TRAMPLE");
        if (isNerve(GET_NERVE(Metbo, MetboNrvRecover)) || isNerve(GET_NERVE(Metbo, MetboNrvSwoonStart))) {
            return false;
        }

        if (isNrvSwoon()) {
            setNerve(GET_NERVE(Metbo, MetboNrvTrampleDown));
        } else {
            setNerve(GET_NERVE(Metbo, MetboNrvGuard));
            MR::tryRumbleDefaultHit(this, 0);
            MR::sendMsgEnemyAttackCounterHipDrop(pSender, pReceiver);
        }
        return true;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        MR::startSoundSeVer(this, "SE_EM_METBO_TRAMPLE");
        if (isNerve(GET_NERVE(Metbo, MetboNrvRecover))) {
            return true;
        }

        if (isNerve(GET_NERVE(Metbo, MetboNrvSwoonStart))) {
            return false;
        }

        MR::tryRumbleDefaultHit(this, 0);
        if (isNrvSwoon()) {
            setNerve(GET_NERVE(Metbo, MetboNrvTrampleDown));
        } else {
            setNerve(GET_NERVE(Metbo, MetboNrvGuard));
        }
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        if (isNerve(GET_NERVE(Metbo, MetboNrvSwoonStart))) {
            return false;
        }

        MR::stopScene(::cSpinHitStop);
        MR::startSoundSeVer(this, "SE_EM_METBO_SPIN_HIT");
        MR::startBlowHitSound(this);
        startSwoon(false);
        return true;
    }

    if (MR::isMsgInvincibleAttack(msg)) {
        mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(Metbo, MetboNrvBlowDown));
        return true;
    }

    return false;
}

bool Metbo::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNrvDown() || !MR::isMsgExplosionAttack(msg)) {
        return false;
    }

    mSpinHitController->startWithoutStopScene(this, pSender->mPosition, pReceiver->mPosition);
    setNerve(GET_NERVE(Metbo, MetboNrvBlowDown));
    return true;
}

bool Metbo::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgInhaleBlackHole(msg)) {
        kill();
        return true;
    }

    return false;
}

// On the ground moves with rParam toward pTarget (the player when null); in the air falls with cFallParam.
void Metbo::moveOrFall(const MR::ActorMoveParam& rParam, const Vec3f* pTarget) {
    const Vec3f target = pTarget != nullptr ? *pTarget : *MR::getPlayerPos();
    MR::moveAndTurnToTarget(this, &mFront, target, MR::isBindedGround(this) ? rParam : ::cFallParam);
}

void Metbo::startSwoon(bool isFromPlayer) {
    Vec3f launch;
    launch.sub(mPosition, *MR::getPlayerPos());
    launch.setLength(::cSpinPowerHRate);
    launch.add(-mGravity);
    MR::normalize(&launch);
    mVelocity.scale(::cSpinPower, launch);

    if (isFromPlayer) {
        MR::turnDirectionFromPlayerDegree(this, &mFront, cNum_startSwoon_f1);
    } else {
        MR::turnDirectionToPlayerDegree(this, &mFront, cNum_startSwoon_turnDirectionToPlayerDegree_1);
    }
    setNerve(GET_NERVE(Metbo, MetboNrvSwoonStart));
}

bool Metbo::tryNonActive() {
    if (!MR::isNearPlayerAnyTime(this, ::cActiveRadius) && MR::isBindedGround(this)) {
        setNerve(GET_NERVE(Metbo, MetboNrvNonActive));
        return true;
    }
    return false;
}

// Starts the search when the player is near, in sight and reachable without falling.
bool Metbo::trySearch() {
    if (!MR::isNearPlayer(this, ::cFindRadiusIn)) {
        return false;
    }

    const Vec3f& rSensorPos = getSensor("body")->mPosition;
    Vec3f toPlayer;
    toPlayer.sub(*MR::getPlayerCenterPos(), rSensorPos);
    if (MR::isExistMapCollision(rSensorPos, toPlayer) || MR::isExistMoveLimitCollision(rSensorPos, toPlayer)) {
        return false;
    }

    Vec3f dir;
    dir.sub(*MR::getPlayerPos(), mPosition);
    MR::normalize(&dir);
    if (::isFallNext(this, dir)) {
        return false;
    }

    setNerve(GET_NERVE(Metbo, MetboNrvSearch));
    return true;
}

bool Metbo::tryPointerSwoon() {
    if (isNerve(GET_NERVE(Metbo, MetboNrvPointerSwoon)) || isNerve(GET_NERVE(Metbo, MetboNrvRecover)) || isNrvSwoon() || isNrvDown()) {
        return false;
    }

    if (!mStarPointerBind->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(Metbo, MetboNrvPointerSwoon));
    return true;
}

bool Metbo::isNrvNormal() const {
    return isNerve(GET_NERVE(Metbo, MetboNrvWait)) || isNerve(GET_NERVE(Metbo, MetboNrvWalkAround)) || isNerve(GET_NERVE(Metbo, MetboNrvSearch)) ||
           isNerve(GET_NERVE(Metbo, MetboNrvChaseStart)) || isNerve(GET_NERVE(Metbo, MetboNrvChase));
}

bool Metbo::isNrvDown() const {
    return isNerve(GET_NERVE(Metbo, MetboNrvTrampleDown)) || isNerve(GET_NERVE(Metbo, MetboNrvBlowDown));
}

bool Metbo::isNrvSwoon() const {
    return isNerve(GET_NERVE(Metbo, MetboNrvSwoonStart)) || isNerve(GET_NERVE(Metbo, MetboNrvSwoonLand)) ||
           isNerve(GET_NERVE(Metbo, MetboNrvSwoonSpin)) || isNerve(GET_NERVE(Metbo, MetboNrvSwoonWait));
}
