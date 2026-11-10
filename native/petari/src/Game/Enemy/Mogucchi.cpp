#include "Game/Enemy/Mogucchi.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/MogucchiHill.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "math_types.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeScatter_f1, "exeScatter.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeEulerRotation_f1, "makeEulerRotation.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateReferenceMtx_f1, "updateReferenceMtx.f1", 180.0f);

namespace {
    NEW_NERVE_ONEND(MogucchiNrvStroll, Mogucchi, Stroll, Stroll);
    NEW_NERVE(MogucchiNrvAppearDown, Mogucchi, AppearDown);
    NEW_NERVE(MogucchiNrvDown, Mogucchi, Down);
    NEW_NERVE(MogucchiNrvDive, Mogucchi, Dive);
    NEW_NERVE(MogucchiNrvScatter, Mogucchi, Scatter);
    NEW_NERVE(MogucchiNrvDie, Mogucchi, Die);

    NEBULA_TUNABLE_RECORD(Vec, cHeadOffset, {40.0f, 0.0f, -8.0f});
    NEBULA_TUNABLE(f32, cHeadRadius, 83.0f);
    NEBULA_TUNABLE(f32, cBodyRadius, 120.0f);
    NEBULA_TUNABLE(f32, cSpinRadius, 180.0f);
    NEBULA_TUNABLE(f32, cDefaultMaxStrollSpeed, 5.0f);
    NEBULA_TUNABLE(f32, cStrollAccel, 0.1f);
    // A hip drop landing this close knocks it out of the ground.
    NEBULA_TUNABLE(f32, cHipDropReach, 800.0f);
    const s32 cDownTime = NebulaFrameClock::stepsOfMilliseconds(500);
    // Hits are ignored for the first frames of being knocked up.
    NEBULA_TUNABLE(s32, cAppearDownGuardTime, 10);
    NEBULA_TUNABLE(s32, cHitStopFrame, 8);
    NEBULA_TUNABLE(f32, cScatterUpSpeed, 50.0f);
    NEBULA_TUNABLE(f32, cScatterUpDecel, 1.2f);
    NEBULA_TUNABLE(f32, cScatterSideSpeed, 23.0f);
    NEBULA_TUNABLE(s32, cScatterTime, 15);
    const s32 cDieTime = NebulaFrameClock::stepsOfSeconds(1);
    // The hill gets one piece per cHillPieceLength of rail, up to cHillPieceMax; the last cHillHiddenPieces stay hidden.
    NEBULA_TUNABLE(f32, cHillPieceLength, 150.0f);
    NEBULA_TUNABLE(s32, cHillPieceMax, 20);
    NEBULA_TUNABLE(s32, cHillHiddenPieces, 5);
    NEBULA_TUNABLE(f32, cHillAppearDistance, 80.0f);
    NEBULA_TUNABLE(f32, cHillAppearDistanceMax, 100.0f);
    NEBULA_TUNABLE(f32, cPointerEffectScale, 1.4f);
    NEBULA_TUNABLE(f32, cGroundCheckLength, 1000.0f);
}  // namespace

Mogucchi::Mogucchi(const char* pName)
    : LiveActor(pName), mHill(), mHole(), mScatterUpSpeed(), mScatterDir(0.0f, 0.0f, 1.0f), mStrollSpeed(), mMaxStrollSpeed(::cDefaultMaxStrollSpeed),
      mIsStoppedByP2() {
    mHoleMtx.identity();
}

void Mogucchi::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mMaxStrollSpeed);
    initModelManagerWithAnm("Mogucchi", nullptr, false);
    MR::connectToSceneEnemy(this);
    initSensor();
    initEffectKeeper(1, nullptr);
    MR::addEffect(this, "PointerTouchManual");
    MR::setEffectHostMtx(this, "PointerTouchManual", MR::getJointMtx(this, "Nose"));
    initSound(4, false);
    initRailRider(rIter);
    MR::declareCoin(this, 1);
    initNerve(GET_NERVE_ANON(MogucchiNrvStroll));
    MR::useStageSwitchWriteDead(this, rIter);

    MR::initStarPointerTargetAtJoint(this, "Head", ::cHeadRadius, Vec3f(::cHeadOffset));
    createMogucchiHill();
    createHole();
    MR::startBck(this, "Walk");
    MR::startBtp(this, "EyeOpen");
    calcAnim();
    makeActorAppeared();
}

void Mogucchi::initAfterPlacement() {
    MR::moveCoordToNearestPos(this, mPosition);
    MR::calcGravityVector(this, MR::getRailPos(this), &mRailGravity, nullptr, 0);
    updatePosition();
    updateReferenceMtx();
}

void Mogucchi::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    MR::showModel(this);
    setNerve(GET_NERVE_ANON(MogucchiNrvStroll));
    mHole->makeActorAppeared();
    MR::showModel(mHole);
    MR::validateClipping(this);
}

void Mogucchi::kill() {
    LiveActor::kill();

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    setNerve(GET_NERVE_ANON(MogucchiNrvDie));

    if (!MR::isDead(mHole)) {
        mHole->kill();
    }
}

void Mogucchi::control() {
    if (isNerve(GET_NERVE_ANON(MogucchiNrvScatter)) || isNerve(GET_NERVE_ANON(MogucchiNrvDie))) {
        return;
    }

    updateReferenceMtx();
}

bool Mogucchi::checkHipDrop() const {
    return MR::isPlayerHipDropLand() && !(MR::calcDistanceToPlayer(this) > ::cHipDropReach);
}

void Mogucchi::exeStroll() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Walk");
        MR::startBtp(this, "EyeOpen");
        MR::startBck(mHole, "Walk");
        mHill->start();
        mStrollSpeed = mMaxStrollSpeed;
        mIsStoppedByP2 = false;
    }

    MR::startLevelSound(this, "SE_EM_LV_MOGHILL_MOVE");
    updateStrollSpeed();
    reflectStarPointer2P();
    MR::moveCoord(this, mStrollSpeed);
    MR::calcGravityVector(this, MR::getRailPos(this), &mRailGravity, nullptr, 0);
    updatePosition();
    makeEulerRotation();

    if (checkHipDrop()) {
        setNerve(GET_NERVE_ANON(MogucchiNrvAppearDown));
    }
}

void Mogucchi::exeAppearDown() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::startBck(this, "SwoonStart");
        MR::startBtp(this, "EyeClose");
        MR::startBck(mHole, "SwoonStart");
        MR::startSound(this, "SE_EM_MOGUCCHI_APPEAR");
        MR::startSound(this, "SE_EV_MOGUCCHI_SWOON");

        MR::setSensorOffset(this, "body", Vec3f(0.0f, 0.0f, 0.0f));
        MR::setSensorOffset(this, "spin", Vec3f(0.0f, 0.0f, 0.0f));
    }

    MR::startLevelSound(this, "SE_EM_LV_SWOON_S");
    MR::setNerveAtBckStopped(this, GET_NERVE_ANON(MogucchiNrvDown));
}

void Mogucchi::exeDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Swoon");
        MR::startBtp(this, "EyeClose");
        MR::startBck(mHole, "Swoon");
    }

    MR::startLevelSound(this, "SE_EM_LV_SWOON_S");
    MR::setNerveAtStep(this, GET_NERVE_ANON(MogucchiNrvDive), ::cDownTime);
}

void Mogucchi::exeDive() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Hide");
        MR::startBtp(this, "EyeOpen");
        MR::startBck(mHole, "Hide");
        MR::startSound(this, "SE_EM_MOGUCCHI_HIDE");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE_ANON(MogucchiNrvStroll));
    }
}

// Knocked away: up against gravity (slowing each frame) and along mScatterDir, facing back along it.
void Mogucchi::exeScatter() {
    if (MR::isFirstStep(this)) {
        mHill->end();
        MR::startBck(this, "PunchDown");
        MR::startBtp(this, "EyeClose");
        MR::startBck(mHole, "PunchDown");
        MR::startBlowHitSound(this);
        mScatterUpSpeed = ::cScatterUpSpeed;
        MR::invalidateClipping(this);
    }

    mScatterDir.orthogonalize(mRailGravity);
    MR::normalizeOrZero(&mScatterDir);

    if (!MR::isNearZero(mScatterDir)) {
        Rot3f mtx;
        mtx.setXDir(mRailGravity.cross(mScatterDir));
        mtx.setYDir(-mRailGravity);
        mtx.setZDir(-mScatterDir);
        mtx.getEulerXYZ(mRotation);
        mRotation.mult(cNum_exeScatter_f1 / PI);
    }

    mPosition.add(mRailGravity.scaleInline(-mScatterUpSpeed) + mScatterDir.multInLine(::cScatterSideSpeed));
    mScatterUpSpeed -= ::cScatterUpDecel;

    if (MR::isGreaterEqualStep(this, ::cScatterTime)) {
        setNerve(GET_NERVE_ANON(MogucchiNrvDie));
    }
}

void Mogucchi::exeDie() {
    if (MR::isFirstStep(this)) {
        MR::appearCoinPop(this, mPosition, 1);
        MR::startSound(this, "SE_EM_EXPLODE_S");
        MR::emitEffect(this, "Death");
        MR::hideModel(this);
        MR::hideModel(mHole);
    }

    if (MR::isGreaterEqualStep(this, ::cDieTime)) {
        kill();
    }
}

void Mogucchi::endStroll() {
    MR::deleteEffect(this, "PointerTouchManual");
}

// Only a strolling Mogucchi hurts a grounded player; otherwise it just pushes.
void Mogucchi::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayerOrRide(pReceiver) || !(MR::isSensor(pSender, "body") || MR::isSensor(pSender, "head"))) {
        return;
    }

    if (isNerve(GET_NERVE_ANON(MogucchiNrvStroll)) && MR::isOnGroundPlayer()) {
        Vec3f attackDir;
        calcAttackDir(&attackDir, pSender->mPosition, pReceiver->mPosition);

        if (MR::sendMsgEnemyAttackStrongToDir(pReceiver, pSender, attackDir)) {
            MR::shakeCameraNormal();
            return;
        }
    }

    MR::sendMsgPush(pReceiver, pSender);
}

bool Mogucchi::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensor(pReceiver, "spin")) {
        return receiveAttackBySpinSensor(msg, pSender, pReceiver);
    }

    return receiveAttackByBodySensor(msg, pSender, pReceiver);
}

void Mogucchi::initSensor() {
    LiveActor::initHitSensor(3);
    MR::addHitSensorAtJointEnemy(this, "head", "Head", 32, ::cHeadRadius, Vec3f(::cHeadOffset));
    MR::addHitSensorAtJointEnemy(this, "body", "Spine", 32, ::cBodyRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemy(this, "spin", 16, ::cSpinRadius, Vec3f(0.0f, 0.0f, 0.0f));
}

void Mogucchi::createMogucchiHill() {
    s32 pieceNum = MR::getRailTotalLength(this) / ::cHillPieceLength;
    if (pieceNum > ::cHillPieceMax) {
        pieceNum = ::cHillPieceMax;
    }

    mHill = new MogucchiHill(this, pieceNum, "モグッチ塚");
    mHill->initWithoutIter();
    if (pieceNum > ::cHillHiddenPieces) {
        mHill->setAppearNum(mHill->mPieceNum - ::cHillHiddenPieces);
    }

    mHill->mAppearDistance = ::cHillAppearDistance;
    mHill->mAppearDistanceMax = ::cHillAppearDistanceMax;
}

void Mogucchi::updateStrollSpeed() {
    if (!(mStrollSpeed < mMaxStrollSpeed)) {
        return;
    }

    mStrollSpeed += ::cStrollAccel;
    if (mStrollSpeed > mMaxStrollSpeed) {
        mStrollSpeed = mMaxStrollSpeed;
    }
}

// The second player's pointer holds Mogucchi still while it is pressed.
void Mogucchi::reflectStarPointer2P() {
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        if (!mIsStoppedByP2) {
            mIsStoppedByP2 = true;
            MR::startPointerHitSound();
            MR::emitEffectWithScale(this, "PointerTouchManual", ::cPointerEffectScale, -1);
        }

        mStrollSpeed = 0.0f;
        MR::startPointerFreezeLevelSound(this);
        return;
    }

    if (mIsStoppedByP2) {
        mIsStoppedByP2 = false;
        MR::deleteEffect(this, "PointerTouchManual");
    }
}

// Stands on the map below the rail point, or on the rail itself where there is no ground.
void Mogucchi::updatePosition() {
    Triangle triangle;
    Vec3f groundPos;

    if (MR::getFirstPolyOnLineToMap(&groundPos, &triangle, MR::getRailPos(this), mRailGravity.scaleInline(::cGroundCheckLength))) {
        mPosition.set(groundPos);
    } else {
        mPosition.set(MR::getRailPos(this));
    }
}

void Mogucchi::createHole() {
    mHole = new ModelObj("モグッチ穴", "MogucchiHole", mHoleMtx, MR::DrawBufferType_MapObjStrongLight, -2, -2, false);
    mHole->initWithoutIter();
}

// Away from Mogucchi along the ground (or its up axis when the two meet), tipped 45 degrees up.
void Mogucchi::calcAttackDir(Vec3f* pDir, const Vec3f& rSenderPos, const Vec3f& rReceiverPos) const {
    pDir->sub(rReceiverPos, rSenderPos);
    pDir->orthogonalize(mRailGravity);
    MR::normalizeOrZero(pDir);

    if (MR::isNearZero(*pDir)) {
        pDir->set< f32 >(getBaseMtx()[0][1], getBaseMtx()[1][1], getBaseMtx()[2][1]);
    }

    pDir->add(-mRailGravity);
    MR::normalize(pDir);
}

void Mogucchi::makeEulerRotation() {
    Pos3f mtx;
    MR::makeMtxUpFront(&mtx, -mRailGravity, MR::getRailDirection(this));
    mtx.getEulerXYZ(mRotation);
    mRotation.mult(cNum_makeEulerRotation_f1 / PI);
}

void Mogucchi::calcScatterVec(const Vec3f& rStart, const Vec3f& rEnd) {
    mScatterDir.sub(rEnd, rStart);
    mScatterDir.orthogonalize(mRailGravity);
    MR::normalizeOrZero(&mScatterDir);
}

// Knocked up, swooning or diving back down; the first frames of being knocked up do not count.
bool Mogucchi::isHittableDown() const {
    if (!isNerve(GET_NERVE_ANON(MogucchiNrvDown)) && !isNerve(GET_NERVE_ANON(MogucchiNrvAppearDown)) && !isNerve(GET_NERVE_ANON(MogucchiNrvDive))) {
        return false;
    }

    return !(isNerve(GET_NERVE_ANON(MogucchiNrvAppearDown)) && MR::isLessStep(this, ::cAppearDownGuardTime));
}

void Mogucchi::startScatter(HitSensor* pSender, HitSensor* pReceiver) {
    MR::stopScene(::cHitStopFrame);
    MR::tryRumblePadMiddle(this, PLAYER_1);
    calcScatterVec(pSender->mPosition, pReceiver->mPosition);
    setNerve(GET_NERVE_ANON(MogucchiNrvScatter));
}

// A spin bounces off a strolling Mogucchi and knocks a downed one away.
bool Mogucchi::receiveAttackBySpinSensor(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isMsgPlayerSpinAttack(msg)) {
        return false;
    }

    if (isNerve(GET_NERVE_ANON(MogucchiNrvStroll))) {
        MR::startSound(this, "SE_EM_MOGUCCHI_REFRECT");
        MR::sendMsgEnemyAttackCounterSpin(pSender, pReceiver);
        return true;
    }

    if (!isHittableDown()) {
        return false;
    }

    startScatter(pSender, pReceiver);
    return true;
}

bool Mogucchi::receiveAttackByBodySensor(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerSpinAttack(msg)) {
        return false;
    }

    if (!isNerve(GET_NERVE_ANON(MogucchiNrvScatter)) && !isNerve(GET_NERVE_ANON(MogucchiNrvDie)) && MR::isMsgStarPieceAttack(msg)) {
        setNerve(GET_NERVE_ANON(MogucchiNrvAppearDown));
        return true;
    }

    const bool isDown =
        isNerve(GET_NERVE_ANON(MogucchiNrvDown)) || isNerve(GET_NERVE_ANON(MogucchiNrvAppearDown)) || isNerve(GET_NERVE_ANON(MogucchiNrvDive));

    if (!isDown) {
        if (MR::isMsgPlayerTrample(msg)) {
            MR::startSound(this, "SE_EM_MOGUCCHI_REFRECT");
            return true;
        }

        return false;
    }

    if (!isHittableDown()) {
        return false;
    }

    if (MR::isMsgPlayerTrample(msg) || MR::isMsgPlayerHipDrop(msg)) {
        MR::startBck(this, "Bounce");
        MR::startBtp(this, "EyeOpen");
        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        startScatter(pSender, pReceiver);
        return true;
    }

    return false;
}

// The hole model follows Mogucchi through mHoleMtx.
void Mogucchi::updateReferenceMtx() {
    Vec3f rotate = mRotation * (PI / cNum_updateReferenceMtx_f1);

    mHoleMtx.setRT(rotate, mPosition);

    mHole->mPosition.set(mPosition);
}
