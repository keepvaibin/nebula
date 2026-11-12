#include "Game/MapObj/AirBubble.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 cHitSensorRadius = 130.0f;
    const f32 cSwingRange = 250.0f;
    // const s32 cGrowTime = _;
    const f32 cCorioriRotSpeed = 1.5f;
    // const f32 cCoriolisAccel = _;
    const f32 cMoveStartAccel = 7.0f;
    // const f32 cFloatAccel = _;
    const s32 cDefaultLife = 360;
};  // namespace

namespace NrvAirBubble {
    NEW_NERVE(AirBubbleNrvWait, AirBubble, Wait);
    NEW_NERVE(AirBubbleNrvMove, AirBubble, Move);
    NEW_NERVE(AirBubbleNrvBreak, AirBubble, Break);
    NEW_NERVE(AirBubbleNrvKillWait, AirBubble, KillWait);
};  // namespace NrvAirBubble

AirBubble::AirBubble(const char* pName) : LiveActor(pName), mDriftDir(0.0f, 0.0f, 1.0f), mBobOffset(), mLife(::cDefaultLife) {
    lumacore::setClass(this, lumacore::kClassId< AirBubble >);
}

void AirBubble::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);
    initModelManagerWithAnm("AirBubble", nullptr, false);
    MR::connectToSceneItem(this);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 8, mScale.x * ::cHitSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));
    initEffectKeeper(0, nullptr);
    initSound(2, false);
    initNerve(GET_NERVE(AirBubble, AirBubbleNrvWait));
    mIsFollowRail = false;
    makeActorAppeared();
    MR::setClippingFar100m(this);
    MR::startBck(this, "Move");
    mIsItemMoving = false;
}

void AirBubble::initAfterPlacement() {
    if (mIsFollowRail) {
        MR::moveCoordAndTransToNearestRailPos(this);
    }

    mBasePos = mPosition;
}

void AirBubble::appear() {
    LiveActor::appear();
}

void AirBubble::kill() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    LiveActor::kill();
}

void AirBubble::control() {
}

void AirBubble::appearMove(const Vec3f& rTrans, s32 life) {
    mBasePos.set(rTrans);
    mPosition.set(rTrans);
    appear();
    MR::showModel(this);
    setNerve(GET_NERVE(AirBubble, AirBubbleNrvMove));
    MR::invalidateClipping(this);

    mLife = life > 0 ? life : ::cDefaultLife;
}

void AirBubble::exeWait() {

    mBobOffset = MR::sin(MR::calcNerveRate(this, NebulaFrameClock::stepsOfSeconds(2)) * MR::pi() * 2.0f);

    if (mIsFollowRail) {
        if (MR::isRailReachedGoal(this)) {
            MR::reverseRailDirection(this);
        }

        f32 speed;

        if (MR::getCurrentRailPointArg0WithInit(this, &speed)) {
            mRailMoveSpeed = speed;
        }

        MR::moveCoordAndFollowTrans(this, mRailMoveSpeed);
    }

    if (mIsItemMoving) {
        return;
    }

    mPosition = mBasePos + Vec3f(0.0f, 1.0f, 0.0f) * mBobOffset * 1.0f;
}

void AirBubble::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::onCalcGravity(this);

        mVelocity.set((-mGravity).multInLine2(::cMoveStartAccel));
    }

    MR::rotateVecDegree(&mDriftDir, mGravity, ::cCorioriRotSpeed);
    mDriftDir.killElement(mDriftDir, mGravity);

    if (MR::isNearZero(mDriftDir)) {
        MR::getRandomVector(&mDriftDir, 1.0f);
    }

    MR::normalizeOrZero(&mDriftDir);
    mVelocity += mDriftDir * 0.1f;
    mVelocity -= mGravity * 0.3f;
    mVelocity.mult(0.85f);

    if (MR::isGreaterStep(this, mLife)) {
        MR::hideModel(this);
        MR::startSound(this, "SE_OJ_AIR_BUBBLE_BREAK");
        MR::emitEffect(this, "RecoveryBubbleBreak");
        MR::offCalcGravity(this);
        setNerve(GET_NERVE(AirBubble, AirBubbleNrvKillWait));
    }
}

void AirBubble::exeBreak() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_AIR_BUBBLE_BREAK");
        MR::emitEffect(this, "RecoveryBubbleBreak");
        MR::incPlayerOxygen(8);
        MR::tryRumblePadMiddle(this, PLAYER_1);
        kill();
    }
}

void AirBubble::exeKillWait() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        kill();
    }
}

bool AirBubble::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isSensorPlayer(pSender)) {
        if (isNerve(GET_NERVE(AirBubble, AirBubbleNrvKillWait))) {
            MR::incPlayerOxygen(8);
            kill();

            return true;
        }

        setNerve(GET_NERVE(AirBubble, AirBubbleNrvBreak));

        return true;
    }

    return false;
}

bool AirBubble::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgItemShow(msg)) {
        return MR::receiveItemShowMsg(msg, pSender, pReceiver);
    }

    if (MR::isMsgItemHide(msg)) {
        return MR::receiveItemHideMsg(msg, pSender, pReceiver);
    }

    if (MR::isMsgItemStartMove(msg)) {
        mIsItemMoving = true;

        return true;
    }

    if (MR::isMsgItemEndMove(msg)) {
        mIsItemMoving = false;
        mBasePos = mPosition;

        return true;
    }

    if (MR::isMsgSpinStormRange(msg) && canSpinGet()) {
        if ((pSender->mPosition - mPosition).length() < ::cSwingRange) {
            setNerve(GET_NERVE(AirBubble, AirBubbleNrvBreak));
            return true;
        }
    }

    return false;
}

bool AirBubble::canSpinGet() const {
    if (isNerve(GET_NERVE(AirBubble, AirBubbleNrvWait)) || isNerve(GET_NERVE(AirBubble, AirBubbleNrvMove))) {
        return true;
    }

    return false;
}

