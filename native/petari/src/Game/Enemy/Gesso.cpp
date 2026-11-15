#include "Game/Enemy/Gesso.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sStepForWalk = 43;
};  // namespace

namespace NrvGesso {
    NEW_NERVE(GessoNrvComeFromBox, Gesso, ComeFromBox);
    NEW_NERVE(GessoNrvWait, Gesso, Wait);
    NEW_NERVE(GessoNrvSearch, Gesso, Search);
    NEW_NERVE(GessoNrvWalkCharge, Gesso, WalkCharge);
    NEW_NERVE(GessoNrvWalk, Gesso, Walk);
    NEW_NERVE(GessoNrvSink, Gesso, Sink);
    NEW_NERVE(GessoNrvLostPlayer, Gesso, LostPlayer);
    NEW_NERVE(GessoNrvAttack, Gesso, Attack);
    NEW_NERVE(GessoNrvPunchDown, Gesso, PunchDown);
    NEW_NERVE(GessoNrvComeBack, Gesso, ComeBack);
    NEW_NERVE_ONEND(GessoNrvPointerSwoon, Gesso, PointerSwoon, PointerSwoon);
    NEW_NERVE(GessoNrvRotate, Gesso, Rotate);

};  // namespace NrvGesso

Gesso::Gesso(const char* pName)
    : LiveActor(pName), mScaleController(), mStateBindStarPointer(), mInitPos(0.0f, 0.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f), mUp(0.0f, 1.0f, 0.0f),
      mUpTarget(0.0f, 1.0f, 0.0f), mClipStep(), mPushVelocity(0.0f, 0.0f, 0.0f), mIsMarioLeft(), mIsHighSpeedMode() {
    lumacore::setClass(this, lumacore::kClassId< Gesso >);
}

void Gesso::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mInitPos = mPosition;
    MR::useStageSwitchReadAppear(this, rIter);
    initModelManagerWithAnm("Gesso", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initHitSensor(1);
    Vec3f sensorOffset;
    sensorOffset.x = 0.0f;
    sensorOffset.y = 0.0f;
    sensorOffset.z = 0.0f;
    MR::addHitSensor(this, "body", ATYPE_KILLER_TARGET_ENEMY, 8, 100.0f, sensorOffset);
    initBinder(150.0f, 0.0f, 0);
    initEffectKeeper(2, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, false);
    Vec3f pointerOffset;
    pointerOffset.x = 0.0f;
    pointerOffset.y = 0.0f;
    pointerOffset.z = 0.0f;
    MR::initStarPointerTarget(this, 100.0f, pointerOffset);
    mScaleController = new AnimScaleController(nullptr);
    mStateBindStarPointer = new WalkerStateBindStarPointer(this, mScaleController);
    MR::initShadowVolumeSphere(this, 90.0f);
    MR::calcFrontVec(&mFront, this);
    MR::calcUpVec(&mUp, this);
    mUpTarget.set(mUp);
    MR::declareCoin(this, 1);
    MR::addToAttributeGroupSearchTurtle(this);
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        s32 initialBehavior;
        MR::getPlacementArg0WithInit(rIter, &initialBehavior);
        if (initialBehavior == 0) {
            initNerve(GET_NERVE(Gesso, GessoNrvComeFromBox));
        } else {
            initNerve(GET_NERVE(Gesso, GessoNrvWait));
        }

        makeActorDead();
    } else {
        initNerve(GET_NERVE(Gesso, GessoNrvWait));
        makeActorAppeared();
    }
}

void Gesso::kill() {
    MR::emitEffect(this, "Death");
    MR::appearCoinPop(this, mPosition, 1);
    LiveActor::kill();
}

void Gesso::control() {
    mScaleController->updateNerve();
    if (tryPointerSwoon()) {
        return;
    }

    if (clipAndInitPos()) {
        return;
    }

    Vec3f playerDirection;
    Vec3f up;
    Vec3f offset;
    MR::normalize(*MR::getPlayerPos() - mPosition, &playerDirection);
    MR::turnVecToVecCosOnPlane(&mUp, mUpTarget, mFront, MR::cosDegree(0.5f));
    if (mVelocity.dot(mPushVelocity) < 0.0f) {
        const Vec3f& rVelocity = mVelocity;
        mVelocity.scaleAdd(-mPushVelocity.dot(rVelocity), mPushVelocity, rVelocity);
    }

    mVelocity.add(mPushVelocity);
    mPushVelocity *= 0.7f;
    if (MR::isNearZero(mPushVelocity)) {
        mPushVelocity.zero();
    }

    up.negate(mGravity);
    offset.scale(50.0f, up);
    if (!MR::isInWater(this, offset) && mVelocity.dot(up) > 0.0f) {
        const Vec3f& rVelocity = mVelocity;
        mVelocity.scaleAdd(-up.dot(rVelocity), up, rVelocity);
    }
}

void Gesso::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::makeMtxUpFrontPos(&mtx, mUp, mFront, mPosition);
    MR::setBaseTRMtx(this, mtx);
    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void Gesso::exeComeFromBox() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "FromBox");
        MR::startSound(this, "SE_EM_GESSO_SWIM_L");
        MR::invalidateHitSensors(this);
        mPosition.scaleAdd(-100.0f, mGravity, mPosition);
        mUpTarget.set(0.0f, 1.0f, 0.3f);
        MR::normalize(&mUpTarget);
        mUp.set(mUpTarget);
    }

    if (MR::isLessStep(this, 10)) {
        calcAndSetVelocity(0.0f, 10.0f, 5.0f);
    } else {
        mVelocity.zero();
    }

    if (MR::isActionEnd(this)) {
        mVelocity.zero();
        MR::validateHitSensors(this);
        setNerve(GET_NERVE(Gesso, GessoNrvWait));
    }
}

void Gesso::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
        calcAndSetUpVecTarget(0.0f, 0.0f, 0.0f);
    }

    if (MR::isStep(this, 2)) {
        MR::invalidateClipping(this);
    }

    mVelocity.scale(1.5f * (MR::sinDegree(45.0f + (2.0f * getNerveStep()))), mGravity);
    MR::rotateVecDegree(&mFront, mGravity, 0.3f);
    if (MR::isNearPlayer(this, 1400.0f) && MR::isPlayerInWaterMode()) {
        MR::validateClipping(this);
        setNerve(GET_NERVE(Gesso, GessoNrvSearch));
    }
}

void Gesso::exeSearch() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Search");
        MR::startSound(this, "SE_EM_GESSO_FIND");
        calcAndSetUpVecTarget(0.0f, 0.0f, 0.0f);
    }

    mVelocity.scale(1.5f * (MR::sinDegree(45.0f + (2.0f * getNerveStep()))), mGravity);
    MR::turnDirectionToPlayerDegree(this, &mFront, 1.5f);
    if (MR::isFaceToPlayerHorizontalDegree(this, mFront, 1.5f) && MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Gesso, GessoNrvWalkCharge));
    }
}

void Gesso::exeWalkCharge() {
    if (MR::isFirstStep(this)) {
        if (tryChangeHighSpeedMode()) {
            MR::startAction(this, "WalkFast");
            mUpTarget.set(0.0f, 1.0f, 1.0f);
            MR::normalize(&mUpTarget);
        } else {
            MR::startAction(this, "Walk");
            mUpTarget.set(0.0f, 1.0f, 1.0f);
            MR::normalize(&mUpTarget);
        }
    }

    mVelocity.mult(0.995f);
    if (MR::isStep(this, ::sStepForWalk)) {
        mIsMarioLeft = isMarioLeft();
        setNerve(GET_NERVE(Gesso, GessoNrvWalk));
    }
}

void Gesso::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_GESSO_SWIM_L");
    }

    MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), 1.5f);
    if (calcWalkMove(getNerveStep())) {
        selectNextNerve();
    }
}

void Gesso::exeSink() {
    if (MR::isFirstStep(this)) {
        if (MR::isNearPlayer(this, 1000.0f)) {
            MR::startAction(this, "SinkFast");
        } else {
            MR::startAction(this, "Sink");
        }
    }

    MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), 1.5f);
    if (MR::isBindedGround(this)) {
        setNerve(GET_NERVE(Gesso, GessoNrvWalkCharge));
    } else if (calcSinkMove(getNerveStep())) {
        selectNextNerve();
    }
}

void Gesso::exeLostPlayer() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "CoolDown");
        calcAndSetUpVecTarget(0.0f, 0.0f, 0.0f);
    }

    mVelocity.scale(1.5f * (MR::sinDegree(45.0f + (2.0f * getNerveStep()))), mGravity);
    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Gesso, GessoNrvWait));
    }
}

void Gesso::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Attack");
        MR::startSound(this, "SE_EM_GESSO_INK");
        calcAndSetUpVecTarget(0.0f, 0.0f, 0.0f);
    }

    if (MR::isBckOneTimeAndStopped(this)) {
        MR::startAction(this, "Wait");
    }

    if (MR::isBckPlaying(this, "Attack")) {
        mVelocity.mult(0.9f);
    } else {
        mVelocity.scale(1.5f * (MR::sinDegree(45.0f + (2.0f * getNerveStep()))), mGravity);
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(4))) {
        setNerve(GET_NERVE(Gesso, GessoNrvWait));
    }
}

void Gesso::exePunchDown() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
        MR::stopScene(5);
        MR::startAction(this, "PunchDown");
        MR::startSound(this, "SE_EM_GESSO_HIT_PUNCH");
        MR::startBlowHitSound(this);
    }

    MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), 1.5f);
    if (MR::isStep(this, 20) || MR::isBinded(this)) {
        MR::startSound(this, "SE_EM_GESSO_DEAD");
        kill();
    }
}

void Gesso::exeComeBack() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "CoolDown");
    }

    if (MR::isBckPlaying(this, "CoolDown")) {
        mVelocity.scale(1.5f * (MR::sinDegree(45.0f + (2.0f * getNerveStep()))), mGravity);
    } else {
        if (MR::isBckOneTimeAndStopped(this)) {
            MR::startAction(this, "Sink");
        }

        if (MR::isPlayerInWaterMode()) {
            setNerve(GET_NERVE(Gesso, GessoNrvWait));
        } else {
            MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), 1.5f);
            Vec3f homeDir;
            Vec3f diff = mInitPos - mPosition;
            MR::normalize(diff, &homeDir);
            mVelocity.scale(3.0f, homeDir);
            if (MR::isNear(this, mInitPos, 100.0f)) {
                setNerve(GET_NERVE(Gesso, GessoNrvWait));
            }
        }
    }
}

void Gesso::exeRotate() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "StarPiece");
        mVelocity.zero();
    }

    MR::startLevelSound(this, "SE_EM_LV_GESSO_STAR_PIECE_HIT");
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Gesso, GessoNrvWait));
    }
}

void Gesso::exePointerSwoon() {
    MR::updateActorStateAndNextNerve(this, mStateBindStarPointer, GET_NERVE(Gesso, GessoNrvWait));
}

void Gesso::endPointerSwoon() {
    mStateBindStarPointer->kill();
}

void Gesso::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(Gesso, GessoNrvPunchDown)) && MR::isSensorEnemy(pSender) && MR::isSensorPlayer(pReceiver) &&
        !isNerve(GET_NERVE(Gesso, GessoNrvPointerSwoon)) && MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
        MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);
        Vec3f recoilDir;
        Vec3f diff = pSender->mPosition - pReceiver->mPosition;
        MR::normalize(diff, &recoilDir);
        mVelocity.scale(25.0f, recoilDir);
        setNerve(GET_NERVE(Gesso, GessoNrvAttack));
    }
}

bool Gesso::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(Gesso, GessoNrvPunchDown))) {
        return false;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        setNerve(GET_NERVE(Gesso, GessoNrvRotate));
        return true;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        knockOut(pSender, pReceiver);
        return true;
    }

    return false;
}

bool Gesso::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(Gesso, GessoNrvPunchDown))) {
        return false;
    }

    if (MR::isMsgExplosionAttack(msg)) {
        knockOut(pSender, pReceiver);
        return true;
    }

    if (pSender->mType == ACTMES_RUSHDROP) {
        knockOut(pSender, pReceiver);
        return false;
    }

    return false;
}

bool Gesso::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isSensorEnemy(pSender) || MR::isSensorMapObj(pSender)) {
        Vec3f pushDir;
        pushDir.sub(mPosition, pSender->mHost->mPosition);
        MR::normalizeOrZero(&pushDir);
        mPushVelocity.scaleAdd(0.2f, mPushVelocity, pushDir);
        return true;
    }

    return false;
}

void Gesso::selectNextNerve() {
    if (!MR::isPlayerInWaterMode()) {
        setNerve(GET_NERVE(Gesso, GessoNrvComeBack));
    } else if (!MR::isNearPlayer(this, 2400.0f)) {
        setNerve(GET_NERVE(Gesso, GessoNrvLostPlayer));
    } else if (isMarioUp()) {
        setNerve(GET_NERVE(Gesso, GessoNrvWalkCharge));
    } else if (!isNerve(GET_NERVE(Gesso, GessoNrvSink))) {
        setNerve(GET_NERVE(Gesso, GessoNrvSink));
    }
}

void Gesso::knockOut(HitSensor* pSender, HitSensor* pReceiver) {
    Vec3f knockDir;
    Vec3f diff = pReceiver->mPosition - pSender->mPosition;
    MR::normalize(diff, &knockDir);
    mVelocity.scale(30.0f, knockDir);
    MR::turnDirectionToTarget(this, &mFront, pSender->mPosition, -1.0f);
    setNerve(GET_NERVE(Gesso, GessoNrvPunchDown));
}

bool Gesso::clipAndInitPos() {
    if (!MR::isNearPlayer(this, 10000.0f) || MR::isJudgedToClipFrustum(mPosition, getSensor(nullptr)->getRadius())) {
        if (mClipStep >= NebulaFrameClock::stepsOfSeconds(5)) {
            mPosition.set(mInitPos);
            setNerve(GET_NERVE(Gesso, GessoNrvWait));
            MR::validateClipping(this);
            mClipStep = 0;
            return true;
        }

        mClipStep++;
        return false;
    }

    mClipStep = 0;
    return false;
}

bool Gesso::calcWalkMove(s32 step) {
    f32 verticalSpeed, sideSign, angle;

    if (mIsMarioLeft) {
        sideSign = 1.0f;
    } else {
        sideSign = -1.0f;
    }

    angle = 0.990099f * step;

    if (mIsHighSpeedMode) {
        verticalSpeed = MR::cosDegree(angle);
        Vec3f* vel = &mVelocity;
        Vec3f upVec;
        Vec3f sideVec;
        verticalSpeed = 7.0f * verticalSpeed;
        MR::calcUpVec(&upVec, this);
        MR::calcSideVec(&sideVec, this);
        mVelocity.scale(verticalSpeed, upVec);
        vel->scaleAdd((sideSign * 3.0f) / 5.0f, sideVec, *vel);
        vel->scaleAdd(2.3f, mFront, *vel);
    } else {
        verticalSpeed = MR::cosDegree(angle);
        Vec3f* vel = &mVelocity;
        Vec3f upVec;
        Vec3f sideVec;
        verticalSpeed = 7.0f * verticalSpeed;
        MR::calcUpVec(&upVec, this);
        MR::calcSideVec(&sideVec, this);
        mVelocity.scale(verticalSpeed, upVec);
        vel->scaleAdd(sideSign * 3.0f, sideVec, *vel);
        vel->scaleAdd(2.3f, mFront, *vel);
    }

    if (MR::isFirstStep(this)) {
        if (mIsHighSpeedMode) {
            calcAndSetUpVecTarget((3.0f * sideSign) / 5.0f, MR::cosDegree(angle) * 7.0f, 4.6f);
        } else {
            calcAndSetUpVecTarget((3.0f * sideSign) / 2.0f, MR::cosDegree(angle) * 7.0f, 4.6f);
        }
    }

    if (angle >= 70.0f) {
        mUpTarget.set(0.0f, 1.0f, 1.0f);
        MR::normalize(&mUpTarget);
    }

    return angle >= 100.0f;
}

bool Gesso::calcSinkMove(s32 step) {
    f32 verticalSpeed, sideSign, angle;

    if (mIsMarioLeft) {
        sideSign = 1.0f;
    } else {
        sideSign = -1.0f;
    }

    angle = 100.0f + 3.0f * step;
    if (angle >= 180.0f) {
        angle = 180.0f;
    }

    verticalSpeed = MR::cosDegree(angle) * 3.0f;
    Vec3f* vel = &mVelocity;
    Vec3f upVec;
    Vec3f sideVec;
    MR::calcUpVec(&upVec, this);
    MR::calcSideVec(&sideVec, this);

    mVelocity.scale(verticalSpeed, upVec);
    vel->scaleAdd(sideSign * 3.0f, sideVec, *vel);
    vel->scaleAdd(2.3f, mFront, *vel);
    mUpTarget.set(0.0f, 1.0f, 1.0f);
    MR::normalize(&mUpTarget);

    return angle >= 180.0f;
}

void Gesso::calcAndSetVelocity(f32 side, f32 up, f32 front) {
    Vec3f* vel = &mVelocity;
    Vec3f sideVec;
    Vec3f upVec;
    MR::calcUpVec(&upVec, this);
    MR::calcSideVec(&sideVec, this);
    mVelocity.scale(up, upVec);
    vel->scaleAdd(side, sideVec, *vel);
    vel->scaleAdd(front, mFront, *vel);
}

void Gesso::calcAndSetUpVecTarget(f32 x, f32 y, f32 z) {
    if (x == 0.0f && y == 0.0f && z == 0.0f) {
        mUpTarget.set(0.0f, 1.0f, 0.0f);
        return;
    }

    mUpTarget.set(x, y, z);
    MR::normalize(&mUpTarget);
    return;
}

bool Gesso::isMarioUp() const {
    Vec3f* pos = MR::getPlayerPos();
    Vec3f playerDist;
    playerDist.sub(*pos, mPosition);
    Vec3f upVec;
    upVec.negate(mGravity);
    return playerDist.dot(upVec) > 0.0f;
}

bool Gesso::isMarioLeft() const {
    Vec3f* pos = MR::getPlayerPos();
    Vec3f playerDist;
    playerDist.sub(*pos, mPosition);
    Vec3f sideVec;
    MR::calcSideVec(&sideVec, this);
    return playerDist.dot(sideVec) > 0.0f;
}

bool Gesso::tryChangeHighSpeedMode() {
    bool change = false;
    if (MR::isNearPlayer(this, 1000.0f) && !MR::isPlayerInWaterMode()) {
        change = true;
    }

    mIsHighSpeedMode = change;
    return change;
}

bool Gesso::tryPointerSwoon() {
    if (isNerve(GET_NERVE(Gesso, GessoNrvPointerSwoon))) {
        return false;
    }

    if (isNerve(GET_NERVE(Gesso, GessoNrvPunchDown))) {
        return false;
    }

    if (isNerve(GET_NERVE(Gesso, GessoNrvRotate))) {
        return false;
    }

    if (!mStateBindStarPointer->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(Gesso, GessoNrvPointerSwoon));
    return true;
}
