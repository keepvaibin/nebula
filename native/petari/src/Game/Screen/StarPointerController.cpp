#include "Game/Screen/StarPointerController.hpp"
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcPastPointingPosOnScreen_f1, "calcPastPointingPosOnScreen.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPastPointingPosOnScreen_f2, "calcPastPointingPosOnScreen.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePointerInfo_f1, "updatePointerInfo.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_forcePointerOut_f1, "forcePointerOut.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_forcePointerOut_f2, "forcePointerOut.f2", 20.0f);

namespace {
    NEBULA_TUNABLE(s32, hIsOutScreenCounter, 5);
};  // namespace

PointerInfo::PointerInfo() : mPos(0.0f, 0.0f), mInScreen(), mDrawReady(), mZDepth(), mViewDistZ() {
}

StarPointerController::StarPointerController()
    : mPadChannel(-1), mScreenPos(0.0f, 0.0f), mOutScreenTime(), mNextPastPosition(), mNextPastPointNum(),
      mScreenDist(), mScreenSpeed(), mScreenVel(0.0f, 0.0f), mWorldVel(0.0f, 0.0f, 0.0f), mWorldPos(0.0f, 0.0f, 0.0f) {
    mNextPastPosition = new Vec2f[16];
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
    updatePointerInfo();
}

void StarPointerController::storeDataFromCallback(const f32* pProjectionParams, const f32* pViewportParams) {
    Vec3f v1(mInfo.mPos.x, mInfo.mPos.y, mInfo.mZDepth);

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
    s32 pastCount = MR::min(MR::getPointerPastCount(mPadChannel), 16);
    mNextPastPointNum = pastCount;

    for (s32 idx = 0; idx < pastCount; idx++) {
        calcPastPointingPosOnScreen(&mNextPastPosition[idx], idx);
    }
}

void StarPointerController::calcPastPointingPosOnScreen(Vec2f* pPos, s32 idx) const {
    MR::getPointerPastPos(pPos, idx, mPadChannel);
    pPos->x += 1.0f;
    pPos->y += 1.0f;
    pPos->x *= MR::getScreenWidth() * cNum_calcPastPointingPosOnScreen_f1;
    pPos->y *= static_cast< s32 >(NebulaFrameClock::cScreenLines) * cNum_calcPastPointingPosOnScreen_f2;
}

void StarPointerController::updatePointerInfo() {
    if (!MR::isPointerInScreen(mPadChannel)) {
        Vec2f vec;
        vec.x = mInfo.mPos.x;
        vec.y = mInfo.mPos.y;

        if (mInfo.mInScreen && cNum_updatePointerInfo_f1 < mScreenVel.length()) {
            forcePointerOut();
        }

        mInfo.mInScreen = false;
        mScreenPos.set(vec);
        updateAdditionalInfo();
    } else {
        mScreenPos.set(mInfo.mPos);
        mInfo.mInScreen = true;
        mInfo.mPos.set(mNextPastPosition[0]);
        StarPointerFunction::forceInsideScreenEdge(&mInfo.mPos);
        mScreenDist = MR::getPointerDistance(mPadChannel);
        updateAdditionalInfo();
    }
}

void StarPointerController::updateAdditionalInfo() {
    mScreenVel.set(mInfo.mPos);
    mScreenVel.sub(mScreenPos);
    mScreenSpeed = mScreenVel.length();
    mWorldVel.set< f32 >(mScreenVel.x, -mScreenVel.y, 0.0f);
    Pos3f mtx;
    NebulaMtxCopy(MR::getStarPointerViewMtx(), mtx);
    NebulaMtxInverse(mtx, mtx);
    NebulaMtxMultVecSR(mtx, &mWorldVel, &mWorldVel);
    MR::normalizeOrZero(&mWorldVel);
}

void StarPointerController::forcePointerOut() {
    if (!StarPointerFunction::isInScreen(mInfo.mPos, 0.0f, 0.0f)) {
        return;
    }

    Vec2f vec;
    vec.set(mInfo.mPos);
    vec.sub(mScreenPos);
    StarPointerFunction::forceOutsideScreenEdge(&mInfo.mPos, mInfo.mPos, vec, -cNum_forcePointerOut_f1, -cNum_forcePointerOut_f2);
}

f32 StarPointerController::calcViewDistanceZ(const Vec3f& rPos, MtxPtr pMtx) {
    return MR::abs(rPos.x * pMtx[2][0] + rPos.y * pMtx[2][1] + rPos.z * pMtx[2][2] + pMtx[2][3]);
}

