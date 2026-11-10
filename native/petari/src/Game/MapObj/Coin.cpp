#include "Game/MapObj/Coin.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/CoinHolder.hpp"
#include "Game/MapObj/CoinRotater.hpp"
#include "Game/MapObj/PurpleCoinHolder.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRebound_f1, "calcRebound.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attenuateVelocity_f1, "attenuateVelocity.f1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_attenuateVelocity_f2, "attenuateVelocity.f2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_attenuateVelocity_f3, "attenuateVelocity.f3", 0.995f);

namespace {
    // The coin's centre above its position.
    NEBULA_TUNABLE(f32, cCenterHeight, 70.0f);
    NEBULA_TUNABLE(f32, cSensorRadius, 55.0f);
    NEBULA_TUNABLE(f32, cBubbleSensorRadius, 150.0f);
    NEBULA_TUNABLE(f32, cShadowRadius, 50.0f);
    NEBULA_TUNABLE(f32, cClippingShadowMargin, 150.0f);
    // Defaults when a negative life or pickup delay is asked for.
    NEBULA_TUNABLE(s32, cDefaultLife, 600);
    const s32 cDefaultCannotTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, cHopSpeed, 30.0f);
    // A non-active coin activated with gravity pops up this fast.
    NEBULA_TUNABLE(f32, cActivateLaunchSpeed, 30.0f);
    const s32 cHopSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, cWaterGravityScale, 0.3f);
    // Rebound: walls and roofs keep 0.6, the ground 0.75 when it lands at least this fast (sound from 6% of the cap).
    NEBULA_TUNABLE(f32, cWallRebound, 0.6f);
    NEBULA_TUNABLE(f32, cGroundRebound, 0.75f);
    NEBULA_TUNABLE(f32, cReboundFriction, 0.5f);
    NEBULA_TUNABLE(f32, cGroundBounceMinSpeed, 2.0f);
    NEBULA_TUNABLE(f32, cGroundBounceMaxSpeed, 30.0f);
    NEBULA_TUNABLE(s32, cBoundSoundMinVolume, 5);
    NEBULA_TUNABLE(s32, cBubbleOxygen, 8);

    enum ShadowType {
        ShadowType_SurfaceCircle = 0,
        ShadowType_VolumeCylinder = 1,
    };
};  // namespace

namespace NrvCoin {
    NEW_NERVE(CoinNrvNonActive, Coin, NonActive);
    NEW_NERVE(CoinNrvFix, Coin, Fix);
    NEW_NERVE(CoinNrvFixHide, Coin, FixHide);
    NEW_NERVE(CoinNrvFixTimer, Coin, FixTimer);
    NEW_NERVE(CoinNrvControled, Coin, Controled);
    NEW_NERVE(CoinNrvSpinDrained, Coin, SpinDrained);
    NEW_NERVE(CoinNrvMove, Coin, Move);
    NEW_NERVE(CoinNrvHop, Coin, Hop);
};  // namespace NrvCoin

Coin::Coin(const char* pName)
    : LiveActor(pName), mHostInfo(nullptr), mFlashCtrl(nullptr), mAirBubble(nullptr), mDropPosition(0.0f, 0.0f, 0.0f), mClippingRange(0.0f, 0.0f, 0.0f),
      mLife(cDefaultLife), mCannotTime(0), mIsInWater(false), mIsCalcShadowAlways(false), mIsPoseFromRotation(false), mIsPurpleCoin(false),
      mIsNeedBubble(false) {
}

void Coin::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm(mIsPurpleCoin ? "PurpleCoin" : "Coin", nullptr, false);
    MR::connectToSceneItemStrongLight(this);
    initHitSensor(1);
    MR::addHitSensor(this, "coin", ATYPE_COIN, 4, cSensorRadius, Vec3f(0.0f, cCenterHeight, 0.0f));
    initBinder(cSensorRadius, cCenterHeight, 0);
    MR::setBinderExceptSensorType(this, &mPosition, cNum_init_f1);
    MR::setClippingFar100m(this);
    initEffectKeeper(0, nullptr);
    initSound(1, false);

    if (MR::isValidInfo(rIter)) {
        setShadowAndPoseModeFromPlacementIter(rIter);
    }

    if (mIsPoseFromRotation) {
        Vec3f axis;
        MR::calcActorAxisY(&axis, this);
        mGravity.set(-axis);
    }

    initShadow(rIter);

    if (mIsNeedBubble) {
        mAirBubble = MR::createPartsModelNoSilhouettedMapObj(this, "アワ", "AirBubble", nullptr);
        mAirBubble->initFixedPosition(Vec3f(0.0f, cCenterHeight, 0.0f), Vec3f(0.0f, 0.0f, 0.0f), nullptr);
        mAirBubble->makeActorDead();
        MR::registerDemoSimpleCastAll(mAirBubble);
        MR::startBck(mAirBubble, "Move");
    }

    mFlashCtrl = new FlashingCtrl(this, true);
    MR::tryCreateMirrorActor(this, mIsPurpleCoin ? "PurpleCoin" : "Coin");
    initNerve(GET_NERVE(Coin, CoinNrvFix));

    if (MR::isValidInfo(rIter)) {
        if (MR::useStageSwitchReadAppear(this, rIter)) {
            MR::syncStageSwitchAppear(this);
            makeActorDead();
        } else {
            appearFix();
        }

        if (MR::useStageSwitchReadB(this, rIter)) {
            MR::listenStageSwitchOnB(this, MR::Functor(this, &Coin::makeActorDead));
        }
    } else {
        makeActorDead();
    }

    MR::useStageSwitchSleep(this, rIter);
}

void Coin::initAfterPlacement() {
    MR::setClippingRangeIncludeShadow(this, &mClippingRange, cClippingShadowMargin);
}

// Arg 5 is the shadow's drop length, arg 6 its type.
void Coin::initShadow(const TableIter& rIter) {
    f32 dropLength = -1.0f;
    s32 shadowType = -1;

    if (MR::isValidInfo(rIter)) {
        MR::getPlacementArg5NoInit(rIter, &dropLength);
        MR::getPlacementArg6NoInit(rIter, &shadowType);
    }

    switch (shadowType) {
    case ShadowType_SurfaceCircle:
        MR::initShadowSurfaceCircle(this, cShadowRadius);
        break;
    case ShadowType_VolumeCylinder:
        MR::initShadowVolumeCylinder(this, cShadowRadius);
        break;
    default:
        MR::initShadowVolumeSphere(this, cShadowRadius);
        break;
    }

    MR::setShadowDropPositionPtr(this, nullptr, &mDropPosition);

    if (dropLength > 0.0f) {
        MR::setShadowDropLength(this, nullptr, dropLength);
    }
}

// Arg 3 recomputes the shadow every frame, arg 4 poses the coin by its rotation (gravity along its -Y), arg 7 puts it
// in an air bubble.
void Coin::setShadowAndPoseModeFromPlacementIter(const TableIter& rIter) {
    bool isCalcShadowAlways = false;
    MR::getPlacementArg3WithInit(rIter, &isCalcShadowAlways);
    mIsCalcShadowAlways = isCalcShadowAlways;
    bool isPoseFromRotation = false;
    MR::getPlacementArg4WithInit(rIter, &isPoseFromRotation);
    mIsPoseFromRotation = isPoseFromRotation;
    mIsNeedBubble = Coin::isNeedBubble(rIter);
}

// Yellow coins do not show while a dark comet is out.
void Coin::makeActorAppeared() {
    MR::offCalcGravity(this);

    if (!mIsPoseFromRotation) {
        MR::calcGravityOrZero(this);
    }

    if (!mIsPurpleCoin && MR::isGalaxyDarkCometAppearInCurrentStage()) {
        LiveActor::makeActorDead();
        return;
    }

    LiveActor::makeActorAppeared();

    if (mIsNeedBubble) {
        mAirBubble->makeActorAppeared();
        MR::setSensorRadius(this, "coin", cBubbleSensorRadius);
    } else {
        MR::setSensorRadius(this, "coin", cSensorRadius);
    }

    mFlashCtrl->end();
    setCalcShadowMode();
    MR::validateShadow(this, nullptr);

    if (!mIsPurpleCoin) {
        mHostInfo->mAppeared++;
    }

    mIsInWater = MR::isInWater(mPosition);
}

void Coin::makeActorDead() {
    LiveActor::makeActorDead();

    if (mAirBubble != nullptr) {
        mAirBubble->makeActorDead();
    }
}

// Upright against gravity (world up with none), spun by the shared coin rotation for its state.
void Coin::calcAndSetBaseMtx() {
    Vec3f up;
    up.set(MR::isNearZero(mGravity) ? Vec3f(0.0f, 1.0f, 0.0f) : -mGravity);
    mDropPosition.scaleAdd(cCenterHeight, up, mPosition);

    Pos3f pos;

    if (mIsPoseFromRotation) {
        MR::makeMtxTR(pos.toMtxPtr(), this);
    } else {
        MR::makeMtxUpNoSupportPos(&pos, up, mPosition);
    }

    if (isNerve(GET_NERVE(Coin, CoinNrvSpinDrained))) {
        pos.concat(pos, MR::getCoinHiSpeedRotateYMatrix());
    } else if (mIsInWater) {
        pos.concat(pos, MR::getCoinInWaterRotateYMatrix());
    } else {
        pos.concat(pos, MR::getCoinRotateYMatrix());
    }

    MR::setBaseTRMtx(this, pos);
}

bool Coin::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgItemGet(msg)) {
        noticeGetCoin();
        MR::emitEffect(this, "CoinGet");
        mFlashCtrl->end();
        kill();
        return true;
    }

    if (MR::isMsgItemPull(msg)) {
        return requestSpinDrain();
    }

    if (MR::isMsgItemShow(msg)) {
        return requestShow();
    }

    if (MR::isMsgItemHide(msg)) {
        return requestHide();
    }

    if (MR::isMsgItemStartMove(msg)) {
        return requestStartControl();
    }

    if (MR::isMsgItemEndMove(msg)) {
        return requestEndControl();
    }

    if (MR::isMsgInhaleBlackHole(msg)) {
        kill();
        return true;
    }

    return false;
}

void Coin::exeNonActive() {
    if (MR::isFirstStep(this) || mIsCalcShadowAlways) {
        MR::setClippingRangeIncludeShadow(this, &mClippingRange, cClippingShadowMargin);
    }
}

void Coin::exeFix() {
    if (MR::isFirstStep(this) || mIsCalcShadowAlways) {
        MR::setClippingRangeIncludeShadow(this, &mClippingRange, cClippingShadowMargin);
    }
}

void Coin::exeFixHide() {
}

// Stays put, flashing out over its life; it can be picked up after mCannotTime.
void Coin::exeFixTimer() {
    if (MR::isFirstStep(this)) {
        mFlashCtrl->start(mLife);
        MR::invalidateClipping(this);
        MR::offBind(this);
    }

    if (MR::isStep(this, mCannotTime)) {
        MR::validateHitSensors(this);
    }

    if (mFlashCtrl->mIsEnded) {
        kill();
    }
}

// Falls and bounces, flashing out over its life; lava and death areas take it.
void Coin::exeMove() {
    if (MR::isFirstStep(this)) {
        mFlashCtrl->start(mLife);
        MR::onCalcShadow(this, nullptr);
        MR::onCalcShadowDropPrivateGravity(this, nullptr);
    }

    if (MR::isStep(this, mCannotTime)) {
        MR::validateHitSensors(this);
    }

    if (MR::isGreaterStep(this, mCannotTime) && MR::isBindedGroundDamageFire(this)) {
        MR::emitEffect(this, "LavaFall");
        MR::startSound(this, "SE_OJ_FALL_IN_MAGMA_S");
        kill();
        return;
    }

    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        kill();
        return;
    }

    MR::calcGravityOrZero(this);

    if (MR::isPressedRoofAndGround(this)) {
        MR::offBind(this);
        MR::zeroVelocity(this);
    }

    if (!MR::isNoBind(this)) {
        calcRebound();
        mVelocity += mGravity * (mIsInWater ? cWaterGravityScale : 1.0f);
        attenuateVelocity();
    }

    if (mFlashCtrl->mIsEnded) {
        kill();
    }
}

void Coin::exeControled() {
    if (MR::isFirstStep(this)) {
        MR::onCalcShadow(this, nullptr);
        MR::onCalcShadowDropPrivateGravity(this, nullptr);
    }

    MR::zeroVelocity(this);
}

void Coin::exeSpinDrained() {
    if (MR::isFirstStep(this)) {
        MR::onCalcShadow(this, nullptr);
        MR::onCalcShadowDropPrivateGravity(this, nullptr);
    }

    MR::emitEffect(this, "CoinSpinBlur");
}

// Collected at once: it jumps up out of a block and vanishes.
void Coin::exeHop() {
    if (MR::isFirstStep(this)) {
        noticeGetCoin();
    }

    mVelocity += mGravity;

    if (MR::isGreaterStep(this, cHopSteps)) {
        MR::emitEffect(this, "CoinGet");
        mFlashCtrl->end();
        kill();
    }
}

void Coin::appear() {
    appearFix();
}

void Coin::appearFix() {
    makeActorAppeared();
    MR::validateClipping(this);
    MR::validateHitSensors(this);
    MR::offBind(this);
    mCannotTime = 0;
    setNerve(GET_NERVE(Coin, CoinNrvFix));
}

void Coin::appearControlPose() {
    makeActorAppeared();
    MR::validateClipping(this);
    MR::validateHitSensors(this);
    MR::offBind(this);
    MR::calcGravityOrZero(this);
    mCannotTime = 0;
    setNerve(GET_NERVE(Coin, CoinNrvControled));
}

void Coin::appearNonActive() {
    makeActorAppeared();
    MR::validateClipping(this);
    MR::invalidateHitSensors(this);
    MR::offBind(this);
    mCannotTime = 0;
    setNerve(GET_NERVE(Coin, CoinNrvNonActive));
}

void Coin::appearFixTimer(s32 life, s32 cannotTime) {
    makeActorAppeared();
    MR::invalidateClipping(this);
    MR::invalidateHitSensors(this);
    MR::offBind(this);
    setLife(life);
    setCannotTime(cannotTime);
    setNerve(GET_NERVE(Coin, CoinNrvFixTimer));
}

// Launched with `rVelocity`; it falls back against it.
void Coin::appearMove(const Vec3f& rPosition, const Vec3f& rVelocity, s32 life, s32 cannotTime) {
    mPosition.set(rPosition);
    makeActorAppeared();
    MR::invalidateClipping(this);
    MR::invalidateHitSensors(this);
    MR::onBind(this);
    MR::onCalcShadow(this, nullptr);
    mVelocity.set(rVelocity);

    Vec3f launchDir;
    if (MR::normalizeOrZero(rVelocity, &launchDir)) {
        mGravity.x = 0.0f;
        mGravity.y = -1.0f;
        mGravity.z = 0.0f;
    } else {
        mGravity.set(-launchDir);
    }

    MR::calcGravityOrZero(this);
    setLife(life);
    setCannotTime(cannotTime);
    setNerve(GET_NERVE(Coin, CoinNrvMove));
}

void Coin::appearHop(const Vec3f& rPosition, const Vec3f& rUp) {
    if (MR::isNearZero(rUp)) {
        mGravity.x = 0.0f;
        mGravity.y = -1.0f;
        mGravity.z = 0.0f;
    } else {
        MR::normalize(-rUp, &mGravity);
    }

    mPosition.set(rPosition);
    makeActorAppeared();
    MR::invalidateClipping(this);
    MR::invalidateHitSensors(this);
    MR::offBind(this);
    MR::invalidateShadow(this, nullptr);
    mVelocity.set(rUp * cHopSpeed);
    mCannotTime = 0;
    setNerve(GET_NERVE(Coin, CoinNrvHop));
}

bool Coin::requestActive() {
    if (!isNerve(GET_NERVE(Coin, CoinNrvNonActive))) {
        return false;
    }

    MR::validateHitSensors(this);
    setNerve(GET_NERVE(Coin, CoinNrvFix));
    return true;
}

bool Coin::requestActiveWithGravity() {
    if (!isNerve(GET_NERVE(Coin, CoinNrvNonActive))) {
        return false;
    }

    MR::invalidateClipping(this);
    MR::validateHitSensors(this);
    MR::onBind(this);
    MR::calcGravityOrZero(this);
    mVelocity.set(-mGravity * cActivateLaunchSpeed);
    setNerve(GET_NERVE(Coin, CoinNrvMove));
    return true;
}

bool Coin::requestDeactive() {
    if (MR::isDead(this) || !isNerve(GET_NERVE(Coin, CoinNrvFix))) {
        return false;
    }

    MR::invalidateHitSensors(this);
    setNerve(GET_NERVE(Coin, CoinNrvNonActive));
    return true;
}

bool Coin::requestStartControl() {
    if (MR::isDead(this)) {
        return false;
    }

    bool isHidden = isNerve(GET_NERVE(Coin, CoinNrvFixHide));

    if (!isNerve(GET_NERVE(Coin, CoinNrvFix)) && !isHidden) {
        return false;
    }

    if (isHidden) {
        MR::showModel(this);
    }

    MR::validateClipping(this);
    MR::invalidateHitSensors(this);
    MR::offBind(this);
    MR::onCalcShadow(this, nullptr);
    setNerve(GET_NERVE(Coin, CoinNrvControled));
    mFlashCtrl->end();
    return true;
}

bool Coin::requestEndControl() {
    if (MR::isDead(this) || !isNerve(GET_NERVE(Coin, CoinNrvControled))) {
        return false;
    }

    MR::validateClipping(this);
    MR::validateHitSensors(this);
    MR::offBind(this);
    MR::offCalcShadow(this, nullptr);
    setCalcShadowMode();
    mCannotTime = 0;
    setNerve(GET_NERVE(Coin, CoinNrvFix));
    return true;
}

// Pulled toward the player's spin; coins in bubbles stay.
bool Coin::requestSpinDrain() {
    if (isNerve(GET_NERVE(Coin, CoinNrvSpinDrained)) || mIsNeedBubble) {
        return false;
    }

    MR::invalidateClipping(this);
    MR::offBind(this);
    MR::validateHitSensors(this);
    setNerve(GET_NERVE(Coin, CoinNrvSpinDrained));
    mFlashCtrl->end();
    return true;
}

// Show and hide report the message untaken either way.
bool Coin::requestShow() {
    if (isNerve(GET_NERVE(Coin, CoinNrvFixHide)) && !MR::isDead(this)) {
        MR::showModel(this);
        MR::validateHitSensors(this);
        setNerve(GET_NERVE(Coin, CoinNrvFix));
    }

    return false;
}

bool Coin::requestHide() {
    if (isNerve(GET_NERVE(Coin, CoinNrvFix)) && !MR::isDead(this)) {
        MR::hideModel(this);
        MR::invalidateHitSensors(this);
        setNerve(GET_NERVE(Coin, CoinNrvFixHide));
    }

    return false;
}

// A bubbled coin also gives air.
void Coin::noticeGetCoin() {
    if (mIsNeedBubble) {
        MR::emitEffect(mAirBubble, "RecoveryBubbleBreak");
        MR::incPlayerOxygen(cBubbleOxygen);
        mAirBubble->kill();
        mIsNeedBubble = false;
    }

    if (mIsPurpleCoin) {
        MR::incPurpleCoin();
        MR::startSystemSE("SE_SY_PURPLE_COIN");
    } else {
        MR::incCoin(1);
        MR::startSystemSE("SE_SY_COIN");
    }
}

void Coin::setLife(s32 life) {
    mLife = life < 0 ? cDefaultLife : life;
}

void Coin::setHostInfo(CoinHostInfo* pInfo) {
    mHostInfo = pInfo;
}

void Coin::setCalcShadowMode() {
    if (mIsCalcShadowAlways) {
        MR::onCalcShadow(this, nullptr);
        MR::onCalcShadowDropPrivateGravity(this, nullptr);
    } else {
        MR::onCalcShadowOneTime(this, nullptr);
        MR::onCalcShadowDropPrivateGravityOneTime(this, nullptr);
    }
}

void Coin::setCannotTime(s32 time) {
    mCannotTime = time < 0 ? cDefaultCannotTime : time;
}

void Coin::calcRebound() {
    if (MR::isBindedRoof(this)) {
        MR::calcReboundVelocity(&mVelocity, *MR::getRoofNormal(this), cWallRebound, cReboundFriction);
    }

    if (MR::isBindedWall(this)) {
        MR::calcReboundVelocity(&mVelocity, *MR::getWallNormal(this), cWallRebound, cReboundFriction);
    }

    if (!MR::isBindedGround(this)) {
        return;
    }

    f32 landSpeed = mVelocity.dot(-*MR::getGroundNormal(this));
    f32 rebound = 0.0f;

    if (landSpeed >= cGroundBounceMinSpeed) {
        if (landSpeed > cGroundBounceMaxSpeed) {
            landSpeed = cGroundBounceMaxSpeed;
        }

        s32 volume = (cNum_calcRebound_f1 * landSpeed) / cGroundBounceMaxSpeed;

        if (volume > cBoundSoundMinVolume) {
            MR::startSound(this, "SE_OJ_COIN_BOUND", volume);
        }

        rebound = cGroundRebound;
    }

    MR::calcReboundVelocity(&mVelocity, -mGravity, rebound, cReboundFriction);
}

void Coin::attenuateVelocity() {
    f32 keep;

    if (mIsInWater) {
        keep = cNum_attenuateVelocity_f1;
    } else if (MR::isOnGround(this)) {
        keep = cNum_attenuateVelocity_f2;
    } else {
        keep = cNum_attenuateVelocity_f3;
    }

    MR::attenuateVelocity(this, keep);
}

bool Coin::isNeedBubble(const TableIter& rIter) {
    if (!MR::isValidInfo(rIter)) {
        return false;
    }

    bool isInBubble = false;
    MR::getPlacementArg7WithInit(rIter, &isInBubble);
    return isInBubble;
}

namespace MR {
    NameObj* createDirectSetCoin(const char* pName) {
        Coin* pCoin = new Coin(pName);
        MR::createCoinHolder();
        MR::createCoinRotater();
        MR::addToCoinHolder(pCoin, pCoin);
        return pCoin;
    }

    NameObj* createCoin(const NameObj* pHost, const char* pName) {
        Coin* pCoin = new Coin(pName);
        MR::createCoinHolder();
        MR::createCoinRotater();
        MR::addToCoinHolder(pHost, pCoin);
        return pCoin;
    }

    NameObj* createDirectSetPurpleCoin(const char* pName) {
        Coin* pCoin = new Coin(pName);
        pCoin->mIsPurpleCoin = true;
        MR::createPurpleCoinHolder();
        MR::createCoinRotater();
        MR::addToPurpleCoinHolder(pCoin, pCoin);
        return pCoin;
    }

    NameObj* createPurpleCoin(const NameObj* pHost, const char* pName) {
        Coin* pCoin = new Coin(pName);
        pCoin->mIsPurpleCoin = true;
        MR::createPurpleCoinHolder();
        MR::createCoinRotater();
        MR::addToPurpleCoinHolder(pHost, pCoin);
        return pCoin;
    }
};  // namespace MR
