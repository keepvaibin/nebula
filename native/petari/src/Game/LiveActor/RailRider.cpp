#include "Game/LiveActor/RailRider.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SceneUtil.hpp"

RailRider::RailRider(const TableIter& rIter)
    : mBezierRail(), mCoord(), mSpeed(), mIsNotReverse(true), mCurPos(0.0f), mCurDirection(1.0f, 0.0f, 0.0f), mStartPos(0.0f), mEndPos(0.0f) {
    const Table* info = nullptr;
    TableIter iter;
    MR::getRailInfo(&iter, &info, rIter);
    initBezierRail(iter, info);
}

RailRider::RailRider(s32 railId, s32 zoneId)
    : mBezierRail(), mCoord(), mSpeed(), mIsNotReverse(true), mCurPos(0.0f), mCurDirection(1.0f, 0.0f, 0.0f), mStartPos(0.0f), mEndPos(0.0f) {
    const Table* info = nullptr;
    TableIter iter;
    MR::getCameraRailInfo(&iter, &info, railId, zoneId);
    initBezierRail(iter, info);
}

void RailRider::move() {
    if (mIsNotReverse) {
        mCoord += mSpeed;
    } else {
        mCoord -= mSpeed;
    }

    mCoord = mBezierRail->normalizePos(mCoord, 1);
    syncPosDir();
}

void RailRider::moveToNearestPos(const Vec3f& rPos) {
    mCoord = mBezierRail->getNearestRailPosCoord(rPos);
    syncPosDir();
}

void RailRider::moveToNearestPoint(const Vec3f& rPos) {
    f32 nearestDistance = FLOAT_MAX;
    int nearestPoint = 0;
    Vec3f pointPos;
    for (int i = 0; i < getPointNum(); i++) {
        copyPointPos(&pointPos, i);
        f32 distance = rPos.squared(pointPos);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestPoint = i;
        }
    }

    mCoord = getPointCoord(nearestPoint);
    syncPosDir();
}

void RailRider::moveToNextPoint() {
    mCoord = mBezierRail->getRailPosCoord(getNextPointNo());
    syncPosDir();
}

void RailRider::reverse() {
    if (mIsNotReverse) {
        mIsNotReverse = false;
    } else {
        mIsNotReverse = true;
    }

    syncPosDir();
}

void RailRider::calcPosAtCoord(Vec3f* pOutVec, f32 coord) const {
    mBezierRail->calcPos(pOutVec, coord);
}

void RailRider::calcDirectionAtCoord(Vec3f* pOutVec, f32 coord) const {
    mBezierRail->calcDirection(pOutVec, coord);
}

f32 RailRider::calcNearestPos(const Vec3f& rPos) const {
    return mBezierRail->getNearestRailPosCoord(rPos);
}

f32 RailRider::getTotalLength() const {
    return mBezierRail->getTotalLength();
}

f32 RailRider::getPartLength(int idx) const {
    return mBezierRail->getPartLength(idx);
}

bool RailRider::isLoop() const {
    return mBezierRail->mIsClosed;
}

bool RailRider::isReachedGoal() const {
    if (isLoop()) {
        return false;
    }

    return (mIsNotReverse && MR::isNearZero(mCoord - getTotalLength())) || (!mIsNotReverse && MR::isNearZero(mCoord));
}

bool RailRider::isReachedEdge() const {
    if (mBezierRail->mIsClosed) {
        return false;
    }

    return MR::isNearZero(mCoord) || MR::isNearZero(mCoord - mBezierRail->getTotalLength());
}

void RailRider::setCoord(f32 coord) {
    mCoord = coord;
    mCoord = mBezierRail->normalizePos(mCoord, 1);
    syncPosDir();
}

void RailRider::setSpeed(f32 speed) {
    mSpeed = speed;
}

bool RailRider::getRailArgWithInit(const char* pStr, s32* pOut) const {
    s32 arg;
    if (!mBezierRail->mIter->getValue< s32 >(pStr, &arg)) {
        return false;
    }

    *pOut = arg;
    return true;
}

bool RailRider::getRailArgNoInit(const char* pStr, s32* pOut) const {
    s32 arg;
    if (!mBezierRail->mIter->getValue< s32 >(pStr, &arg) || arg == -1) {
        return false;
    }

    *pOut = arg;
    return true;
}

f32 RailRider::getNextPointCoord() const {
    return mBezierRail->getRailPosCoord(getNextPointNo());
}

f32 RailRider::getCurrentPointCoord() const {
    return mBezierRail->getRailPosCoord(mCurPoint);
}

s32 RailRider::getPointNum() const {
    return mBezierRail->mPointNum;
}

void RailRider::copyPointPos(Vec3f* pOut, s32 pointNum) const {
    TableIter iter;
    mBezierRail->calcRailCtrlPointIter(&iter, pointNum);
    MR::getRailPointPos0(iter, pOut);
}

f32 RailRider::getPointCoord(s32 idx) const {
    return mBezierRail->getRailPosCoord(idx);
}

void RailRider::initBezierRail(const TableIter& rIter, const Table* pInfo) {
    mBezierRail = new BezierRail(rIter, pInfo);
    syncPosDir();
    setCoord(mBezierRail->getTotalLength());
    mEndPos.set(mCurPos);
    setCoord(0.0f);
    mStartPos.set(mCurPos);
}

// A point argument of -1, or none, is unset: *pOut is left as it was.
bool RailRider::getPointArgS32NoInit(const char* pStr, s32* pOut, s32 pointNum) const {
    TableIter iter;
    mBezierRail->calcRailCtrlPointIter(&iter, pointNum);
    s32 arg = -1;
    iter.getValue< s32 >(pStr, &arg);

    if (arg != -1) {
        *pOut = arg;
        return true;
    }

    return false;
}

bool RailRider::getPointArgS32WithInit(const char* pStr, s32* pOut, s32 pointNum) const {
    TableIter iter;
    mBezierRail->calcRailCtrlPointIter(&iter, pointNum);
    iter.getValue< s32 >(pStr, pOut);
    return true;
}

bool RailRider::getCurrentPointArgS32NoInit(const char* pStr, s32* pOut) const {
    return getPointArgS32NoInit(pStr, pOut, mCurPoint);
}

bool RailRider::getCurrentPointArgS32WithInit(const char* pStr, s32* pOut) const {
    return getPointArgS32WithInit(pStr, pOut, mCurPoint);
}

bool RailRider::getNextPointArgS32NoInit(const char* pStr, s32* pOut) const {
    return getPointArgS32NoInit(pStr, pOut, getNextPointNo());
}

bool RailRider::getNextPointArgS32WithInit(const char* pStr, s32* pOut) const {
    return getPointArgS32WithInit(pStr, pOut, getNextPointNo());
}

s32 RailRider::getNextPointNo() const {
    int step = mIsNotReverse ? 1 : -1;
    if (isLoop()) {
        int pointNum = getPointNum();
        int nextPoint = pointNum + mCurPoint;
        int offset = step + pointNum;
        return (offset + nextPoint) % pointNum;
    }

    s32 nextPoint = mCurPoint + step;
    s32 lastPoint = getPointNum() - 1;
    if (nextPoint < 0) {
        return 0;
    }

    if (nextPoint > lastPoint) {
        return lastPoint;
    }

    return nextPoint;
}

void RailRider::syncPosDir() {
    if (0.0f < mCoord && mCoord < mBezierRail->getTotalLength()) {
        mBezierRail->calcPosDir(&mCurPos, &mCurDirection, mCoord);
    } else {
        if (mCoord == 0.0f) {
            mBezierRail->calcPos(&mCurPos, mCoord);
            mBezierRail->calcDirection(&mCurDirection, 0.1f);
        } else {
            mBezierRail->calcPos(&mCurPos, mCoord);
            mBezierRail->calcDirection(&mCurDirection, mBezierRail->getTotalLength() - 0.1f);
        }
    }

    if (!mIsNotReverse) {
        mCurDirection.scale(-1.0f);
    }

    TableIter iter;
    f32 coord = mCoord;
    mBezierRail->calcCurrentRailCtrlPointIter(&iter, coord, mIsNotReverse);
    iter.getValue< s32 >("id", &mCurPoint);
}
