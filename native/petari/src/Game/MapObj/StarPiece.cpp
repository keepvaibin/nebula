#include "Game/MapObj/StarPiece.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/MapObj/PowerStar.hpp"
#include "Game/MapObj/StarPieceDirector.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/TriangleFilter.hpp"
#include <nebula_geometry.hpp>

#include <NebulaGpuTypes.h>
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_goToPlayer_f1, "goToPlayer.f1", 2.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeFall_reboundVelocityFromCollision_1, "exeFall.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFall_reboundVelocityFromCollision_2, "exeFall.reboundVelocityFromCollision.2", 1.0f);

namespace {
    // Indexed by the star pointer port that pulled the piece.
    const NebulaColor8 cTouchColors[] = {{0x00, 0x55, 0xff, 0xFF}, {0xFF, 0xFF, 0x00, 0xFF}, {0x55, 0xFF, 0x00, 0xFF}, {0x7F, 0x7F, 0x00, 0xFF}};
    const NebulaColor8 cPieceColors[] = {{0x80, 0x00, 0x99, 0xFF}, {0xE6, 0xA0, 0x00, 0xFF}, {0x46, 0xA1, 0x08, 0xFF},
                                         {0x37, 0x5A, 0xA0, 0xFF}, {0xBE, 0x33, 0x0B, 0xFF}, {0x80, 0x80, 0x80, 0xFF}};

    NEBULA_TUNABLE(f32, cBinderRadius, 40.0f);
    NEBULA_TUNABLE(f32, cThrowBinderRadius, 20.0f);
    NEBULA_TUNABLE(f32, cSensorRadius, 30.0f);
    NEBULA_TUNABLE(f32, cShadowRadius, 30.0f);
    NEBULA_TUNABLE(f32, cPointerRadius, 28.0f);
    NEBULA_TUNABLE(u16, cSensorGroupSize, 16);
    NEBULA_TUNABLE(f32, cGiftBtkFrame, 5.0f);
    NEBULA_TUNABLE(f32, cSpinSpeed, 15.0f);
    NEBULA_TUNABLE(f32, cScaleMin, 0.1f);
    NEBULA_TUNABLE(f32, cScaleMax, 2.0f);
    NEBULA_TUNABLE(f32, cScaleRange, 1.9f);
    // Effects are skipped when the piece is this close to the camera.
    NEBULA_TUNABLE(f32, cEffectCameraDistance, 200.0f);
    NEBULA_TUNABLE(f32, cHopSpeed, 30.0f);
    const s32 cHopSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, cFallDamp, 0.97f);
    NEBULA_TUNABLE(f32, cFallGravity, 1.0f);
    NEBULA_TUNABLE(f32, cWaterFallGravity, 0.1f);
    NEBULA_TUNABLE(f32, cReboundBounce, 0.99f);
    NEBULA_TUNABLE(f32, cLandSpeedMin, 20.0f);
    NEBULA_TUNABLE(f32, cWaterLandSpeedMin, 10.0f);
    const s32 cFallKillSteps = NebulaFrameClock::stepsOfSeconds(10);
    NEBULA_TUNABLE(s32, cReflectFallSteps, 9);
    NEBULA_TUNABLE(f32, cPlayerTouchDistance, 160.0f);
    // More launched pieces than this use the small effects.
    NEBULA_TUNABLE(s32, cEffectFullLaunchedMax, 10);
    NEBULA_TUNABLE(f32, cTurnRateStart, 0.02f);
    NEBULA_TUNABLE(f32, cTurnRateStep, 0.01f);
    NEBULA_TUNABLE(f32, cChaseRateStep, 0.03f);
    NEBULA_TUNABLE(f32, cChaseSpeed, 85.0f);
    NEBULA_TUNABLE(f32, cTargetScale, 0.5f);
    NEBULA_TUNABLE(f32, cTargetScaleRate, 0.05f);
    NEBULA_TUNABLE(f32, cTouchEffectSize, 10.0f);
    NEBULA_TUNABLE(s32, cToPlayerEndShrinkSteps, 10);
    const s32 cToPlayerEndSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 cThrowSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, cThrowBindStep, 28);
    const s32 cThrowFallSteps = NebulaFrameClock::stepsOfSeconds(1);
    // The throw path is a straight line from start to end.
    NEBULA_TUNABLE(f32, cThrowArcHeight, 0.0f);
    NEBULA_TUNABLE(f32, cThrowBindCameraDistance, 1500.0f);
    NEBULA_TUNABLE(f32, cGiftManyScale, 2.0f);
    NEBULA_TUNABLE(s32, cGoToPlayerDelay, 15);
    NEBULA_TUNABLE(s32, cLaunchGettableDelay, 25);
    NEBULA_TUNABLE(f32, cWaterLaunchScale, 0.5f);
    NEBULA_TUNABLE(f32, cPointRadius, 30.0f);
    NEBULA_TUNABLE(f32, cNearCameraScreenRadius, 30.0f);
    NEBULA_TUNABLE(f32, cGettableDistance, 15000.0f);
    NEBULA_TUNABLE(s32, cGettableEffectPeriod, 90);
    NEBULA_TUNABLE(s32, cGettableEffectStep, 20);
    NEBULA_TUNABLE(f32, cGettableEffectSize, 4.0f);
    NEBULA_TUNABLE(f32, cGettableEffectScale, 0.8f);
    NEBULA_TUNABLE(f32, cTouchEffectScale, 1.0f);
    // The piece color plus 255 always clamped to white.
    NEBULA_TUNABLE(u8, cGettablePrimary, 0xFF);
    NEBULA_TUNABLE(s32, cGettableLighten, 0x20);
    NEBULA_TUNABLE(f32, cReflectElasticity, 0.5f);
    NEBULA_TUNABLE(f32, cReflectBounceSpeed, 35.0f);

    f32 approachOne(f32 rate, f32 step) {
        if (rate + step >= 1.0f) {
            return 1.0f;
        }

        return rate + step;
    }

    u8 lighten(u8 channel) {
        return MR::clamp(static_cast< s32 >(channel) + cGettableLighten, 0x00, 0xFF);
    }
}  // namespace

namespace NrvStarPiece {
    NEW_NERVE(HostTypeNrvFloating, StarPiece, Floating);
    NEW_NERVE(HostTypeNrvHop, StarPiece, Hop);
    NEW_NERVE(HostTypeNrvFall, StarPiece, Fall);
    NEW_NERVE(HostTypeNrvFallAfterReflect, StarPiece, Fall);
    NEW_NERVE(HostTypeNrvToTarget, StarPiece, ToTarget);
    NEW_NERVE(HostTypeNrvSpinDrained, StarPiece, SpinDrained);
    NEW_NERVE(HostTypeNrvToPlayerEnd, StarPiece, ToPlayerEnd);
    NEW_NERVE_ONEND(HostTypeNrvThrow, StarPiece, Throw, Throw);
    NEW_NERVE_ONEND(HostTypeNrvThrowNoFall, StarPiece, Throw, Throw);
    NEW_NERVE_ONEND(HostTypeNrvThrowFall, StarPiece, ThrowFall, Throw);
    NEW_NERVE(HostTypeNrvFollowPlayer, StarPiece, FollowPlayer);
    NEW_NERVE(HostTypeNrvRailMove, StarPiece, RailMove);
};  // namespace NrvStarPiece

StarPiece::StarPiece(const char* pName)
    : LiveActor(pName), mFront(0, 0, 1), mTurnRate(cTurnRateStart), mChaseRate(), mThrowStart(0, 0, 0), mThrowEnd(0, 0, 0), mDelegator(nullptr), mTargetSensor(),
      mGettableEffectStep(-1), mGettableDelayCounter(-1), mFallKillTimer(), mColor(NebulaColor8{0, 0, 0, 255}), mGroupType(groupType_noGroup), mHostInfo(nullptr),
      mReceiverInfo(nullptr), mNumGift(1) {
    lumacore::setClass(this, lumacore::kClassId< StarPiece >);
    mFlags.isGoToPlayer = false;
    mFlags.isGettable = true;
    mFlags.isCheckGround = false;
    mFlags.isNoShadow = false;
    mFlags.isGroup = false;
    mFlags.InWater = false;
    mFlags.isForceEffectLight = false;
    mFlags.isLaunched = false;
}

void StarPiece::initAndSetFloatingFromGroup(const TableIter& rIter) {
    mGroupType = groupType_FloatingGroup;
    init(rIter);
    MR::invalidateClipping(this);
    mFlags.isGroup = true;
}

void StarPiece::initAndSetRailMoveFromGroup(const TableIter& rIter) {
    mGroupType = groupType_RailMoveGroup;
    init(rIter);
    MR::invalidateClipping(this);
    mFlags.isGroup = true;
}

void StarPiece::init(const TableIter& rIter) {
    s32 colorIndex = -1;

    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        MR::getPlacementArg3NoInit(rIter, &colorIndex);
    }

    if (colorIndex >= 0 && colorIndex < getNumColor()) {
        mColor = cPieceColors[colorIndex];
    } else {
        mColor = cPieceColors[MR::getRandom(0, getNumColor())];
    }

    initModelManagerWithAnm("StarPiece", nullptr, true);
    MR::initMaterialColor0(this, "StarPiece1_v", &mColor);
    MR::prepareMaterials(this);
    MR::connectToSceneNoSilhouettedMapObj(this);

    mScale.set(1.0f);
    initBinder(cBinderRadius, 0.0f, 0);

    switch (mGroupType) {
    case groupType_noGroup:
    case groupType_FloatingGroup:
        initNerve(GET_NERVE(StarPiece, HostTypeNrvFloating));
        break;
    case groupType_RailMoveGroup:
        initNerve(GET_NERVE(StarPiece, HostTypeNrvRailMove));
        break;
    }

    initEffectKeeper(0, "StarPiece");
    initSound(4, false);
    initHitSensor(2);

    MR::addHitSensorEye(this, "attack", cSensorGroupSize, cSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, "body", ATYPE_STAR_PIECE, cSensorGroupSize, cSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initShadowVolumeSphere(this, cShadowRadius);

    MR::offCalcShadow(this, nullptr);
    MR::offCalcShadowDropPrivateGravity(this, nullptr);
    MR::onCalcShadowOneTime(this, nullptr);
    MR::onCalcShadowDropPrivateGravityOneTime(this, nullptr);

    if (MR::isValidInfo(rIter)) {
        // -1: shadow dropped once; 0: shadow followed and sand under the piece checked; 1: no shadow.
        s32 shadowType = -1;
        MR::getPlacementArg4NoInit(rIter, &shadowType);

        if (shadowType == 0) {
            MR::onCalcShadow(this, nullptr);
            MR::onCalcShadowDropPrivateGravity(this, nullptr);
            mFlags.isCheckGround = true;
            mFlags.isNoShadow = false;
        } else if (shadowType == -1) {
            mFlags.isCheckGround = false;
            mFlags.isNoShadow = false;
        } else if (shadowType == 1) {
            mFlags.isCheckGround = false;
            mFlags.isNoShadow = true;
            MR::invalidateShadow(this, nullptr);
        }
    }

    MR::initStarPointerTarget(this, cPointerRadius, Vec3f(0, 0, 0));
    MR::offCalcGravity(this);

    mDelegator = MR::createTriangleFilterDelegator(this, &StarPiece::isIgnoreTriOnThrow);

    MR::tryCreateMirrorActor(this, "StarPiece");
    MR::startBtk(this, "Gift");
    MR::setBtkFrameAndStop(this, cGiftBtkFrame);

    if (mGroupType == groupType_noGroup) {
        appear();
    } else {
        makeActorDead();
    }
}

void StarPiece::initAfterPlacement() {
}

void StarPiece::appear() {
    LiveActor::appear();
    MR::onCalcShadow(this, nullptr);
    MR::onCalcShadowDropPrivateGravity(this, nullptr);
}

void StarPiece::setColor(s32 colorIndex) {
    mColor = cPieceColors[colorIndex];
}

s32 StarPiece::getNumColor() {
    return ARRAY_SIZE(cPieceColors);
}

void StarPiece::appearFromGroup() {
    appear();

    switch (mGroupType) {
    case groupType_FloatingGroup:
        setNerve(GET_NERVE(StarPiece, HostTypeNrvFloating));
        break;
    case groupType_RailMoveGroup:
        setNerve(GET_NERVE(StarPiece, HostTypeNrvRailMove));
        break;
    default:
        break;
    }
}

void StarPiece::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    getSensor("attack")->invalidate();
    mGettableEffectStep = -1;
}

void StarPiece::kill() {
    MR::forceDeleteEffect(this, "StarPieceFlyingBlur");

    if (isAwayFromCamera()) {
        emitEffectBySize("StarPieceBreak", "StarPieceBreakS");
    }

    startSoundByWater("SE_OJ_STAR_PIECE_BREAK", "SE_OJ_STAR_PIECE_BREAK_W");
    LiveActor::kill();
}

void StarPiece::makeActorDead() {
    LiveActor::makeActorDead();
    mTargetSensor = nullptr;
    mReceiverInfo = nullptr;
    mGettableEffectStep = -1;

    mFlags.isForceEffectLight = false;

    if (mFlags.isLaunched) {
        MR::getStarPieceDirector()->mNumStarPiecesLaunched--;
        mFlags.isLaunched = false;
    }

    if (mHostInfo != nullptr) {
        mHostInfo->mNumAppeared--;
        mHostInfo = nullptr;
    }
}

void StarPiece::startClipped() {
    LiveActor::startClipped();

    if (!isNerve(GET_NERVE(StarPiece, HostTypeNrvFloating)) && !isNerve(GET_NERVE(StarPiece, HostTypeNrvRailMove))) {
        makeActorDead();
    }

    mGettableEffectStep = -1;
}

void StarPiece::changeScale(f32 scale) {
    mScale.set(scale);

    f32 rate = (MR::clamp(scale, cScaleMin, cScaleMax) - cScaleMin) / cScaleRange;
    MR::setBtkFrameAndStop(this, MR::getBtkFrameMax(this) * (1.0f - rate));
    MR::setBinderRadius(this, cBinderRadius * scale);
    MR::setShadowVolumeSphereRadius(this, nullptr, cShadowRadius * scale);
}

void StarPiece::control() {
    if (MR::isFirstStep(this)) {
        if (!isNerve(GET_NERVE(StarPiece, HostTypeNrvToTarget)) || mFlags.isGoToPlayer) {
            MR::setBtkFrameAndStop(this, cGiftBtkFrame);
        }
    }

    if (mGettableDelayCounter >= 0) {
        mGettableDelayCounter -= 1;
    }

    if (isNerveFall() && (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f)) || MR::isPressedRoofAndGround(this))) {
        kill();
        return;
    }

    if (isNerveFall() || isNerveThrow()) {
        changeScale(calcNearCameraScale());
    }
}

void StarPiece::exeFloating() {
    if (MR::isFirstStep(this)) {
        MR::offBind(this);
        tryCalcGravity();

        if (!mFlags.isNoShadow) {
            MR::validateShadow(this, nullptr);
        } else {
            MR::invalidateShadow(this, nullptr);
        }

        if (mFlags.isGroup) {
            MR::invalidateClipping(this);
        } else {
            MR::validateClipping(this);
        }
    }

    spin();
    tryGotJudge();

    if (mFlags.isCheckGround) {
        HitInfo info;
        MR::checkStrikePointToMap(mPosition, &info);

        if (MR::isCodeSand(&info.mParentTriangle)) {
            kill();
            MR::emitEffect(this, "SandColumnS");
        }
    }
}

void StarPiece::appearHop(const Vec3f& rPosition, const Vec3f& rUp) {
    if (MR::isNearZero(rUp)) {
        mGravity.set(0.0f, -1.0f, 0.0f);
    } else {
        MR::normalize(-rUp, &mGravity);
    }

    mPosition.set(rPosition);
    makeActorAppeared();
    mVelocity.set(rUp * cHopSpeed);
    setNerve(GET_NERVE(StarPiece, HostTypeNrvHop));
}

void StarPiece::exeHop() {
    if (MR::isFirstStep(this)) {
        MR::gotByPlayer();
        MR::invalidateClipping(this);
        MR::offBind(this);
        MR::invalidateShadow(this, nullptr);
    }

    mVelocity += mGravity;

    if (MR::isGreaterStep(this, cHopSteps)) {
        if (mHostInfo != nullptr) {
            mHostInfo->mNumGot++;
        }

        kill();
    }
}

void StarPiece::exeFall() {
    if (MR::isFirstStep(this)) {
        emitEffectBySize("StarPieceLight", "StarPieceLightS");
        MR::onBind(this);
        MR::invalidateClipping(this);
        MR::validateShadow(this, nullptr);
        MR::onCalcShadow(this, nullptr);
        MR::onCalcShadowDropGravity(this, nullptr);
        mFallKillTimer = 0;
    }

    tryCalcGravity();
    mVelocity *= cFallDamp;
    mVelocity += mGravity * (mFlags.InWater ? cWaterFallGravity : cFallGravity);
    spin();

    if (MR::reboundVelocityFromCollision(this, cReboundBounce, cNum_exeFall_reboundVelocityFromCollision_1, cNum_exeFall_reboundVelocityFromCollision_2)) {
        if (MR::isBindedGround(this) && !MR::isBindedWall(this) && !MR::isBindedRoof(this)) {
            mVelocity.scale(mGravity.dot(mVelocity), mGravity);
        }

        MR::startBck(this, "Land");

        if (!MR::isDemoActive()) {
            startSoundByWater("SE_OJ_STAR_PIECE_HIT", "SE_OJ_STAR_PIECE_HIT_W");
        }

        f32 landSpeedMin = mFlags.InWater ? cWaterLandSpeedMin : cLandSpeedMin;

        if (mVelocity.squared() < landSpeedMin * landSpeedMin) {
            mVelocity.setLength(landSpeedMin);
        }
    }

    if (!MR::isDemoActive()) {
        mFallKillTimer++;

        if (mFallKillTimer > cFallKillSteps) {
            kill();
            return;
        }
    }

    if (isNerve(GET_NERVE(StarPiece, HostTypeNrvFallAfterReflect))) {
        if (MR::isGreaterStep(this, cReflectFallSteps)) {
            kill();
        }

        return;
    }

    tryGotJudge();
}

bool StarPiece::isTouchToTarget(Vec3f* pToTarget, f32 height) {
    const Vec3f* pTarget = mFlags.isGoToPlayer ? MR::getPlayerCenterPos() : &mTargetSensor->mPosition;

    Vec3f toTarget(*pTarget);
    toTarget -= mGravity * height;
    toTarget -= mPosition;

    if (pToTarget != nullptr) {
        pToTarget->set(toTarget);
    }

    f32 touchDistance = mFlags.isGoToPlayer ? cPlayerTouchDistance : mTargetSensor->mRadius + getSensor("body")->mRadius;
    return toTarget.length() < touchDistance;
}

bool StarPiece::isEffectLight() {
    return mFlags.isForceEffectLight || cEffectFullLaunchedMax < MR::getStarPieceDirector()->mNumStarPiecesLaunched;
}

void StarPiece::exeToTarget() {
    if (MR::isFirstStep(this)) {
        if (mFlags.isGoToPlayer) {
            startSoundByWater("SE_OJ_STAR_PIECE_FLY", "SE_OJ_STAR_PIECE_FLY_W");
        }

        MR::offBind(this);
        MR::invalidateClipping(this);
        mVelocity.zero();
        mTurnRate = cTurnRateStart;
        mChaseRate = 0.0f;
        MR::forceDeleteEffect(this, "StarPieceFlyingBlur");
        MR::emitEffect(this, "StarPieceGetBlur");

        if (mFlags.isGoToPlayer) {
            emitTouchEffect(*MR::getStarPointerLastPointedPort(this), cTouchEffectSize);
        } else {
            emitTouchEffect(0, cTouchEffectSize);
        }

        MR::invalidateShadow(this, nullptr);
    }

    tryCalcGravity();
    spin();

    if (!mFlags.isGoToPlayer) {
        mScale += (Vec3f(cTargetScale, cTargetScale, cTargetScale) - mScale) * cTargetScaleRate;
        changeScale(mScale.y);
    }

    Vec3f toTarget;

    if (isTouchToTarget(&toTarget, 0.0f)) {
        if (mFlags.isGoToPlayer) {
            touchPlayer();
            return;
        }

        if (mReceiverInfo != nullptr) {
            mReceiverInfo->mGotCount += mNumGift;
            mReceiverInfo->mNumFlying -= mNumGift;
        }

        MR::sendMsgStarPieceGift(mTargetSensor, getSensor("attack"), mNumGift);
        changeScale(1.0f);
        MR::emitEffectHit(this, getSensor("attack")->mPosition, "InvalidHitMark");
        makeActorDead();
        return;
    }

    if (mFlags.isGoToPlayer && MR::isResetChasingStarPiece()) {
        kill();
        return;
    }

    mTurnRate = ::approachOne(mTurnRate, cTurnRateStep);
    mChaseRate = ::approachOne(mChaseRate, cChaseRateStep);

    MR::normalizeOrZero(&toTarget);
    Quat4f rotateQuat;
    rotateQuat.setRotate(mFront, toTarget, mTurnRate);
    rotateQuat.rotate(mFront);

    MR::normalizeOrZero(&mFront);

    Vec3f chaseVelocity(mFront);
    chaseVelocity *= mChaseRate * cChaseSpeed + 1.0f;
    mVelocity.set(chaseVelocity);

    const Vec3f* pTargetVelocity = mFlags.isGoToPlayer ? MR::getPlayerVelocity() : &mTargetSensor->mHost->mVelocity;

    Vec3f targetDir(*pTargetVelocity);
    MR::normalizeOrZero(&targetDir);

    if (!MR::isNearZero(targetDir)) {
        f32 dot = mFront.dot(targetDir);

        // Keep pace with a target moving away.
        if (0.0f < dot) {
            mVelocity += *pTargetVelocity * mChaseRate * dot;
        }
    }
}

void StarPiece::exeSpinDrained() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::offBind(this);
        MR::onCalcShadow(this, nullptr);
        MR::onCalcShadowDropPrivateGravity(this, nullptr);
        MR::emitEffect(this, "StarPieceFlyingBlur");
        startSoundByWater("SE_OJ_STAR_PIECE_FLY", "SE_OJ_STAR_PIECE_FLY_W");
    }

    tryCalcGravity();
}

void StarPiece::exeToPlayerEnd() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::invalidateShadow(this, nullptr);

        mThrowEnd.set(mPosition);
        mThrowEnd -= (*MR::getPlayerCenterPos());
        mVelocity.zero();

        MR::deleteEffect(this, "StarPieceGetBlur");

        if (mHostInfo != nullptr) {
            mHostInfo->mNumGot++;
            mHostInfo->mNumAppeared--;
            mHostInfo = nullptr;
        }
    }

    f32 newScale = 1.0f - MR::calcNerveRate(this, cToPlayerEndShrinkSteps);
    changeScale(newScale);
    mPosition.set(*MR::getPlayerCenterPos());
    mPosition += mThrowEnd * newScale;

    if (MR::isGreaterStep(this, cToPlayerEndSteps)) {
        getSensor("body")->validate();
        changeScale(1.0f);
        makeActorDead();
    }
}

void StarPiece::exeThrow() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
        MR::offBind(this);
        MR::setBinderRadius(this, cThrowBinderRadius);
        MR::setBindTriangleFilter(this, mDelegator);

        getSensor("attack")->validate();
        MR::emitEffect(this, "StarPieceFlyingBlur");
        MR::invalidateClipping(this);

        mGravity.set(*MR::getPlayerGravity());

        MR::validateShadow(this, nullptr);
        MR::onCalcShadow(this, nullptr);
        MR::onCalcShadowDropPrivateGravity(this, nullptr);
    }

    spin();

    Vec3f prevPos(mPosition);
    Vec3f nextPos(0, 0, 0);
    f32 rate = getNerveStep() / static_cast< f32 >(cThrowSteps);
    Vec3f throwDir;

    throwDir.set(mThrowEnd);
    throwDir -= mThrowStart;

    f32 arcHeight = cThrowArcHeight;
    nextPos = mThrowStart + throwDir * rate + (-mGravity) * arcHeight;

    if (mTargetSensor != nullptr) {
        f32 homing = MR::calcNerveRate(this, cThrowSteps);
        nextPos = mTargetSensor->mPosition * homing + nextPos * (1.0f - homing);
    }

    mVelocity.set(nextPos);
    mVelocity -= prevPos;

    Vec3f camZDir(MR::getCamZdir());
    Vec3f camPos(MR::getCamPos());
    Vec3f camToPiece(mPosition);
    Vec3f camToPlayer(*MR::getPlayerCenterPos());

    camToPiece -= camPos;
    camToPlayer -= camPos;

    // Collision starts once the piece is far off, past the player, or near the end.
    if (MR::isNoBind(this)) {
        if (cThrowBindCameraDistance < MR::getCamPos().distance(mPosition) || camZDir.dot(camToPlayer) < camZDir.dot(camToPiece) ||
            MR::isGreaterStep(this, cThrowBindStep)) {
            MR::onBind(this);
        }
    }

    if (tryHitBinded()) {
        return;
    }

    if (MR::isGreaterStep(this, cThrowSteps)) {
        setNerve(GET_NERVE(StarPiece, HostTypeNrvThrowFall));
    }
}

void StarPiece::endThrow() {
    getSensor("attack")->invalidate();
    MR::onBind(this);
    MR::setBinderRadius(this, cBinderRadius);
    MR::setBindTriangleFilter(this, nullptr);
}

void StarPiece::exeThrowFall() {
    if (MR::isFirstStep(this)) {
        getSensor("attack")->validate();
        MR::invalidateClipping(this);
        MR::onCalcShadow(this, nullptr);
        MR::onCalcShadowDropPrivateGravity(this, nullptr);
    }

    tryCalcGravity();
    mVelocity *= cFallDamp;
    mVelocity += mGravity * cFallGravity;
    spin();

    if (tryHitBinded()) {
        return;
    }

    if (MR::isGreaterStep(this, cThrowFallSteps)) {
        kill();
    }
}

void StarPiece::exeFollowPlayer() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::emitEffect(this, "StarPieceFlyingBlur");
        MR::offBind(this);
        MR::invalidateShadow(this, nullptr);
        MR::offCalcShadow(this, nullptr);
    }

    tryGotJudge();
}

void StarPiece::exeRailMove() {
    if (MR::isFirstStep(this)) {
        MR::offBind(this);
        MR::invalidateClipping(this);

        if (!mFlags.isNoShadow) {
            MR::validateShadow(this, nullptr);
        } else {
            MR::invalidateShadow(this, nullptr);
        }

        MR::onCalcShadow(this, nullptr);
        MR::onCalcShadowDropPrivateGravity(this, nullptr);
    }

    tryCalcGravity();
    spin();
    tryGotJudge();
}

void StarPiece::setFollowPlayerAndAppear() {
    appear();
    MR::invalidateClipping(this);
    setNerve(GET_NERVE(StarPiece, HostTypeNrvFollowPlayer));
}

bool StarPiece::tryCalcGravity() {
    Vec3f gravity;
    MR::calcGravityVectorOrZero(this, &gravity, nullptr, 0);

    if (!MR::isNearZero(gravity)) {
        mGravity.set(gravity);
        return true;
    }

    return false;
}

void StarPiece::trySetGravityAndFront(const Vec3f& rGravity) {
    if (!tryCalcGravity()) {
        mGravity.set(rGravity);
        MR::normalizeOrZero(&mGravity);
    }

    // Swapping y and z gives a front that is not along gravity.
    if (MR::isSameDirection(mGravity, mFront)) {
        mFront.set< f32 >(mGravity.x, mGravity.z, mGravity.y);
    }
}

bool StarPiece::throwToTarget(HitSensor* pSensor, const Vec3f& rStart, const Vec3f& rGravity, f32 speed) {
    throwToTargetCore(pSensor->mPosition, rStart, rGravity, speed, true);
    mTargetSensor = pSensor;
    return true;
}

bool StarPiece::throwToTargetCore(const Vec3f& rEnd, const Vec3f& rStart, const Vec3f& rGravity, f32 /*speed*/, bool isFall) {
    mGravity.set(rGravity);
    mThrowEnd.set(rEnd);
    mThrowStart.set(rStart);

    MR::showModel(this);
    MR::resetPosition(this, mThrowStart);
    MR::invalidateClipping(this);
    makeActorAppeared();

    if (isFall) {
        setNerve(GET_NERVE(StarPiece, HostTypeNrvThrow));
    } else {
        setNerve(GET_NERVE(StarPiece, HostTypeNrvThrowNoFall));
    }

    MR::addStarPiece(-1);
    return true;
}

void StarPiece::giftToTarget(StarPieceReceiverInfo* pReceiverInfo, u32 numGift, HitSensor* pSensor, const Vec3f& rPosition) {
    mReceiverInfo = pReceiverInfo;
    mNumGift = numGift;
    mFlags.isGoToPlayer = false;
    mTargetSensor = pSensor;
    mPosition.set(rPosition);
    mFront.set(pSensor->mPosition - rPosition);
    MR::normalizeOrZero(&mFront);

    MR::showModel(this);
    MR::resetPosition(this);
    appear();
    MR::invalidateClipping(this);
    setNerve(GET_NERVE(StarPiece, HostTypeNrvToTarget));
    changeScale(numGift != 1 ? cGiftManyScale : 1.0f);
    MR::addStarPiece(-numGift);
}

void StarPiece::goToPlayer(Vec3f velocity) {
    mFront.set(mVelocity);
    MR::normalizeOrZero(&mFront);

    if (MR::isNearZero(mFront)) {
        MR::calcFrontVec(&mFront, this);
    }

    if (isNerve(GET_NERVE(StarPiece, HostTypeNrvFloating))) {
        emitEffectBySize("StarPieceLight", "StarPieceLightS");
    } else if (isNerve(GET_NERVE(StarPiece, HostTypeNrvFollowPlayer))) {
        f32 dot = mFront.dot(mGravity);

        if (0.0f < dot) {
            mFront -= mGravity * dot * cNum_goToPlayer_f1;
        }
    }

    mFlags.isGoToPlayer = true;
    mGettableDelayCounter = cGoToPlayerDelay;
    mVelocity.set(velocity);
    mFront.set(velocity);

    MR::invalidateClipping(this);
    setNerve(GET_NERVE(StarPiece, HostTypeNrvToTarget));
}

void StarPiece::launch(const Vec3f& rPosition, f32 randomRange, f32 speed, bool notCheckInWater, bool isForceEffectLight) {
    mPosition.set(rPosition);
    tryCalcGravity();

    if (MR::isNearZero(mGravity)) {
        MR::calcUpVec(&mGravity, this);
        mGravity *= -1.0f;
        trySetGravityAndFront(mGravity);
    }

    Vec3f randVec(MR::getRandom(-randomRange, randomRange), MR::getRandom(-randomRange, randomRange), MR::getRandom(-randomRange, randomRange));
    mVelocity.set(-mGravity * speed);
    mVelocity += randVec;
    launchCommon(rPosition, notCheckInWater, isForceEffectLight);
}

void StarPiece::launch(const Vec3f& rPosition, const Vec3f& rUp, f32 randomRange, f32 speed, bool notCheckInWater, bool isForceEffectLight) {
    mPosition.set(rPosition);
    trySetGravityAndFront(-rUp);

    Vec3f randVec(MR::getRandom(-randomRange, randomRange), MR::getRandom(-randomRange, randomRange), MR::getRandom(-randomRange, randomRange));
    mVelocity.set(rUp * speed);
    mVelocity += randVec;
    launchCommon(rPosition, notCheckInWater, isForceEffectLight);
}

void StarPiece::launch(const Vec3f& rPosition, const Vec3f& rVelocity, bool notCheckInWater, bool isForceEffectLight) {
    mPosition.set(rPosition);
    trySetGravityAndFront(rVelocity);
    mVelocity.set(rVelocity);
    launchCommon(rPosition, notCheckInWater, isForceEffectLight);
}

void StarPiece::launchCommon(const Vec3f& rPosition, bool notCheckInWater, bool isForceEffectLight) {
    if (notCheckInWater) {
        mFlags.InWater = false;
    } else {
        mFlags.InWater = MR::isInWater(mPosition);

        if (mFlags.InWater) {
            mVelocity *= cWaterLaunchScale;
        }
    }

    MR::resetPosition(this, rPosition);
    appear();
    setNerve(GET_NERVE(StarPiece, HostTypeNrvFall));
    MR::invalidateClipping(this);

    mGettableDelayCounter = cLaunchGettableDelay;
    mFlags.isForceEffectLight = isForceEffectLight;
    MR::getStarPieceDirector()->mNumStarPiecesLaunched++;

    mFlags.isLaunched = true;
}

f32 StarPiece::calcEffectScale(f32 screenSize, f32 minScale, bool isFitScreen) {
    f32 pointRadius2D = MR::calcPointRadius2D(mPosition, cPointRadius);

    if (isFitScreen || pointRadius2D * minScale < screenSize) {
        return screenSize / pointRadius2D;
    }

    return minScale;
}

f32 StarPiece::calcNearCameraScale() {
    f32 pointRadius2D = MR::calcPointRadius2D(mPosition, cPointRadius);

    if (cNearCameraScreenRadius < pointRadius2D) {
        return cNearCameraScreenRadius / pointRadius2D;
    }

    return 1.0f;
}

f32 StarPiece::calcDistToCamera() {
    Vec3f dirToCamera(getSensor("body")->mPosition);
    dirToCamera.sub(MR::getCamPos());
    return dirToCamera.length();
}

void StarPiece::emitTouchEffect(s32 port, f32 screenSize) {
    f32 effectScale = calcEffectScale(screenSize, cTouchEffectScale, false);

    if (isAwayFromCamera()) {
        MR::emitEffectWithScale(this, "StarPieceTouchLight", effectScale, -1);
        MR::setEffectPrmColor(this, "StarPieceTouchLight", cTouchColors[port].r, cTouchColors[port].g, cTouchColors[port].b);
    }
}

void StarPiece::emitGettableEffect(f32 screenSize) {
    f32 effectScale = calcEffectScale(screenSize, cGettableEffectScale, true);

    if (isAwayFromCamera()) {
        MR::emitEffectWithScale(this, "GetAble", effectScale, -1);
    }

    MR::setEffectColor(this, "GetAble", cGettablePrimary, cGettablePrimary, cGettablePrimary, ::lighten(mColor.r), ::lighten(mColor.g),
                       ::lighten(mColor.b));
}

void StarPiece::tryGotJudge() {
    if (cGettableDistance * cGettableDistance < (*MR::getPlayerCenterPos() - mPosition).squared() || !mFlags.isGettable) {
        mGettableEffectStep = -1;
        return;
    }

    mGettableEffectStep = (mGettableEffectStep + 1) % cGettableEffectPeriod;

    if (mGettableEffectStep == cGettableEffectStep) {
        emitGettableEffect(cGettableEffectSize);
    }

    if (mGettableDelayCounter < 0 && MR::isStarPointerPointing1Por2P(this, "弱", false, false)) {
        goToPlayer(Vec3f(mFront));
    }
}

bool StarPiece::isFollowing() {
    return isNerve(GET_NERVE(StarPiece, HostTypeNrvFollowPlayer));
}

bool StarPiece::setFall() {
    if (MR::isDead(this)) {
        return false;
    }

    if (isNerve(GET_NERVE(StarPiece, HostTypeNrvRailMove)) || isNerve(GET_NERVE(StarPiece, HostTypeNrvFloating))) {
        mFlags.isGettable = true;
        setNerve(GET_NERVE(StarPiece, HostTypeNrvFall));
        return true;
    }

    return false;
}

bool StarPiece::isOnRailMove() {
    return !MR::isDead(this) && isNerve(GET_NERVE(StarPiece, HostTypeNrvRailMove));
}

bool StarPiece::isFloat() {
    return !MR::isDead(this) && isNerve(GET_NERVE(StarPiece, HostTypeNrvFloating));
}

void StarPiece::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEye(pSender) || !isNerveThrow()) {
        return;
    }

    if (MR::sendArbitraryMsg(ACTMES_IS_STAR_PIECE_REFLECT, pReceiver, pSender)) {
        Vec3f dirReceiverToSender(pSender->mPosition - pReceiver->mPosition);
        MR::normalizeOrZero(&dirReceiverToSender);

        Vec3f hitPos(pReceiver->mPosition);
        hitPos.add(dirReceiverToSender * pReceiver->mRadius);
        setReflect(dirReceiverToSender, hitPos);
        return;
    }

    if (MR::sendMsgStarPieceAttack(pReceiver, pSender)) {
        MR::startSound(this, "SE_OJ_STAR_PIECE_HIT_ENEMY");
        kill();
    }
}

void StarPiece::setReflect(const Vec3f& rNormal, const Vec3f& rHitPos) {
    if (!MR::isNearZero(rNormal)) {
        MR::calcReflectionVector(&mVelocity, rNormal, cReflectElasticity, 0.0f);
    }

    mVelocity.add(mGravity * -cReflectBounceSpeed);

    if (isAwayFromCamera()) {
        MR::emitEffectHit(this, rHitPos, "InvalidHitMark");
    }

    startSoundByWater("SE_OJ_STAR_PIECE_REFLECT", "SE_OJ_STAR_PIECE_REFLECT_W");
    setNerve(GET_NERVE(StarPiece, HostTypeNrvFallAfterReflect));
}

bool StarPiece::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgInhaleBlackHole(msg) && isNerveFall()) {
        kill();
        return true;
    }

    if (MR::isMsgItemGet(msg)) {
        if (isNerveThrow()) {
            return false;
        }

        if (isNerve(GET_NERVE(StarPiece, HostTypeNrvToPlayerEnd))) {
            return true;
        }

        if (mFlags.isGoToPlayer && isNerve(GET_NERVE(StarPiece, HostTypeNrvToTarget))) {
            return true;
        }

        if (isNerve(GET_NERVE(StarPiece, HostTypeNrvFloating)) || isNerve(GET_NERVE(StarPiece, HostTypeNrvFall)) ||
            isNerve(GET_NERVE(StarPiece, HostTypeNrvSpinDrained)) || isNerve(GET_NERVE(StarPiece, HostTypeNrvRailMove))) {
            if (mGettableDelayCounter < 0) {
                touchPlayer();
                MR::notifyDirectGetStarPiecePlayer();
            }

            return true;
        }
    }

    if (MR::isMsgItemPull(msg) && mGettableDelayCounter < 0) {
        if (isNerve(GET_NERVE(StarPiece, HostTypeNrvSpinDrained)) || isNerve(GET_NERVE(StarPiece, HostTypeNrvToPlayerEnd)) ||
            isNerve(GET_NERVE(StarPiece, HostTypeNrvToTarget))) {
            return false;
        }

        mTargetSensor = pSender;
        mFlags.isGoToPlayer = MR::isSensorPlayer(mTargetSensor);
        setNerve(GET_NERVE(StarPiece, HostTypeNrvSpinDrained));
        return true;
    }

    if (MR::isMsgStartPowerStarGet(msg)) {
        if ((isNerve(GET_NERVE(StarPiece, HostTypeNrvToTarget)) && mFlags.isGoToPlayer) || isNerve(GET_NERVE(StarPiece, HostTypeNrvSpinDrained)) ||
            isNerve(GET_NERVE(StarPiece, HostTypeNrvToPlayerEnd))) {
            MR::gotByPlayer();
        }

        makeActorDead();
        return true;
    }

    return false;
}

bool StarPiece::isIgnoreTriOnThrow(const Triangle* pTriangle) {
    return 0.0f <= pTriangle->getFaceNormal()->dot(mVelocity);
}

bool StarPiece::touchPlayer() {
    if (MR::gotByPlayer()) {
        if (isAwayFromCamera()) {
            emitEffectBySize("StarPieceGet", "StarPieceGetS");
        }

        MR::tryRumblePadMiddle(this, PLAYER_1);
    }

    MR::stopSound(this, "SE_OJ_STAR_PIECE_FLY_W");
    MR::stopSound(this, "SE_OJ_STAR_PIECE_FLY");

    MR::forceDeleteEffect(this, "StarPieceFlyingBlur");
    setNerve(GET_NERVE(StarPiece, HostTypeNrvToPlayerEnd));
    getSensor("body")->invalidate();
    return true;
}

void StarPiece::calcAndSetBaseMtx() {
    Pos3f pos;

    if (MR::isNearZero(mGravity) || MR::isNearZero(mFront) || MR::isSameDirection(mGravity, mFront)) {
        pos.set(getBaseMtx());
        pos.setTrans(mPosition);
    } else {
        MR::calcMtxFromGravityAndZAxis(&pos, this, mGravity, mFront);
    }

    Pos3f rotate;
    rotate.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), MR::toRadian(mRotation.y));
    pos.concat(pos, rotate);
    MR::setBaseTRMtx(this, pos);
}

void StarPiece::setHostInfo(StarPieceHostInfo* pHostInfo) {
    mHostInfo = pHostInfo;
}

bool StarPiece::isNerveFall() const {
    return isNerve(GET_NERVE(StarPiece, HostTypeNrvFall)) || isNerve(GET_NERVE(StarPiece, HostTypeNrvFallAfterReflect));
}

bool StarPiece::isNerveThrow() const {
    return isNerve(GET_NERVE(StarPiece, HostTypeNrvThrow)) || isNerve(GET_NERVE(StarPiece, HostTypeNrvThrowNoFall)) ||
           isNerve(GET_NERVE(StarPiece, HostTypeNrvThrowFall));
}

bool StarPiece::isAwayFromCamera() {
    return cEffectCameraDistance < calcDistToCamera();
}

bool StarPiece::tryHitBinded() {
    if (!MR::isBinded(this)) {
        return false;
    }

    if (MR::sendMsgToBindedSensor(ACTMES_IS_STAR_PIECE_REFLECT, this, getSensor("attack"))) {
        setReflect(*MR::getBindedNormal(this), *MR::getBindedHitPos(this));
        return true;
    }

    if (MR::sendMsgToBindedSensor(ACTMES_STAR_PIECE_ATTACK, this, getSensor("attack"))) {
        MR::startSound(this, "SE_OJ_STAR_PIECE_HIT_ENEMY");
    }

    kill();
    return true;
}

void StarPiece::spin() {
    mRotation.y += cSpinSpeed;
    MR::repeatDegree(&mRotation.y);
}

void StarPiece::startSoundByWater(const char* pName, const char* pWaterName) {
    if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        MR::startSound(this, pWaterName);
    } else {
        MR::startSound(this, pName);
    }
}

void StarPiece::emitEffectBySize(const char* pName, const char* pSmallName) {
    if (isEffectLight()) {
        MR::emitEffect(this, pSmallName);
    } else {
        MR::emitEffect(this, pName);
    }
}
