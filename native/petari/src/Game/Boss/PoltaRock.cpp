#include "Game/Boss/PoltaRock.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/Polta.hpp"
#include "Game/Boss/PoltaArm.hpp"
#include "Game/Boss/PoltaFunction.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 cRadius = 120.0f;
    // Follow speed and distance band while floating beside the owner.
    const f32 cFollowSpeedMin = 0.0f;
    const f32 cFollowSpeedMax = 1.2f;
    const f32 cFollowLengthMin = 0.0f;
    const f32 cFollowLengthMax = 200.0f;
    const f32 cFloatHeight = 800.0f;
    const f32 cFloatHeightSpeed = 1.5f;
    const f32 cFloatHeightRange = 300.0f;
    const f32 cFloatFriction = 0.92f;
    const s32 cFloatTime = NebulaFrameClock::stepsOfMilliseconds(1500);
    const s32 cSignTime = NebulaFrameClock::stepsOfSeconds(1);
    // The throw speeds up over cThrowAccelTime frames, then the rock drifts up against gravity until it hits or times out.
    const s32 cThrowAccelTime = NebulaFrameClock::stepsOfMilliseconds(500);
    const f32 cThrowAccel = 3.0f;
    const f32 cFlyGravity = -0.02f;
    const f32 cFlyFriction = 0.999f;
    const s32 cFlyTime = NebulaFrameClock::stepsOfSeconds(5);
}  // namespace

namespace NrvPoltaRock {
    NEW_NERVE(PoltaRockNrvFloat, PoltaRock, Float);
    NEW_NERVE(PoltaRockNrvSign, PoltaRock, Sign);
    NEW_NERVE(PoltaRockNrvMove, PoltaRock, Move);
}  // namespace NrvPoltaRock

PoltaRock::PoltaRock(const char* pName)
    : LiveActor(pName), mOwner(nullptr), mRollQuat(0.0f, 0.0f, 0.0f, 1.0f), mChaseDir(0.0f, 0.0f, 1.0f), mOwnerLocalPos(0.0f, 0.0f, 0.0f), mColor(Color_White) {
    lumacore::setClass(this, lumacore::kClassId< PoltaRock >);
}

void PoltaRock::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("PoltaRock", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    MR::onCalcGravity(this);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "Body", 8, ::cRadius, Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(::cRadius, 0.0f, 0);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    MR::initShadowFromCSV(this, "Shadow");
    initNerve(GET_NERVE(PoltaRock, PoltaRockNrvFloat));
    MR::invalidateClipping(this);
    makeActorDead();
}

// A black rock leaves no break effect; its bomb boo is the break.
void PoltaRock::kill() {
    MR::startSound(this, "SE_OJ_POLTA_ROCK_BREAK");
    LiveActor::kill();
    if (mColor == Color_Yellow) {
        MR::emitEffect(this, "BreakYellow");
    } else if (mColor == Color_White) {
        MR::emitEffect(this, "BreakWhite");
    }
    mOwner = nullptr;
}

void PoltaRock::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mRollQuat);
    MR::setBaseScale(this, mScale);
}

void PoltaRock::start(Polta* pOwner, const Vec3f& rPosition, const Vec3f& rOwnerLocalPos) {
    mOwner = pOwner;
    mPosition.set(rPosition);
    mOwnerLocalPos.set(rOwnerLocalPos);
    setNerve(GET_NERVE(PoltaRock, PoltaRockNrvFloat));
    appear();
    MR::offBind(this);
}

void PoltaRock::setColor(Color color) {
    mColor = color;
    MR::startBva(this, "Color");
    MR::setBvaFrameAndStop(this, static_cast< f32 >(color));
}

void PoltaRock::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if (!isEnableAttack()) {
        MR::sendMsgPush(pReceiver, pSender);
        return;
    }

    if (MR::sendMsgEnemyAttack(pReceiver, pSender)) {
        generateKill();
    }
}

bool PoltaRock::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg) || (MR::isMsgPlayerSpinAttack(msg) && isEnableAttack())) {
        MR::zeroVelocity(this);
        generateKill();
        return true;
    }

    return false;
}

bool PoltaRock::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgExplosionAttack(msg) && isEnableExplosion()) {
        MR::zeroVelocity(this);
        generateKill();
        return true;
    }

    return false;
}

void PoltaRock::followOwner() {
    Vec3f target;
    NebulaMtxMultVec(getOwner()->getBaseMtx(), &mOwnerLocalPos, target);
    MR::addVelocityMoveToTarget(this, target, ::cFollowSpeedMin, ::cFollowSpeedMax, ::cFollowLengthMin, ::cFollowLengthMax);
    MR::addVelocityKeepHeightUseShadow(this, ::cFloatHeight, ::cFloatHeightSpeed, ::cFloatHeightRange, nullptr);
    MR::attenuateVelocity(this, ::cFloatFriction);
}

void PoltaRock::exeFloat() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_POLTA_ROCK_FLOAT_ST");
        MR::startAction(this, "Froat");
        MR::emitEffect(this, "Appear");
    }

    MR::startLevelSound(this, "SE_OJ_LV_POLTA_ROCK_FLOAT");
    followOwner();

    if (MR::isGreaterStep(this, ::cFloatTime)) {
        setNerve(GET_NERVE(PoltaRock, PoltaRockNrvSign));
        MR::onBind(this);
    }
}

void PoltaRock::exeSign() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Sign");
    }

    followOwner();

    if (MR::isGreaterStep(this, ::cSignTime)) {
        setNerve(GET_NERVE(PoltaRock, PoltaRockNrvMove));
        MR::onBind(this);
    }
}

void PoltaRock::exeMove() {
    if (MR::isFirstStep(this)) {
        mChaseDir.set(*MR::getPlayerCenterPos() - mPosition);
        MR::normalizeOrZero(&mChaseDir);
        MR::startSound(this, "SE_OJ_POLTA_ROCK_FLY");
        MR::zeroVelocity(this);
    }

    if (MR::isLessStep(this, ::cThrowAccelTime)) {
        mVelocity.add(mChaseDir * (::cThrowAccel * MR::calcNerveRate(this, ::cThrowAccelTime)));
    }

    MR::addVelocityToGravity(this, ::cFlyGravity);
    MR::attenuateVelocity(this, ::cFlyFriction);
    MR::rotateQuatRollBall(&mRollQuat, mVelocity, -mGravity, ::cRadius);

    if (MR::isBinded(this)) {
        MR::sendMsgEnemyAttackToBindedSensor(this, getSensor("Body"));
        MR::tryRumbleDefaultHit(this, 0);
        generateKill();
        return;
    }

    if (MR::isGreaterStep(this, ::cFlyTime)) {
        kill();
    }
}

// Black rocks free a bomb boo and yellow ones a coin.
void PoltaRock::generateKill() {
    if (mColor == Color_Black) {
        PoltaFunction::appearBombTeresaNormal(getOwner(), mPosition, Vec3f(0.0f, 0.0f, 0.0f));
    } else if (mColor == Color_Yellow) {
        MR::appearCoinPop(getOwner(), mPosition, 1);
    }

    kill();
}

bool PoltaRock::isEnableAttack() const {
    return isNerve(GET_NERVE(PoltaRock, PoltaRockNrvMove));
}

// Explosions pass a rock that is still floating out (Float lasts cFloatTime + 1 frames).
bool PoltaRock::isEnableExplosion() const {
    return !isNerve(GET_NERVE(PoltaRock, PoltaRockNrvFloat));
}
