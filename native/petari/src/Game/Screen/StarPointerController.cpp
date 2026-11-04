#include "Game/Screen/StarPointerController.hpp"
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"

namespace {
// static const _32 hMarginH =
// static const _32 hMarginV =
// static const _32 hForcePointerOutVel =
    static const s32 hIsOutScreenCounter = 5;
    // static const _32 hNearValue =
    // static const _32 hDrawDebugRadius =
};  // namespace

DpdInfo::DpdInfo() : mPos(0.0f, 0.0f), mInScreen(), mDrawReady(), mZDepth(), mViewDistZ() {
}

StarPointerController::StarPointerController()
    : mPadChannel(-1), mScreenPos(0.0f, 0.0f), mOutScreenTime(), mPastPosition(), mPastPointNum(), mNextPastPosition(), mNextPastPointNum(),
      mScreenDist(), mScreenSpeed(), mScreenVel(0.0f, 0.0f), mWorldVel(0.0f, 0.0f, 0.0f), mWorldPos(0.0f, 0.0f, 0.0f) {
    mPastPosition = new TVec2f[16];
    mNextPastPosition = new TVec2f[16];
}

bool StarPointerController::isOutScreenLong() const {
    return mOutScreenTime > ::hIsOutScreenCounter;
}

void StarPointerController::initAndSetPort(s32 padChannel) {
    mPadChannel = padChannel;
}

void StarPointerController::movement(const f32* pProjectionParams, const f32* pViewportParams) {
    storeDataFromCallback(pProjectionParams, pViewportParams);
    storePastPointingData();
    updateDpdInfo();
}

void StarPointerController::storeDataFromCallback(const f32* pProjectionParams, const f32* pViewportParams) {
    TVec3f v1(mInfo.mPos.x, mInfo.mPos.y, mInfo.mZDepth);

    if (mInfo.mDrawReady == true) {
        TDDraw::invProject(&mWorldPos, v1, MR::getStarPointerViewMtx(), pProjectionParams, pViewportParams, true);
        mInfo.mViewDistZ = calcViewDistanceZ(mWorldPos, MR::getStarPointerViewMtx());
    }

    mPastInfo = mInfo;
    if (mPastInfo.mInScreen) {
        mOutScreenTime = 0;
    } else {
        mOutScreenTime++;
    }

    mInfo.mDrawReady = false;
}

void StarPointerController::storePastPointingData() {
    mPastPointNum = mNextPastPointNum;
    for (s32 idx = 0; idx < mNextPastPointNum; idx++) {
        mPastPosition[idx] = mNextPastPosition[idx];
    }

    s32 pastCount = MR::min(MR::getPointerPastCount(mPadChannel), 16);
    mNextPastPointNum = pastCount;

    for (s32 idx = 0; idx < pastCount; idx++) {
        calcPastPointingPosOnScreen(&mNextPastPosition[idx], idx);
    }
}

void StarPointerController::calcPastPointingPosOnScreen(TVec2f* pPos, s32 idx) const {
    MR::getPointerPastPos(pPos, idx, mPadChannel);
    pPos->x += 1.0f;
    pPos->y += 1.0f;
    pPos->x *= MR::getScreenWidth() * 0.5f;
    pPos->y *= static_cast< s32 >(NebulaFrameClock::cScreenLines) * 0.5f;
}

void StarPointerController::updateDpdInfo() {
    if (!MR::isPointerInScreen(mPadChannel)) {
        TVec2f vec;
        vec.x = mInfo.mPos.x;
        vec.y = mInfo.mPos.y;

        if (mInfo.mInScreen && 10.0f < mScreenVel.length()) {
            forcePointerOut();
        }

        mInfo.mInScreen = false;
        mScreenPos.set(vec);
        updateAdditionalInfo();
    } else {
        mScreenPos.set(mInfo.mPos);
        mInfo.mInScreen = true;
        mInfo.mPos.set(mNextPastPosition[0]);
        mNotInScreen = StarPointerFunction::forceInsideScreenEdge(&mInfo.mPos);
        mScreenDist = MR::getPointerDistance(mPadChannel);
        updateAdditionalInfo();
    }
}

void StarPointerController::updateAdditionalInfo() {
    mScreenVel.set(mInfo.mPos);
    mScreenVel.sub(mScreenPos);
    mScreenSpeed = mScreenVel.length();
    mWorldVel.set< f32 >(mScreenVel.x, -mScreenVel.y, 0.0f);
    TPos3f mtx;
    NebulaMtxCopy(MR::getStarPointerViewMtx(), mtx);
    NebulaMtxInverse(mtx, mtx);
    NebulaMtxMultVecSR(mtx, &mWorldVel, &mWorldVel);
    MR::normalizeOrZero(&mWorldVel);
}

void StarPointerController::forcePointerOut() {
    if (!StarPointerFunction::isInScreen(mInfo.mPos, 0.0f, 0.0f)) {
        return;
    }

    TVec2f vec;
    vec.set(mInfo.mPos);
    vec.sub(mScreenPos);
    StarPointerFunction::forceOutsideScreenEdge(&mInfo.mPos, mInfo.mPos, vec, -20.0f, -20.0f);
}

f32 StarPointerController::calcViewDistanceZ(const TVec3f& rPos, MtxPtr pMtx) {
    return MR::abs(rPos.x * pMtx[2][0] + rPos.y * pMtx[2][1] + rPos.z * pMtx[2][2] + pMtx[2][3]);
}

