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
#include "Game/System/LumaCore.hpp"

namespace {
    static Color8 sPointLightColor(255, 255, 255, 255);

    // Placement arg 0.
    enum AppearanceType {
        APPEAR_FROM_GROUND = 0,
        APPEAR_FROM_WALL = 1,
    };

    const f32 sShadowRadius = 80.0f;
    const f32 sChaseDistance = 1800.0f;
    const f32 sLoveDistance = 1500.0f;
    const f32 sShyDistance = 1500.0f;
    const f32 sWalkEndDistance = 200.0f;
    const f32 sFloatHeight = 150.0f;
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
    lumacore::setClass(this, lumacore::kClassId< Teresa >);
    mWalkRange = 600.0f;
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
        MR::setShadowDropLength(this, nullptr, 3000.0f);
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
    MR::addHitSensorEnemyAttack(this, "attack", 8, 70.0f * scale, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemy(this, "body", 8, 85.0f * scale, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initStarPointerTarget(this, 85.0f * scale, Vec3f(0.0f, 0.0f, 0.0f));
}

void Teresa::initBind() {
    const f32 scale = mScale.x;
    initBinder(60.0f * scale, 0.0f, 0);
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
        MR::blendQuatUpFront(pQuat, -rGravity, rFront, 0.1f, 0.2f);
    }
};  // namespace

void Teresa::control() {
    if (!isNerve(GET_NERVE(Teresa, TeresaNrvDrift)) && !isNerve(GET_NERVE(Teresa, TeresaNrvAscension))) {
        mAnimScale.x = 1.0f + (0.95f * (mAnimScale.x - 1.0f));
        mAnimScale.z = 1.0f / mAnimScale.x;
    }

    tryHideWater();
    ::blendTeresaQuat(&mPoseQuat, mGravity, mWallNormal);

    if (mTransparency > 0.9f) {
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
        mVelocity.add(away * 0.1f);
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
        MR::addVelocitySeparateHV(this, pSender, pReceiver, 20.0f, 0.0f);
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
    if (MR::isGreaterStep(this, 192)) {
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
    if (MR::isGreaterStep(this, 75)) {
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
    if (MR::isGreaterStep(this, 50)) {
        setNerve(GET_NERVE(Teresa, TeresaNrvWait));
        return true;
    }

    return false;
}

bool Teresa::tryHideWater() {
    Vec3f footPos;
    footPos.scaleAdd(70.0f * mScale.y, mGravity, mPosition);

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
    if (MR::isInAreaObj("GlaringLightArea", mPosition) || !MR::isInShadeFromTheSun(mPosition, 10000.0f)) {
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
    push *= 0.2f;
    mVelocity.add(push);
    MR::attenuateVelocity(this, 0.9f);
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

    MR::turnDirectionToTarget(this, &mWallNormal, mWalkTarget, 0.9995f);
    MR::addVelocityMoveToDirection(this, mWallNormal, 0.2f);
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

    if (toRail.length() < 10.0f) {
        MR::moveCoord(this, 10.0f);
    }

    MR::turnDirectionToTarget(this, &mWallNormal, MR::getRailPos(this), 0.9995f);
    MR::addVelocityMoveToTarget(this, MR::getRailPos(this), 0.35f);
    updateNormalTransparency();
    updateNormalVelocity();

    if (!tryAscension() && !tryShay() && !tryRailTurn()) {
        tryLoveFind();
    }
}

void Teresa::exeRailTurn() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::turnDirection(this, &mWallNormal, MR::getRailDirection(this), 0.9997f);
    }

    MR::addVelocityMoveToTarget(this, MR::getRailPos(this), 0.1f);
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

    MR::turnDirectionToTarget(this, &mWallNormal, *MR::getPlayerPos(), 0.98f);
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

    MR::turnDirectionToTarget(this, &mWallNormal, *MR::getPlayerPos(), 0.999f);
    MR::addVelocityMoveToDirection(this, mWallNormal, (0.75f * MR::calcNerveRate(this, 20)));
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
    MR::turnDirectionToTarget(this, &mWallNormal, *MR::getPlayerPos(), 0.999f);
    MR::addVelocityMoveToDirection(this, mWallNormal, (0.75f * MR::calcNerveRate(this, 20)));
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
        setTransparency(MR::calcNerveEaseInOutValue(this, NebulaFrameClock::stepsOfSeconds(1), mTransparency, 0.5f));
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

    MR::turnDirectionToTarget(this, &mWallNormal, *MR::getPlayerPos(), 0.99f);
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

    MR::turnDirectionToTarget(this, &mWallNormal, *MR::getPlayerPos(), 0.96f);
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
    MR::addVelocityKeepHeightUseShadow(this, sFloatHeight + (85.0f * (mScale.y - 1.0f)), 0.40f, 40.0f, nullptr);
    MR::reboundVelocityFromCollision(this, -0.2f, 4.0f, 1.0f);
    MR::attenuateVelocity(this, 0.9f);
}

void Teresa::addDriftVelocity() {
    if (MR::isLessEqualStep(this, 35)) {
        Vec3f pull;
        MR::calcPlayerSpinPullVelocity(&pull, mPosition);
        pull *= 0.12f;
        mVelocity.add(pull);
    }

    MR::addVelocityAwayFromTarget(this, *MR::getPlayerPos(), -1.25f);
}

void Teresa::updateDriftAnimScale() {
    // Stretches along its side by the sideways speed.
    Vec3f side;
    mPoseQuat.getXDir(side);
    f32 sideSpeed = side.dot(mVelocity);
    f32 stretch = 1.0f + (0.30f * MR::normalize(MR::abs(sideSpeed), 3.0f, 53.0f));
    mAnimScale.x = stretch;
    mAnimScale.z = 1.0f / stretch;
}

void Teresa::updateDriftTransparency() {
    // Fades to see-through and back over the drift: 1 - (4r(r-1))^2.
    f32 rate = MR::calcNerveRate(this, 50);
    f32 bump = (4.0f * rate) * (rate - 1.0f);
    f32 target = 1.0f - (bump * bump);
    setTransparency(MR::calcNerveValue(this, 25, mTransparency, target));
    MR::setShadowVolumeSphereRadius(this, nullptr, sShadowRadius * target);
}

void Teresa::updateNormalTransparency() {
    if (mTransparency < 1.0f) {
        mTransparency += 0.02f;

        if (mTransparency > 1.0f) {
            mTransparency = 1.0f;
        }
    }

    setTransparency(mTransparency);
}

void Teresa::setTransparency(f32 val) {
    MR::setBrkFrameAndStop(this, (9.0f * (1.0f - val)));

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

