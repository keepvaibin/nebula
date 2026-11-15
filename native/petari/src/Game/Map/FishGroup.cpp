#include "Game/Map/FishGroup.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sDefaultFishNum = 10;
    const f32 sDefaultGroupSpeed = 5.0f;
    const f32 sFriction = 0.95f;
    const f32 sWanderAccel = 1.0f;
    const s32 sStepToWanderMoveMin = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sStepToWanderMoveMax = NebulaFrameClock::stepsOfSeconds(3);
    const f32 sApproachAccel = 5.0f;
    const s32 sStepToApproachMin = 5;
    const s32 sStepToApproachMax = NebulaFrameClock::stepsOfMilliseconds(500);
    const f32 sDistanceToApproachMin = 100.0f;
    const f32 sDistanceToApproachMax = 500.0f;
    const f32 sEscapeAccel = 5.0f;
    const s32 sStepToEscapeMin = 5;
    const s32 sStepToEscapeMax = 20;
    const f32 sDistanceSafe = 600.0f;
    const f32 sDistanceToSpinEscape = 500.0f;
    const s32 sTimeToSearchPlayerMin = 5;
    const s32 sTimeToSearchPlayerMax = 10;
    const f32 sDistanceToEscapeFromPlayer = 150.0f;
    const f32 sRotateDotMax = 0.9f;
    const f32 sRotateRate = 0.2f;
    const f32 sDirectionMaxY = 0.99f;
    const f32 sAnimFrameRate = 0.2f;
    const f32 sFollowPosOffsetMax = 150.0f;

    // Indexed by Fish::mFishType; an unlisted group object name is kind 0.
    struct FishKind {
        const char* mGroupName;
        const char* mModelName;
    };

    const FishKind sFishKinds[] = {
        {"FishGroupA", "FishA"}, {"FishGroupB", "FishB"}, {"FishGroupC", "FishC"},
        {"FishGroupD", "FishD"}, {"FishGroupE", "FishE"}, {"FishGroupF", "FishF"},
    };

    Vec3f calcRandomFollowPosOffset() {
        // Drawn z, y, x: the console's right-to-left order of the arguments (as the Windows build's), on every platform.
        const f32 z = MR::getRandom(-sFollowPosOffsetMax, sFollowPosOffsetMax);
        const f32 y = MR::getRandom(-sFollowPosOffsetMax, sFollowPosOffsetMax);
        const f32 x = MR::getRandom(-sFollowPosOffsetMax, sFollowPosOffsetMax);
        return Vec3f(x, y, z);
    }
};  // namespace

namespace NrvFish {
    NEW_NERVE(FishNrvWander, Fish, Wander);
    NEW_NERVE(FishNrvApproach, Fish, Approach);
    NEW_NERVE(FishNrvEscape, Fish, Escape);
};  // namespace NrvFish

Fish::Fish(const char* pName, FishGroup* pGroup, int fishType, bool canEscape)
    : LiveActor(pName), mGroup(pGroup), mFishType(fishType), mCanEscape(canEscape), mFollowPos(0.0f, 0.0f, 0.0f),
      mFollowPosOffset(::calcRandomFollowPosOffset()),
      mFront(1.0f, 0.0f, 0.0f), mDistanceToApproach(MR::getRandom(::sDistanceToApproachMin, ::sDistanceToApproachMax)),
      mTimeToSearchPlayer(MR::getRandom(::sTimeToSearchPlayerMin, ::sTimeToSearchPlayerMax)), mStepToMove(), mEscapePos() {
    lumacore::setClass(this, lumacore::kClassId< Fish >);
}

void Fish::init(const TableIter& /*rIter*/) {
    updateFollowPointPos();
    mPosition.set(mFollowPos);
    mFront.set(MR::getRailDirection(mGroup));

    initModelManagerWithAnm(::sFishKinds[mFishType].mModelName, nullptr, false);
    MR::startBck(this, "Swim");
    MR::connectToSceneEnvironment(this);
    initNerve(GET_NERVE(Fish, FishNrvWander));
    MR::invalidateClipping(this);
    makeActorAppeared();
}

void Fish::exeWander() {
    if (MR::isFirstStep(this)) {
        mStepToMove = 0;
    }

    if (tryEscapeFromPlayer()) {
        return;
    }

    if (--mStepToMove <= 0) {
        mVelocity.x += mFront.x * ::sWanderAccel;
        mVelocity.y += mFront.y * ::sWanderAccel;
        mVelocity.z += mFront.z * ::sWanderAccel;
        mStepToMove = MR::getRandom(::sStepToWanderMoveMin, ::sStepToWanderMoveMax);
    }

    if (mFollowPos.squared(mPosition) > mDistanceToApproach * mDistanceToApproach) {
        setNerve(GET_NERVE(Fish, FishNrvApproach));
    }
}

void Fish::exeApproach() {
    if (MR::isFirstStep(this)) {
        mStepToMove = 0;
    }

    if (tryEscapeFromPlayer()) {
        return;
    }

    if (--mStepToMove <= 0) {
        Vec3f front = mFollowPos;
        front.sub(mPosition);
        MR::normalize(&front);

        if (mFront.dot(front) > ::sRotateDotMax) {
            mFront.set(front);
        } else {
            Vec3f currFront = mFront;
            mFront.x = currFront.x * (1.0f - ::sRotateRate) + front.x * ::sRotateRate;
            mFront.y = currFront.y * (1.0f - ::sRotateRate) + front.y * ::sRotateRate;
            mFront.z = currFront.z * (1.0f - ::sRotateRate) + front.z * ::sRotateRate;

            if (MR::isNearZero(mFront, 0.01f)) {
                mFront.set(front);
            } else {
                MR::normalize(&mFront);
            }
        }

        mVelocity.x += mFront.x * ::sApproachAccel;
        mVelocity.y += mFront.y * ::sApproachAccel;
        mVelocity.z += mFront.z * ::sApproachAccel;
        mStepToMove = MR::getRandom(::sStepToApproachMin, ::sStepToApproachMax);
    }

    if (mFollowPos.squared(mPosition) < mDistanceToApproach * mDistanceToApproach) {
        setNerve(GET_NERVE(Fish, FishNrvWander));
    }
}

void Fish::exeEscape() {
    if (MR::isFirstStep(this)) {
        mStepToMove = 0;
    }

    if (--mStepToMove <= 0) {
        Vec3f front = mPosition;
        front.sub(*mEscapePos);
        MR::normalize(&front);

        mVelocity.x += front.x * ::sEscapeAccel;
        mVelocity.y += front.y * ::sEscapeAccel;
        mVelocity.z += front.z * ::sEscapeAccel;

        mFront.set(mVelocity);
        MR::normalize(&mFront);

        mStepToMove = MR::getRandom(::sStepToEscapeMin, ::sStepToEscapeMax);
    }

    if (mEscapePos->squared(mPosition) > ::sDistanceSafe * ::sDistanceSafe) {
        setNerve(GET_NERVE(Fish, FishNrvApproach));
    }
}

void Fish::control() {
    mVelocity.scale(::sFriction);
    if (!mVelocity.isZero()) {
        MR::setBckRate(this, mVelocity.length() * ::sAnimFrameRate);
    }

    if (mCanEscape && MR::isSpin(PLAYER_1)) {
        if (!isNerve(GET_NERVE(Fish, FishNrvEscape)) && MR::calcDistanceToPlayer(this) < ::sDistanceToSpinEscape) {
            mEscapePos = MR::getPlayerPos();
            setNerve(GET_NERVE(Fish, FishNrvEscape));
        }
    }

    if (MR::isNearZero(mFront)) {
        if (MR::isNearZero(mVelocity)) {
            mFront.set(1.0f, 0.0f, 0.0f);
        } else {
            mFront.set(mVelocity);
        }
    }

    if (MR::abs(mFront.y) > ::sDirectionMaxY) {
        if (mFront.x > 0.0f) {
            mFront.x += 0.1f;
        } else {
            mFront.x -= 0.1f;
        }
        MR::normalize(&mFront);
    }

    updateFollowPointPos();

    if (MR::isJudgedToClipFrustum300m(mPosition, 100.0f)) {
        if (!MR::isHiddenModel(this)) {
            MR::hideModel(this);
        }
    } else {
        if (MR::isHiddenModel(this)) {
            MR::showModel(this);
        }
    }
}

void Fish::calcAndSetBaseMtx() {
    Pos3f mtx;
    mtx.identity();
    MR::makeMtxFrontUp(&mtx, mFront, mGroup->mUp);
    mtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, mtx);
}

bool Fish::tryEscapeFromPlayer() {
    if (!mCanEscape) {
        return false;
    }

    if (--mTimeToSearchPlayer <= 0) {
        if (MR::calcDistanceToPlayer(this) < ::sDistanceToEscapeFromPlayer) {
            mEscapePos = MR::getPlayerPos();
            setNerve(GET_NERVE(Fish, FishNrvEscape));
            return true;
        }
        mTimeToSearchPlayer = MR::getRandom(::sTimeToSearchPlayerMin, ::sTimeToSearchPlayerMax);
    }

    return false;
}

void Fish::updateFollowPointPos() {
    mFollowPos.set(MR::getRailPos(mGroup));
    mFollowPos.add(mFollowPosOffset);
}

FishGroup::FishGroup(const char* pName)
    : LiveActor(pName), mUp(0.0f, 1.0f, 0.0f), mFishNum(::sDefaultFishNum), mFish(), mSpeed(::sDefaultGroupSpeed), mClippingCenter(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< FishGroup >);
}

void FishGroup::init(const TableIter& rIter) {
    MR::connectToSceneEnemyMovement(this);
    MR::initDefaultPos(this, rIter);
    MR::calcActorAxisY(&mUp, this);
    initRailRider(rIter);
    MR::moveCoordToNearestPos(this, mPosition);

    MR::getPlacementArg0NoInit(rIter, &mFishNum);
    if (mFishNum < 0) {
        mFishNum = 0;
    }

    bool canEscape = false;
    MR::getPlacementArg1NoInit(rIter, &canEscape);

    bool calcGravity = false;
    MR::getPlacementArg2NoInit(rIter, &calcGravity);
    if (calcGravity) {
        MR::onCalcGravity(this);
    }
    const char* objName;
    MR::getObjectName(&objName, rIter);
    int fishType = 0;
    for (u32 i = 0; i < ARRAY_SIZE(::sFishKinds); i++) {
        if (MR::isEqualString(objName, ::sFishKinds[i].mGroupName)) {
            fishType = static_cast< int >(i);
            break;
        }
    }

    mFish = new Fish*[mFishNum];
    for (int i = 0; i < mFishNum; i++) {
        mFish[i] = new Fish("魚", this, fishType, canEscape);
        mFish[i]->initWithoutIter();
    }

    MR::initAndSetRailClipping(&mClippingCenter, this, 100.0f, 500.0f);
    makeActorAppeared();
}

void FishGroup::startClipped() {
    LiveActor::startClipped();
    for (int i = 0; i < mFishNum; i++) {
        mFish[i]->startClipped();
    }
}

void FishGroup::endClipped() {
    LiveActor::endClipped();
    for (int i = 0; i < mFishNum; i++) {
        mFish[i]->endClipped();
    }
}

void FishGroup::control() {
    mUp.set(-mGravity);
    MR::moveCoordAndFollowTrans(this, mSpeed);
}
