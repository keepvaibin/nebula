#include "Game/LiveActor/ShadowController.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowDrawer.hpp"
#include "Game/Map/CollisionChanges.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/NebulaHost.hpp"

#if defined(NEBULA_PLAIN_PATHS)
namespace {
    // Equal to the bit: a NaN equals a NaN of the same bits, 0 and -0 differ (a query with either may answer differently).
    bool isSameBits(const Vec3f& rA, const Vec3f& rB) {
        return __builtin_memcmp(&rA, &rB, sizeof(Vec3f)) == 0;
    }
}  // namespace
#endif

ShadowControllerHolder::ShadowControllerHolder() : NameObj("影管理"), mControllers(), mRequested() {
    lumacore::setClass(this, lumacore::kClassId< ShadowControllerHolder >);
    mControllers.init(0x500);
    mRequested.init(0x400);
    MR::connectToScene(this, MR::MovementType_ShadowControllerHolder, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

ShadowController::ShadowController(LiveActor* pActor, const char* pName)
    : mActor(pActor), mName(pName), mGroupName(""), mDrawer(), mProjectedSensor(), mCollisionPartsFilter(), mHostMtx(), mDropPos(), mDropMtx(),
      mDropOffset(0.0f, 0.0f, 0.0f), mDropDir(), mDropDirection(0.0f, -1.0f, 0.0f), mProjPos(), mProjNorm(), mProjectionPos(0.0f, 0.0f, 0.0f),
      mProjectionNormal(0.0f, 1.0f, 0.0f), mStartOffset(50.0f), mDropLength(), mCollisionCalc(CollisionCalc::On), mCollisionCount(),
      mDropGravity(DropGravity::None), mDropType(DropType::Map), mVisibility(Visibility::SyncHost), mIsProjected(), mIsRequested(),
      mIsFollowHostScale(), mIsValid(true), mLineKept(false), mLineHit(false), mLineType(DropType::Map), mLineFilter(), mLineChanges(),
      mLineStart(0.0f, 0.0f, 0.0f), mLine(0.0f, 0.0f, 0.0f), mLinePos(0.0f, 0.0f, 0.0f), mLineNormal(0.0f, 1.0f, 0.0f), mLineSensor() {
    MR::createSceneObj(SceneObj_ShadowControllerHolder);
    MR::addShadowController(this);
}

void ShadowControllerHolder::movement() {
    updateController();
}

void ShadowControllerHolder::initAfterPlacement() {
    const s32 count = mControllers.size();
    for (s32 i = 0; i < count; i++) {
        mControllers[i]->updateDirection();
        mControllers[i]->updateProjection();
    }
}

#if defined(NEBULA_PLAIN_PATHS)
namespace {
    // Shadows a step thread takes at a time: a gravity search each, and a line query each whose answer is not kept.
    const u32 sPrepareGrain = 16;

    // The pass: the requests, and a room of hits for each range of them.
    struct ShadowPass {
        ShadowController* const* mRequested;
        HitInfo* mRooms;
    };

    // The requested shadows' first part on the step threads: a range of the request list (its first a multiple of the
    // grain), with the range's room.
    void prepareShadows(void* pContext, u32 first, u32 last) {
        const ShadowPass* const pPass = static_cast< const ShadowPass* >(pContext);
        HitInfo* const pRoom = pPass->mRooms + (first / sPrepareGrain) * MR::cLineRoom;
        for (u32 i = first; i < last; i++) {
            pPass->mRequested[i]->prepareUpdate(pRoom);
        }
    }
}  // namespace
#endif

void ShadowControllerHolder::updateController() {
    const s32 count = mRequested.size();
#if defined(NEBULA_PLAIN_PATHS)
    // Each requested shadow's draw test, direction (its gravity search), line, kept answer and else its line's query on
    // the step threads; then the projections in request order on this one. A shadow's first part reads nothing another's
    // changes. The parts' indexes are counted and made first, as the queries on this thread would.
    if (count > 0) {
        MR::readyLineQueries(0);
        MR::readyLineQueries(2);
        const u32 ranges = (static_cast< u32 >(count) + ::sPrepareGrain - 1) / ::sPrepareGrain;
        const ::ShadowPass pass = {&mRequested[0], mLineRooms.room(ranges * MR::cLineRoom)};
        NebulaHost::parallelFor(static_cast< u32 >(count), ::sPrepareGrain, &::prepareShadows, const_cast< ::ShadowPass* >(&pass));
    }
    for (s32 i = 0; i < count; i++) {
        mRequested[i]->finishUpdate();
    }
#else
    for (s32 i = 0; i < count; i++) {
        mRequested[i]->update();
    }
#endif

    mRequested.clear();
}

ShadowControllerList::ShadowControllerList(LiveActor* pActor, u32 listCount) : mShadowList(), mHost(pActor) {
    mShadowList.init(listCount);
}

void ShadowControllerList::addController(ShadowController* pController) {
    mShadowList.push_back(pController);
}

u32 ShadowControllerList::getControllerCount() const {
    return mShadowList.size();
}

ShadowController* ShadowControllerList::getController(u32 idx) const {
    return mShadowList[idx];
}

ShadowController* ShadowControllerList::getController(const char* pName) const {
    if (mShadowList.size() == 1) {
        return mShadowList[0];
    }

    for (u32 i = 0; i < mShadowList.size(); i++) {
        if (MR::isEqualString(pName, mShadowList[i]->mName)) {
            return mShadowList[i];
        }
    }

    return nullptr;
}

void ShadowControllerList::resetCalcCount() {
    for (u32 i = 0; i < mShadowList.size(); i++) {
        mShadowList[i]->mCollisionCount = 0;
    }
}

void ShadowControllerList::requestCalc() {
    for (u32 i = 0; i < mShadowList.size(); i++) {
        mShadowList[i]->requestCalc();
    }
}

void ShadowController::requestCalc() {
    if (!mIsRequested) {
        mIsRequested = true;
        MR::getSceneObj< ShadowControllerHolder >(SceneObj_ShadowControllerHolder)->mRequested.push_back(this);
    }
}

void ShadowController::update() {
    if (isDraw()) {
        updateDirection();
        updateProjection();
    }

    mIsRequested = false;
}

void ShadowController::updateDirection() {
    if (!isCalcGravity()) {
        return;
    }

    Vec3f position;
    getDropPos(&position);
    Vec3f previousDirection(mDropDirection);
    if (!isCalcShadowGravity() || !MR::calcDropShadowVectorOrZero(mActor, position, &mDropDirection, nullptr, 0)) {
        if (!MR::calcGravityVectorOrZero(mActor, position, &mDropDirection, nullptr, 0)) {
            mDropDirection = previousDirection;
        }
    }
}

void ShadowController::updateProjection() {
    if (!isCalcCollision()) {
        return;
    }

#if defined(NEBULA_PLAIN_PATHS)
    Vec3f position;
    Vec3f line;
    lineOf(&position, &line);
    projectLine(position, line, isLineAnswerKept(position, line));
}

// The line a projection queries: from the drop point, moved back along the drop direction by the start offset, as long
// as the drop length and the offset.
void ShadowController::lineOf(Vec3f* pStart, Vec3f* pLine) const {
    getDropPos(pStart);
    Vec3f direction;
    getDropDir(&direction);
    *pStart -= direction * mStartOffset;
    *pLine = direction * (mDropLength + mStartOffset);
}

// The same line as last time, nothing changed near it since: the last answer holds (P-140). Reads only this
// controller's fields and the collision change log.
bool ShadowController::isLineAnswerKept(const Vec3f& rStart, const Vec3f& rLine) const {
    return mLineKept && mLineType == mDropType && mLineFilter == mCollisionPartsFilter && isSameBits(mLineStart, rStart) &&
           isSameBits(mLine, rLine) && CollisionChanges::isLineUnchanged(mLineChanges, rStart, rLine);
}

// The projection of the line: the kept answer, or the query's (kept for the next time).
void ShadowController::projectLine(const Vec3f& rStart, const Vec3f& rLine, bool kept, const MR::LineAnswer* pAsked) {
    Triangle triangle;
    const Vec3f position(rStart);
    const Vec3f& line = rLine;
    // The same line as last time, nothing changed near it since: the same answer, without the query (P-140). A miss
    // leaves the projection's place and normal as they were, as the query does.
    if (kept) {
        mIsProjected = mLineHit;
        if (mLineHit) {
            mProjectionPos = mLinePos;
            mProjectedSensor = mLineSensor;
            mProjectionNormal = mLineNormal;
        } else {
            mProjectedSensor = nullptr;
        }
#if defined(NEBULA_SHADOW_LINE_VERIFY)
        // A build that checks the reuse: the query made anyway, its answer the kept one to the bit.
        Vec3f checkPos(mProjectionPos);
        bool orderFree = false;
        const bool checkHit = MR::getFirstPolyOnLineOrderFree(&checkPos, &triangle, position, line, mCollisionPartsFilter,
                                                              mDropType == DropType::Map ? 0 : 2, &orderFree);
        if (checkHit != mLineHit ||
            (checkHit && (!isSameBits(checkPos, mLinePos) || triangle.getSensor() != mLineSensor ||
                          !isSameBits(*triangle.getNormal(0), mLineNormal)))) {
            NEBULA_PANIC("shadow line reuse differs from the query: %s, hit %d kept %d", mName, checkHit, mLineHit);
        }
#endif
    } else if (pAsked != nullptr) {
        // The query made on a step thread (P-146): its answer, as the query here would give it.
        mIsProjected = pAsked->mHit;
        if (mIsProjected) {
            mProjectionPos = pAsked->mPos;
            mProjectedSensor = pAsked->mSensor;
            mProjectionNormal.set(pAsked->mNormal);
        } else {
            mProjectedSensor = nullptr;
        }
#if defined(NEBULA_SHADOW_LINE_VERIFY)
        // A build that checks the step thread's query: the query made here too, its answer the same to the bit.
        Vec3f checkPos(mProjectionPos);
        bool checkFree = false;
        const bool checkHit = MR::getFirstPolyOnLineOrderFree(&checkPos, &triangle, position, line, mCollisionPartsFilter,
                                                              mDropType == DropType::Map ? 0 : 2, &checkFree);
        if (checkHit != pAsked->mHit || checkFree != pAsked->mOrderFree ||
            (checkHit && (!isSameBits(checkPos, pAsked->mPos) || triangle.getSensor() != pAsked->mSensor ||
                          !isSameBits(*triangle.getNormal(0), pAsked->mNormal)))) {
            NEBULA_PANIC("shadow line query on a step thread differs: %s, hit %d asked %d", mName, checkHit, pAsked->mHit);
        }
#endif
        rememberLine(position, line, pAsked->mOrderFree);
    } else {
        bool orderFree = false;
        mIsProjected = MR::getFirstPolyOnLineOrderFree(&mProjectionPos, &triangle, position, line, mCollisionPartsFilter,
                                                       mDropType == DropType::Map ? 0 : 2, &orderFree);
        if (mIsProjected) {
            mProjectedSensor = triangle.getSensor();
            mProjectionNormal.set(*triangle.getNormal(0));
        } else {
            mProjectedSensor = nullptr;
        }

        rememberLine(position, line, orderFree);
    }

    if (mCollisionCalc == CollisionCalc::Once) {
        mCollisionCount++;
    }
}

// A query's answer kept for the next time (P-140): the line, the state it was asked in and the projection it gave.
void ShadowController::rememberLine(const Vec3f& rStart, const Vec3f& rLine, bool orderFree) {
    mLineKept = orderFree;
    mLineHit = mIsProjected;
    mLineType = mDropType;
    mLineFilter = mCollisionPartsFilter;
    mLineChanges = CollisionChanges::count();
    mLineStart = rStart;
    mLine = rLine;
    mLinePos = mProjectionPos;
    mLineNormal = mProjectionNormal;
    mLineSensor = mProjectedSensor;
}

// On a step thread: whether it draws, its direction, its line, whether the last answer holds, and else the line's query
// (P-146). Reads its actor, the gravities, the collision and its change log, which nothing changes while the pass runs;
// writes only its own fields and the step thread's room.
void ShadowController::prepareUpdate(HitInfo* pRoom) {
    mPassDraw = isDraw();
    mPassCollide = false;
    mPassKept = false;
    mPassAsked = false;
    if (!mPassDraw) {
        return;
    }
    updateDirection();
    mPassCollide = isCalcCollision();
    if (mPassCollide) {
        lineOf(&mPassStart, &mPassLine);
        mPassKept = isLineAnswerKept(mPassStart, mPassLine);
        if (!mPassKept) {
            mPassAsked = MR::askFirstPolyOnLine(&mPassAnswer, pRoom, mPassStart, mPassLine, mCollisionPartsFilter,
                                                mDropType == DropType::Map ? 0 : 2);
        }
    }
}

// On the game thread, in request order: the projection, as update makes it.
void ShadowController::finishUpdate() {
    if (mPassCollide) {
        projectLine(mPassStart, mPassLine, mPassKept, mPassAsked ? &mPassAnswer : nullptr);
    }
    mIsRequested = false;
}
#else
    Triangle triangle;
    Vec3f position;
    getDropPos(&position);
    Vec3f direction;
    getDropDir(&direction);
    position -= direction * mStartOffset;

    const Vec3f line = direction * (mDropLength + mStartOffset);
    if (mDropType == DropType::Map) {
        mIsProjected = MR::getFirstPolyOnLineToMap(&mProjectionPos, &triangle, position, line, mCollisionPartsFilter, nullptr);
    } else {
        mIsProjected = MR::getFirstPolyOnLineToWaterSurface(&mProjectionPos, &triangle, position, line, mCollisionPartsFilter, nullptr);
    }

    if (mIsProjected) {
        mProjectedSensor = triangle.getSensor();
        mProjectionNormal.set(*triangle.getNormal(0));
    } else {
        mProjectedSensor = nullptr;
    }

    if (mCollisionCalc == CollisionCalc::Once) {
        mCollisionCount++;
    }
}
#endif

ShadowDrawer* ShadowController::getShadowDrawer() {
    return mDrawer;
}

LiveActor* ShadowController::getHost() const {
    return mActor;
}

void ShadowController::setShadowDrawer(ShadowDrawer* pDrawer) {
    mDrawer = pDrawer;
    pDrawer->setShadowController(this);
}

void ShadowController::getDropPos(Vec3f* pOut) const {
    if (mDropPos != nullptr) {
        pOut->set(*mDropPos);
    } else if (mDropMtx != nullptr) {
        NebulaMtxMultVec(mDropMtx, (const Vec*)&mDropOffset, (Vec*)pOut);
    } else {
        pOut->set(mDropOffset);
    }
}

void ShadowController::getDropDir(Vec3f* pOut) const {
    pOut->set(mDropDir != nullptr ? *mDropDir : mDropDirection);
}

f32 ShadowController::getDropLength() const {
    return mDropLength;
}

void ShadowController::getProjectionPos(Vec3f* pOut) const {
    pOut->set(mProjPos != nullptr ? *mProjPos : mProjectionPos);
}

void ShadowController::getProjectionNormal(Vec3f* pOut) const {
    pOut->set(mProjNorm != nullptr ? *mProjNorm : mProjectionNormal);
}

// -1 with no projection; 0 when the projection is behind the drop point.
f32 ShadowController::getProjectionLength() const {
    if (!mIsProjected) {
        return -1.0f;
    }

    Vec3f position;
    getDropPos(&position);
    Vec3f direction;
    getDropDir(&direction);
    Vec3f difference(mProjectionPos);
    difference -= position;
    if (direction.dot(difference) < 0.0f) {
        return 0.0f;
    }

    return position.distance(mProjectionPos);
}

bool ShadowController::isProjected() const {
    return mIsProjected;
}

bool ShadowController::isDraw() const {
    if (!mIsValid) {
        return false;
    }

    // An actor that isn't there has no shadow, whatever its table says: the hungry Lumas of the Observatory wait dead until
    // their time comes, and their tables keep the shadow apart from the actor.
    if (mActor->mFlag.mIsDead) {
        return false;
    }

    switch (mVisibility) {
    case Visibility::Always:
        return true;
    case Visibility::UnlessHidden:
        return !mActor->mFlag.mIsHiddenModel;
    case Visibility::SyncHost:
        break;
    }

    return MR::isValidDraw(mActor);
}

bool ShadowController::isCalcCollision() const {
    switch (mCollisionCalc) {
    case CollisionCalc::Off:
        return false;
    case CollisionCalc::Once:
        return mCollisionCount < 1;
    case CollisionCalc::On:
        break;
    }

    return true;
}

bool ShadowController::isCalcGravity() const {
    return mDropGravity != DropGravity::None;
}

bool ShadowController::isCalcShadowGravity() const {
    return mDropGravity == DropGravity::ShadowGravity;
}

void ShadowController::setGroupName(const char* pName) {
    mGroupName = pName;
}

void ShadowController::setDropPosPtr(const Vec3f* pDropPos) {
    mDropPos = pDropPos;
    mDropMtx = nullptr;
}

void ShadowController::setDropPosMtxPtr(MtxPtr pMtx, const Vec3f& rOffset) {
    mHostMtx = pMtx;
    mDropPos = nullptr;
    mDropMtx = pMtx;
    mDropOffset.set(rOffset);
}

void ShadowController::setDropPosFix(const Vec3f& rPos) {
    mDropOffset.set(rPos);
    mDropPos = nullptr;
    mDropMtx = nullptr;
}

void ShadowController::setDropDirPtr(const Vec3f* pDropDir) {
    mDropDir = pDropDir;
    mDropGravity = DropGravity::None;
}

void ShadowController::setProjectionPtr(const Vec3f* pPosition, const Vec3f* pNormal) {
    mProjPos = pPosition;
    mProjNorm = pNormal;
    mCollisionCalc = CollisionCalc::Off;
    mIsProjected = true;
    mProjectedSensor = nullptr;
}

void ShadowController::setDropDirFix(const Vec3f& rDirection) {
    mDropDirection.set(rDirection);
    mDropDir = nullptr;
    mDropGravity = DropGravity::None;
}

void ShadowController::setDropLength(f32 len) {
    mDropLength = len;
}

void ShadowController::setDropStartOffset(f32 offs) {
    mStartOffset = offs;
}

void ShadowController::setDropTypeNormal() {
    mDropType = DropType::Map;
}

void ShadowController::setDropTypeSurface() {
    mDropType = DropType::WaterSurface;
}

void ShadowController::setProjectionFix(const Vec3f& rPosition, const Vec3f& rNormal, bool projected) {
    mProjectionPos.set(rPosition);
    mProjectionNormal.set(rNormal);
    mIsProjected = projected;
    mProjectedSensor = nullptr;
}

void ShadowController::onCalcCollision() {
    mCollisionCalc = CollisionCalc::On;
}

void ShadowController::offCalcCollision() {
    mCollisionCalc = CollisionCalc::Off;
}

void ShadowController::onCalcCollisionOneTime() {
    mCollisionCalc = CollisionCalc::Once;
    mCollisionCount = 0;
}

void ShadowController::onCalcDropGravity() {
    mDropDirection.set(0.0f, 1.0f, 0.0f);
    mDropDir = nullptr;
    mDropGravity = DropGravity::Gravity;
}

void ShadowController::onCalcDropGravityOneTime() {
    onCalcDropGravity();
}

void ShadowController::offCalcDropGravity() {
    mDropGravity = DropGravity::None;
}

void ShadowController::onCalcDropPrivateGravity() {
    mDropDirection.set(0.0f, 1.0f, 0.0f);
    mDropDir = nullptr;
    mDropGravity = DropGravity::ShadowGravity;
}

void ShadowController::onCalcDropPrivateGravityOneTime() {
    onCalcDropPrivateGravity();
}

void ShadowController::offCalcDropPrivateGravity() {
    mDropGravity = DropGravity::None;
}

void ShadowController::setCollisionPartsFilter(CollisionPartsFilterBase* pBase) {
    mCollisionPartsFilter = pBase;
    // A new filter may be made where an old one was: the last line query's answer is not kept (P-140).
    mLineKept = false;
}

void ShadowController::onFollowHostScale() {
    mIsFollowHostScale = true;
}

void ShadowController::offFollowHostScale() {
    mIsFollowHostScale = false;
}

bool ShadowController::isFollowHostScale() const {
    return mIsFollowHostScale;
}

void ShadowController::onVisibleSyncHost() {
    mVisibility = Visibility::SyncHost;
}

void ShadowController::offVisibleSyncHost() {
    mVisibility = Visibility::Always;
}

void ShadowController::offVisibleSyncHostUnlessHidden() {
    mVisibility = Visibility::UnlessHidden;
}

void ShadowController::validate() {
    mIsValid = true;
}

void ShadowController::invalidate() {
    mIsValid = false;
}

namespace MR {
    void addShadowController(ShadowController* pController) {
        getSceneObj< ShadowControllerHolder >(SceneObj_ShadowControllerHolder)->mControllers.push_back(pController);
    }

    void requestCalcActorShadowAppear(LiveActor* pActor) {
        if (!isInitializeStatePlacementSomething()) {
            if (pActor->mShadowControllerList) {
                pActor->mShadowControllerList->resetCalcCount();
            }

            requestCalcActorShadow(pActor);
        }
    }

    void requestCalcActorShadow(LiveActor* pActor) {
        if (pActor->mShadowControllerList) {
            pActor->mShadowControllerList->requestCalc();
        }
    }
};  // namespace MR

ShadowControllerHolder::~ShadowControllerHolder() {
}
