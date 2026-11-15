#include "Game/MapObj/SpiderThreadMainPoint.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/SpiderThreadPart.hpp"
#include "Game/MapObj/SpiderThreadRadialLine.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_math.h>

namespace {
    const f32 sFrictionRate = 0.9f;
    const f32 sWindFrictionRate = 0.9f;
    const f32 sBasePosAccelRate = 0.3f;
    const f32 sBasePosDiffMin = 1.0f;
    const f32 sNearMainPointAccelRate = 0.1f;
    const f32 sNearHangPointAccelRate = 0.02f;
    const f32 sNearPointDiffMin = 1.0f;
};  // namespace

SpiderThreadPointNearInfo::SpiderThreadPointNearInfo()
    : mMainPoint(nullptr), mPartPoint(nullptr), mDistToPoint(0.0f), mSpringFactor(0.0f), mThreadPart(nullptr) {
}

void SpiderThreadPointNearInfo::setInfo(f32 springFactor, SpiderThreadPart* pThreadPart, const Vec3f& rVec) {
    mSpringFactor = springFactor;
    mThreadPart = pThreadPart;

    if (mMainPoint != nullptr) {
        mDistToPoint = rVec.distance(mMainPoint->mPosition);
    } else if (mPartPoint != nullptr) {
        mDistToPoint = rVec.distance(mPartPoint->mPosition);
    } else {
        mDistToPoint = 0.0f;
    }
}

void SpiderThreadPointNearInfo::clear() {
    mMainPoint = nullptr;
    mPartPoint = nullptr;
    mSpringFactor = 0.0f;
    mThreadPart = nullptr;
    mDistToPoint = 0.0f;
}

const Vec3f* SpiderThreadPointNearInfo::getPos() const {
    if (mMainPoint != nullptr) {
        return &mMainPoint->mPosition;
    }

    if (mPartPoint != nullptr) {
        return &mPartPoint->mPosition;
    }

    return nullptr;
}

SpiderThreadMainPoint::SpiderThreadMainPoint(const Vec3f& rPos, int nearInfoBufferSize)
    : SpiderThreadPoint(rPos, ::sFrictionRate), mRadialLine(nullptr), mNumNearInfos(0), mNearInfos(nullptr),
      mNearInfoCapacity(nearInfoBufferSize > 0 ? nearInfoBufferSize : 0) {
    mNearInfos = new SpiderThreadPointNearInfo[mNearInfoCapacity];
}

void SpiderThreadMainPoint::addNearMainPoint(SpiderThreadMainPoint* pMainPoint, SpiderThreadPart* pThreadPart) {
    // An entry past the buffer is not kept: that neighbour pulls no spring.
    if (mNumNearInfos >= mNearInfoCapacity) {
        return;
    }

    getNearPointInfo(mNumNearInfos)->mMainPoint = pMainPoint;
    getNearPointInfo(mNumNearInfos)->setInfo(::sNearMainPointAccelRate, pThreadPart, mPosition);
    mNumNearInfos++;
}

void SpiderThreadMainPoint::addNearPartPoint(SpiderThreadPoint* pThreadPoint, f32 springFactor) {
    if (mNumNearInfos >= mNearInfoCapacity) {
        return;
    }

    getNearPointInfo(mNumNearInfos)->mPartPoint = pThreadPoint;
    getNearPointInfo(mNumNearInfos)->setInfo(springFactor, nullptr, mPosition);
    mNumNearInfos++;
}

void SpiderThreadMainPoint::removeNearPoint(const SpiderThreadPoint* pThreadPoint) {
    for (s32 idx = 0; idx < mNumNearInfos; idx++) {
        if (mNearInfos[idx].getPos() == &pThreadPoint->mPosition) {
            mNearInfos[idx] = *getLastInfo();
            getLastInfo()->clear();
            mNumNearInfos--;
            return;
        }
    }
}

void SpiderThreadMainPoint::addNearPointToRadial(SpiderThreadPoint* pThreadPoint) {
    if (mRadialLine != nullptr) {
        for (s32 idx = 0; idx < mRadialLine->mNumPoints; idx++) {
            if (mRadialLine->getPoint(idx) == this) {
                addNearPartPoint(pThreadPoint, ::sNearMainPointAccelRate);
            } else {
                mRadialLine->getPoint(idx)->addNearPartPoint(pThreadPoint, ::sNearHangPointAccelRate);
            }
        }
    }
}

void SpiderThreadMainPoint::removeNearPointFromRadial(const SpiderThreadPoint* pThreadPoint) {
    if (mRadialLine != nullptr) {
        for (s32 idx = 0; idx < mRadialLine->mNumPoints; idx++) {
            mRadialLine->getPoint(idx)->removeNearPoint(pThreadPoint);
        }
    }
}

void SpiderThreadMainPoint::cutNearPoints(s32* numCutPoints, SpiderThreadMainPoint** pMainPoints, s32 numMainPoints) {
    SpiderThreadPart* threadPart;
    for (s32 idx = 0; idx < mNumNearInfos; idx++) {
        SpiderThreadMainPoint* mainPoint;
        // A neighbour with no cut point left in the buffer stays joined.
        if (getNearPointInfo(idx)->mMainPoint != nullptr && *numCutPoints < numMainPoints) {
            getNearPointInfo(idx)->mMainPoint->removeNearPoint(this);

            mainPoint = pMainPoints[*numCutPoints];
            threadPart = getNearPointInfo(idx)->mThreadPart;

            getNearPointInfo(idx)->mMainPoint->addNearMainPoint(mainPoint, threadPart);
            mainPoint->mPosition.x = mPosition.x;
            mainPoint->mPosition.y = mPosition.y;
            mainPoint->mPosition.z = mPosition.z;
            mainPoint->addNearMainPoint(getNearPointInfo(idx)->mMainPoint, threadPart);
            threadPart->cut(this, mainPoint);

            *numCutPoints = *numCutPoints + 1;
        }
    }
}

void SpiderThreadMainPoint::updateVelocity() {
    updateWind(::sWindFrictionRate);

    Vec3f springBack(mBasePos);
    springBack.sub(mPosition);

    Vec3f diff(springBack);

    if (springBack.length() > ::sBasePosDiffMin) {
        MR::normalizeOrZero(&springBack);
        springBack.scale(::sBasePosAccelRate);
        mVelocity.add(springBack);
    }

    for (s32 idx = 0; idx < mNumNearInfos; idx++) {
        closeToNearPoint(getNearPointInfo(idx));
    }
}

void SpiderThreadMainPoint::closeToNearPoint(const SpiderThreadPointNearInfo* pNearInfo) {
    if (pNearInfo->getPos() != nullptr) {
        Vec3f springBack(*pNearInfo->getPos());
        springBack.sub(mPosition);
        f32 dist = springBack.length();

        if (dist < ::sNearPointDiffMin) {
            return;
        }

        f32 distDiff = dist - pNearInfo->mDistToPoint;

        if (distDiff < 0.0f) {
            return;
        }

        MR::normalizeOrZero(&springBack);
        springBack.scale(distDiff * pNearInfo->mSpringFactor);
        mVelocity.add(springBack);
    }
}
