#include "Game/Enemy/CocoNutBall.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/CocoNut.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcHitBackVelocityAndGravity_f1, "calcHitBackVelocityAndGravity.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcHitBackVelocityAndGravity_f2, "calcHitBackVelocityAndGravity.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcHitBackVelocityAndGravity_f3, "calcHitBackVelocityAndGravity.f3", 2.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitBackToHost_setBckRate_1, "exeHitBackToHost.setBckRate.1", 1.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeHitBackToHost_1, "exeHitBackToHost.1", 1);
NEBULA_TUNABLE_NAMED(s32, cStep_exeHitBackToHost_2, "exeHitBackToHost.2", 2);
NEBULA_TUNABLE_NAMED(s32, cStep_exeHitBackToHost_3, "exeHitBackToHost.3", 1);

namespace {
    NEBULA_TUNABLE_RECORD(Vec, cReboundVelocity, {0.0f, 15.0f, 5.0f});
    NEBULA_TUNABLE(f32, cBodyRadius, 40.0f);
    NEBULA_TUNABLE(f32, cBindRadius, 500.0f);
    NEBULA_TUNABLE(u16, cSensorGroupSize, 8);
    NEBULA_TUNABLE(f32, cBinderRadius, 60.0f);
    NEBULA_TUNABLE(f32, cPointerRadius, 150.0f);
    NEBULA_TUNABLE(f32, cShadowRadius, 60.0f);
    NEBULA_TUNABLE(f32, cDefaultHeightMax, 450.0f);
    NEBULA_TUNABLE(f32, cDefaultKillDistance, 10000.0f);
    NEBULA_TUNABLE(f32, cThrowSpeed, 15.0f);
    NEBULA_TUNABLE(f32, cRallyAngleMin, 12.5f);
    NEBULA_TUNABLE(f32, cRallyAngleMax, 17.5f);
    NEBULA_TUNABLE(f32, cRallySpeedPerHit, 3.0f);
    NEBULA_TUNABLE(f32, cPunchDistance, 400.0f);
    // Horizontal speed of a hit back; the flight time follows from the distance.
    NEBULA_TUNABLE(f32, cHitBackSpeed, 42.0f);
    NEBULA_TUNABLE(f32, cHitBackGravityScale, 2.2f);
    NEBULA_TUNABLE(f32, cHitBackCurve, 1.2f);
    // The player is in line between the host and the ball below this dot.
    NEBULA_TUNABLE(f32, cHitBackFrontDot, -0.95f);
    NEBULA_TUNABLE(f32, cHitBackSideOffset, 150.0f);
    NEBULA_TUNABLE(f32, cHitBackHeight, 100.0f);
    NEBULA_TUNABLE(f32, cAimHeight, 120.0f);
    // Height-limited aim: the step raised past a clear line, the probe length scale, and the step otherwise.
    NEBULA_TUNABLE(f32, cAimClearRaise, 120.0f);
    NEBULA_TUNABLE(f32, cAimProbeScale, 1.5f);
    NEBULA_TUNABLE(f32, cAimRaiseStep, 60.0f);
    NEBULA_TUNABLE(f32, cNearClipDistance, 300.0f);
    NEBULA_TUNABLE(s32, cFreezableStep, 20);
    NEBULA_TUNABLE(f32, cFarFromPlayer, 5000.0f);
    NEBULA_TUNABLE(s32, cHitBackStopSceneFrames, 6);
    NEBULA_TUNABLE(f32, cReboundBckRate, 0.25f);
    NEBULA_TUNABLE(f32, cReboundGravityScale, 2.0f);
    NEBULA_TUNABLE(f32, cFreezeShakeSpeed, 75.0f);
    NEBULA_TUNABLE(f32, cFreezeShakeWidth, 7.5f);
    NEBULA_TUNABLE(s32, cFreezeSteps, 20);
};  // namespace

namespace NrvCocoNutBall {
    NEW_NERVE(CocoNutBallNrvThrow, CocoNutBall, Throw);
    NEW_NERVE(CocoNutBallNrvHitBackToHost, CocoNutBall, HitBackToHost);
    NEW_NERVE(CocoNutBallNrvHitBackToPlayer, CocoNutBall, HitBackToPlayer);
    NEW_NERVE(CocoNutBallNrvRebound, CocoNutBall, Rebound);
    NEW_NERVE(CocoNutBallNrvFreeze, CocoNutBall, Freeze);
    NEW_NERVE(CocoNutBallNrvFreezeRelease, CocoNutBall, FreezeRelease);
};  // namespace NrvCocoNutBall

CocoNutBall::CocoNutBall(const char* pName)
    : LiveActor(pName), mHost(), mHitBackGravity(0.0f, -1.0f, 0.0f), mRallyNum(), mFreezeStep(), mFreezePos(gZeroVec), mFreezeVelocity(gZeroVec), mIsThrowRight(),
      mIsHeightLimited(), mHeightMax(cDefaultHeightMax), mHeightMin(), mUp(0.0f, 1.0f, 0.0f), mKillDistance(cDefaultKillDistance), mIsRallyBgm() {
    lumacore::setClass(this, lumacore::kClassId< CocoNutBall >);
}

void CocoNutBall::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm(CocoNut::getModelName(), nullptr, false);
    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);
    MR::initLightCtrl(this);
    initHitSensor(2);
    MR::addHitSensor(this, "body", ATYPE_COCO_NUT, cSensorGroupSize, cBodyRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, "bind", ATYPE_COCO_NUT, cSensorGroupSize, cBindRadius, Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(cBinderRadius, 0.0f, 0);
    initEffectKeeper(0, "CocoNut");
    MR::initStarPointerTarget(this, cPointerRadius, Vec3f(0, 0, 0));
    initSound(4, false);
    MR::initShadowVolumeCylinder(this, cShadowRadius);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvThrow));
    makeActorDead();
}

void CocoNutBall::appear() {
    LiveActor::appear();
    mRallyNum = 0;
    MR::onBind(this);
    setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvThrow));
}

void CocoNutBall::kill() {
    MR::forceDeleteEffect(this, "CocoNutBlur");
    MR::forceDeleteEffect(this, "CocoNutLight");
    MR::startSound(this, "SE_OJ_COCONUT_BALL_BREAK");

    if (mRallyNum > 0) {
        MR::sendArbitraryMsg(ACTMES_RUSH_END, mHost->getSensor("body"), getSensor("body"));
    }

    LiveActor::kill();
    MR::emitEffect(this, CocoNut::getBreakEffectName());
}

void CocoNutBall::appearAndThrow(const Vec3f& rAppearPos, f32 angleDegree) {
    mPosition.set(rAppearPos);
    MR::calcGravity(this);
    mUp.negate(mGravity);
    setVelocityToPlayer(cThrowSpeed, angleDegree);
    mIsThrowRight = MR::isHalfProbability();
    appear();
}

void CocoNutBall::hitBackToPlayer() {
    f32 angle = MR::getRandom(cRallyAngleMin, cRallyAngleMax);

    // Rally returns alternate sides.
    if (mIsThrowRight) {
        angle = -angle;
        mIsThrowRight = false;
    } else {
        mIsThrowRight = true;
    }

    setVelocityToPlayer(cRallySpeedPerHit * mRallyNum + cThrowSpeed, angle);
    setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvHitBackToPlayer));
}

void CocoNutBall::demoBreak(const Vec3f& rPos) {
    mPosition.set(rPos);
    mRotation.zero();
    mVelocity.zero();

    Pos3f TRMtx;
    MR::makeMtxTR(TRMtx.toMtxPtr(), this);
    MR::setBaseTRMtx(this, TRMtx);

    MR::emitEffect(this, "Hit");
    MR::startSound(this, "SE_OJ_COCONUT_BALL_BREAK");
    MR::emitEffect(this, CocoNut::getBreakEffectName());
}

void CocoNutBall::calcAndSetBaseMtx() {
    if (!MR::isNearZero(mVelocity)) {
        Vec3f velocityDirection;
        MR::normalize(mVelocity, &velocityDirection);
        Pos3f baseTRMtx;
        MR::makeMtxFrontUpPos(&baseTRMtx, velocityDirection, mUp, mPosition);
        MR::setBaseTRMtx(this, baseTRMtx);
    }
}

bool CocoNutBall::isSensorBody(HitSensor* pSensor) const {
    return pSensor == getSensor("body");
}

void CocoNutBall::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isSensorBody(pSender)) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver) && isNerveThrowToOrFreeze() && MR::sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FLIP_VERYWEAK, pReceiver, pSender)) {
        setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvRebound));
        return;
    }

    if (!MR::isSensorEnemy(pReceiver)) {
        return;
    }

    if (pReceiver->mHost == mHost) {
        if (isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvHitBackToHost)) && MR::sendMsgEnemyAttack(pReceiver, pSender)) {
            MR::emitEffect(this, "Hit");
            kill();
        }

        return;
    }

    if (isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvThrow)) || isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvHitBackToHost)) ||
        isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvHitBackToPlayer))) {
        if (MR::sendMsgEnemyAttack(pReceiver, pSender)) {
            kill();
        }
    }
}

bool CocoNutBall::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isMsgPlayerSpinAttack(msg)) {
        if (isValidReceivePunch()) {
            setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvHitBackToHost));
            return true;
        }

        return false;
    }

    return isSensorBody(pReceiver) && MR::isMsgStarPieceReflect(msg);
}

HitSensor* CocoNutBall::isBindedAny() const {
    if (MR::isBindedGround(this)) {
        return MR::getGroundSensor(this);
    }

    if (MR::isBindedWall(this)) {
        return MR::getWallSensor(this);
    }

    if (MR::isBindedRoof(this)) {
        return MR::getRoofSensor(this);
    }

    return nullptr;
}

bool CocoNutBall::isNerveThrowToOrFreeze() const {
    return isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvThrow)) || isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvHitBackToPlayer)) ||
           isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvFreeze)) || isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvFreezeRelease));
}

bool CocoNutBall::isValidReceivePunch() const {
    if (MR::isDead(this) || (!isNerveThrowToOrFreeze() && !isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvRebound)))) {
        return false;
    }

    return mPosition.distance(*MR::getPlayerCenterPos()) < cPunchDistance;
}

void CocoNutBall::calcHitBackVelocityAndGravity() {
    bool hitBackRight = isHitBackRight();
    bool hitBackFront = isHitBackFront();
    Vec3f hitBackDstPos;
    calcHitBackDstPos(&hitBackDstPos, hitBackRight, hitBackFront);

    Vec3f dir;
    dir.sub(hitBackDstPos, mPosition);
    Vec3f side;
    Vec3f rise;
    rise.scale(mUp.dot(dir), mUp);
    MR::vecKillElement(dir, mUp, &dir);
    f32 flightSteps = dir.length() / cHitBackSpeed;
    MR::normalize(&dir);
    side.cross(mUp, dir);
    MR::normalize(&side);
    mHitBackGravity.scale(cHitBackGravityScale, mGravity);
    Vec3f horizontal;
    horizontal.scale(cHitBackSpeed, dir);

    // The vertical speed that lands the rise in flightSteps under mHitBackGravity.
    Vec3f verticalVelocity;
    verticalVelocity = (rise.scaleInline(cNum_calcHitBackVelocityAndGravity_f1) - mHitBackGravity.scaleInline(flightSteps).scaleInline(flightSteps)).scaleInline(1.0f / (cNum_calcHitBackVelocityAndGravity_f2 * flightSteps));
    mVelocity.add(horizontal, verticalVelocity);

    // Off-line hits curve: a side speed that a side pull cancels over the flight.
    if (!hitBackFront) {
        f32 curve = (hitBackRight ? cHitBackCurve : -cHitBackCurve);
        Vec3f sideVelocity;
        sideVelocity.scale(curve * flightSteps * flightSteps / (flightSteps * cNum_calcHitBackVelocityAndGravity_f3), side);
        mVelocity.add(sideVelocity);
        sideVelocity.scale(curve, side);
        mHitBackGravity.sub(sideVelocity);
    }
}

bool CocoNutBall::isHitBackRight() const {
    Vec3f playerToHost;
    playerToHost.sub(mHost->mPosition, *MR::getPlayerPos());

    Vec3f playerToBall;
    playerToBall.sub(mPosition, *MR::getPlayerPos());

    MR::vecKillElement(playerToHost, mUp, &playerToHost);
    MR::vecKillElement(playerToBall, mUp, &playerToBall);

    MR::normalize(&playerToHost);
    MR::normalize(&playerToBall);

    Vec3f cross = playerToHost.cross(playerToBall);

    return 0.0f < mUp.dot(cross);
}

bool CocoNutBall::isHitBackFront() const {
    Vec3f hostToPlayer;
    hostToPlayer.sub(*MR::getPlayerPos(), mHost->mPosition);

    Vec3f playerToBall;
    playerToBall.sub(mPosition, *MR::getPlayerPos());

    MR::vecKillElement(hostToPlayer, mUp, &hostToPlayer);
    MR::vecKillElement(playerToBall, mUp, &playerToBall);

    MR::normalize(&hostToPlayer);
    MR::normalize(&playerToBall);

    return hostToPlayer.dot(playerToBall) < cHitBackFrontDot;
}

void CocoNutBall::calcHitBackDstPos(Vec3f* pOut, bool isRight, bool isFront) {
    Vec3f offset(0.0f, 0.0f, 0.0f);
    if (!isFront) {
        Vec3f side;
        Vec3f playerToHost;

        playerToHost.sub(mHost->mPosition, *MR::getPlayerPos());

        MR::vecKillElement(playerToHost, mUp, &playerToHost);
        MR::normalize(&playerToHost);

        side.cross(mUp, playerToHost);
        MR::vecKillElement(side, mUp, &side);
        MR::normalize(&side);

        offset.scale(isRight ? cHitBackSideOffset : -cHitBackSideOffset, side);
    }

    Vec3f height;

    height.scale(cHitBackHeight, mUp);
    offset.add(height);

    pOut->add(mHost->getSensor("body")->mPosition, offset);
}

bool CocoNutBall::tryToKill(bool alwaysKill) {
    HitSensor* bindedSensor = isBindedAny();

    if (bindedSensor != nullptr) {
        if (bindedSensor->isType(ATYPE_PUNCH_BOX) && !isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvRebound))) {
            MR::sendMsgEnemyAttack(bindedSensor, getSensor("body"));
        }

        kill();
        return true;
    }

    if (alwaysKill) {
        kill();
        return true;
    }

    return false;
}

void CocoNutBall::setVelocityToPlayer(f32 speed, f32 angleDegree) {
    Vec3f target;

    if (!mIsHeightLimited) {
        target.scale(cAimHeight, mUp);
        target.add(*MR::getPlayerPos());
    } else {
        target.set(*MR::getPlayerPos());
    }

    Pos3f rotate;
    rotate.makeRotate(mUp, MR::toRadian(angleDegree));
    Vec3f toTarget;
    toTarget.sub(target, mPosition);
    rotate.mult33(toTarget);
    target.add(mPosition, toTarget);

    // Raise the aim until the line to it (lengthened) clears the map, within the host's height band.
    if (mIsHeightLimited) {
        f32 heightMin = mHeightMin + mHost->mPosition.y;
        f32 heightMax = mHeightMax + mHost->mPosition.y;

        target.y = MR::max(target.y, heightMin);

        f32 clearRaise = cAimClearRaise;
        bool isClear = false;
        while (target.y < heightMax - clearRaise) {
            toTarget.sub(target, mPosition);
            toTarget.scale(cAimProbeScale);

            if (!MR::getFirstPolyOnLineToMap(nullptr, nullptr, mPosition, toTarget)) {
                isClear = true;
                target.y += clearRaise;
                break;
            }

            target.y += cAimRaiseStep;
        }

        if (!isClear) {
            target.y = heightMax;
        }
    }

    toTarget.sub(target, mPosition);
    mVelocity.setLength(toTarget, speed);
}

void CocoNutBall::freeze() {
    mFreezeStep = 0;
    mFreezePos.set(mPosition);
    mFreezeVelocity.set(mVelocity);
    setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvFreeze));
}

bool CocoNutBall::isFreezable() {
    return !MR::isHiddenModel(this) && MR::isStarPointerPointing2POnPressButton(this, "弱", true, false);
}

void CocoNutBall::processApproachToPlayer() {
    if (MR::changeShowModelFlagSyncNearClipping(this, cNearClipDistance)) {
        MR::emitEffect(this, "CocoNutLight");
    } else {
        MR::deleteEffect(this, "CocoNutLight");
    }

    if ((isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvFreezeRelease)) || MR::isGreaterStep(this, cFreezableStep)) && isFreezable()) {
        freeze();
        return;
    }

    tryToKill(!MR::isNear(this, mHost, mKillDistance));
}

void CocoNutBall::exeThrow() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SpinX");
        MR::emitEffect(this, "CocoNutLight");

        if (mIsRallyBgm) {
            MR::startSound(this, "SE_BM_OTAKING_SPIT_RALLY_BALL");
        } else {
            MR::startSound(this, "SE_OJ_COCONUT_BALL_SPIT_OUT");
        }
    }

    processApproachToPlayer();
}

void CocoNutBall::exeHitBackToHost() {
    if (MR::isFirstStep(this)) {
        MR::setBckRate(this, cNum_exeHitBackToHost_setBckRate_1);
        MR::deleteEffect(this, "Touch");
        MR::emitEffect(this, "CocoNutBlur");
        MR::emitEffect(this, "CocoNutLight");
        MR::emitEffect(this, "SpinHitMark");
        MR::offBind(this);
        MR::startSound(this, "SE_OJ_COCONUT_BALL_VOLLEY");
        MR::startSpinHitSound(this);

        if (mIsRallyBgm && MR::isPlayingStageBgm()) {
            if (mRallyNum == 0) {
                MR::startSystemME("ME_RALLY_COMBO_FIRST");
            } else {
                MR::startSystemME("ME_RALLY_COMBO_SECOND");
            }
        }

        mRallyNum++;
        calcHitBackVelocityAndGravity();
    }

    if (MR::isStep(this, cStep_exeHitBackToHost_1)) {
        MR::stopScene(cHitBackStopSceneFrames);
    }

    if (MR::isStep(this, cStep_exeHitBackToHost_2)) {
        MR::shakeCameraWeak();
    }

    if (MR::isGreaterEqualStep(this, cStep_exeHitBackToHost_3)) {
        mVelocity.add(mHitBackGravity);
    }

    tryToKill(!MR::isNearPlayer(this, cFarFromPlayer));
}

void CocoNutBall::exeHitBackToPlayer() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Hit");
        MR::startSound(this, "SE_OJ_COCONUT_BALL_VOLLEY");

        if (mIsRallyBgm && MR::isPlayingStageBgm()) {
            MR::startSystemME("ME_RALLY_COMBO_SECOND");
        }

        MR::onBind(this);
    }

    processApproachToPlayer();
}

void CocoNutBall::exeRebound() {
    if (MR::isFirstStep(this)) {
        MR::setBckRate(this, cReboundBckRate);
        MR::deleteEffect(this, "CocoNutBlur");
        MR::deleteEffect(this, "CocoNutLight");

        Vec3f awayFromPlayer;
        awayFromPlayer.sub(mPosition, *MR::getPlayerPos());

        Pos3f pos;
        pos.identity();

        MR::makeMtxUpFront(&pos, mUp, awayFromPlayer);

        mVelocity.set(::cReboundVelocity);

        pos.mult33(mVelocity);
    }

    Vec3f gravity;
    gravity.scale(cReboundGravityScale, mGravity);
    mVelocity.add(gravity);

    tryToKill(!MR::isNearPlayer(this, cFarFromPlayer));
}

void CocoNutBall::exeFreeze() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();

        MR::deleteEffect(this, "CocoNutBlur");
        MR::deleteEffect(this, "CocoNutLight");
        MR::emitEffect(this, "Touch");

        if (mFreezeStep == 0) {
            MR::startPointerHitSound();
        }
    }

    mFreezeStep++;
    MR::startPointerFreezeLevelSound(this);

    f32 cos = MR::cosDegree(MR::repeatDegree(mFreezeStep * cFreezeShakeSpeed));

    // The shake fades out over the release count.
    f32 scaleFactor = ((cFreezeShakeWidth * cos) * (cFreezeSteps - getNerveStep())) / static_cast< f32 >(cFreezeSteps);

    Vec3f camXDirScaled;
    camXDirScaled.set(MR::getCamXdir());
    camXDirScaled.scale(scaleFactor);
    mPosition.add(mFreezePos, camXDirScaled);

    if (MR::changeShowModelFlagSyncNearClipping(this, cNearClipDistance)) {
        MR::emitEffect(this, "Touch");
    } else {
        MR::deleteEffect(this, "Touch");
    }

    // Pointing again re-enters Freeze, restarting the release count.
    if (isFreezable()) {
        setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvFreeze));
    } else if (MR::isStep(this, cFreezeSteps)) {
        setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvFreezeRelease));
    }
}

void CocoNutBall::exeFreezeRelease() {
    if (MR::isFirstStep(this)) {
        mPosition.set(mFreezePos);
        mVelocity.set(mFreezeVelocity);
        MR::deleteEffect(this, "Touch");
        MR::emitEffect(this, "CocoNutLight");
    }

    processApproachToPlayer();
}
