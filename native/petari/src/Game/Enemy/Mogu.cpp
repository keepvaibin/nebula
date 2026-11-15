#include "Game/Enemy/Mogu.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/MoguStone.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    struct SightParam {
        f32 mRange;
        f32 mAngleH;
        f32 mAngleV;
    };

    const SightParam sThrowableSightParam = {900.0f, 10.0f, 90.0f};
    const SightParam sCannonFleetSightParam = {1500.0f, 10.0f, 90.0f};

    const f32 sHideNearDistance = 400.0f;
    const f32 sHideFarDistance = 2000.0f;
    const f32 sPlayerAboveDot = -0.75f;
    const s32 sHideWaitSteps = NebulaFrameClock::stepsOfSeconds(2);
    const f32 sFastHidePlayerSpeed = 8.0f;
    const f32 sFastHideDistance = 200.0f;
    const f32 sFastHideRate = 1.5f;
    const s32 sAppearHipDropSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    const f32 sTurnSpeed = 0.03f;
    const s32 sThrowAfterSteps = 45;
    const s32 sThrowReleaseStep = 47;
    const f32 sThrowSpeed = 15.0f;
    const s32 sSwoonSteps = NebulaFrameClock::stepsOfSeconds(2);
    const s32 sStampDeathSteps = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sHitBlowSteps = 20;
    const f32 sHipDropNearDistance = 130.0f;
    const f32 sHipDropFarDistance = 1500.0f;
    const f32 sPunchSpeedH = 30.0f;
    const f32 sPunchSpeedUp = 50.0f;
    const f32 sPunchLift = 5.0f;
};  // namespace

namespace NrvMogu {
    NEW_NERVE(HostTypeNrvHideWait, Mogu, HideWait);
    NEW_NERVE(HostTypeNrvHide, Mogu, Hide);
    NEW_NERVE(HostTypeNrvAppear, Mogu, Appear);
    NEW_NERVE(HostTypeNrvSearch, Mogu, Search);
    NEW_NERVE(HostTypeNrvTurn, Mogu, Search);
    NEW_NERVE_ONEND(HostTypeNrvThrow, Mogu, Throw, Throw);
    NEW_NERVE(HostTypeNrvSwoonStart, Mogu, SwoonStart);
    NEW_NERVE(HostTypeNrvSwoonEnd, Mogu, SwoonEnd);
    NEW_NERVE(HostTypeNrvSwoon, Mogu, Swoon);
    NEW_NERVE(HostTypeNrvHipDropReaction, Mogu, HipDropReaction);
    NEW_NERVE(HostTypeNrvStampDeath, Mogu, StampDeath);
    NEW_NERVE(HostTypeNrvHitBlow, Mogu, HitBlow);
};  // namespace NrvMogu

Mogu::Mogu(const char* pName)
    : LiveActor(pName), mAnimScaleController(nullptr), mStoneHoldPos(nullptr), mStone(nullptr), mHole(nullptr), mSight(0, 0, 1), mUp(0, 1, 0), mThrowOnGravity(true),
      mIsCannonFleet(false) {
    lumacore::setClass(this, lumacore::kClassId< Mogu >);
}

// Arg 0 other than -1 throws the stone straight instead of along gravity.
void Mogu::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        MR::useStageSwitchWriteDead(this, rIter);
        MR::useStageSwitchReadA(this, rIter);
        MR::useStageSwitchSleep(this, rIter);

        s32 throwStraight = -1;
        MR::getPlacementArg0NoInit(rIter, &throwStraight);
        mThrowOnGravity = throwStraight == -1;
    }

    initModelManagerWithAnm("Mogu", nullptr, false);

    Pos3f mtx;
    mtx.setInline(getBaseMtx());
    mtx.getYDir(mUp);

    MR::connectToSceneEnemy(this);
    MR::declareStarPiece(this, 3);
    MR::declareCoin(this, 1);

    f32 scaleY = mScale.y;
    f32 binderRadius = scaleY * 160.0f;
    initBinder(binderRadius, binderRadius, 0);
    initHitSensor(2);

    Vec3f sensorOffset(-55.0f, 0.0f, 13.0f);
    sensorOffset *= scaleY;

    MR::addHitSensorAtJointEnemy(this, "body", "Head", 32, 150.0f * scaleY, sensorOffset);
    initEffectKeeper(0, nullptr);
    MR::initStarPointerTarget(this, 100.0f, Vec3f(0.0f, 50.0f, 0.0f));
    initSound(4, false);
    initNerve(GET_NERVE(Mogu, HostTypeNrvSearch));
    mGravity.set(-mUp);
    MR::initShadowVolumeSphere(this, 60.0f * mScale.y);
    MR::invalidateShadow(this, nullptr);
    MR::initLightCtrl(this);
    mAnimScaleController = new AnimScaleController(nullptr);
    makeActorAppeared();

    mStoneHoldPos = new FixedPosition(this, "ArmR2", Vec3f(67.38f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));

    mStone = new MoguStone("投石用の石", "MoguStone");
    mStone->initWithoutIter();

    mHole = new ModelObj("巣穴モデル", "MoguHole", nullptr, -2, -2, -2, false);
    mHole->mPosition.set(mPosition);
    mHole->mRotation.set(mRotation);
    mHole->initWithoutIter();

    mIsCannonFleet = MR::isEqualStageName("CannonFleetGalaxy");
}

void Mogu::kill() {
    LiveActor::kill();
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    MR::emitEffect(this, "Death");
}

void Mogu::control() {
    mAnimScaleController->updateNerve();
}

// Back from clipping, a turning one starts its turn again.
void Mogu::endClipped() {
    LiveActor::endClipped();

    if (isNerve(GET_NERVE(Mogu, HostTypeNrvTurn))) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvTurn));
    }
}

// Stays under while the player is well above it, then comes up once the player is in range.
void Mogu::exeHideWait() {
    Vec3f toPlayerDir = *MR::getPlayerPos() - mPosition;
    MR::normalizeOrZero(&toPlayerDir);

    if (mGravity.dot(toPlayerDir) < sPlayerAboveDot) {
        return;
    }

    f32 distanceToPlayer = MR::calcDistanceToPlayer(this);

    if (MR::isGreaterStep(this, sHideWaitSteps) && sHideNearDistance < distanceToPlayer && distanceToPlayer < sHideFarDistance) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvAppear));
    }
}

// Hides faster when the player runs at it or is close.
void Mogu::exeHide() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Hide");
        MR::startAction(mHole, "Hide");
        MR::startSound(this, "SE_EM_MOGU_HIDE");

        Vec3f fromPlayer(mPosition);
        fromPlayer -= *MR::getPlayerCenterPos();
        f32 distance = fromPlayer.length();

        MR::vecKillElement(fromPlayer, mGravity, &fromPlayer);
        MR::normalizeOrZero(&fromPlayer);

        f32 approachSpeed = MR::getPlayerVelocity()->dot(fromPlayer);
        if (sFastHidePlayerSpeed < approachSpeed || distance < sFastHideDistance) {
            MR::setBckRate(this, sFastHideRate);
        }
    }

    if (MR::isActionEnd(this)) {
        MR::startSound(this, "SE_EM_MOGUHOLE_CLOSE");
        setNerve(GET_NERVE(Mogu, HostTypeNrvHideWait));
    }
}

void Mogu::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Appear");
        MR::startAction(mHole, "Open");
        MR::startSound(this, "SE_EM_MOGUHOLE_OPEN");
        MR::startSound(this, "SE_EM_MOGU_APPEAR");
        Vec3f toPlayerH;
        MR::calcVecToPlayerH(&toPlayerH, this, nullptr);
        MR::turnVecToVecRadian(&mSight, mSight, toPlayerH, PI, mUp);
    }

    if (tryStartSwoon(MR::isGreaterStep(this, sAppearHipDropSteps))) {
        return;
    }

    if (isPlayerTooNear(MR::calcDistanceToPlayer(this))) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvHide));
        return;
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvSearch));
    }
}

// The player stands over the hole.
bool Mogu::isPlayerExistUp() const {
    Vec3f toPlayerCenter = *MR::getPlayerCenterPos() - mPosition;

    if (mUp.dot(toPlayerCenter) < 0.0f) {
        return false;
    }

    MR::vecKillElement(toPlayerCenter, *MR::getPlayerGravity(), &toPlayerCenter);

    return toPlayerCenter.length() < sHideNearDistance;
}

bool Mogu::isPlayerTooNear(f32 distanceToPlayer) const {
    return distanceToPlayer < sHideNearDistance || isPlayerExistUp();
}

// The second player's pointer with the trigger, or a hip drop nearby (when `isHipDropFelt`), knocks it over.
bool Mogu::tryStartSwoon(bool isHipDropFelt) {
    if (MR::isStarPointerPointing2POnTriggerButton(this, "強", true, false)) {
        MR::start2PAttackAssistSound();
        setNerve(GET_NERVE(Mogu, HostTypeNrvSwoonStart));
        return true;
    }

    if (isHipDropFelt && isNearPlayerHipDrop()) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvSwoonStart));
        return true;
    }

    return false;
}

// Search waits; within sight range it turns toward the player (the Turn nerve runs this too) and throws once the player
// is in its sight fan and the stone is back; the stage's switch A, when set, must be on for both.
void Mogu::exeSearch() {
    bool isTurn = isNerve(GET_NERVE(Mogu, HostTypeNrvTurn));
    if (MR::isFirstStep(this)) {
        if (isTurn) {
            MR::startAction(this, "Turn");
        } else {
            MR::startAction(this, "Wait");
        }
    }

    if (tryStartSwoon(true)) {
        return;
    }

    if (isTurn) {
        Vec3f toPlayerH;
        MR::calcVecToPlayerH(&toPlayerH, this, nullptr);
        MR::turnVecToVecRadian(&mSight, mSight, toPlayerH, sTurnSpeed, mUp);
    }

    f32 distanceToPlayer = MR::calcDistanceToPlayer(this);
    const SightParam& rSight = mIsCannonFleet ? sCannonFleetSightParam : sThrowableSightParam;

    if (isPlayerTooNear(distanceToPlayer) || sHideFarDistance < distanceToPlayer) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvHide));
        return;
    }

    bool isAwake = !MR::isValidSwitchA(this) || MR::isOnSwitchA(this);
    if (!isTurn && distanceToPlayer < rSight.mRange) {
        if (isAwake) {
            setNerve(GET_NERVE(Mogu, HostTypeNrvTurn));
            return;
        }
    } else if (isTurn && rSight.mRange < distanceToPlayer) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvSearch));
        return;
    }

    if (isAwake && MR::isInSightFanPlayer(this, mSight, rSight.mRange, rSight.mAngleH, rSight.mAngleV) && MR::isGreaterStep(this, sThrowAfterSteps) &&
        MR::isDead(mStone)) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvThrow));
    }
}

// The stone rides the right hand (see calcAndSetBaseMtx) until the release step, then flies along the sight as far as
// the player is across, and as far up or down as the player is.
void Mogu::exeThrow() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Throw");
        mStone->appear();
        MR::startAction(mStone, "Rotate");
        MR::startSound(this, "SE_EM_MOGU_TAKE_ITEM");
    }

    if (tryStartSwoon(true)) {
        return;
    }

    if (MR::isStep(this, sThrowReleaseStep)) {
        Vec3f upVec;
        MR::calcUpVec(&upVec, this);

        Vec3f stoneToPlayer(*MR::getPlayerCenterPos());
        stoneToPlayer -= mStone->mPosition;

        Vec3f across;
        MR::vecKillElement(stoneToPlayer, upVec, &across);

        f32 upDistance = upVec.dot(stoneToPlayer);
        Vec3f upPart;
        upPart.scale(upDistance, upVec);

        mStone->emit(mThrowOnGravity, mStone->mPosition, mStone->mPosition + mSight * across.length() + upPart, sThrowSpeed);
        MR::startSound(this, "SE_EM_MOGU_THROW");
    }

    f32 distanceToPlayer = MR::calcDistanceToPlayer(this);
    if (isPlayerTooNear(distanceToPlayer) || sHideFarDistance < distanceToPlayer) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvHide));
        return;
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvSearch));
    }
}

// A stone still in hand goes with the throw.
void Mogu::endThrow() {
    if (mStone->isTaken()) {
        mStone->kill();
    }
}

void Mogu::exeSwoonStart() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_MOGU_TURNOVER");
        MR::startSound(this, "SE_EV_MOGU_TURNOVER");
        MR::startAction(this, "SwoonStart");

        if (!MR::isBckPlaying(mHole, "Open")) {
            MR::startAction(mHole, "Break");
        }
    }

    MR::startLevelSound(this, "SE_EM_LV_SWOON_S");

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvSwoon));
    }
}

void Mogu::exeSwoon() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Swoon");
    }

    MR::startLevelSound(this, "SE_EM_LV_SWOON_S");

    if (isNearPlayerHipDrop()) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvHipDropReaction));
        return;
    }

    if (MR::isGreaterStep(this, sSwoonSteps)) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvSwoonEnd));
    }
}

void Mogu::exeHipDropReaction() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "HipDropReaction");
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvSwoon));
    }
}

void Mogu::exeSwoonEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "SwoonEnd");
    }

    MR::startLevelSound(this, "SE_EM_LV_MOGU_SWOON_RECOVER");

    if (MR::isActionEnd(this)) {
        MR::startAction(mHole, "Close");
        MR::startSound(this, "SE_EM_MOGUHOLE_CLOSE");
        setNerve(GET_NERVE(Mogu, HostTypeNrvHideWait));
    }
}

// Stomped: a coin.
void Mogu::exeStampDeath() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Down");
        MR::startAction(mHole, "Close");
        MR::startSound(this, "SE_EM_STOMPED_S");
    }

    if (MR::isGreaterStep(this, sStampDeathSteps)) {
        kill();
        MR::emitEffect(this, "Death");
        MR::startSound(this, "SE_EM_EXPLODE_S");
        MR::startAction(mHole, "Down");
        MR::appearCoinPop(this, mPosition, 1);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    }
}

// Punched away: star pieces.
void Mogu::exeHitBlow() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "PunchDown");
        MR::startAction(mHole, "Close");
        MR::startSound(this, "SE_EM_MOGUHOLE_CLOSE");
        MR::startBlowHitSound(this);
        MR::validateShadow(this, nullptr);
        Vec3f toPlayerH;
        MR::calcVecToPlayerH(&toPlayerH, this, nullptr);
        MR::turnVecToVecRadian(&mSight, mSight, toPlayerH, PI, mUp);
    }

    MR::applyVelocityDampAndGravity(this, 2.0f, 0.8f, 0.98f, 0.98f, 1.0f);

    if (MR::isGreaterStep(this, sHitBlowSteps)) {
        MR::startSound(this, "SE_EM_EXPLODE_S");
        kill();
        MR::startAction(mHole, "Down");
        MR::appearStarPiece(this, mPosition, 3, 10.0f, 40.0f, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    }
}

bool Mogu::isNearPlayerHipDrop() const {
    if (!MR::isPlayerHipDropLand()) {
        return false;
    }

    f32 distanceToPlayer = MR::calcDistanceToPlayer(this);
    return sHipDropNearDistance < distanceToPlayer && distanceToPlayer < sHipDropFarDistance;
}

// Under the ground or already beaten.
bool Mogu::isHiddenOrDown() const {
    return isNerve(GET_NERVE(Mogu, HostTypeNrvHideWait)) || isNerve(GET_NERVE(Mogu, HostTypeNrvHide)) || isNerve(GET_NERVE(Mogu, HostTypeNrvStampDeath)) ||
           isNerve(GET_NERVE(Mogu, HostTypeNrvHitBlow));
}

bool Mogu::isSwoon() const {
    return isNerve(GET_NERVE(Mogu, HostTypeNrvSwoonStart)) || isNerve(GET_NERVE(Mogu, HostTypeNrvSwoonEnd)) || isNerve(GET_NERVE(Mogu, HostTypeNrvSwoon)) ||
           isNerve(GET_NERVE(Mogu, HostTypeNrvHipDropReaction));
}

void Mogu::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender != getSensor("body") || isHiddenOrDown()) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

// A star piece knocks over one that is up; a shell or invincible attack sends it flying; swooning, a stomp kills it and a
// spin sends it flying.
bool Mogu::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceAttack(msg)) {
        if (isSwoon()) {
            mAnimScaleController->startHitReaction();
        } else if (isNerve(GET_NERVE(Mogu, HostTypeNrvAppear)) || isNerve(GET_NERVE(Mogu, HostTypeNrvSearch)) ||
                   isNerve(GET_NERVE(Mogu, HostTypeNrvThrow))) {
            setNerve(GET_NERVE(Mogu, HostTypeNrvSwoonStart));
        }
        return true;
    }

    if (MR::isMsgJetTurtleAttack(msg) || MR::isMsgInvincibleAttack(msg)) {
        if (isHiddenOrDown()) {
            return false;
        }

        startHitBlow(pSender, pReceiver);
        return true;
    }

    if (!isSwoon()) {
        return false;
    }

    if (MR::isMsgPlayerHipDrop(msg) || MR::isMsgPlayerTrample(msg)) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvStampDeath));
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        startHitBlow(pSender, pReceiver);
        return true;
    }

    return false;
}

// Flies away from the attacker and up; hit from straight above it is stomped instead.
void Mogu::startHitBlow(HitSensor* pAttacker, HitSensor* pVictim) {
    Vec3f direction(pVictim->mPosition);
    direction -= pAttacker->mPosition;
    MR::vecKillElement(direction, mGravity, &direction);
    MR::normalizeOrZero(&direction);
    if (MR::isNearZero(direction)) {
        setNerve(GET_NERVE(Mogu, HostTypeNrvStampDeath));
        return;
    }

    direction *= sPunchSpeedH;
    Vec3f gravity(mGravity);
    gravity *= sPunchSpeedUp;
    direction -= gravity;
    mVelocity = direction;

    if (MR::isOnGround(this)) {
        mPosition += -mGravity * sPunchLift;
    }

    setNerve(GET_NERVE(Mogu, HostTypeNrvHitBlow));
}

void Mogu::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::makeMtxUpFrontPos(&mtx, mUp, mSight, mPosition);
    MR::setBaseTRMtx(this, mtx);
    Vec3f scale = mAnimScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);

    if (isNerve(GET_NERVE(Mogu, HostTypeNrvThrow)) && MR::isLessStep(this, sThrowReleaseStep)) {
        mStoneHoldPos->calc();
        mStoneHoldPos->mMtx.getTrans(mStone->mPosition);
    }
}
