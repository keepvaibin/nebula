#include "Game/MapObj/BenefitItemObj.hpp"
#include "Game/LiveActor/FlashingCtrl.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initModelAndEfx_f1, "initModelAndEfx.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initModelAndEfx_f2, "initModelAndEfx.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initModelAndEfx_f3, "initModelAndEfx.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initModelAndEfx_f4, "initModelAndEfx.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtxInMovement_f1, "calcAndSetBaseMtxInMovement.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtxInMovement_f2, "calcAndSetBaseMtxInMovement.f2", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtxInMovement_f3, "calcAndSetBaseMtxInMovement.f3", 0.2f);

namespace {
    NEBULA_TUNABLE(f32, cAppearThrowUpSpd, 10.0f);
    NEBULA_TUNABLE(f32, cAppearThrowUpSpd2, 15.0f);
    NEBULA_TUNABLE(f32, cGravity, 0.5f);
    NEBULA_TUNABLE(f32, cReflectWallX, 0.3f);
    // Half a turn a second.
    const f32 cSpdRotateY = FloatUtil::PI() / static_cast< f32 >(NebulaFrameClock::stepsOfSeconds(1));
    NEBULA_TUNABLE(f32, cDefaultRailSpeed, 5.0f);
    NEBULA_TUNABLE(f32, cDefaultShadowLength, 2000.0f);
    NEBULA_TUNABLE(s32, cDefaultDemoFrames, 120);
    NEBULA_TUNABLE(s32, cShootLandSteps, 10);
    const s32 cShootSensorOnStep = NebulaFrameClock::stepsOfSeconds(1);
    const s32 cGroundAppearStep = NebulaFrameClock::stepsOfSeconds(1);
    const s32 cPreEscapeSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    const s16 cEscapeLifeSteps = NebulaFrameClock::stepsOfSeconds(10);
    NEBULA_TUNABLE(f32, cEscapeSpeed, 7.0f);
    NEBULA_TUNABLE(f32, cEscapeTurnAway, 0.02f);
    NEBULA_TUNABLE(f32, cEscapeKeepAway, 0.01f);
    NEBULA_TUNABLE(f32, cEscapeSpeedUp, 1.1f);
    NEBULA_TUNABLE(f32, cEscapeAirFriction, 0.95f);
    NEBULA_TUNABLE(f32, cEscapeGroundFall, 5.0f);
    NEBULA_TUNABLE(f32, cEscapeFallAccel, 1.0f);
    NEBULA_TUNABLE(f32, cEscapeFallSpeedMax, 20.0f);
    NEBULA_TUNABLE(f32, cEscapeWallHop, 20.0f);

    // Before the escape runs out the item blinks: from these steps left, hidden this many of every this many steps.
    NEBULA_TUNABLE(s16, cBlinkSlowLife, 180);
    NEBULA_TUNABLE(s16, cBlinkMidLife, 120);
    NEBULA_TUNABLE(s16, cBlinkFastLife, 60);

    u8 toShadowType(s32 shadowArg) {
        switch (shadowArg) {
        case -1:
            return BenefitItemObj::ShadowBit_PrivateGravity;
        case 1:
            return BenefitItemObj::ShadowBit_EveryFrame;
        case 2:
            return BenefitItemObj::ShadowBit_PrivateGravity | BenefitItemObj::ShadowBit_EveryFrame;
        case 3:
            return BenefitItemObj::ShadowBit_Once;
        default:
            return 0;
        }
    }
};  // namespace

void ShadowClipActor::endClipped() {
    LiveActor::endClipped();

    if (MR::isClipped(mHost)) {
        mHost->endClipped();
    }
}

BenefitItemObj::BenefitItemObj(const char* pName, const char* pModelName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< BenefitItemObj >);
    mIsDisabled = false;
    mIsItemMoving = false;
    mIsInAir = false;
    mIsDemoPending = false;
    mHasAppearCamera = false;
    mCameraFrames = 0;
    mRotateY = 0.0f;
    mFollowMtx = nullptr;
    mIsThrownHigh = false;
    mHasAppearPicture = false;
    mCameraInfo = nullptr;
    mHitSensorActor = nullptr;
    mEscapeVelocity.zero();
    mFallVelocity.zero();
    mModelName = pModelName;
    NebulaMtxIdentity(mModelMtx);
}

void BenefitItemObj::initModelAndEfx() {
    initModelManagerWithAnm(mModelName, nullptr, false);
    initEffectKeeper(5, mModelName);

    initBinder(cNum_initModelAndEfx_f1, cNum_initModelAndEfx_f2, 0);
    initHitSensor(1);
    MR::addHitSensor(this, "body", ATYPE_KINOKO_ONEUP, 4, cNum_initModelAndEfx_f3, Vec3f(0.0f, cNum_initModelAndEfx_f4, 0.0f));
}

// Unplaced (created by code) it waits for appear. Placed, the args: 0 above 0 turns in place per arg 6; 1 the appear
// type in its low 4 bits, 0x80 the appear picture; 2 the escape type; 3 = 1 clips at the shadow length; 4 the shadow
// (-1 along its own gravity once, 1 every frame, 2 both, 3 once, other none); 5 = 1 an appear camera; 6 the turn flags;
// 7 the shadow length (else 2000, cut at the ground). A rail's arg 0 is the escape speed in tenths.
void BenefitItemObj::init(const TableIter& rIter) {
    LiveActor::init(rIter);

    bool isPlaced = MR::isValidInfo(rIter);
    if (isPlaced) {
        MR::initDefaultPos(this, rIter);
    }

    initModelAndEfx();

    MR::connectToSceneItemStrongLight(this);
    MR::offCalcGravity(this);
    MR::calcUpVec(&mGravity, this);
    mGravity = -mGravity;

    initSound(4, false);
    mFlashCtrl = new FlashingCtrl(this, true);
    MR::offBind(this);

    mIsOnRail = false;

    if (isPlaced) {
        mIsOnRail = MR::isConnectedWithRail(rIter);

        if (mIsOnRail) {
            initRailRider(rIter);
        }

        mRailSpeed = cDefaultRailSpeed;

        s32 railSpeed;
        if (mIsOnRail && MR::getRailArg0NoInit(this, &railSpeed)) {
            mRailSpeed = railSpeed / cNum_init_f1;
        }
    }

    MR::setClippingFar100m(this);
    MR::setClippingTypeSphere(this, cNum_init_f2);

    initNerve(GET_NERVE(BenefitItemObj, HostTypeNrvWait));

    mIsActive = true;
    mAppearType = AppearType_InPlace;
    mEscapeType = EscapeType_None;
    mIsWaitingAppear = false;

    s32 activeArg = -1;
    s32 appearArg = -1;
    s32 escapeArg = -1;
    s32 clipArg = -1;
    s32 shadowArg = -1;
    s32 cameraArg = -1;
    s32 rotateArg = -1;
    s32 shadowLengthArg = -1;

    if (isPlaced) {
        MR::getPlacementArg0NoInit(rIter, &activeArg);
        MR::getPlacementArg1NoInit(rIter, &appearArg);
        MR::getPlacementArg2NoInit(rIter, &escapeArg);
        MR::getPlacementArg3NoInit(rIter, &clipArg);
        MR::getPlacementArg4NoInit(rIter, &shadowArg);
        MR::getPlacementArg5NoInit(rIter, &cameraArg);
        MR::getPlacementArg6NoInit(rIter, &rotateArg);
        MR::getPlacementArg7NoInit(rIter, &shadowLengthArg);
    } else {
        mIsWaitingAppear = true;
        mIsActive = false;
    }

    if (activeArg == -1 || activeArg == 0) {
        mIsActive = false;
    }

    s32 appearType = appearArg & 0xF;
    if (appearType >= AppearType_Ground && appearType <= AppearType_ThrowUpQuestionBox) {
        mAppearType = appearType;
    }

    if (appearArg != -1 && (appearArg & 0x80) != 0) {
        mHasAppearPicture = true;
    }

    if (escapeArg >= 1 && escapeArg <= 3) {
        mEscapeType = escapeArg;
    }

    bool isClipAtShadowLength = clipArg == 1;
    mShadowType = toShadowType(shadowArg);

    if (cameraArg == 1 && isPlaced) {
        MR::initActorCamera(this, rIter, &mCameraInfo);
        mHasAppearCamera = true;
    }

    mRotateFlags = rotateArg == -1 ? 0 : rotateArg;

    f32 shadowLength = cDefaultShadowLength;
    if (shadowLengthArg != -1) {
        shadowLength = shadowLengthArg;
    }

    if (!mIsDisabled && isPlaced) {
        MR::useStageSwitchWriteDead(this, rIter);

        if (MR::useStageSwitchReadAppear(this, rIter)) {
            MR::listenStageSwitchOnAppear(this, MR::Functor(this, &BenefitItemObj::appear));
            mIsWaitingAppear = true;
        }
    }

    if (mIsDisabled) {
        mIsActive = false;
        makeActorDead();
        return;
    }

    if (mIsActive) {
        makeActorAppeared();

        if (mIsWaitingAppear) {
            MR::invalidateHitSensors(this);
        } else {
            runEfx("Glow");
        }
    } else if (mIsWaitingAppear) {
        makeActorDead();
    } else {
        makeActorAppeared();
        runEfx("Glow");
    }

    if (!MR::isDead(this) && isPlaced) {
        MR::useStageSwitchSleep(this, rIter);
    }

    Vec3f rotY;
    MR::getRotatedAxisY(&rotY, mRotation);
    mUp.set(rotY);

    if (mShadowType == 0) {
        return;
    }

    MR::initShadowVolumeCylinder(this, cNum_init_f3);
    MR::setShadowDropLength(this, nullptr, shadowLength);

    if (shadowLengthArg == -1) {
        MR::onShadowVolumeCutDropLength(this, nullptr);
        MR::setShadowVolumeEndDropOffset(this, nullptr, cNum_init_f4);
    }

    bool isEveryFrame = (mShadowType & ShadowBit_EveryFrame) != 0;
    if ((mShadowType & ShadowBit_PrivateGravity) != 0) {
        if (isEveryFrame) {
            MR::onCalcShadowDropPrivateGravity(this, nullptr);
            MR::onCalcShadow(this, nullptr);
        } else {
            MR::onCalcShadowDropPrivateGravityOneTime(this, nullptr);
            MR::onCalcShadowOneTime(this, nullptr);
        }
    } else if (isEveryFrame) {
        MR::onCalcShadow(this, nullptr);
    } else {
        MR::onCalcShadowOneTime(this, nullptr);
    }

    if (isClipAtShadowLength) {
        MR::setClippingTypeSphere(this, shadowLength);
    }
}

void BenefitItemObj::initAfterPlacement() {
    if (mIsOnRail) {
        MR::moveCoordAndTransToNearestRailPos(this);
    }
}

void BenefitItemObj::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    runEfx("Glow");
}

void BenefitItemObj::appear() {
    LiveActor::appear();

    mIsWaitingAppear = false;
    mIsActive = true;

    switch (mAppearType) {
    case AppearType_InPlace:
        if (mEscapeType != EscapeType_None) {
            setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvPreEscape));
        }

        MR::validateHitSensors(this);
        break;
    case AppearType_Ground:
        appearGround();
        break;
    case AppearType_ThrowUp:
        appearThrowUp();
        break;
    case AppearType_ThrowUpQuestionBox:
        appearThrowUpQuestionBox();
        break;
    }

    LiveActor::calcAndSetBaseMtx();
    NebulaMtxCopy(getBaseMtx(), mModelMtx);

    if (mHasAppearCamera) {
        mIsDemoPending = true;
        MR::invalidateClipping(this);
    }

    if (mEscapeType != EscapeType_None) {
        MR::invalidateClipping(this);
    }

    if (mHasAppearPicture) {
        MR::emitEffect(this, "AppearPicture");
    }
}

// Grows out of the ground (see exeAppearGround).
void BenefitItemObj::appearGround() {
    MR::startSystemSE("SE_SY_ITEM_APPEAR");
    setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvAppearGround));
    MR::hideModel(this);
    MR::invalidateClipping(this);
}

void BenefitItemObj::appearThrowUp() {
    MR::startSystemSE("SE_SY_ITEM_APPEAR");

    runBck("Appear");

    Vec3f rotY;
    MR::getRotatedAxisY(&rotY, mRotation);
    mUp.set(rotY);

    setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvShoot));

    if (!mIsThrownHigh) {
        mVelocity = rotY * cAppearThrowUpSpd;
    } else {
        mVelocity = rotY * cAppearThrowUpSpd2;
    }

    MR::invalidateClipping(this);
}

void BenefitItemObj::shoot(const Vec3f& rPosition, const Vec3f&, bool) {
    mPosition = rPosition;

    if (!mIsThrownHigh) {
        mAppearType = AppearType_ThrowUp;
    } else {
        mAppearType = AppearType_ThrowUpQuestionBox;
    }

    appear();
}

void BenefitItemObj::appearThrowUpQuestionBox() {
    appearThrowUp();
}

// Runs the appear camera demo once it can start; vanishes on fire ground or, moving, in a death area.
void BenefitItemObj::control() {
    if (mIsDemoPending && MR::tryStartDemoWithoutCinemaFrame(this, "出現")) {
        mIsDemoPending = false;

        MR::startActorCameraTargetSelf(this, mCameraInfo);
        MR::requestMovementOn(this);
        MR::pauseOffCameraDirector();

        mCameraFrames = MR::getActorCameraFrames(this, mCameraInfo);
        if (mCameraFrames == 0) {
            mCameraFrames = cDefaultDemoFrames;
        }
    }

    MR::calcGravityOrZero(this);
    calcAndSetBaseMtxInMovement();

    if (mHasAppearCamera && mCameraFrames != 0) {
        mCameraFrames--;

        if (mCameraFrames == 0) {
            mHasAppearCamera = false;

            MR::endActorCamera(this, mCameraInfo, false);
            MR::endDemo(this, "出現");
            MR::validateClipping(this);
        }
    }

    if (mIsItemMoving) {
        return;
    }

    if (MR::isBindedGroundDamageFire(this)) {
        runEfx("Vanish");
        kill();
    }

    if (!MR::isNearZero(mVelocity) && MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        runEfx("Vanish");
        kill();
    }
}

void BenefitItemObj::exeWait() {
    if (mIsActive && !mIsWaitingAppear && mRotateFlags != 0) {
        doRotateY();
    }
}

void BenefitItemObj::doRotateY() {
    if (isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvAppearGround))) {
        mRotateY = 0.0f;
    } else {
        mRotateY = MR::repeat(mRotateY + cSpdRotateY, 0.0f, TWO_PI);
    }
}

// Falls under gravity, losing part of its speed into walls; on landing waits (or escapes).
void BenefitItemObj::exeShoot() {
    if (MR::isFirstStep(this)) {
        MR::onBind(this);
    }

    Vec3f newVel(mVelocity);
    f32 fallSpeed = MR::vecKillElement(newVel, mGravity, &newVel);

    if (MR::isBindedGround(this)) {
        runBck("Land");
        MR::validateHitSensors(this);
        mVelocity.zero();

        if (mEscapeType == EscapeType_None) {
            if (MR::isGreaterStep(this, cShootLandSteps)) {
                setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvWait));
            }
        } else {
            setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvEscape));
        }

        return;
    }

    f32 nextFallSpeed = fallSpeed + cGravity;

    if (getNerveStep() == cShootSensorOnStep) {
        MR::validateHitSensors(this);
    }

    if (MR::isBindedWall(this)) {
        f32 intoWall = MR::vecKillElement(newVel, *MR::getWallNormal(this), &newVel);
        newVel -= *MR::getWallNormal(this) * intoWall * cReflectWallX;
    }

    newVel += mGravity * nextFallSpeed;
    mVelocity.set(newVel);
}

void BenefitItemObj::exeCatch() {
    if (MR::isFirstStep(this)) {
        runEfx("Get");
        MR::requestOneUp();
        MR::hideModel(this);
        MR::incPlayerLeft();
        MR::explainOneUpIfAtFirst();
        kill();
    }

    mVelocity.set(0.0f);
    mPosition.set(mHitSensorActor->mPosition);
}

bool BenefitItemObj::receiveMsgPlayerAttack(u32 msg, HitSensor*, HitSensor*) {
    return MR::isMsgStarPieceReflect(msg);
}

bool BenefitItemObj::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isDead(this)) {
        return false;
    }

    if (MR::receiveItemShowMsg(msg, pSender, pReceiver) || MR::receiveItemHideMsg(msg, pSender, pReceiver)) {
        return true;
    }

    if (MR::isMsgItemStartMove(msg)) {
        mIsItemMoving = true;
        return true;
    }

    if (MR::isMsgItemEndMove(msg)) {
        mIsItemMoving = false;
        return true;
    }

    if (msg != ACTMES_ITEM_GET || mHitSensorActor != nullptr) {
        return false;
    }

    if (isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvWait)) || isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvShoot)) ||
        isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvAppearGround)) || isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvEscape))) {
        mHitSensorActor = pSender->mHost;
        setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvCatch));
        MR::tryRumblePadWeak(this, PLAYER_1);
        return true;
    }

    return false;
}

void BenefitItemObj::exeAppearGround() {
    if (MR::isFirstStep(this)) {
        MR::onBind(this);
    }

    if (getNerveStep() < cGroundAppearStep) {
        return;
    }

    if (getNerveStep() == cGroundAppearStep) {
        MR::showModel(this);
        runBck("Appear");
        MR::startSound(this, "SE_OJ_KINOKO_1UP_GROW");
    }

    if (!MR::isBckStopped(this)) {
        return;
    }

    if (mEscapeType == EscapeType_None) {
        setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvWait));
    } else {
        setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvPreEscape));
    }

    MR::validateHitSensors(this);
}

void BenefitItemObj::exePreEscape() {
    if (getNerveStep() >= cPreEscapeSteps) {
        setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvEscape));
    }
}

void BenefitItemObj::initEscape() {
    mEscapeLife = cEscapeLifeSteps;
    mFlashPeriod = 0;
    mFlashHideTime = 0;
    mFlashCounter = 0;
    mHideCounter = 0;

    Vec3f rotZ;
    MR::getRotatedAxisZ(&rotZ, mRotation);
    mEscapeVelocity = rotZ * cEscapeSpeed;

    MR::startSound(this, "SE_OJ_KINOKO_1UP_RUN_START");
}

// On the ground it runs along it (bending away from the player for EscapeType_FromPlayer); in the air it slows and
// falls; into a wall it loses the speed into it and hops.
void BenefitItemObj::doEscape() {
    MR::startLevelSound(this, "SE_OJ_LV_KINOKO_1UP_RUN");

    if (MR::isBindedGround(this)) {
        f32 speed = mEscapeVelocity.length();
        MR::vecKillElement(mEscapeVelocity, mGravity, &mEscapeVelocity);
        mEscapeVelocity.setLength(speed);

        if (mEscapeType == EscapeType_FromPlayer) {
            Vec3f awayFromPlayer(mPosition - *MR::getPlayerPos());
            MR::vecKillElement(awayFromPlayer, mGravity, &awayFromPlayer);
            MR::normalizeOrZero(&awayFromPlayer);

            f32 turnRate = mEscapeVelocity.dot(awayFromPlayer) < 0.0f ? cEscapeTurnAway : cEscapeKeepAway;
            mEscapeVelocity += awayFromPlayer * turnRate;
            mEscapeVelocity.scale(cEscapeSpeedUp);
        }

        if (mEscapeVelocity.length() > cEscapeSpeed) {
            mEscapeVelocity.setLength(cEscapeSpeed);
        }

        if (mIsInAir) {
            mIsInAir = false;
            runBck("Land");
        }

        mFallVelocity = mGravity * cEscapeGroundFall;
        runEfx("Move");
    } else {
        mEscapeVelocity.scale(cEscapeAirFriction);
        mIsInAir = true;

        mFallVelocity += mGravity * cEscapeFallAccel;
        if (mFallVelocity.length() > cEscapeFallSpeedMax) {
            mFallVelocity.setLength(cEscapeFallSpeedMax);
        }

        stopEfx("Move");
    }

    if (!MR::isBindedWall(this) || mIsInAir) {
        return;
    }

    const Vec3f& rWallNormal = *MR::getWallNormal(this);
    if (!(rWallNormal.dot(mVelocity) < 0.0f)) {
        return;
    }

    f32 intoWall = MR::vecKillElement(mEscapeVelocity, rWallNormal, &mEscapeVelocity);
    mEscapeVelocity += -rWallNormal * intoWall;
    mFallVelocity += -mGravity * cEscapeWallHop;

    mIsInAir = true;
}

// Escapes for a while (along its rail if it has one), blinking faster toward the end, then vanishes.
void BenefitItemObj::exeEscape() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::onBind(this);
        initEscape();
    }

    doEscape();

    if (mEscapeLife != 0) {
        MR::showModel(this);
        mEscapeLife--;

        if (mHideCounter != 0) {
            mHideCounter--;
            MR::hideModel(this);
        }

        if (mFlashCounter != 0) {
            mFlashCounter--;

            if (mFlashCounter == 0) {
                mFlashCounter = mFlashPeriod;
                mHideCounter = mFlashHideTime;
            }
        }

        if (mEscapeLife == cBlinkFastLife) {
            mFlashPeriod = 4;
            mFlashHideTime = 2;
        } else if (mEscapeLife == cBlinkMidLife) {
            mFlashPeriod = 8;
            mFlashHideTime = 3;
        } else if (mEscapeLife == cBlinkSlowLife) {
            mFlashPeriod = 12;
            mFlashHideTime = 4;
            mFlashCounter = 12;
            mHideCounter = 4;
        }

        if (mEscapeLife == 0) {
            runEfx("Vanish");
            makeActorDead();
        }
    }

    if (mIsOnRail) {
        MR::moveCoord(this, mRailSpeed);
        mEscapeVelocity = MR::getRailPos(this) - mPosition;
    }

    mVelocity = mEscapeVelocity;
    mVelocity += mFallVelocity;

    if ((mRotateFlags & RotateBit_Escape) != 0) {
        doRotateY();
    }

    if (mShadowType == 0 || (mShadowType & ShadowBit_EveryFrame) != 0) {
        return;
    }

    if ((mShadowType & ShadowBit_PrivateGravity) != 0) {
        MR::onCalcShadowOneTime(this, nullptr);
    } else {
        MR::onCalcShadowDropPrivateGravityOneTime(this, nullptr);
    }
}

void BenefitItemObj::calcAndSetBaseMtx() {
}

// Waiting it stands (on its follow matrix if it has one) turned by its spin; moving it faces its velocity, up along the
// ground or wall it touches, blending toward that pose.
void BenefitItemObj::calcAndSetBaseMtxInMovement() {
    if (isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvWait))) {
        LiveActor::calcAndSetBaseMtx();
        NebulaMtxCopy(getBaseMtx(), mModelMtx);

        if (mFollowMtx != nullptr) {
            NebulaMtxCopy(mFollowMtx, mModelMtx);
            MR::extractMtxTrans(mFollowMtx, &mPosition);
        }

        MR::multMtx(getBaseMtx(), MR::tmpMtxRotYRad(mRotateY), mModelMtx);
        return;
    }

    if (MR::isBindedGround(this)) {
        mUp = *MR::getGroundNormal(this);
    } else if (MR::isBindedWall(this)) {
        mUp = *MR::getWallNormal(this);
    }

    Vec3f front(mVelocity);
    MR::normalizeOrZero(&front);

    if (isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvPreEscape))) {
        MR::vecBlendSphere(mBlendFront, front, &front, cNum_calcAndSetBaseMtxInMovement_f1);
        mBlendFront = front;
    }

    if (MR::isNearZero(front)) {
        return;
    }

    if (MR::isSameDirection(mUp, front, cNum_calcAndSetBaseMtxInMovement_f2)) {
        MR::setMtxTrans(getBaseMtx(), mPosition);
        return;
    }

    Pos3f pose;
    MR::makeMtxUpFrontPos(&pose, mUp, front, mPosition);
    MR::blendMtx(mModelMtx, pose, cNum_calcAndSetBaseMtxInMovement_f3, mModelMtx);
    MR::multMtx(getBaseMtx(), MR::tmpMtxRotYRad(mRotateY), mModelMtx);
    MR::setMtxTrans(getBaseMtx(), mPosition);
}

void BenefitItemObj::setFollowMtx(MtxPtr pMtx) {
    mFollowMtx = pMtx;
    MR::extractMtxTrans(pMtx, &mPosition);
}

void BenefitItemObj::runBck(const char* pBckName) {
    MR::startBck(this, pBckName);
}

void BenefitItemObj::runEfx(const char* pEfxName) {
    MR::emitEffect(this, pEfxName);
}

void BenefitItemObj::stopEfx(const char* pEfxName) {
    MR::deleteEffect(this, pEfxName);
}
