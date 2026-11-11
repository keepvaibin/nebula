#include "Game/LiveActor/ClippingActorInfo.hpp"
#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/ClippingJudge.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ViewGroupCtrl.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/Util.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_ClippingActorInfo_f1, "ClippingActorInfo.f1", 300.0f);

namespace {
    // Distance a clipping sphere must pass beyond the view volume before an actor that runs is clipped (or stops being
    // drawn while clipped); about the width of a large enemy, more than any camera or actor jitter at an edge.
    NEBULA_TUNABLE(f32, sClipLeaveMargin, 150.0f);
};  // namespace

ClippingActorInfo::ClippingActorInfo(LiveActor* pActor)
    : mActor(pActor), mCenter(), mRadius(0.0f), mInfo(), mFarClipLevel(6), mViewGroupId(-1), mViewGroup(), mGround(), mGroundFound(false), mList(),
      mIndex(-1) {
    setTypeToSphere(cNum_ClippingActorInfo_f1, nullptr);
}

void ClippingActorInfo::judgeClipping() {
    if (isJudgedToClip()) {
        if (!MR::isClipped(mActor)) {
            startClipped();
        } else {
            updateDrawnClipped();
        }
    } else if (MR::isClipped(mActor)) {
        endClipped();
    }
}

// An actor that runs leaves the frustum only once its sphere is a margin outside it; it comes back at the frustum itself.
// With one test for both, a camera or actor moving by less than a unit across the edge started and stopped the actor's
// effects every step (an emitter stopped and started leaves gaps, one deleted and made again restarts its particles).
bool ClippingActorInfo::isJudgedToClip() const {
    const f32 radius = MR::isClipped(mActor) ? mRadius : mRadius + ::sClipLeaveMargin;
    return MR::getClippingJudge()->isJudgedToClipFrustum(*mCenter, radius, mViewGroup->mIsPlayerInside ? 0 : mFarClipLevel);
}

// The level's distances say when an actor runs; the camera says whether it is seen. Past its distance and in view it
// stays stopped and keeps its model, effects and parts, so nothing runs that the level did not expect and nothing pops
// in or goes dark. Scenery always; coins, enemies and characters once they have run: a level may place them where only
// another scenario uses them (Space Junk's web coins), relying on their staying clipped there. Never over a planet that
// is not there: view cubes and switches hide a planet and leave what stands on it to its distance.
// A clipped actor already drawn stays so until its sphere is a margin outside the frustum (same reason as isJudgedToClip).
// While the host traces, the judge's margin more (RT-MASTER-PLAN 5.3 G-1): what stands just off screen stays drawn,
// stopped, for the tracer's shadows and mirrors; the host leaves it out of its raster passes.
bool ClippingActorInfo::isDrawnWhileClipped() const {
    const f32 radius = (mActor->mFlag.mIsDrawnClipped ? mRadius + ::sClipLeaveMargin : mRadius) + MR::getClippingJudge()->getTracedDrawMargin();
    return !mViewGroup->mIsHidden && !MR::isNoEntryDrawBuffer(mActor) && MR::isDrawnFarWhenClipped(mActor, mActor->mFlag.mHasPose) &&
           !MR::getClippingJudge()->isJudgedToClipFrustum(*mCenter, radius, 0) && const_cast< ClippingActorInfo* >(this)->isGroundShown();
}

bool ClippingActorInfo::isGroundShown() {
    if (!mGroundFound) {
        mGroundFound = true;
        mGround = MR::getClippingDirector()->mActorHolder->findGround(this);
    }
    if (mGround == nullptr) {
        return true;
    }
    return !mGround->mViewGroup->mIsHidden &&!MR::isDead(mGround->mActor) && !MR::isHiddenModel(mGround->mActor);
}

void ClippingActorInfo::startClipped() {
    if (!isDrawnWhileClipped()) {
        mActor->startClipped();
        return;
    }

    const bool neverRun = !mActor->mFlag.mHasPose;
    mActor->mFlag.mIsDrawnClipped = true;
    mActor->prepareDrawClipped();
    LiveActor::sClipScope = LiveActor::ClipScope_Run;
    mActor->startClipped();
    if (neverRun) {
        // Seen before it has ever run: what it shows when it wakes (a torch's flame, a block's glow), its model already
        // connected.
        LiveActor::sClipScope = LiveActor::ClipScope_Seen;
        mActor->endClipped();
    }
    LiveActor::sClipScope = LiveActor::ClipScope_All;
}

void ClippingActorInfo::endClipped() {
    if (mActor->mFlag.mIsDrawnClipped) {
        mActor->mFlag.mIsDrawnClipped = false;
        LiveActor::sClipScope = LiveActor::ClipScope_Run;
        mActor->endClipped();
        LiveActor::sClipScope = LiveActor::ClipScope_All;
    } else {
        mActor->endClipped();
    }
}

void ClippingActorInfo::updateDrawnClipped() {
    const bool drawn = isDrawnWhileClipped();
    if (drawn == mActor->mFlag.mIsDrawnClipped) {
        return;
    }

    mActor->mFlag.mIsDrawnClipped = drawn;
    LiveActor::sClipScope = LiveActor::ClipScope_Seen;
    if (drawn) {
        mActor->prepareDrawClipped();
        mActor->endClipped();
    } else {
        mActor->startClipped();
    }
    LiveActor::sClipScope = LiveActor::ClipScope_All;
}

bool ClippingActorInfo::isGroupClipping() const {
    return mInfo;
}

void ClippingActorInfo::setTypeToSphere(f32 radius, const Vec3f* pCenter) {
    mRadius = radius;
    mCenter = pCenter != nullptr ? pCenter : &mActor->mPosition;
}

void ClippingActorInfo::setGroupClippingNo(const TableIter& rIter) {
    mInfo = new PlacementId(MR::createPlacementIdFromClippingGroupId(rIter));
}

ClippingActorInfoList::ClippingActorInfoList(s32 capacity)
    : mCapacity(capacity), mCount(0), mClippingActorList(new ClippingActorInfo*[capacity]()) {
}

void ClippingActorInfoList::add(ClippingActorInfo* pInfo) {
    if (mCount >= mCapacity) {
        mClippingActorList = MR::growArray(mClippingActorList, mCapacity);
        mCapacity *= 2;
    }

    pInfo->mList = this;
    pInfo->mIndex = mCount;
    mClippingActorList[mCount++] = pInfo;
}

void ClippingActorInfoList::remove(ClippingActorInfo* pInfo) {
    ClippingActorInfo* pLast = mClippingActorList[--mCount];
    mClippingActorList[pInfo->mIndex] = pLast;
    pLast->mIndex = pInfo->mIndex;
    pInfo->mList = nullptr;
    pInfo->mIndex = -1;
}
