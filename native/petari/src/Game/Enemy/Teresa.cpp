#include "Game/Enemy/Teresa.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Map/SunshadeMapHolder.hpp"
#include "Game/MapObj/DummyDisplayModel.hpp"
#include "Game/MapObj/KeySwitch.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/TriangleFilter.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_Teresa_f1, "Teresa.f1", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 85.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f3, "initSensor.f3", 85.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initBind_f1, "initBind.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_blendTeresaQuat_f1, "blendTeresaQuat.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_blendTeresaQuat_f2, "blendTeresaQuat.f2", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPush_f1, "receiveMsgPush.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryHideWater_f1, "tryHideWater.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryAscension_f1, "tryAscension.f1", 10000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateAppear_f1, "updateAppear.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalk_f1, "exeWalk.f1", 0.9995f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalk_f2, "exeWalk.f2", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRailWalk_f1, "exeRailWalk.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRailWalk_f2, "exeRailWalk.f2", 0.9995f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRailWalk_f3, "exeRailWalk.f3", 0.35f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRailTurn_f1, "exeRailTurn.f1", 0.9997f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRailTurn_f2, "exeRailTurn.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLoveFind_f1, "exeLoveFind.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLoveChase_f1, "exeLoveChase.f1", 0.999f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLoveChase_f2, "exeLoveChase.f2", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeChase_f1, "exeChase.f1", 0.999f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeChase_f2, "exeChase.f2", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShay_f1, "exeShay.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackSuccess_f1, "exeAttackSuccess.f1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f1, "exeDrift.f1", 0.96f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_f1, "updateNormalVelocity.f1", 85.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_f2, "updateNormalVelocity.f2", 0.40f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_f3, "updateNormalVelocity.f3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_addDriftVelocity_f1, "addDriftVelocity.f1", 0.12f);
NEBULA_TUNABLE_NAMED(f32, cNum_addDriftVelocity_f2, "addDriftVelocity.f2", 1.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftAnimScale_f1, "updateDriftAnimScale.f1", 0.30f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftAnimScale_f2, "updateDriftAnimScale.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftAnimScale_f3, "updateDriftAnimScale.f3", 53.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftTransparency_f1, "updateDriftTransparency.f1", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalTransparency_f1, "updateNormalTransparency.f1", 0.02f);
NEBULA_TUNABLE_NAMED(f32, cNum_setTransparency_f1, "setTransparency.f1", 9.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_requestLoveHit_addVelocitySeparateHV_1, "requestLoveHit.addVelocitySeparateHV.1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestLoveHit_addVelocitySeparateHV_2, "requestLoveHit.addVelocitySeparateHV.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateAppear_attenuateVelocity_1, "updateAppear.attenuateVelocity.1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRailWalk_moveCoord_1, "exeRailWalk.moveCoord.1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromCollision_1, "updateNormalVelocity.reboundVelocityFromCollision.1", -0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromCollision_2, "updateNormalVelocity.reboundVelocityFromCollision.2", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromCollision_3, "updateNormalVelocity.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_attenuateVelocity_1, "updateNormalVelocity.attenuateVelocity.1", 0.9f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_tryWalk_1, "tryWalk.1", 192);
NEBULA_TUNABLE_NAMED(s32, cStep_tryAggressiveEnd_1, "tryAggressiveEnd.1", 75);
NEBULA_TUNABLE_NAMED(s32, cStep_tryDriftEnd_1, "tryDriftEnd.1", 50);
NEBULA_TUNABLE_NAMED(s32, cStep_addDriftVelocity_1, "addDriftVelocity.1", 35);

namespace {
    static Color8 sPointLightColor(255, 255, 255, 255);

    // Placement arg 0.
    enum AppearanceType {
        APPEAR_FROM_GROUND = 0,
        APPEAR_FROM_WALL = 1,
    };

    NEBULA_TUNABLE(f32, sShadowRadius, 80.0f);
    NEBULA_TUNABLE(f32, sChaseDistance, 1800.0f);
    NEBULA_TUNABLE(f32, sLoveDistance, 1500.0f);
    NEBULA_TUNABLE(f32, sShyDistance, 1500.0f);
    NEBULA_TUNABLE(f32, sWalkEndDistance, 200.0f);
    NEBULA_TUNABLE(f32, sFloatHeight, 150.0f);
};  // namespace

namespace NrvTeresa {
    NEW_NERVE_ONEND(TeresaNrvAppearFromWall, Teresa, AppearFromWall, AppearFromWall);
    NEW_NERVE_ONEND(TeresaNrvAppearFromGround, Teresa, AppearFromGround, AppearFromGround);
    NEW_NERVE(TeresaNrvWait, Teresa, Wait);
    NEW_NERVE(TeresaNrvWalk, Teresa, Walk);
    NEW_NERVE(TeresaNrvRailWalk, Teresa, RailWalk);
    NEW_NERVE(TeresaNrvRailTurn, Teresa, RailTurn);
    NEW_NERVE(TeresaNrvLoveFind, Teresa, LoveFind);
    NEW_NERVE(TeresaNrvLoveChase, Teresa, LoveChase);
    NEW_NERVE(TeresaNrvLoveHit, Teresa, LoveHit);
    NEW_NERVE(TeresaNrvLoveEnd, Teresa, LoveEnd);
    NEW_NERVE(TeresaNrvChase, Teresa, Chase);
    NEW_NERVE(TeresaNrvShay, Teresa, Shay);
    NEW_NERVE(TeresaNrvAggressive, Teresa, Aggressive);
    NEW_NERVE(TeresaNrvAttackSuccess, Teresa, AttackSuccess);
    NEW_NERVE_ONEND(TeresaNrvDrift, Teresa, Drift, Drift);
    NEW_NERVE(TeresaNrvHideWall, Teresa, HideWall);
    NEW_NERVE(TeresaNrvHideWater, Teresa, HideWater);
    NEW_NERVE(TeresaNrvAscension, Teresa, Ascension);
    NEW_NERVE(TeresaNrvStop, Teresa, Stop);
};  // namespace NrvTeresa

Teresa::Teresa(const char* pName)
    : LiveActor(pName), mDisplayModel(), mKeySwitch(), mPoseQuat(0.0f, 0.0f, 0.0f, 1.0f), mWallNormal(0, 0, 1), mWallHitPos(0, 0, 0),
      mInitPos(0, 0, 0), mAnimScale(1, 1, 1), mWalkTarget(0, 0, 0) {
    mWalkRange = cNum_Teresa_f1;
    mTransparency = 1.0f;
    mAppearanceType = -1;
    mHasCoin = true;
    mIsChief = false;
}

void Teresa::init(const TableIter& rIter) {
    initFromPlacementParam(rIter);
    mInitPos.set(mPosition);
    mWallHitPos.set(mPosition);

    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        MR::moveCoordAndTransToNearestRailPos(this);
    }

    initModelManagerWithAnm("Teresa", nullptr, false);
    MR::connectToSceneIndirectEnemy(this);
    initDummyModel(rIter);
    initSound(4, false);
    initSensor();
    MR::initShadowVolumeSphere(this, sShadowRadius);
    MR::onShadowFollowHostScale(this, nullptr);
    initBind();
    initEffectKeeper(0, nullptr);
    initNerve(GET_NERVE(Teresa, TeresaNrvWait));
    setTransparency(1.0f);
    MR::useStageSwitchWriteDead(this, rIter);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
    }

    if (MR::isObjectName(rIter, "TeresaChief")) {
        MR::declarePowerStar(this);
        mHasCoin = false;
        mIsChief = true;
        MR::invalidateClipping(this);
        MR::setShadowDropLength(this, nullptr, cNum_init_f1);
    }

    if (mHasCoin) {
        MR::declareCoin(this, 1);
    }

    if (MR::isValidInfo(rIter) && !MR::isExistStageSwitchAppear(rIter)) {
        makeActorAppeared();
    } else {
        makeActorDead();
    }

    setStartNerve();
}

void Teresa::initDummyModel(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        mDisplayModel = MR::createDummyDisplayModel(this, rIter, "SklRoot", -1);
    }

    if (mDisplayModel != nullptr) {
        mDisplayModel->kill();

        if (MR::getDummyDisplayModelId(rIter, -1) == DummyDisplayModelId_KeySwitch) {
            mKeySwitch = new KeySwitch("鍵スイッチ");
            mKeySwitch->initKeySwitchByOwner(rIter);
            mHasCoin = false;
        }

        if (MR::isBckExist(mDisplayModel, "InTeresa")) {
            MR::startBck(mDisplayModel, "InTeresa");
        }
    }
}

void Teresa::initFromPlacementParam(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        MR::makeQuatAndFrontFromRotate(&mPoseQuat, &mWallNormal, this);
        MR::getPlacementArg0WithInit(rIter, &mAppearanceType);
    }
}

void Teresa::initSensor() {
    f32 scale = mScale.x;
    initHitSensor(2);
    MR::addHitSensorEnemyAttack(this, "attack", 8, cNum_initSensor_f1 * scale, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemy(this, "body", 8, cNum_initSensor_f2 * scale, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initStarPointerTarget(this, cNum_initSensor_f3 * scale, Vec3f(0.0f, 0.0f, 0.0f));
}

void Teresa::initBind() {
    const f32 scale = mScale.x;
    initBinder(cNum_initBind_f1 * scale, 0.0f, 0);
    MR::setBindTriangleFilter(this, MR::createTriangleFilterDelegator(this, &Teresa::filterBind));
}

void Teresa::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    MR::onCalcGravity(this);
}

void Teresa::appear() {
    LiveActor::appear();
    setTransparency(0.0f);

    if (mAppearanceType != APPEAR_FROM_GROUND && mAppearanceType != APPEAR_FROM_WALL) {
        setStartNerve();
        return;
    }

    MR::invalidateClipping(this);
    MR::onBind(this);
    mWallHitPos.set(mPosition);

    if (mAppearanceType == APPEAR_FROM_GROUND) {
        setNerve(GET_NERVE(Teresa, TeresaNrvAppearFromGround));
    } else {
        setNerve(GET_NERVE(Teresa, TeresaNrvAppearFromWall));
    }
}

void Teresa::kill() {
    LiveActor::kill();
    MR::emitEffect(this, "Death");

    if (mKeySwitch != nullptr) {
        mKeySwitch->appearKeySwitch(mPosition);
    }

    if (mIsChief) {
        MR::startAfterBossBGM();
        MR::requestAppearPowerStar(this, mPosition);
    }

    if (mHasCoin) {
        MR::appearCoinPop(this, mPosition, 1);
    }

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

namespace {
    inline void blendTeresaQuat(Quat4f* pQuat, const Vec3f& rGravity, const Vec3f& rFront) {
        MR::blendQuatUpFront(pQuat, -rGravity, rFront, cNum_blendTeresaQuat_f1, cNum_blendTeresaQuat_f2);
    }
};  // namespace

void Teresa::control() {
    if (!isNerve(GET_NERVE(Teresa, TeresaNrvDrift)) && !isNerve(GET_NERVE(Teresa, TeresaNrvAscension))) {
        mAnimScale.x = 1.0f + (cNum_control_f1 * (mAnimScale.x - 1.0f));
        mAnimScale.z = 1.0f / mAnimScale.x;
    }

    tryHideWater();
    ::blendTeresaQuat(&mPoseQuat, mGravity, mWallNormal);

    if (mTransparency > cNum_control_f2) {
        MR::requestPointLight(this, mPosition, ::sPointLightColor, 1.0f, -1);
    }
}

void Teresa::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);
    Vec3f scale = mAnimScale * mScale;
    MR::setBaseScale(this, scale);
}

void Teresa::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        requestAttack(pSender, pReceiver);
    } else if (MR::isSensorEnemy(pSender)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool Teresa::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemy(pReceiver)) {
        return false;
    }

    if (MR::isSensorEnemy(pSender)) {
        Vec3f away = MR::getSensorPos(pReceiver) - MR::getSensorPos(pSender);
        MR::normalizeOrZero(&away);
        mVelocity.add(away * cNum_receiveMsgPush_f1);
        return true;
    }

    return false;
}

bool Teresa::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgSearchlightAttack(msg) || MR::isMsgFireBallAttack(msg) || MR::isMsgInvincibleAttack(msg)) {
        return requestSearchLightDead();
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return isEnableStarPieceStop();
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        if (isEnableStarPieceStop()) {
            setNerve(GET_NERVE(Teresa, TeresaNrvStop));
        }
    }

    return false;
}

bool Teresa::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgSpinStormRange(msg)) {
        return requestDrift();
    }

    if (msg == ACTMES_TERESA_PLAYER_TOUCH) {
        return requestLoveHit(pSender, pReceiver);
    }

    return false;
}

bool Teresa::filterBind(const Triangle* pTriangle) {
    if (pTriangle->mSensor->isType(ATYPE_CLIP_FIELD_MAP_PARTS)) {
        return true;
    }

    if (MR::isWallCodeGhostThrough(pTriangle)) {
        return isNerve(GET_NERVE(Teresa, TeresaNrvDrift));
    }

    return false;
}

bool Teresa::requestAttack(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemyAttack(pSender) || !MR::isSensorPlayer(pReceiver)) {
        return false;
    }

    if (!canAttack()) {
        return false;
    }

    if (MR::sendMsgEnemyAttack(pReceiver, pSender)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvAttackSuccess));
        return true;
    }

    return false;
}

bool Teresa::requestDrift() {
    if (canDrift()) {
        setNerve(GET_NERVE(Teresa, TeresaNrvDrift));
        return true;
    }

    return false;
}

bool Teresa::requestLoveHit(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(Teresa, TeresaNrvLoveChase))) {
        MR::addVelocitySeparateHV(this, pSender, pReceiver, cNum_requestLoveHit_addVelocitySeparateHV_1, cNum_requestLoveHit_addVelocitySeparateHV_2);
        setNerve(GET_NERVE(Teresa, TeresaNrvLoveHit));
        return true;
    }

    return false;
}

bool Teresa::requestSearchLightDead() {
    if (canSearchLightDead()) {
        setNerve(GET_NERVE(Teresa, TeresaNrvAscension));
        return true;
    }

    return false;
}

void Teresa::setStartNerve() {
    if (MR::isExistRail(this)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvRailWalk));
        setTransparency(1.0f);
        MR::startAction(this, "Wait");
        MR::moveCoordToNearestPos(this, mPosition);
    } else {
        setNerve(GET_NERVE(Teresa, TeresaNrvWait));
    }
}

bool Teresa::tryAppearEnd() {
    if (MR::isBckStopped(this)) {
        setStartNerve();
        return true;
    }

    return false;
}

bool Teresa::tryRailTurn() {
    if (MR::isRailReachedGoal(this)) {
        MR::reverseRailDirection(this);
        setNerve(GET_NERVE(Teresa, TeresaNrvRailTurn));
        return true;
    }

    return false;
}

bool Teresa::tryRailTurnEnd() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(2500))) {
        setNerve(GET_NERVE(Teresa, TeresaNrvRailWalk));
        return true;
    }

    return false;
}

bool Teresa::tryWalk() {
    if (MR::isGreaterStep(this, cStep_tryWalk_1)) {
        Vec3f offset;
        MR::getRandomVector(&offset, 1.0f);
        MR::normalizeOrZero(&offset);
        offset.orthogonalize(mGravity);
        offset *= mWalkRange;
        mWalkTarget = offset + mInitPos;
        setNerve(GET_NERVE(Teresa, TeresaNrvWalk));
        return true;
    }

    return false;
}

bool Teresa::tryWalkEnd() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(5)) || mWalkTarget.distance(mPosition) <= sWalkEndDistance) {
        setNerve(GET_NERVE(Teresa, TeresaNrvWait));
        return true;
    }

    return false;
}

bool Teresa::tryChase() {
    if (MR::isPlayerElementModeTeresa()) {
        return false;
    }

    if (!isShay() && MR::isNearPlayer(this, sChaseDistance)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvChase));
        return true;
    }

    return false;
}

bool Teresa::tryChaseEnd() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(4)) || MR::isPlayerElementModeTeresa() || isNotNear()) {
        setStartNerve();
        return true;
    }

    return false;
}

bool Teresa::tryShay() {
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvShay));
        return true;
    }

    if (MR::isPlayerElementModeTeresa()) {
        return false;
    }

    if (isShay()) {
        setNerve(GET_NERVE(Teresa, TeresaNrvShay));
        return true;
    }

    return false;
}

bool Teresa::tryShayEnd() {
    if (MR::isStarPointerPointing2POnPressButton(this, nullptr, false, false)) {
        return false;
    }

    if (MR::isPlayerElementModeTeresa()) {
        setStartNerve();
        return true;
    }

    if (isShay()) {
        return false;
    }

    if (MR::isExistRail(this)) {
        setStartNerve();
    } else {
        setNerve(GET_NERVE(Teresa, TeresaNrvAggressive));
    }

    return true;
}

bool Teresa::tryLoveFind() {
    if (MR::isPlayerElementModeTeresa() && MR::isNearPlayerAnyTime(this, sLoveDistance)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvLoveFind));
        return true;
    }

    return false;
}

bool Teresa::tryLoveEnd() {
    if (!MR::isPlayerElementModeTeresa() && MR::isNearPlayerAnyTime(this, sLoveDistance)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvLoveEnd));
        return true;
    }

    return false;
}

bool Teresa::tryLoveFindEnd() {
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvLoveChase));
        return true;
    }

    return false;
}

bool Teresa::tryLoveChaseEnd() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(4))) {
        setStartNerve();
        return true;
    }

    return false;
}

bool Teresa::tryLoveHitEnd() {
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvLoveChase));
        return true;
    }

    return false;
}

bool Teresa::tryAggressiveEnd() {
    if (MR::isGreaterStep(this, cStep_tryAggressiveEnd_1)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvWait));
        return true;
    }

    return false;
}

bool Teresa::tryAttackSuccessEnd() {
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvWait));
        return true;
    }

    return false;
}

bool Teresa::tryDriftEnd() {
    if (MR::isGreaterStep(this, cStep_tryDriftEnd_1)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvWait));
        return true;
    }

    return false;
}

bool Teresa::tryHideWater() {
    Vec3f footPos;
    footPos.scaleAdd(cNum_tryHideWater_f1 * mScale.y, mGravity, mPosition);

    if (isCheckWater() && MR::isInWater(footPos)) {
        Pos3f effectMtx;
        MR::makeMtxFrontNoSupportPos(&effectMtx, -mGravity, footPos);
        MR::emitEffectHit(this, effectMtx, "HideWall");
        MR::invalidateClipping(this);
        MR::invalidateHitSensors(this);
        MR::offBind(this);
        MR::hideModel(this);
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(Teresa, TeresaNrvHideWater));
        return true;
    }

    return false;
}

bool Teresa::tryHideWall() {
    if (MR::isBindedWall(this)) {
        mWallNormal.set(*MR::getWallNormal(this));
        mWallHitPos.set(*MR::getWallHitPos(this));
        Pos3f frontMtx;
        MR::makeMtxFrontNoSupportPos(&frontMtx, mWallNormal, mWallHitPos);
        MR::emitEffectHit(this, frontMtx, "HideWall");
        MR::invalidateClipping(this);
        MR::invalidateHitSensors(this);
        MR::offBind(this);
        MR::hideModel(this);
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(Teresa, TeresaNrvHideWall));
        return true;
    }

    return false;
}

bool Teresa::tryHideWallEnd() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::validateHitSensors(this);
        MR::onBind(this);
        MR::showModel(this);
        setNerve(GET_NERVE(Teresa, TeresaNrvAppearFromWall));
        return true;
    }

    return false;
}

bool Teresa::tryAscension() {
    if (MR::isInAreaObj("GlaringLightArea", mPosition) || !MR::isInShadeFromTheSun(mPosition, cNum_tryAscension_f1)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvAscension));
        return true;
    }

    return false;
}

void Teresa::endAppear() {
    if (!mIsChief) {
        MR::validateClipping(this);
    }

    MR::startBrk(this, "Erase");
    setTransparency(1.0f);
}

void Teresa::endDrift() {
    MR::forceDeleteEffect(this, "SpinBlur");
    setTransparency(1.0f);
    MR::setShadowVolumeSphereRadius(this, nullptr, sShadowRadius);
}

// Comes out along rDir from mWallHitPos while the shadow eases to full size over 60 steps.
void Teresa::updateAppear(const Vec3f& rDir, const char* pAction) {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::startAction(this, pAction);
        MR::zeroVelocity(this);
        Pos3f frontMtx;
        MR::makeMtxFrontNoSupportPos(&frontMtx, rDir, mWallHitPos);
        MR::emitEffectHit(this, frontMtx, "Appear");
        MR::startSound(this, "SE_EM_TERESA_APPEAR");
    }

    Vec3f push(rDir);
    push *= cNum_updateAppear_f1;
    mVelocity.add(push);
    MR::attenuateVelocity(this, cNum_updateAppear_attenuateVelocity_1);
    f32 rate = MR::calcNerveRate(this, NebulaFrameClock::stepsOfSeconds(1));
    MR::setShadowVolumeSphereRadius(this, nullptr, (sShadowRadius * (1.0f - (1.0f - rate) * (1.0f - rate))));
    tryAppearEnd();
}

void Teresa::exeAppearFromWall() {
    Vec3f front;
    mPoseQuat.getZDir(front);
    updateAppear(front, "AppearWall");
}

void Teresa::exeAppearFromGround() {
    Vec3f up;
    mPoseQuat.getYDir(up);
    updateAppear(up, "AppearGround");
}

void Teresa::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
    }

    MR::startLevelSound(this, "SE_EM_LV_TERESA_MOVE");
    updateNormalTransparency();
    updateNormalVelocity();

    if (!tryAscension() && !tryShay() && !tryChase() && !tryWalk()) {
        tryLoveFind();
    }
}

void Teresa::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::startBtp(this, "Normal");
    }

    MR::turnDirectionToTarget(this, &mWallNormal, mWalkTarget, cNum_exeWalk_f1);
    MR::addVelocityMoveToDirection(this, mWallNormal, cNum_exeWalk_f2);
    updateNormalTransparency();
    updateNormalVelocity();

    if (!tryAscension() && !tryShay() && !tryWalkEnd()) {
        tryLoveFind();
    }
}

void Teresa::exeRailWalk() {
    // Steps the rail point ahead once level with it.
    Vec3f toRail;
    toRail.killElement(MR::getRailPos(this) - mPosition, mGravity);

    if (toRail.length() < cNum_exeRailWalk_f1) {
        MR::moveCoord(this, cNum_exeRailWalk_moveCoord_1);
    }

    MR::turnDirectionToTarget(this, &mWallNormal, MR::getRailPos(this), cNum_exeRailWalk_f2);
    MR::addVelocityMoveToTarget(this, MR::getRailPos(this), cNum_exeRailWalk_f3);
    updateNormalTransparency();
    updateNormalVelocity();

    if (!tryAscension() && !tryShay() && !tryRailTurn()) {
        tryLoveFind();
    }
}

void Teresa::exeRailTurn() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::turnDirection(this, &mWallNormal, MR::getRailDirection(this), cNum_exeRailTurn_f1);
    }

    MR::addVelocityMoveToTarget(this, MR::getRailPos(this), cNum_exeRailTurn_f2);
    updateNormalTransparency();
    updateNormalVelocity();

    if (!tryAscension() && !tryShay() && !tryRailTurnEnd()) {
        tryLoveFind();
    }
}

void Teresa::exeLoveFind() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "LoveFind");
        MR::startSound(this, "SE_EM_TERESA_LOVE_FIND");
        MR::startSound(this, "SE_EV_TERESA_LAUGH");
    }

    MR::turnDirectionToTarget(this, &mWallNormal, *MR::getPlayerPos(), cNum_exeLoveFind_f1);
    updateNormalTransparency();
    updateNormalVelocity();

    if (!tryAscension() && !tryShay() && !tryLoveFindEnd()) {
        tryLoveEnd();
    }
}

void Teresa::exeLoveChase() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "LoveChase");
    }

    MR::turnDirectionToTarget(this, &mWallNormal, *MR::getPlayerPos(), cNum_exeLoveChase_f1);
    MR::addVelocityMoveToDirection(this, mWallNormal, (cNum_exeLoveChase_f2 * MR::calcNerveRate(this, 20)));
    updateNormalTransparency();
    updateNormalVelocity();

    if (!tryAscension() && !tryShay() && !tryLoveEnd()) {
        tryLoveChaseEnd();
    }
}

void Teresa::exeLoveHit() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "LoveHit");
        MR::startSound(this, "SE_EM_TERESA_LOVE_HIT");
        MR::startSound(this, "SE_EV_TERESA_LOVE_HIT");
    }

    updateNormalTransparency();
    updateNormalVelocity();

    if (!tryAscension() && !tryShay()) {
        tryLoveHitEnd();
    }
}

void Teresa::exeLoveEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "LoveEnd");
    }

    updateNormalTransparency();
    updateNormalVelocity();

    if (!tryAscension() && !tryShay()) {
        tryAggressiveEnd();
    }
}

void Teresa::exeChase() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Chase");
        MR::startSound(this, "SE_EV_TERESA_LAUGH");
    }

    MR::startLevelSound(this, "SE_EM_LV_TERESA_MOVE");
    MR::turnDirectionToTarget(this, &mWallNormal, *MR::getPlayerPos(), cNum_exeChase_f1);
    MR::addVelocityMoveToDirection(this, mWallNormal, (cNum_exeChase_f2 * MR::calcNerveRate(this, 20)));
    updateNormalTransparency();
    updateNormalVelocity();

    if (!tryAscension() && !tryShay()) {
        tryChaseEnd();
    }
}

void Teresa::exeShay() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Shay");
        MR::startSound(this, "SE_EM_TERESA_STOP");
        MR::zeroVelocity(this);
    }

    if (mIsChief) {
        MR::startLevelSound(this, "SE_OJ_LV_POW_STAR_EXIST");
    }

    if (mDisplayModel != nullptr) {
        setTransparency(MR::calcNerveEaseInOutValue(this, NebulaFrameClock::stepsOfSeconds(1), mTransparency, cNum_exeShay_f1));
    } else {
        updateNormalTransparency();
    }

    updateNormalVelocity();

    if (!tryAscension()) {
        tryShayEnd();
    }
}

void Teresa::exeAggressive() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Aggressive");
    }

    MR::startLevelSound(this, "SE_EM_LV_TERESA_MOVE");
    updateNormalTransparency();
    updateNormalVelocity();

    if (!tryAscension() && !tryShay()) {
        tryAggressiveEnd();
    }
}

void Teresa::exeAttackSuccess() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Laugh");
        MR::setShadowVolumeSphereRadius(this, nullptr, sShadowRadius);
        MR::startSound(this, "SE_EV_TERESA_ATTACK_SUCCESS");
    }

    MR::turnDirectionToTarget(this, &mWallNormal, *MR::getPlayerPos(), cNum_exeAttackSuccess_f1);
    updateNormalTransparency();
    updateNormalVelocity();

    if (!tryAscension()) {
        tryAttackSuccessEnd();
    }
}

void Teresa::exeDrift() {
    if (MR::isFirstStep(this)) {
        MR::forceDeleteEffectAll(this);
        MR::startAction(this, "Drift");
        MR::emitEffect(this, "SpinBlur");
        mAnimScale.set(1.0f, 1.0f, 1.0f);
        MR::startSound(this, "SE_EM_TERESA_TORNADE");

        if (mDisplayModel != nullptr) {
            mDisplayModel->appear();
        }
    }

    MR::turnDirectionToTarget(this, &mWallNormal, *MR::getPlayerPos(), cNum_exeDrift_f1);
    addDriftVelocity();
    updateNormalVelocity();
    updateDriftAnimScale();
    updateDriftTransparency();

    if (!tryAscension() && !tryHideWall()) {
        tryDriftEnd();
    }
}

void Teresa::exeHideWall() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Hide");
        MR::setShadowVolumeSphereRadius(this, nullptr, 0.0f);
    }

    tryHideWallEnd();
}

void Teresa::exeHideWater() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Hide");
        MR::setShadowVolumeSphereRadius(this, nullptr, 0.0f);
        mWallHitPos.set(mInitPos);
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::validateHitSensors(this);
        MR::onBind(this);
        MR::showModel(this);
        setNerve(GET_NERVE(Teresa, TeresaNrvAppearFromGround));
    }
}

void Teresa::exeAscension() {
    if (MR::isFirstStep(this)) {
        mAnimScale.set(1.0f, 1.0f, 1.0f);
        MR::setBaseScale(this, mScale);
        MR::startSound(this, "SE_EM_TERESA_DEAD");
        MR::startSound(this, "SE_EV_TERESA_DEAD");
        kill();
    }
}

void Teresa::exeStop() {
    if (MR::isFirstStep(this)) {
        MR::zeroVelocity(this);
        MR::startAction(this, "Wait");
        setTransparency(0.0f);
    }

    if (!tryAscension() && MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        setStartNerve();
    }
}

void Teresa::endAppearFromGround() {
    endAppear();
}

void Teresa::endAppearFromWall() {
    endAppear();
}

void Teresa::updateNormalVelocity() {
    MR::addVelocityKeepHeightUseShadow(this, sFloatHeight + (cNum_updateNormalVelocity_f1 * (mScale.y - 1.0f)), cNum_updateNormalVelocity_f2, cNum_updateNormalVelocity_f3, nullptr);
    MR::reboundVelocityFromCollision(this, cNum_updateNormalVelocity_reboundVelocityFromCollision_1, cNum_updateNormalVelocity_reboundVelocityFromCollision_2, cNum_updateNormalVelocity_reboundVelocityFromCollision_3);
    MR::attenuateVelocity(this, cNum_updateNormalVelocity_attenuateVelocity_1);
}

void Teresa::addDriftVelocity() {
    if (MR::isLessEqualStep(this, cStep_addDriftVelocity_1)) {
        Vec3f pull;
        MR::calcPlayerSpinPullVelocity(&pull, mPosition);
        pull *= cNum_addDriftVelocity_f1;
        mVelocity.add(pull);
    }

    MR::addVelocityAwayFromTarget(this, *MR::getPlayerPos(), -cNum_addDriftVelocity_f2);
}

void Teresa::updateDriftAnimScale() {
    // Stretches along its side by the sideways speed.
    Vec3f side;
    mPoseQuat.getXDir(side);
    f32 sideSpeed = side.dot(mVelocity);
    f32 stretch = 1.0f + (cNum_updateDriftAnimScale_f1 * MR::normalize(MR::abs(sideSpeed), cNum_updateDriftAnimScale_f2, cNum_updateDriftAnimScale_f3));
    mAnimScale.x = stretch;
    mAnimScale.z = 1.0f / stretch;
}

void Teresa::updateDriftTransparency() {
    // Fades to see-through and back over the drift: 1 - (4r(r-1))^2.
    f32 rate = MR::calcNerveRate(this, 50);
    f32 bump = (cNum_updateDriftTransparency_f1 * rate) * (rate - 1.0f);
    f32 target = 1.0f - (bump * bump);
    setTransparency(MR::calcNerveValue(this, 25, mTransparency, target));
    MR::setShadowVolumeSphereRadius(this, nullptr, sShadowRadius * target);
}

void Teresa::updateNormalTransparency() {
    if (mTransparency < 1.0f) {
        mTransparency += cNum_updateNormalTransparency_f1;

        if (mTransparency > 1.0f) {
            mTransparency = 1.0f;
        }
    }

    setTransparency(mTransparency);
}

void Teresa::setTransparency(f32 val) {
    MR::setBrkFrameAndStop(this, (cNum_setTransparency_f1 * (1.0f - val)));

    // The dummy model inside shows through only while see-through.
    if (mDisplayModel != nullptr) {
        if (val < 1.0f) {
            mDisplayModel->appear();
        } else {
            mDisplayModel->kill();
        }
    }

    mTransparency = val;
}

bool Teresa::isAppearing() const {
    return isNerve(GET_NERVE(Teresa, TeresaNrvAppearFromWall)) || isNerve(GET_NERVE(Teresa, TeresaNrvAppearFromGround));
}

bool Teresa::isHidden() const {
    return isNerve(GET_NERVE(Teresa, TeresaNrvHideWater)) || isNerve(GET_NERVE(Teresa, TeresaNrvHideWall));
}

bool Teresa::canAttack() const {
    return !(isAppearing() || isNerve(GET_NERVE(Teresa, TeresaNrvAttackSuccess)) || isNerve(GET_NERVE(Teresa, TeresaNrvDrift)) ||
             isNerve(GET_NERVE(Teresa, TeresaNrvAscension)));
}

bool Teresa::canDrift() const {
    return !(isNerve(GET_NERVE(Teresa, TeresaNrvDrift)) || isNerve(GET_NERVE(Teresa, TeresaNrvAscension)));
}

bool Teresa::canSearchLightDead() const {
    return !(isAppearing() || isHidden() || isNerve(GET_NERVE(Teresa, TeresaNrvAscension)));
}

bool Teresa::isEnableStarPieceStop() const {
    return !(isAppearing() || isNerve(GET_NERVE(Teresa, TeresaNrvDrift)) || isHidden() || isNerve(GET_NERVE(Teresa, TeresaNrvAscension)) ||
             isNerve(GET_NERVE(Teresa, TeresaNrvStop)));
}

bool Teresa::isCheckWater() const {
    return canSearchLightDead();
}

namespace {
    inline bool isFacingTeresa(const Vec3f& rPosition) {
        Vec3f playerFront;
        MR::getPlayerFrontVec(&playerFront);

        return playerFront.dot(rPosition - *MR::getPlayerPos()) > 0.0f;
    }
};  // namespace

// Shy while the player stands facing it close by.
bool Teresa::isShay() const {
    return !MR::isPlayerSquat() && ::isFacingTeresa(mPosition) && MR::isNearPlayer(this, sShyDistance);
}

