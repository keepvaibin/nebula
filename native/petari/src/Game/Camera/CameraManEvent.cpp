#include "Game/Camera/CameraManEvent.hpp"
#include "Game/Camera/Camera.hpp"
#include "Game/Camera/CameraAnim.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Camera/CameraHeightArrange.hpp"
#include "Game/Camera/CameraHolder.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraParamChunkHolder.hpp"
#include "Game/Camera/CameraParamChunkID.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setSafePose_f1, "setSafePose.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_setSafePose_f2, "setSafePose.f2", 0.98f);

namespace {
    NEBULA_TUNABLE(f32, sMinDistance, 300.0f);
    NEBULA_TUNABLE(s32, sDefaultBlendFrame, 60);
};  // namespace

CameraManEvent::CameraManEvent(CameraHolder* pHolder, CameraParamChunkHolder* pChunkHolder, const char* pName)
    : CameraMan(pName), mHolder(pHolder), mChunkHolder(pChunkHolder), mCamera(), mChunk(), mRequestReset() {
    lumacore::setClass(this, lumacore::kClassId< CameraManEvent >);
    for (u32 i = 0; i < cPriorityNum; i++) {
        mSlots[i].mActive.mChunk = nullptr;
        mSlots[i].mRequested.mChunk = nullptr;
    }
}

void CameraManEvent::init(const TableIter& /*rIter*/) {
}

void CameraManEvent::calc() {
    takeRequests();
    applyChunk();
    changeCamera();
    resetCameraIfRequested();
    CameraTargetObj* target = mCamera->calc();
    CameraLocalUtil::setUsedTarget(this, target);
    setSafePose();
}

void CameraManEvent::start(s32 zoneID, const char* pName, const CameraTargetArg& rTargetArg, s32 frame) {
    CameraParamChunkEvent* chunk = findChunk(zoneID, pName);
    // An event camera name the zone's camera data lacks starts nothing.
    if (chunk == nullptr) {
        return;
    }
    request(chunk, chunk->mEvPriority, rTargetArg, frame);
    rTargetArg.setTarget();
}

void CameraManEvent::end(s32 zoneID, const char* pName, s32 frame) {
    CameraParamChunkEvent* chunk = findChunk(zoneID, pName);

    if (removeRequest(chunk) && chunk != nullptr) {
        sendFinishInterpolateFrame(chunk, frame);
    }

    if (hasNoRequest()) {
        mChunk = nullptr;
        MR::cleanEventCameraTarget_temporally();
    }
}

bool CameraManEvent::isEventActive(s32 zoneID, const char* pName) const {
    if (mCamera != nullptr) {
        CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
        chunkID.createEventID(zoneID, pName);

        CameraParamChunkEvent* chunk = static_cast< CameraParamChunkEvent* >(mChunkHolder->getChunk(chunkID));

        // From the highest priority down. The loop counted up (i++) and read past the slots when the top one was empty.
        for (s32 i = cPriorityNum - 1; i >= 0; i--) {
            if (mSlots[i].mRequested.mChunk != nullptr) {
                return mSlots[i].mRequested.mChunk == chunk;
            }

            if (mSlots[i].mActive.mChunk != nullptr) {
                return mSlots[i].mActive.mChunk == chunk;
            }
        }
    }

    return false;
}

bool CameraManEvent::isActive() const {
    return !hasNoRequest();
}

bool CameraManEvent::isInterpolationOff() const {
    if (mCamera != nullptr && mCamera->isInterpolationOff()) {
        return true;
    }

    if (mChunk != nullptr && mChunk->isAntiBlurOff()) {
        return true;
    }

    return false;
}

bool CameraManEvent::isCollisionOff() const {
    if (mCamera != nullptr && mCamera->isCollisionOff()) {
        return true;
    }

    if (mChunk != nullptr && mChunk->isCollisionOff()) {
        return true;
    }

    return false;
}

bool CameraManEvent::isZeroFrameMoveOff() const {
    if (mCamera != nullptr) {
        return mCamera->isZeroFrameMoveOff();
    }

    return false;
}

bool CameraManEvent::doesNextChunkHaveInterpolation() const {
    const Request* item = nullptr;

    for (u32 i = 0; i < cPriorityNum; i++) {
        if (mSlots[i].mRequested.mChunk != nullptr) {
            item = &mSlots[i].mRequested;
            break;
        }

        if (mSlots[i].mActive.mChunk != nullptr) {
            break;
        }
    }

    if (item != nullptr && getInterpolateFrame(item->mChunk, item->mFrame) > 0) {
        return true;
    }

    return false;
}

bool CameraManEvent::isCorrectingErpPositionOff() const {
    return mCamera != nullptr && mCamera->isCorrectingErpPositionOff();
}

u32 CameraManEvent::getAnimCameraFrame(s32 zoneID, const char* pName) const {
    const CameraParamChunkEvent* const pChunk = findChunk(zoneID, pName);
    // An event camera name the zone's camera data lacks has no frames.
    if (pChunk == nullptr) {
        return 0;
    }
    return CameraAnim::getAnimFrame(pChunk->mGeneralParam->mAnimData);
}

void CameraManEvent::pauseOnAnimCamera(s32 zoneID, const char* pName) {
    CameraParamChunkEvent* chunk = findChunk(zoneID, pName);

    if (chunk == mChunk && mCamera != nullptr) {
        static_cast< CameraAnim* >(mCamera)->mIsPaused = true;
    }
}

void CameraManEvent::pauseOffAnimCamera(s32 zoneID, const char* pName) {
    CameraParamChunkEvent* chunk = findChunk(zoneID, pName);

    if (chunk == mChunk && mCamera != nullptr) {
        static_cast< CameraAnim* >(mCamera)->mIsPaused = false;
    }
}

void CameraManEvent::takeRequests() {
    for (u32 i = 0; i < cPriorityNum; i++) {
        if (mSlots[i].mRequested.mChunk != nullptr) {
            mSlots[i].mActive = mSlots[i].mRequested;
            mSlots[i].mRequested.mChunk = nullptr;
        }
    }
}

void CameraManEvent::applyChunk() {
    Request* item = nullptr;

    for (u32 i = 0; i < cPriorityNum; i++) {
        if (mSlots[i].mActive.mChunk != nullptr) {
            item = &mSlots[i].mActive;
            break;
        }
    }

    checkReset(item);
    mChunk = item->mChunk;
}

void CameraManEvent::checkReset(Request* pItem) {
    CameraParamChunkEvent* itemChunk = pItem->mChunk;

    if (mChunk != itemChunk) {
        mRequestReset = true;
        sendStartInterpolateFrame(pItem->mChunk, pItem->mFrame);
        pItem->mTargetArg.setTarget();
    } else {
        u8 cameraIndex = itemChunk->mCameraTypeIndex;

        if (cameraIndex != mHolder->getIndexOf(mCamera)) {
            mRequestReset = true;
        }
    }
}

void CameraManEvent::changeCamera() {
    mCamera = mHolder->getCameraInner(mChunk->mCameraTypeIndex);
    mCamera->setParamFromChunk(mChunk);
    setExtraParam();
    setVPanParam();
}

void CameraManEvent::setExtraParam() {
    s32 zoneID = mChunk->getZoneID();
    mCamera->setZoneMtx(zoneID);

    CameraLocalUtil::setGlobalOffset(mCamera, mChunk->mExParam.mWOffset);
    CameraLocalUtil::setLocalOffset(mCamera, CameraLocalUtil::getLocalOffset(this));
    CameraLocalUtil::setFrontOffset(mCamera, mChunk->mExParam.mLOffset);
    CameraLocalUtil::setUpperOffset(mCamera, mChunk->mExParam.mLOffsetV);

    if (mChunk->isOnUseFovy()) {
        CameraLocalUtil::setFovy(mCamera, mChunk->mExParam.mFovy);
    } else {
        CameraLocalUtil::setFovy(mCamera, mDirector->getDefaultFovy());
    }

    if (mChunk->isLOfsErpOff()) {
        mCamera->mIsLOfsErpOff = true;
    } else {
        mCamera->mIsLOfsErpOff = false;
    }

    CameraLocalUtil::setRoll(mCamera, mChunk->mExParam.mRoll);
}

void CameraManEvent::setVPanParam() {
    if (mCamera->doesVPanExist()) {
        CameraHeightArrange* vPan = mCamera->mVPan;
        vPan->resetParameter();

        vPan->mFocalScaleUpper = mChunk->mExParam.mUpper;
        vPan->mFocalScaleLower = mChunk->mExParam.mLower;
        vPan->mGndInt = mChunk->mExParam.mGndInt;
        vPan->mPosOffsetMinRiseLag = mChunk->mExParam.mUPlay;
        vPan->mPosOffsetMinDropLag = mChunk->mExParam.mLPlay;
        vPan->mRiseDelay = mChunk->mExParam.mPushDelay;
        vPan->mDropDelay = mChunk->mExParam.mPushDelayLow;
        vPan->mMaxRiseEaseTime = mChunk->mExParam.mUDown;
        vPan->mVPanUse = mChunk->mExParam.mVPanUse != 0;

        Vec3f axis;
        mChunk->getVPanAxis(&axis);

        vPan->mVPanAxis.set(axis);

        vPan->mUpdateGlobalAxis = true;
    }
}

void CameraManEvent::resetCameraIfRequested() {
    if (mRequestReset) {
        mCamera->mCameraMan = this;
        mCamera->reset();
        mRequestReset = false;
    }
}

void CameraManEvent::setSafePose() {
    Vec3f pos = CameraLocalUtil::getPos(mCamera);
    Vec3f watchPos = CameraLocalUtil::getWatchPos(mCamera);
    Vec3f up = CameraLocalUtil::getUpVec(mCamera);

    Vec3f watchOffset = watchPos - pos;
    f32 dist = watchOffset.length();
    if (dist < ::sMinDistance) {
        if (dist < 1.0f) {
            watchPos.set(pos + CameraLocalUtil::getWatchPos(this) - CameraLocalUtil::getPos(this));
        } else {
            watchOffset.normalize();
            watchPos.set(pos + watchOffset * ::sMinDistance);
        }
    }

    Vec3f camWatchDir = watchPos - pos;
    MR::normalize(&camWatchDir);
    MR::normalizeOrZero(&up);

    if (MR::isNearZero(up) || MR::abs(camWatchDir.dot(up)) > cNum_setSafePose_f1) {
        Vec3f watchDir = CameraLocalUtil::getWatchPos(this) - CameraLocalUtil::getPos(this);
        MR::normalize(&watchDir);
        if (MR::abs(camWatchDir.dot(watchDir)) > cNum_setSafePose_f2) {
            up.set(CameraLocalUtil::getUpVec(this));
        } else {
            Quat4f rot;
            rot.setRotate(watchDir, camWatchDir);
            rot.transform(CameraLocalUtil::getUpVec(this), up);
        }

        CameraLocalUtil::recalcUpVec(&up, camWatchDir);
    }

    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getWatchUpVec(mCamera));
    CameraLocalUtil::setGlobalOffset(this, CameraLocalUtil::getGlobalOffset(mCamera));
    CameraLocalUtil::setLocalOffset(this, CameraLocalUtil::getLocalOffset(mCamera));
    CameraLocalUtil::setFovy(this, CameraLocalUtil::getFovy(mCamera));
    CameraLocalUtil::setRoll(this, CameraLocalUtil::getRoll(mCamera));
}

CameraParamChunkEvent* CameraManEvent::findChunk(s32 zoneID, const char* pName) const {
    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createEventID(zoneID, pName);

    return static_cast< CameraParamChunkEvent* >(mChunkHolder->getChunk(chunkID));
}

void CameraManEvent::request(CameraParamChunkEvent* pChunk, u32 priority, const CameraTargetArg& rArg, s32 frame) {
    Request& rRequest = mSlots[priority].mRequested;
    rRequest.mChunk = pChunk;
    rRequest.mTargetArg = rArg;
    rRequest.mFrame = frame;
}

// Clears pChunk's request; true when no higher-priority slot held one.
bool CameraManEvent::removeRequest(CameraParamChunk* pChunk) {
    s32 held = 0;

    for (u32 i = 0; i < cPriorityNum; i++) {
        if (mSlots[i].mRequested.mChunk != nullptr && mSlots[i].mRequested.mChunk == pChunk) {
            mSlots[i].mActive.mChunk = nullptr;
            mSlots[i].mRequested.mChunk = nullptr;
            break;
        }

        if (mSlots[i].mActive.mChunk != nullptr && mSlots[i].mActive.mChunk == pChunk) {
            mSlots[i].mActive.mChunk = nullptr;
            break;
        }

        if (mSlots[i].mActive.mChunk != nullptr || mSlots[i].mRequested.mChunk != nullptr) {
            held++;
        }
    }

    return held == 0;
}

bool CameraManEvent::hasNoRequest() const {
    for (u32 i = 0; i < cPriorityNum; i++) {
        if (mSlots[i].mActive.mChunk != nullptr) {
            return false;
        }

        if (mSlots[i].mRequested.mChunk != nullptr) {
            return false;
        }
    }

    return true;
}

void CameraManEvent::sendStartInterpolateFrame(CameraParamChunkEvent* pChunk, s32 frame) {
    u32 frames = getInterpolateFrame(pChunk, frame);

    if (frames == 0) {
        mRequestLOfsReset = true;
    }

    mDirector->setInterpolation(frames);
}

s32 CameraManEvent::getInterpolateFrame(CameraParamChunkEvent* pChunk, s32 frame) const {
    s32 frames = -1;

    if (pChunk->mEnableErpFrame != 0) {
        frames = pChunk->mExParam.mCamInt;
    }

    if (frames < 0 && frame >= 0) {
        frames = frame;
    }

    if (frames < 0) {
        frames = ::sDefaultBlendFrame;
    }

    return frames;
}

void CameraManEvent::sendFinishInterpolateFrame(CameraParamChunkEvent* pChunk, s32 frame) {
    s32 frames = -1;

    if (pChunk->mEnableEndErpFrame) {
        frames = pChunk->mCamEndInt;
    } else if (pChunk->mEnableErpFrame) {
        frames = pChunk->mExParam.mCamInt;
    }

    if (frames < 0 && frame >= 0) {
        frames = frame;
    }

    if (frames < 0) {
        frames = ::sDefaultBlendFrame;
    }

    mDirector->setInterpolation(frames);
}

bool CameraManEvent::isRequested(CameraParamChunk* pChunk) const {
    for (u32 i = 0; i < cPriorityNum; i++) {
        if (mSlots[i].mActive.mChunk == pChunk || mSlots[i].mRequested.mChunk == pChunk) {
            return true;
        }
    }

    return false;
}

void CameraManEvent::notifyActivate() {
    mRequestReset = true;
}

void CameraManEvent::notifyDeactivate() {
    mCamera = nullptr;
}

bool CameraManEvent::isAnimCameraEnd(s32 zoneID, const char* pName) const {
    CameraParamChunkEvent* chunk = findChunk(zoneID, pName);

    if (isRequested(chunk)) {
        if (chunk == mChunk) {
            if (mCamera != nullptr) {
                return static_cast< CameraAnim* >(mCamera)->isAnimEnd();
            }
        } else {
            return false;
        }
    }

    return true;
}
