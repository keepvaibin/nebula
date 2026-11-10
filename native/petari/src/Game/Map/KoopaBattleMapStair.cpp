#include "Game/Map/KoopaBattleMapStair.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

namespace {
    struct EdgeOffset {
        f32 x;
        f32 z;
    };

    const s32 cDefaultTimeToBreak = NebulaFrameClock::stepsOfSeconds(5);
    NEBULA_TUNABLE(f32, cNormalHalfWidthX, 200.0f);
    NEBULA_TUNABLE(f32, cNormalHalfWidthZ, 300.0f);
    NEBULA_TUNABLE(f32, cHalfHeight, 100.0f);
    NEBULA_TUNABLE(f32, cBigHalfWidthX, 600.0f);
    NEBULA_TUNABLE(f32, cBigHalfWidthZ, 400.0f);
    NEBULA_TUNABLE(f32, cTurnHalfWidthX, 140.0f);
    NEBULA_TUNABLE(f32, cTurnHalfWidthZ, 300.0f);
    const s32 cDefaultStepWaitFall = NebulaFrameClock::stepsOfSeconds(1);
    const s32 cDefaultStepFall = NebulaFrameClock::stepsOfSeconds(4);
    NEBULA_TUNABLE(f32, cFallGravity, 0.03f);
    NEBULA_TUNABLE(f32, cFallSpeedMax, 1.5f);
    NEBULA_TUNABLE(f32, cClippingRadius, 500.0f);
    NEBULA_TUNABLE(f32, cBigClippingRadius, 800.0f);
    NEBULA_TUNABLE(f32, cDisappearBrkRate, 0.25f);
    // The eight edge and corner points round the stair top, in units of the half widths.
    NEBULA_TUNABLE_RECORDS(EdgeOffset, cEdgeOffset, , {{0.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 0.0f},  {1.0f, 1.0f},
                                      {0.0f, 1.0f},  {-1.0f, 1.0f}, {-1.0f, 0.0f}, {-1.0f, -1.0f}});
}  // namespace

namespace NrvKoopaBattleMapStair {
    NEW_NERVE(KoopaBattleMapStairNrvWaitSwitch, KoopaBattleMapStair, WaitSwitch);
    NEW_NERVE(KoopaBattleMapStairNrvWaitKoopaFire, KoopaBattleMapStair, WaitKoopaFire);
    NEW_NERVE(KoopaBattleMapStairNrvWaitFall, KoopaBattleMapStair, WaitFall);
    NEW_NERVE(KoopaBattleMapStairNrvFall, KoopaBattleMapStair, Fall);
    NEW_NERVE(KoopaBattleMapStairNrvDisappear, KoopaBattleMapStair, Disappear);
};  // namespace NrvKoopaBattleMapStair

KoopaBattleMapStair::KoopaBattleMapStair(const char* pName)
    : LiveActor(pName), mTimeToBreak(::cDefaultTimeToBreak), mFireAttackStep(-1), mLevel(), mTargetCorner(-1), mIsJumpAfterAttack(), mType(), mIsBig(),
      mIsTurn(), mIsKoopaAttack(), mTargetPos(0.0f, 0.0f, 0.0f), mWaitFallStep(::cDefaultStepWaitFall), mFallStep(::cDefaultStepFall) {
}

void KoopaBattleMapStair::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mTimeToBreak);
    MR::getPlacementArg1NoInit(rIter, &mLevel);
    MR::getPlacementArg2NoInit(rIter, &mType);
    MR::getPlacementArg3NoInit(rIter, &mWaitFallStep);
    MR::getPlacementArg4NoInit(rIter, &mFallStep);
    MR::getPlacementArg5NoInit(rIter, &mTargetCorner);
    MR::getPlacementArg6NoInit(rIter, &mIsJumpAfterAttack);

    const char* objName = nullptr;
    MR::getObjectName(&objName, rIter);

    initModelManagerWithAnm(objName, nullptr, false);

    if (MR::isEqualString(objName, "KoopaBattleMapStairBig")) {
        mIsBig = true;
    } else if (MR::isEqualString(objName, "KoopaBattleMapStairTurn")) {
        mIsTurn = true;
    }

    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    MR::initCollisionParts(this, objName, getSensor("body"), nullptr);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(KoopaBattleMapStair, KoopaBattleMapStairNrvWaitSwitch));
    MR::needStageSwitchReadA(this, rIter);

    MR::setClippingTypeSphere(this, mIsBig ? ::cBigClippingRadius : ::cClippingRadius);

    makeActorAppeared();
}

void KoopaBattleMapStair::initAfterPlacement() {
    if (!isTypeNoRequestFire()) {
        mFireAttackStep = KoopaFunction::registerBattleMapStair(this);
    }
}

bool KoopaBattleMapStair::isRequestAttackVs1() const {
    return isTypeNormal() && isNerve(GET_NERVE(KoopaBattleMapStair, KoopaBattleMapStairNrvWaitKoopaFire)) && MR::isStep(this, mFireAttackStep);
}

s32 KoopaBattleMapStair::calcRemainTimeToBreak() const {
    return mTimeToBreak - getNerveStep();
}

bool KoopaBattleMapStair::isRequestAttackVs3() const {
    return !mIsKoopaAttack && isTypeNormal() && isNerve(GET_NERVE(KoopaBattleMapStair, KoopaBattleMapStairNrvWaitKoopaFire)) &&
           (MR::isStep(this, mFireAttackStep) || mFireAttackStep < 0);
}

namespace {
    // A chosen corner takes that point outright; with none (-1) the point nearest the reference wins, ties to the later one.
    void updateTargetPos(Vec3f* pPos, f32* pDist, const Vec3f& rCandidatePos, const Vec3f& rReferencePos, s32 targetCorner, s32 corner) {
        if (targetCorner >= 0) {
            if (targetCorner == corner) {
                pPos->set(rCandidatePos);
            }
            return;
        }

        f32 dist = rCandidatePos.distance(rReferencePos);
        if (dist > *pDist) {
            return;
        }

        pPos->set(rCandidatePos);
        *pDist = dist;
    }
}  // namespace

// Starts at the top centre; returns the distance to the centre, or to the nearest point when no corner is chosen and one is nearer.
f32 KoopaBattleMapStair::calcAndSetTargetPos(Vec3f* pPos, const Vec3f& rReferencePos) {
    Vec3f axisZ;
    Vec3f axisY;
    Vec3f axisX;
    MR::calcActorAxis(&axisX, &axisY, &axisZ, this);

    Vec3f center = (axisY * ::cHalfHeight) + mPosition;
    f32 distance = center.distance(rReferencePos);
    pPos->set(center);

    if (!mIsBig && mIsTurn) {
        updateTargetPos(pPos, &distance, center + axisZ * ::cTurnHalfWidthZ - axisX * ::cTurnHalfWidthX, rReferencePos, -1, -1);
    } else {
        const f32 halfWidthX = mIsBig ? ::cBigHalfWidthX : ::cNormalHalfWidthX;
        const f32 halfWidthZ = mIsBig ? ::cBigHalfWidthZ : ::cNormalHalfWidthZ;

        for (s32 i = 0; i < ARRAY_SIZE(::cEdgeOffset); i++) {
            const EdgeOffset& rOffset = ::cEdgeOffset[i];
            Vec3f candidate = center + (axisX * halfWidthX * rOffset.x) + (axisZ * halfWidthZ * rOffset.z);
            updateTargetPos(pPos, &distance, candidate, rReferencePos, mTargetCorner, i);
        }
    }

    mTargetPos.set(*pPos);

    return distance;
}

f32 KoopaBattleMapStair::calcTimeRate() const {
    // A break time not past the fire step (placement arg 0) is already over: rate 1.
    const s32 span = mTimeToBreak - mFireAttackStep;
    if (span <= 0) {
        return 1.0f;
    }

    return static_cast< f32 >(getNerveStep() - mFireAttackStep) / span;
}

bool KoopaBattleMapStair::isBreak() const {
    return isNerve(GET_NERVE(KoopaBattleMapStair, KoopaBattleMapStairNrvWaitFall));
}

bool KoopaBattleMapStair::isTypeNormal() const {
    return mType == Type_Normal;
}

bool KoopaBattleMapStair::isTypeDemoFar() const {
    return mType == Type_DemoFar;
}

bool KoopaBattleMapStair::isTypeDemoNear() const {
    return mType == Type_DemoNear;
}

bool KoopaBattleMapStair::isTypeNoRequestFire() const {
    return mType == Type_NoRequestFire;
}

void KoopaBattleMapStair::exeWaitSwitch() {
    if (MR::isFirstStep(this)) {
        MR::startAllAnim(this, "Wait");
    }

    if (MR::isOnSwitchA(this)) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(KoopaBattleMapStair, KoopaBattleMapStairNrvWaitKoopaFire));
    }
}

void KoopaBattleMapStair::exeWaitKoopaFire() {
    if (MR::isStep(this, mTimeToBreak)) {
        setNerve(GET_NERVE(KoopaBattleMapStair, KoopaBattleMapStairNrvWaitFall));
    }
}

void KoopaBattleMapStair::exeWaitFall() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_STAIR_BREAK_START");
        MR::startAllAnim(this, "WaitFall");
    }

    MR::startLevelSound(this, "SE_OJ_LV_STAIR_BREAK");

    if (MR::isStep(this, mWaitFallStep)) {
        mVelocity.zero();
        setNerve(GET_NERVE(KoopaBattleMapStair, KoopaBattleMapStairNrvFall));
    }
}

void KoopaBattleMapStair::exeFall() {
    if (MR::isFirstStep(this)) {
        MR::onCalcGravity(this);
        MR::startAllAnim(this, "Fall");
    }

    MR::addVelocityToGravity(this, ::cFallGravity);
    MR::restrictVelocity(this, ::cFallSpeedMax);
    MR::startLevelSound(this, "SE_OJ_LV_STAIR_BREAK");

    if (MR::isStep(this, mFallStep)) {
        MR::startSound(this, "SE_OJ_STAIR_BREAK_END");
        setNerve(GET_NERVE(KoopaBattleMapStair, KoopaBattleMapStairNrvDisappear));
    }
}

void KoopaBattleMapStair::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startAllAnim(this, "Disappear");
        MR::setBrkRate(this, ::cDisappearBrkRate);
    }

    MR::addVelocityToGravity(this, ::cFallGravity);
    MR::restrictVelocity(this, ::cFallSpeedMax);

    if (MR::isBrkStopped(this)) {
        kill();
    }
}
