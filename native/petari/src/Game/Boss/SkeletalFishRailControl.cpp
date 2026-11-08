#include "Game/Boss/SkeletalFishRailControl.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/RailUtil.hpp"

namespace {
    const f32 cDefaultSpeed = 20.0f;
};  // namespace

SkeletalFishRailControl::SkeletalFishRailControl() {
    mState = State_OnRail;
    mJoinRail = nullptr;
    mRail = nullptr;
    mJoinCoord = 0.0f;
    mSpeed = cDefaultSpeed;
}

// With a join rail the fish swims it first and then onto `pRail` where the join rail ends.
void SkeletalFishRailControl::setRailActor(LiveActor* pRail, LiveActor* pJoinRail, bool isResetRail) {
    mJoinRail = pJoinRail;
    mRail = pRail;

    if (pJoinRail != nullptr) {
        mState = State_OnJoinRail;
        MR::setRailCoord(pJoinRail, 0.0f);
        MR::setRailDirectionToEnd(mJoinRail);
    } else {
        mState = State_OnRail;
    }

    if (isResetRail) {
        MR::setRailCoord(mRail, 0.0f);
        MR::setRailDirectionToEnd(mRail);
    }
}

// While joining, the head is on the rail and mRailCoord runs on from the join point until the whole body (one rail
// length) has crossed.
void SkeletalFishRailControl::update() {
    if (mJoinRail != nullptr) {
        MR::setRailCoordSpeed(mJoinRail, mSpeed);
    }

    MR::setRailCoordSpeed(mRail, mSpeed);

    switch (mState) {
    case State_OnJoinRail: {
        f32 railLength = MR::getRailTotalLength(mJoinRail);
        f32 overshoot = ((mSpeed + MR::getRailCoord(mJoinRail) - railLength));
        if (overshoot >= 0.0f) {
            mJoinCoord = MR::calcNearestRailCoord(mRail, MR::getRailPointPosEnd(mJoinRail));
            mRailCoord = (mJoinCoord - overshoot);
            MR::setRailCoord(mRail, mRailCoord);
            mState = State_Joining;
        }
        break;
    }
    case State_Joining:
        if (mRailCoord - mJoinCoord >= MR::getRailTotalLength(mRail)) {
            mState = State_OnRail;
        }
        break;
    case State_OnRail:
        break;
    }

    switch (mState) {
    case State_OnJoinRail:
        MR::moveRailRider(mJoinRail);
        break;
    case State_Joining:
        mRailCoord += mSpeed;
        MR::setRailCoord(mRail, mRailCoord);
        break;
    case State_OnRail:
        MR::moveRailRider(mRail);
        break;
    }
}

// The rail and coordinate `behind` back from the head; while joining, a point not yet across is on the join rail.
void SkeletalFishRailControl::getRailInfo(SkeletalFishRailInfo* pOutInfo, f32 behind) {
    switch (mState) {
    case State_OnJoinRail: {
        f32 coord = MR::getRailCoord(mJoinRail);
        pOutInfo->mRail = mJoinRail;
        pOutInfo->mCoord = coord - behind;
        break;
    }
    case State_Joining: {
        f32 coord = mRailCoord - behind;
        if (coord < mJoinCoord) {
            f32 railLength = MR::getRailTotalLength(mJoinRail);
            f32 beforeJoin = mJoinCoord - coord;
            pOutInfo->mRail = mJoinRail;
            coord = railLength - beforeJoin;
        } else {
            pOutInfo->mRail = mRail;
        }

        pOutInfo->mCoord = coord;
        break;
    }
    case State_OnRail: {
        f32 coord = MR::getRailCoord(mRail);
        pOutInfo->mRail = mRail;
        pOutInfo->mCoord = coord - behind;
        break;
    }
    }
}

// Before an open rail's start the point continues straight back along its start direction.
void SkeletalFishRailControl::getPos(Vec3f* pOut, f32 behind) {
    SkeletalFishRailInfo info;
    getRailInfo(&info, behind);

    if (info.mCoord < 0.0f && !MR::isLoopRail(info.mRail)) {
        MR::calcRailPosAtCoord(pOut, info.mRail, 0.0f);
        Vec3f railDir;
        MR::calcRailDirectionAtCoord(&railDir, info.mRail, 0.0f);
        railDir.scale(info.mCoord);
        pOut->add(railDir);
    } else {
        MR::calcRailPosAtCoord(pOut, info.mRail, info.mCoord);
    }
}

// The frame at that point: front along the rail, up against its gravity.
void SkeletalFishRailControl::getMtx(Pos3f* pOut, f32 behind) {
    SkeletalFishRailInfo info;
    getRailInfo(&info, behind);

    Vec3f railPos;
    Vec3f railDir;
    Vec3f gravityVec;

    if (info.mCoord < 0.0f && !MR::isLoopRail(info.mRail)) {
        MR::calcRailPosAtCoord(&railPos, info.mRail, 0.0f);
        MR::calcRailDirectionAtCoord(&railDir, info.mRail, 0.0f);
        Vec3f beforeStart = railDir;
        beforeStart.scale(info.mCoord);
        railPos.add(beforeStart);
    } else {
        MR::calcRailPosAtCoord(&railPos, info.mRail, info.mCoord);
        MR::calcRailDirectionAtCoord(&railDir, info.mRail, info.mCoord);
    }

    MR::normalizeOrZero(&railDir);
    MR::calcGravityVector(info.mRail, railPos, &gravityVec, nullptr, 0);

    Vec3f side = (-gravityVec).cross(railDir);
    Vec3f up = railDir.cross(side);

    MR::normalizeOrZero(&side);
    MR::normalizeOrZero(&up);
    MR::normalizeOrZero(&railDir);
    pOut->setTR(side, up, railDir, railPos);
}
