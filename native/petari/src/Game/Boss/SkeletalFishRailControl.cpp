#include "Game/Boss/SkeletalFishRailControl.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/RailUtil.hpp"

SkeletalFishRailControl::SkeletalFishRailControl() {
    mState = 2;
    mJoinRail = nullptr;
    mRail = nullptr;
    mJoinCoord = 0.0f;
    mSpeed = 20.0f;
}

void SkeletalFishRailControl::setRailActor(LiveActor* a1, LiveActor* a2, bool a3) {
    mJoinRail = a2;
    mRail = a1;

    if (a2 != nullptr) {
        mState = 0;
        MR::setRailCoord(a2, 0.0f);
        MR::setRailDirectionToEnd(mJoinRail);
    } else {
        mState = 2;
    }

    if (a3) {
        MR::setRailCoord(mRail, 0.0f);
        MR::setRailDirectionToEnd(mRail);
    }
}

void SkeletalFishRailControl::update() {
    if (mJoinRail != nullptr) {
        MR::setRailCoordSpeed(mJoinRail, mSpeed);
    }

    MR::setRailCoordSpeed(mRail, mSpeed);

    switch (mState) {
    case 0: {
        f32 railLength = MR::getRailTotalLength(mJoinRail);
        f32 v3 = ((mSpeed + MR::getRailCoord(mJoinRail) - railLength));
        if (v3 >= 0.0f) {
            f32 coord = MR::calcNearestRailCoord(mRail, MR::getRailPointPosEnd(mJoinRail));
            mJoinCoord = coord;
            mRailCoord = (mJoinCoord - v3);
            MR::setRailCoord(mRail, mRailCoord);
            mState = 1;
        }
        break;
    }

    case 1:
        if (mRailCoord - mJoinCoord >= MR::getRailTotalLength(mRail)) {
            mState = 2;
        }
        break;
    case 2:
        break;
    }

    switch (mState) {
    case 0:
        MR::moveRailRider(mJoinRail);
        break;
    case 1:
        mRailCoord += mSpeed;
        MR::setRailCoord(mRail, mRailCoord);
        break;
    case 2:
        MR::moveRailRider(mRail);
        break;
    }
}

void SkeletalFishRailControl::getRailInfo(SkeletalFishRailInfo* pOutInfo, f32 a2) {
    switch (mState) {
    case 0: {
        f32 coord = MR::getRailCoord(mJoinRail);
        pOutInfo->mRail = mJoinRail;
        pOutInfo->mCoord = coord - a2;
        break;
    }
    case 1: {
        f32 dist = mRailCoord - a2;
        if (dist < mJoinCoord) {
            f32 railLength = MR::getRailTotalLength(mJoinRail);
            f32 diff = mJoinCoord - dist;
            pOutInfo->mRail = mJoinRail;
            dist = railLength - diff;
        } else {
            pOutInfo->mRail = mRail;
        }

        pOutInfo->mCoord = dist;
        break;
    }

    case 2: {
        f32 coord = MR::getRailCoord(mRail);
        pOutInfo->mRail = mRail;
        pOutInfo->mCoord = coord - a2;
        break;
    }
    }
}

void SkeletalFishRailControl::getPos(Vec3f* pOut, f32 a2) {
    SkeletalFishRailInfo info;
    getRailInfo(&info, a2);

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

void SkeletalFishRailControl::getMtx(Pos3f* pOut, f32 a2) {
    SkeletalFishRailInfo normalRailInfo;
    SkeletalFishRailInfo distRailInfo;
    getRailInfo(&normalRailInfo, a2);
    getRailInfo(&distRailInfo, a2 - 1000.0f);

    Vec3f railPos;
    Vec3f railDir;
    Vec3f gravityVec;
    Vec3f stack_3C;

    if (normalRailInfo.mCoord < 0.0f && !MR::isLoopRail(normalRailInfo.mRail)) {
        MR::calcRailPosAtCoord(&railPos, normalRailInfo.mRail, 0.0f);
        MR::calcRailDirectionAtCoord(&railDir, normalRailInfo.mRail, 0.0f);
        stack_3C = railDir;
        stack_3C.scale(normalRailInfo.mCoord);
        railPos.add(stack_3C);
    } else {
        MR::calcRailPosAtCoord(&railPos, normalRailInfo.mRail, normalRailInfo.mCoord);
        MR::calcRailDirectionAtCoord(&railDir, normalRailInfo.mRail, normalRailInfo.mCoord);
    }

    MR::normalizeOrZero(&railDir);
    MR::calcGravityVector(normalRailInfo.mRail, railPos, &gravityVec, nullptr, 0);

    Vec3f v15 = (-gravityVec).cross(railDir);
    Vec3f v14 = railDir.cross(v15);

    MR::normalizeOrZero(&v15);
    MR::normalizeOrZero(&v14);
    MR::normalizeOrZero(&railDir);
    pOut->setTR(v15, v14, railDir, railPos);
}
