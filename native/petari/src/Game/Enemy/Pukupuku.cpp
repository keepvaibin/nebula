#include "Game/Enemy/Pukupuku.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ParabolicPath.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/ValueControl.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_updatePoseByJumpPath_f1, "updatePoseByJumpPath.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f1, "kill.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f2, "kill.f2", 40.0f);

namespace {
    NEW_NERVE(PukupukuStateLandingLandingMoveLand, PukupukuStateLanding, LandingMoveLand);
    NEW_NERVE(PukupukuStateLandingLandingJumpFromWater, PukupukuStateLanding, LandingJumpFromWater);
    NEW_NERVE(PukupukuStateLandingLandingJumpFromLand, PukupukuStateLanding, LandingJumpFromLand);

    NEW_NERVE(PukupukuWait, Pukupuku, Wait);
    NEW_NERVE(PukupukuMoveWater, Pukupuku, MoveWater);
    NEW_NERVE(PukupukuMoveWaterAfterJump, Pukupuku, MoveWaterAfterJump);
    NEW_NERVE(PukupukuMoveWaterAfterJumpAfterPointing, Pukupuku, MoveWaterAfterJumpAfterPointing);
    NEW_NERVE(PukupukuLanding, Pukupuku, Landing);
    NEW_NERVE(PukupukuTrampled, Pukupuku, Trampled);
    NEW_NERVE(PukupukuBlownOff, Pukupuku, BlownOff);
    NEW_NERVE(PukupukuBindStarPointer, Pukupuku, BindStarPointer);

    struct BckBtp {
        const char* mBck;
        const char* mBtp;
    };

    // The jump animations a pointer bind can interrupt, with the eye animation that plays with each.
    const BckBtp sJumpBckBtpTable[] = {{"Bound", "CloseEye"}, {"FlyStart", "OpenEye"}, {"FlyEnd", "OpenEye"}, {"SwimStart", "OpenEye"}};

    const char* getBtpName(const char* pBck) {
        for (const BckBtp& rEntry : sJumpBckBtpTable) {
            if (MR::isEqualStringCase(rEntry.mBck, pBck)) {
                return rEntry.mBtp;
            }
        }

        return nullptr;
    }

    NEBULA_TUNABLE(s32, sJumpFrameMax, 30);
    NEBULA_TUNABLE(f32, sHopHeight, 300.0f);
    NEBULA_TUNABLE(f32, sJumpHeight, 500.0f);
    NEBULA_TUNABLE(f32, sJumpSpeed, 15.0f);
    NEBULA_TUNABLE(f32, sJumpPoseRate, 0.15f);
    NEBULA_TUNABLE(f32, sRailPoseRate, 0.08f);
    NEBULA_TUNABLE(f32, sEndJumpOnGroundRate, 0.5f);
    NEBULA_TUNABLE(f32, sSwimSpeed, 5.0f);
    NEBULA_TUNABLE(f32, sBinderRadius, 70.0f);
    NEBULA_TUNABLE(f32, sBodyRadius, 60.0f);
    NEBULA_TUNABLE(f32, sBodyRadiusBound, 100.0f);
    NEBULA_TUNABLE(f32, sGroundProbeLength, 100.0f);
    NEBULA_TUNABLE(f32, sWaterColumnScale, 1.5f);
    NEBULA_TUNABLE(f32, sTrampleFallSpeed, 10.0f);
    NEBULA_TUNABLE(f32, sTrampleFriction, 0.89f);
    const s32 sTrampledSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, sBlownOffSteps, 15);
    NEBULA_TUNABLE(f32, sBlowSpeed, 35.0f);
    NEBULA_TUNABLE(f32, sBlowUpSpeed, 20.0f);
    NEBULA_TUNABLE(f32, sBlowRateInWater, 0.6f);
    NEBULA_TUNABLE(f32, sBlowRateOnLand, 1.0f);
};  // namespace

PukupukuStateLanding::PukupukuStateLanding(Pukupuku* pParent)
    : ActorStateBase< Pukupuku >("プクプク地上", pParent), mPath(new ParabolicPath()), mValueCtrl(new ValueControl(sJumpFrameMax)),
      mIsWaterColumnEmitted(false) {
    initNerve(GET_NERVE_ANON(PukupukuStateLandingLandingMoveLand));
    kill();
}

// Hops along the land points; a point with arg 0 = 1 is where it jumps back into the water.
void PukupukuStateLanding::exeLandingMoveLand() {
    if (MR::isFirstStep(this)) {
        setupJumping(sHopHeight, sJumpSpeed);
        mHost->startAnim("Bound", "CloseEye");
        emitGroundHitEffect();
    }

    mValueCtrl->update();
    updateJumping();
    mHost->updatePoseByRailIgnoreUpScale();
    tryEndJumpOnLand();
}

void PukupukuStateLanding::exeLandingJumpFromWater() {
    if (MR::isFirstStep(this)) {
        mIsWaterColumnEmitted = false;
        setupJumping(sJumpHeight, sJumpSpeed);
        mHost->rotatePoseByLocalZ();
        mHost->startAnim("FlyStart", "OpenEye");
        MR::startSound(mHost, "SE_EM_PUKUPUKU_WATER_OUT");
    }

    mValueCtrl->update();
    updateJumping();
    emitWaterColumnIfNeed(true, false);
    updatePoseByJumpPath(sJumpPoseRate);
    tryEndJumpOnLand();
}

void PukupukuStateLanding::exeLandingJumpFromLand() {
    if (MR::isFirstStep(this)) {
        mIsWaterColumnEmitted = false;
        setupJumping(sJumpHeight, sJumpSpeed);
        mHost->startAnim("FlyEnd", "OpenEye");
        MR::startSound(mHost, "SE_EM_PUKUPUKU_WING");
        emitGroundHitEffect();
    }

    mValueCtrl->update();
    updateJumping();
    emitWaterColumnIfNeed(false, false);
    updatePoseByJumpPath(sJumpPoseRate);

    if (mValueCtrl->isFrameMaxFrame()) {
        MR::moveCoordAndTransToNextPoint(mHost);
        emitWaterColumnIfNeed(false, true);
        kill();
    }
}

// A jump ends at its last frame, or past its middle once it touches ground.
void PukupukuStateLanding::tryEndJumpOnLand() {
    if (!mValueCtrl->isFrameMaxFrame() && !(MR::isBinded(mHost) && sEndJumpOnGroundRate < mValueCtrl->getValue())) {
        return;
    }

    MR::moveCoordToRailPoint(mHost, MR::getNextRailPointNo(mHost));

    if (MR::isBinded(mHost)) {
        mHost->mVelocity.zero();
    }

    setNerveAfterJumpAccordingToNextPoint();
}

void PukupukuStateLanding::updatePoseByJumpPath(f32 rate) {
    Vec3f direction;
    mPath->calcDirection(&direction, mValueCtrl->getValue(), cNum_updatePoseByJumpPath_f1);
    MR::normalize(&direction);

    Vec3f up = -mHost->mGravity;
    if (!MR::isSameDirection(direction, up)) {
        Quat4f quat;
        MR::makeQuatFromVec(&quat, direction, up);
        mHost->mPoseQuat.slerp(quat, rate);
    }
}

void PukupukuStateLanding::setNerveAfterJumpAccordingToNextPoint() {
    if (mHost->isReadyToJumpFromLand()) {
        setNerve(GET_NERVE_ANON(PukupukuStateLandingLandingJumpFromLand));
    } else {
        setNerve(GET_NERVE_ANON(PukupukuStateLandingLandingMoveLand));
    }
}

// An arc to the next rail point peaking `height` above the current point (less what it already stands above it), run at
// `averageSpeed`.
void PukupukuStateLanding::setupJumping(f32 height, f32 averageSpeed) {
    Vec3f curPos;
    mHost->getCurrentRailPointPos(&curPos);
    Vec3f nextPos;
    mHost->getNextRailPointPos(&nextPos);

    Vec3f fromPoint(mHost->mPosition);
    fromPoint.sub(curPos);
    Vec3f up = -mHost->mGravity;
    f32 aboveCur = up.dot(fromPoint);

    if (0.0f < aboveCur) {
        height = (height - aboveCur);
    }

    mPath->initFromUpVectorAddHeight(mHost->mPosition, nextPos, up, height);
    f32 speed = mPath->calcPathSpeedFromAverageSpeed(averageSpeed);
    f32 maxFrame = (1.0f / speed);
    mValueCtrl->setMaxFrame(maxFrame);
    mValueCtrl->setDirToOneResetFrame();
}

void PukupukuStateLanding::updateJumping() {
    Vec3f pos;
    mPath->calcPosition(&pos, mValueCtrl->getValue());
    pos.sub(mHost->mPosition);
    mHost->mVelocity.set(pos);
}

// Once per jump: where the body crosses the water surface (in or out, as `isFromWater` says), or at once when `isForce`.
void PukupukuStateLanding::emitWaterColumnIfNeed(bool isFromWater, bool isForce) {
    if (mIsWaterColumnEmitted) {
        return;
    }

    Vec3f centerJointPos;
    MR::copyJointPos(mHost, "Center", &centerJointPos);

    if (isForce || isFromWater != MR::isInWater(centerJointPos)) {
        Pos3f hitMtx;
        mHost->calcGroundHitMtx(&hitMtx);
        hitMtx.setTrans(centerJointPos);
        hitMtx.scaleXYZ(sWaterColumnScale);
        MR::emitEffectHit(mHost, hitMtx, "WaterColumn");
        mIsWaterColumnEmitted = true;
    }
}

// In the air the effect takes the floor code of the ground below.
void PukupukuStateLanding::emitGroundHitEffect() {
    if (!MR::isBindedGround(mHost)) {
        Triangle triangle;
        Vec3f hitPos;
        if (MR::getFirstPolyOnLineToMap(&hitPos, &triangle, mHost->mPosition, mHost->mGravity * sGroundProbeLength)) {
            MR::updateEffectFloorCode(mHost, &triangle);
        }
    }

    Pos3f hitMtx;
    mHost->calcGroundHitMtx(&hitMtx);
    MR::emitEffectHit(mHost, hitMtx, "Land");
    MR::startSound(mHost, "SE_EM_PUKUPUKU_LAND");
}

Pukupuku::Pukupuku(const char* pName)
    : LiveActor(pName), mStateLanding(nullptr), mScaleCtrl(nullptr), mStarPointer(nullptr), mBckBeforeBind(nullptr), mNerveAfterBind(nullptr),
      mBckFrameBeforeBind(0.0f), mBtpFrameBeforeBind(0.0f), mIsTrampled(false) {
    mClipCenter.zero();
    mBinderOffset.zero();
    mPoseQuat.set< f32 >(0.0f, 0.0f, 0.0f, 1.0f);
}

// Swims along its rail; rail points with arg 0 = 1 are where it jumps between water and land.
void Pukupuku::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::onCalcGravity(this);
    MR::useStageSwitchWriteDead(this, rIter);
    initModelManagerWithAnm("Pukupuku", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initHitSensor(1);
    MR::addHitSensorAtJoint(this, "body", "Center", ATYPE_PUKUPUKU, 8, sBodyRadius, Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(sBinderRadius, 0.0f, 0);
    MR::setBinderOffsetVec(this, &mBinderOffset, false);
    initRailRider(rIter);
    MR::moveCoordAndTransToNearestRailPos(this);
    Rot3f mtx;
    MR::makeMtxTR(mtx, this);
    mtx.getQuat(mPoseQuat);
    updatePoseByRail();
    MR::initAndSetRailClipping(&mClipCenter, this, cNum_init_f1, cNum_init_f2);
    initEffectKeeper(1, nullptr);
    MR::declareCoin(this, 1);
    MR::declareStarPiece(this, 3);
    MR::initShadowVolumeSphere(this, cNum_init_f3);
    initSound(8, false);
    initNerve(GET_NERVE_ANON(PukupukuWait));
    mStateLanding = new PukupukuStateLanding(this);
    MR::initStarPointerTarget(this, cNum_init_f4);
    mScaleCtrl = new AnimScaleController(nullptr);
    mStarPointer = new WalkerStateBindStarPointer(this, mScaleCtrl);
    MR::addToAttributeGroupSearchTurtle(this);
    makeActorAppeared();
}

// Trampled it drops a coin, otherwise star pieces.
void Pukupuku::kill() {
    if (isInWater()) {
        MR::emitEffect(this, "DeathWater");
        MR::startSound(this, "SE_EM_EXPLODE_UNDER_WATER");
    } else {
        MR::emitEffect(this, "Death");
        MR::startSound(this, "SE_EM_EXPLODE_S");
    }

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    if (mIsTrampled) {
        MR::appearCoinPop(this, mPosition, 1);
    } else {
        MR::appearStarPiece(this, mPosition, 3, cNum_kill_f1, cNum_kill_f2, false);
        if (isInWater()) {
            MR::startSound(this, "SE_OJ_STAR_PIECE_BURST_W");
        } else {
            MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        }
    }

    LiveActor::kill();
}

// On land the body stays upright: the rail direction turns it about the up axis only.
void Pukupuku::updatePoseByRailIgnoreUpScale() {
    Vec3f up = -mGravity;
    const Vec3f& rRailDir = MR::getRailDirection(this);
    if (!MR::isSameDirection(rRailDir, up)) {
        Quat4f quat;
        MR::makeQuatFromVec(&quat, rRailDir, up);
        mPoseQuat.slerp(quat, sRailPoseRate);
    }
}

// Rolls half a turn: it swims belly up and jumps belly down.
void Pukupuku::rotatePoseByLocalZ() {
    Quat4f rotZ;
    rotZ.setEulerZ(MR::pi());
    mPoseQuat.mult(mPoseQuat, rotZ);
}

void Pukupuku::calcGroundHitMtx(Pos3f* pMtx) const {
    Vec3f zDir;
    mPoseQuat.getZDir(zDir);
    MR::calcMtxFromGravityAndZAxis(pMtx, this, mGravity, zDir);
}

bool Pukupuku::isReadyToJumpFromLand() const {
    s32 jumpPoint = -1;
    MR::getRailPointArg0NoInit(this, MR::getCurrentRailPointNo(this), &jumpPoint);
    return jumpPoint == 1;
}

bool Pukupuku::isDamaged() const {
    return isNerve(GET_NERVE_ANON(PukupukuTrampled)) || isNerve(GET_NERVE_ANON(PukupukuBlownOff));
}

bool Pukupuku::isInWater() const {
    return MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f));
}

// Placed out of water it starts by hopping on land.
void Pukupuku::exeWait() {
    if (isInWater()) {
        setNerve(GET_NERVE_ANON(PukupukuMoveWater));
    } else {
        startLanding(GET_NERVE_ANON(PukupukuStateLandingLandingMoveLand));
    }
}

void Pukupuku::exeMoveWater() {
    if (tryBindStarPointer()) {
        return;
    }

    if (MR::isFirstStep(this)) {
        startAnim("Swim", "OpenEye");
    }

    MR::startLevelSound(this, "SE_EM_LV_PUKUPUKU_SWIM");
    if (MR::isEqualStringCase(MR::getPlayingBckName(this), "BodyAttack") && MR::isBckStopped(this)) {
        startAnim("Swim", "OpenEye");
    }

    exeMoveWaterCommon();
}

void Pukupuku::exeMoveWaterAfterJump() {
    if (MR::isFirstStep(this)) {
        startAnim("SwimStart", "OpenEye");
        rotatePoseByLocalZ();
        MR::startSound(this, "SE_EM_PUKUPUKU_WATER_IN");
    }

    updateMoveWaterAfterJump();
}

void Pukupuku::exeLanding() {
    if (tryBindStarPointer()) {
        return;
    }

    mStateLanding->update();
    if (mStateLanding->mIsDead) {
        setNerve(GET_NERVE_ANON(PukupukuMoveWaterAfterJump));
    }
}

// Flattened against the ground, falls (slowing in the air) and dies.
void Pukupuku::exeTrampled() {
    if (MR::isFirstStep(this)) {
        Pos3f hitMtx;
        calcGroundHitMtx(&hitMtx);
        hitMtx.getQuat(mPoseQuat);
        mVelocity.set(mGravity * sTrampleFallSpeed);
        startAnim("Flat", "CloseEye");
        MR::startSound(this, "SE_EM_STOMPED_S");
    } else if (!MR::isBinded(this)) {
        mVelocity.scale(sTrampleFriction);
    }

    if (MR::isStep(this, sTrampledSteps)) {
        kill();
    }
}

void Pukupuku::exeBlownOff() {
    if (MR::isFirstStep(this)) {
        startAnim("Rotate", "CloseEye");

        if (isInWater()) {
            MR::emitEffect(this, "SpinDamageWater");
        } else {
            MR::emitEffect(this, "SpinDamage");
        }

        MR::startBlowHitSound(this);
    } else if (!MR::isBinded(this)) {
        Vec3f fall(mGravity);
        fall.scale(getBlownOffSpeedRate());
        mVelocity.add(fall);
    }

    MR::stopSceneAtStep(this, 2, 4);
    if (MR::isStep(this, sBlownOffSteps)) {
        kill();
    }
}

// Back from a bind in a jump, the jump's animations resume where they were.
void Pukupuku::exeBindStarPointer() {
    if (!MR::updateActorStateAndNextNerve(this, mStarPointer, mNerveAfterBind) || mNerveAfterBind == GET_NERVE_ANON(PukupukuMoveWater)) {
        return;
    }

    if (mBckFrameBeforeBind == 0.0f) {
        return;
    }

    MR::startBck(this, mBckBeforeBind);
    MR::startBtp(this, getBtpName(mBckBeforeBind));
    MR::setBckFrame(this, mBckFrameBeforeBind);
    MR::setBtpFrame(this, mBtpFrameBeforeBind);
}

// In water the body's Y axis turns toward the rail direction.
void Pukupuku::updatePoseByRail() {
    Vec3f yDir;
    mPoseQuat.getYDir(yDir);

    const Vec3f& rRailDir = MR::getRailDirection(this);
    if (!MR::isSameDirection(rRailDir, yDir)) {
        Quat4f quat;
        MR::makeQuatFromVec(&quat, rRailDir, yDir);
        mPoseQuat.slerp(quat, sRailPoseRate);
    }
}

void Pukupuku::updateMoveWaterAfterJump() {
    if (tryBindStarPointer()) {
        return;
    }

    exeMoveWaterCommon();

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE_ANON(PukupukuMoveWater));
    }
}

// Remembers the nerve and the animation frames to return to.
bool Pukupuku::tryBindStarPointer() {
    if (!mStarPointer->tryStartPointBind()) {
        return false;
    }

    mNerveAfterBind = nullptr;
    mBckBeforeBind = MR::getPlayingBckName(this);

    if (isNerve(GET_NERVE_ANON(PukupukuMoveWater))) {
        mNerveAfterBind = GET_NERVE_ANON(PukupukuMoveWater);
    }

    if (isNerve(GET_NERVE_ANON(PukupukuMoveWaterAfterJump)) || isNerve(GET_NERVE_ANON(PukupukuMoveWaterAfterJumpAfterPointing))) {
        mNerveAfterBind = GET_NERVE_ANON(PukupukuMoveWaterAfterJumpAfterPointing);
    }

    if (isNerve(GET_NERVE_ANON(PukupukuLanding))) {
        mNerveAfterBind = GET_NERVE_ANON(PukupukuLanding);
    }

    mBckFrameBeforeBind = MR::getBckFrame(this);
    mBtpFrameBeforeBind = MR::getBtpFrame(this);
    setNerve(GET_NERVE_ANON(PukupukuBindStarPointer));
    return true;
}

void Pukupuku::exeMoveWaterCommon() {
    MR::moveCoordAndFollowTrans(this, sSwimSpeed);
    updatePoseByRail();

    if (isReadyToJumpFromLand()) {
        startLanding(GET_NERVE_ANON(PukupukuStateLandingLandingJumpFromWater));
    }
}

void Pukupuku::startLanding(const Nerve* pLandingNerve) {
    mStateLanding->appear();
    mStateLanding->setNerve(pLandingNerve);
    setNerve(GET_NERVE_ANON(PukupukuLanding));
}

f32 Pukupuku::getBlownOffSpeedRate() const {
    return isInWater() ? sBlowRateInWater : sBlowRateOnLand;
}

void Pukupuku::startBlownOff(HitSensor* pAttacker, HitSensor* pVictim) {
    f32 rate = getBlownOffSpeedRate();
    MR::setVelocityBlowAttack(this, pAttacker, pVictim, sBlowSpeed * rate, sBlowUpSpeed * rate, 4);
    setNerve(GET_NERVE_ANON(PukupukuBlownOff));
}

// The binder sits a radius above the body; bound by the pointer, the body sensor widens to knock other Pukupuku away.
void Pukupuku::control() {
    mBinderOffset.set(-mGravity);
    mBinderOffset.scale(sBinderRadius);
    mScaleCtrl->updateNerve();

    if (isNerve(GET_NERVE_ANON(PukupukuBindStarPointer))) {
        MR::setSensorRadius(this, "body", sBodyRadiusBound);
    } else {
        MR::setSensorRadius(this, "body", sBodyRadius);
    }
}

void Pukupuku::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver) && !isDamaged()) {
        if (MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
            if (isNerve(GET_NERVE_ANON(PukupukuMoveWater))) {
                startAnim("BodyAttack", "CloseEye");
            }
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    } else if (isNerve(GET_NERVE_ANON(PukupukuBindStarPointer)) && MR::isSensorType(pReceiver, ATYPE_PUKUPUKU) &&
               MR::sendMsgEnemyAttack(pReceiver, pSender)) {
        startBlownOff(pReceiver, pSender);
    }
}

bool Pukupuku::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if (isDamaged()) {
        return false;
    }

    if (MR::isMsgPlayerTrample(msg) || MR::isMsgPlayerHipDrop(msg)) {
        setNerve(GET_NERVE_ANON(PukupukuTrampled));
        mIsTrampled = true;
        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        startBlownOff(pSender, pReceiver);
        return true;
    }

    return false;
}

bool Pukupuku::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* pSender, HitSensor* pReceiver) {
    if (isDamaged() || !MR::isSensorType(pSender, ATYPE_PUKUPUKU)) {
        return false;
    }

    startBlownOff(pSender, pReceiver);
    return true;
}

void Pukupuku::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);
    Vec3f scale = mScale;
    scale.mul(scale, mScaleCtrl->mScale);
    MR::setBaseScale(this, scale);
}
