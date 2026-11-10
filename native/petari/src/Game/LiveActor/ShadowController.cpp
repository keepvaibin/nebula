#include "Game/LiveActor/ShadowController.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowDrawer.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

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
      mIsFollowHostScale(), mIsValid(true) {
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

void ShadowControllerHolder::updateController() {
    const s32 count = mRequested.size();
    for (s32 i = 0; i < count; i++) {
        mRequested[i]->update();
    }

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
