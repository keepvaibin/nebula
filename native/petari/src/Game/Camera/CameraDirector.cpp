#include "Game/Camera/CameraDirector.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Boss/BossStinkBug.hpp"
#include "Game/Camera/CameraCover.hpp"
#include "Game/Camera/CameraHolder.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraManEvent.hpp"
#include "Game/Camera/CameraManGame.hpp"
#include "Game/Camera/CameraManSubjective.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraParamChunkHolder.hpp"
#include "Game/Camera/CameraParamChunkID.hpp"
#include "Game/Camera/CameraPoseParam.hpp"
#include "Game/Camera/CameraRailHolder.hpp"
#include "Game/Camera/CameraRegisterHolder.hpp"
#include "Game/Camera/CameraRotChecker.hpp"
#include "Game/Camera/CameraShaker.hpp"
#include "Game/Camera/CameraTargetHolder.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/Camera/CameraViewInterpolator.hpp"
#include "Game/Camera/GameCameraCreator.hpp"
#include "Game/Camera/OnlyCamera.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_format.h>
#include "Game/Util/Tunable.hpp"

namespace {
    NEBULA_TUNABLE(s32, sSubjectivePosInterpolateFrame, 0);
    NEBULA_TUNABLE(s32, sSubjectiveRotInterpolateFrame, 10);
    NEBULA_TUNABLE(s32, sSubjectiveInterpolateFrame, 20);
    NEBULA_TUNABLE(f32, sDefaultFovy, 45.0f);
    static const char* sTalkCameraName = "共通会話カメラ";
    static const char* sStartAnimCameraName = "スタートアニメカメラ";
    static const char* sSubjectiveCameraName = "主観カメラ";
};  // namespace

CameraDirector::CameraDirector(const char* pName) : NameObj(pName) {
    mTargetObj = nullptr;
    mStack = new CameraManStack();
    mOnlyCamera = new OnlyCamera("OnlyCamera");
    mPoseParam1 = new CameraPoseParam();
    mPoseParam2 = new CameraPoseParam();
    mHolder = new CameraHolder("カメラホルダー");
    mChunkHolder = new CameraParamChunkHolder(mHolder, "パラメータ");
    mCameraCreator = new GameCameraCreator(mChunkHolder);
    mRailHolder = new CameraRailHolder("カメラレール管理");
    mRegisterHolder = new CameraRegisterHolder("カメラレジスタ");
    mTargetHolder = new CameraTargetHolder();
    mShaker = new CameraShaker("振動");
    mViewInterpolator = new CameraViewInterpolator();
    mCover = new CameraCover("CameraCover");
    mRotChecker = new CameraRotChecker();
    mCameraManGame = new CameraManGame(mHolder, mChunkHolder, "ゲームカメラマン");
    mCameraManEvent = new CameraManEvent(mHolder, mChunkHolder, "イベントカメラマン");
    mCameraManSubjective = new CameraManSubjective("主観カメラマン");
    mEventNum = 0;
    mIsStartCameraActive = true;
    mStartTime = 0;
    mStartCameraCreated = false;
    mCameraTargetMtx = new CameraTargetMtx("カメラターゲットダミー");
    mRequestCameraManReset = false;
    mRequestCameraLocalOffsetReset = false;
    mIsSubjectiveCamera = false;
    mIsStartSubjectiveCamera = false;
    mSubjectiveFrame = 0;
    mNearZ = -100.0f;
    mIsSubjectiveCalced = false;
    mIsCameraNG = false;

    MR::connectToSceneCamera(this);
    push(mCameraManGame);
    mTargetMtx.identity();
    mCameraTargetMtx->setMtx(mTargetMtx);
    setInterpolation(0);
    mCameraManSubjective->owned(this);
    mViewMtx.identity();
    MR::createCenterScreenBlur();
}

void CameraDirector::init(const TableIter& /*rIter*/) {
}

void CameraDirector::movement() {
    backLastMtx();
    mTargetHolder->movement();
    updateCameraMan();
    calcPose();
    createViewMtx();
    getCurrentCameraMan()->mMatrix.set(MR::getCameraInvViewMtx());
    mPoseParam2->copyFrom(*getCurrentCameraMan()->mPoseParam);
    calcSubjective();
    mShaker->movement();
    checkStartCondition();
    checkEndOfEventCamera();
    mRotChecker->update();

    mRequestCameraManReset = false;
    mRequestCameraLocalOffsetReset = false;
}

void CameraDirector::setTarget(CameraTargetObj* pTarget) {
    mTargetHolder->set(pTarget);
}

CameraTargetObj* CameraDirector::getTarget() {
    return mTargetHolder->get();
}

void CameraDirector::push(CameraMan* pMan) {
    if (mStack->mCount != 0) {
        getCurrentCameraMan()->deactivate();
    }

    if (mStack->mCount == mStack->mCapacity) {
        mStack->mElements = MR::growArray(mStack->mElements, static_cast< s32 >(mStack->mCapacity));
        mStack->mCapacity *= 2;
    }

    mStack->mElements[mStack->mCount++] = pMan;

    pMan->owned(this);
    pMan->activate();
}

CameraMan* CameraDirector::pop() {
    CameraMan* man = mStack->mElements[--mStack->mCount];

    man->deactivate();
    man->released();

    if (mStack->mCount != 0) {
        getCurrentCameraMan()->activate();
    }

    return man;
}

void CameraDirector::backLastMtx() {
    if (mIsSubjectiveCalced) {
        MR::setCameraViewMtx(mViewMtx);

        Vec3f invTrans;
        MR::getCameraInvViewMtx().getTrans(invTrans);
    }
}

CameraMan* CameraDirector::getCurrentCameraMan() const {
    return mStack->mElements[mStack->mCount - 1];
}

void CameraDirector::updateCameraMan() {
    if (mRequestCameraManReset) {
        resetCameraMan();
    }

    if (mRequestCameraLocalOffsetReset) {
        CameraMan* man = getCurrentCameraMan();
        man->mRequestLOfsReset = true;
    }

    getCurrentCameraMan()->movement();
    controlCameraSE();
}

void CameraDirector::calcPose() {
    switchAntiOscillation();

    if (getCurrentCameraMan()->isCollisionOff()) {
        mViewInterpolator->mIsCollisionOff = true;
    }

    if (getCurrentCameraMan()->isCorrectingErpPositionOff()) {
        mViewInterpolator->mIsCorrectErpPositionOn = false;
    }

    mOnlyCamera->calcPose(getCurrentCameraMan());
    mPoseParam1->copyFrom(*mOnlyCamera->mPoseParam);

    // The game's own pose, for game_camera; then a plugin's camera (set_camera): where it says at once, through walls.
    NebulaMods::recordGameCamera(mPoseParam1->mPos.x, mPoseParam1->mPos.y, mPoseParam1->mPos.z, mPoseParam1->mWatchPos.x,
                                 mPoseParam1->mWatchPos.y, mPoseParam1->mWatchPos.z);
    f32 position[3];
    f32 target[3];
    const u32 given = NebulaMods::cameraPose(position, target);
    if ((given & 1u) != 0u) {
        mPoseParam1->mPos.set(position[0], position[1], position[2]);
        mViewInterpolator->mIsCollisionOff = true;
        mViewInterpolator->mIsForceCameraChange = true;
    }
    if ((given & 2u) != 0u) {
        mPoseParam1->mWatchPos.set(target[0], target[1], target[2]);
    }
}

void CameraDirector::calcSubjective() {
    mViewMtx.set(MR::getCameraViewMtx());
    mIsSubjectiveCalced = true;

    if (MR::isDemoActive()) {
        MR::stopPlayerFpView();
    }

    if (mIsSubjectiveCamera) {
        mSubjectiveFrame++;
        if (mSubjectiveFrame > ::sSubjectiveInterpolateFrame) {
            mSubjectiveFrame = ::sSubjectiveInterpolateFrame;
        }
    } else {
        mSubjectiveFrame--;
        if (mSubjectiveFrame <= 0) {
            mSubjectiveFrame = 0;
            if (mIsStartSubjectiveCamera) {
                if (mNearZ >= 0.0f) {
                    MR::setNearZ(mNearZ);
                }

                mNearZ = -100.0f;
                MR::turnOnDOFInSubjective();
                mIsStartSubjectiveCamera = false;
            }
        }
    }

    if (!mIsStartSubjectiveCamera) {
        return;
    }

    Pos3f inv;
    inv.set(MR::getCameraInvViewMtx());
    mCameraManSubjective->calc();

    Pos3f viewMtx;
    calcViewMtxFromPoseParam(&viewMtx, mCameraManSubjective->mPoseParam);

    s32 posFrame = mSubjectiveFrame - ::sSubjectivePosInterpolateFrame;
    if (posFrame > ::sSubjectiveInterpolateFrame - ::sSubjectivePosInterpolateFrame) {
        posFrame = ::sSubjectiveInterpolateFrame - ::sSubjectivePosInterpolateFrame;
    }

    f32 posRate;
    if (mSubjectiveFrame >= ::sSubjectiveInterpolateFrame - ::sSubjectivePosInterpolateFrame) {
        posRate = 1.0f;
    } else {
        posRate = MR::cos(MR::pi() + posFrame * MR::pi() / (::sSubjectiveInterpolateFrame - ::sSubjectivePosInterpolateFrame)) * 0.5f + 0.5f;
    }

    const CameraPoseParam* param = mCameraManSubjective->mPoseParam;
    Vec3f localPos;
    inv.getTrans(localPos);

    Vec3f newPos = localPos * (1.0f - posRate) + param->mPos * posRate;

    s32 rotFrame = mSubjectiveFrame - ::sSubjectiveRotInterpolateFrame;
    if (rotFrame < 0) {
        rotFrame = 0;
    }

    f32 rotRate = MR::cos(MR::pi() + rotFrame * MR::pi() / (::sSubjectiveInterpolateFrame - ::sSubjectiveRotInterpolateFrame)) * 0.5f + 0.5f;

    Quat4f invRot;
    inv.getQuat(invRot);
    Quat4f viewRot;
    viewMtx.getQuat(viewRot);

    Quat4f newRot;
    newRot.slerp(invRot, viewRot, rotRate);

    viewMtx.makeQuat(newRot);

    Vec3f front;
    viewMtx.getZDir(front);
    viewMtx.setTrans(newPos);
    viewMtx.invert(viewMtx);

    MR::setCameraViewMtx(viewMtx);
}

bool CameraDirector::isInterpolationOff() {
    return getCurrentCameraMan()->isInterpolationOff() || mViewInterpolator->mIsForceCameraChange;
}

void CameraDirector::switchAntiOscillation() {
    if (isInterpolationOff()) {
        mViewInterpolator->mIsAntiOscillation = false;
    } else {
        mViewInterpolator->mIsAntiOscillation = true;
    }
}

void CameraDirector::createViewMtx() {
    Pos3f view;
    calcViewMtxFromPoseParam(&view, mPoseParam1);

    mViewInterpolator->updateCameraMtx(view, mPoseParam1->getWatchPos(), getTargetObj(), NebulaMods::cameraFov(CameraLocalUtil::getFovy(getCurrentCameraMan())));
}

void CameraDirector::checkStartCondition() {
    if (mIsStartCameraActive && getCurrentCameraMan() == mCameraManGame && mStartTime++ > 30 && mTargetHolder->isMoving()) {
        mIsStartCameraActive = false;
        mCameraManGame->endStartPosCamera();
    }
}

void CameraDirector::startEvent(s32 zoneID, const char* pName, const CameraTargetArg& rTargetArg, s32 frame) {
    NebulaMods::fire(NEBULA_MOD_EVENT_CAMERA_CHANGE, 1, nullptr, pName);
    mViewInterpolator->mIsInterpolationOff = false;
    removeEndEventAtLanding(zoneID, pName);

    if (getCurrentCameraMan() != mCameraManEvent) {
        if (mStack->mCount != 0) {
            if (getCurrentCameraMan() == mCameraManGame) {
                mCameraManEvent->mPoseParam->copyFrom(*mCameraManGame->mPoseParam);
                mCameraManEvent->mMatrix.set(MR::getCameraInvViewMtx());
            }
        }

        push(mCameraManEvent);
    }

    mCameraManEvent->start(zoneID, pName, rTargetArg, frame);
}

void CameraDirector::endEvent(s32 zoneID, const char* pName, bool resetView, s32 frame) {
    if (getCurrentCameraMan() == mCameraManEvent) {
        mCameraManEvent->end(zoneID, pName, frame);

        if (!mCameraManEvent->isActive()) {
            pop();

            if (!isForceCameraChange() && resetView && getCurrentCameraMan() == mCameraManGame) {
                mCameraManGame->mPoseParam->copyFrom(*mPoseParam1);
                mCameraManGame->mMatrix.set(MR::getCameraInvViewMtx());
            }

            mViewInterpolator->mIsInterpolationOff = true;

            // The area's camera has the view again: plugins hear it as one (value 0), by its name.
            if (getCurrentCameraMan() == mCameraManGame) {
                const CameraParamChunk* chunk = mCameraManGame->mChunk;
                NebulaMods::fire(NEBULA_MOD_EVENT_CAMERA_CHANGE, 0, nullptr,
                                 chunk != nullptr && chunk->mParamChunkID != nullptr ? chunk->mParamChunkID->mName : nullptr);
            }
        }
    }
}

void CameraDirector::endEventAtLanding(s32 zoneID, const char* pName, s32 frame) {
    if (getCurrentCameraMan() == mCameraManEvent) {
        CameraEvent& rEvent = mEvents.at(mEventNum);
        rEvent.mZoneID = zoneID;
        rEvent.mName = MR::internName(pName);
        rEvent.mFrame = frame;
        mEventNum++;
    }
}

CameraParamChunkEvent* CameraDirector::getEventParameter(s32 zoneID, const char* pName) {
    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createEventID(zoneID, pName);

    return static_cast< CameraParamChunkEvent* >(mChunkHolder->getChunk(chunkID));
}

void CameraDirector::requestToResetCameraMan() {
    mRequestCameraManReset = true;
}

void CameraDirector::setInterpolation(u32 time) {
    mViewInterpolator->setInterpolation(time);

    if (time == 0 && !mIsStartCameraActive) {
        mViewInterpolator->mIsCollisionOff = true;
        mCover->cover(2);
    }
}

void CameraDirector::cover(u32 time) {
    mCover->cover(time);
}

void CameraDirector::closeCreatingCameraChunk() {
    mCameraCreator->scanArea();
    mCameraCreator->scanStartPos();
    createStartAnimCamera();
    createTalkCamera();
    createSubjectiveCamera();
    mCameraManGame->closeCreatingCameraChunk();
    mChunkHolder->loadCameraParameters();
    mChunkHolder->sort();
    mIsStartCameraActive = true;
    mCameraManGame->startStartPosCamera(false);
}

void CameraDirector::initCameraCodeCollection(const char* pName, s32 zoneId) {
    mCameraCreator->initCameraCodeCollection(pName, zoneId);
}

void CameraDirector::registerCameraCode(u32 code) {
    mCameraCreator->registerCameraCode(code);
}

void CameraDirector::termCameraCodeCollection() {
    mCameraCreator->termCameraCodeCollection();
}

void CameraDirector::declareEvent(s32 zoneID, const char* pName) {
    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createEventID(zoneID, pName);

    mChunkHolder->createChunk(chunkID, nullptr);
}

void CameraDirector::started() {
    mIsStartCameraActive = false;
    mCameraManGame->endStartPosCamera();
}

void CameraDirector::setTargetActor(const LiveActor* pActor) {
    mTargetHolder->set(pActor);
}

void CameraDirector::setTargetPlayer(const MarioActor* pActor) {
    mTargetHolder->set(pActor);
}

bool CameraDirector::isRotatingHard() const {
    return mRotChecker->mIsRotatingHard;
}

bool CameraDirector::isSubjectiveCamera() const {
    return mIsSubjectiveCamera;
}

bool CameraDirector::isEnableToControl() const {
    bool enable = false;

    if (!getCurrentCameraMan()->isSubjectiveCameraOff()) {
        bool change = true;
        bool equals = getCurrentCameraMan() == mCameraManEvent;

        if (equals && !isEventCameraActive(0, ::sSubjectiveCameraName)) {
            change = false;
        }

        if (change) {
            enable = true;
        }
    }

    return enable;
}

bool CameraDirector::isEnableToRoundLeft() const {
    return getCurrentCameraMan()->isEnableToRoundLeft();
}

bool CameraDirector::isEnableToRoundRight() const {
    return getCurrentCameraMan()->isEnableToRoundRight();
}

bool CameraDirector::isEnableToReset() const {
    return getCurrentCameraMan()->isEnableToReset();
}

bool CameraDirector::isEventCameraActive(s32 zoneID, const char* pName) const {
    if (getCurrentCameraMan() == mCameraManEvent) {
        return mCameraManEvent->isEventActive(zoneID, pName);
    }

    return false;
}

bool CameraDirector::isEventCameraActive() const {
    return getCurrentCameraMan() == mCameraManEvent;
}

void CameraDirector::startStartPosCamera(bool interpolate) {
    mIsStartCameraActive = true;
    mCameraManGame->startStartPosCamera(interpolate);
}

bool CameraDirector::isInterpolatingNearlyEnd() const {
    if (getCurrentCameraMan() == mCameraManEvent && mCameraManEvent->doesNextChunkHaveInterpolation()) {
        return false;
    }

    return mViewInterpolator->isInterpolatingNearlyEnd();
}

bool CameraDirector::isForceCameraChange() const {
    return mViewInterpolator->mIsForceCameraChange;
}

f32 CameraDirector::getDefaultFovy() const {
    return ::sDefaultFovy;
}

void CameraDirector::startStartAnimCamera() {
    if (mStartCameraCreated) {
        ActorCameraInfo info = ActorCameraInfo();

        MR::startEventCamera(&info, ::sStartAnimCameraName, CameraTargetArg(mCameraTargetMtx), 0);
    }
}

bool CameraDirector::isStartAnimCameraEnd() const {
    if (mStartCameraCreated) {
        return isAnimCameraEnd(0, ::sStartAnimCameraName);
    }

    return true;
}

u32 CameraDirector::getStartAnimCameraFrame() const {
    if (mStartCameraCreated) {
        return getAnimCameraFrame(0, ::sStartAnimCameraName);
    }

    return 0;
}

void CameraDirector::endStartAnimCamera() {
    ActorCameraInfo info = ActorCameraInfo();
    MR::endEventCamera(&info, ::sStartAnimCameraName, true, 0);
}

void CameraDirector::startTalkCamera(const Vec3f& rPosition, const Vec3f& rUp, f32 axisX, f32 axisY, s32 frame) {
    CameraParamChunkEvent* chunk = getEventParameter(0, ::sTalkCameraName);

    if (chunk != nullptr) {
        chunk->mGeneralParam->mWPoint.set(rPosition);
        chunk->mGeneralParam->mUp.set(rUp);

        CameraGeneralParam* generalParam = chunk->mGeneralParam;
        generalParam->mAxis.x = axisX;
        generalParam->mAxis.y = axisY;
        generalParam->mAxis.z = 0.0f;

        CameraTargetArg targetArg;
        MR::setCameraTargetToPlayer(&targetArg);
        startEvent(0, ::sTalkCameraName, targetArg, frame);
    }
}

void CameraDirector::endTalkCamera(bool resetView, s32 frame) {
    endEvent(0, ::sTalkCameraName, resetView, frame);
}

void CameraDirector::startSubjectiveCamera(s32 /*camType*/) {
    mIsStartCameraActive = false;
    mCameraManGame->endStartPosCamera();
    mIsSubjectiveCamera = true;

    if (!mIsStartSubjectiveCamera) {
        mIsStartSubjectiveCamera = true;
        mSubjectiveFrame = 0;

        mCameraManSubjective->activate();
        mNearZ = MR::getNearZ();

        MR::setNearZ(10.0f);
        MR::turnOffDOFInSubjective();
    }

    if (mSubjectiveFrame < ::sSubjectiveInterpolateFrame) {
        MR::startCenterScreenBlur(::sSubjectiveInterpolateFrame, 15.0f, 80, 5, 10);
    }
}

// endType 0 and 1 end at once (no blur, frame count reset); other values blur out.
void CameraDirector::endSubjectiveCamera(s32 endType) {
    bool forceEnd = endType == 0 || endType == 1;

    if (mIsSubjectiveCamera) {
        mIsSubjectiveCamera = false;

        if (!forceEnd) {
            MR::startCenterScreenBlur(mSubjectiveFrame, 15.0f, 80, 5, 10);
        }
    }

    if (mIsStartSubjectiveCamera && forceEnd) {
        mSubjectiveFrame = 0;
    }
}

bool CameraDirector::isAnimCameraEnd(s32 zoneID, const char* pName) const {
    if (getCurrentCameraMan() == mCameraManEvent) {
        return mCameraManEvent->isAnimCameraEnd(zoneID, pName);
    }

    return true;
}

u32 CameraDirector::getAnimCameraFrame(s32 zoneID, const char* pName) const {
    if (getCurrentCameraMan() == mCameraManEvent) {
        return mCameraManEvent->getAnimCameraFrame(zoneID, pName);
    }

    return 0;
}

void CameraDirector::pauseOnAnimCamera(s32 zoneID, const char* pName) {
    if (getCurrentCameraMan() == mCameraManEvent) {
        mCameraManEvent->pauseOnAnimCamera(zoneID, pName);
    }
}

void CameraDirector::pauseOffAnimCamera(s32 zoneID, const char* pName) {
    if (getCurrentCameraMan() == mCameraManEvent) {
        mCameraManEvent->pauseOffAnimCamera(zoneID, pName);
    }
}

void CameraDirector::zoomInGameCamera() {
    mCameraManGame->zoomIn();
}

void CameraDirector::zoomOutGameCamera() {
    mCameraManGame->zoomOut();
}

void CameraDirector::checkEndOfEventCamera() {
    if (mEventNum != 0 && mTargetHolder->isOnGround()) {
        for (u32 i = 0; i < mEventNum; i++) {
            endEvent(mEvents[i].mZoneID, mEvents[i].mName, true, mEvents[i].mFrame);
        }

        mEventNum = 0;
    }
}

void CameraDirector::createTalkCamera() {
    declareEvent(0, ::sTalkCameraName);

    CameraParamChunkEvent* chunk = getEventParameter(0, ::sTalkCameraName);
    if (chunk != nullptr) {
        chunk->setCameraType("CAM_TYPE_TALK", mHolder);
        chunk->mIsCodeChunk = true;
    }
}

void CameraDirector::controlCameraSE() {
    mIsCameraNG = false;

    if (MR::isPlayerDead()) {
        return;
    }

    if (mIsSubjectiveCamera) {
        if (MR::isPressedLeft(PLAYER_1) || MR::isPressedRight(PLAYER_1) || MR::testFpViewStartTrigger()) {
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_NG");
                mIsCameraNG = true;
            }
        }

        return;
    }

    if (CameraLocalUtil::testCameraPadTriggerRoundLeft()) {
        if (isEnableToRoundLeft()) {
            getCurrentCameraMan()->roundLeft();
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_MOVE");
            }
        } else {
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_NG");
                mIsCameraNG = true;
            }
        }
    }

    if (CameraLocalUtil::testCameraPadTriggerRoundRight()) {
        if (isEnableToRoundRight()) {
            getCurrentCameraMan()->roundRight();
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_MOVE");
            }
        } else {
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_NG");
                mIsCameraNG = true;
            }
        }
    }

    if (CameraLocalUtil::testCameraPadTriggerReset()) {
        if (isEnableToReset()) {
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_RESET");
                MR::startSystemSE("SE_SY_CAMERA_MOVE");
            }
        } else {
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_NG");
                mIsCameraNG = true;
            }
        }
    }

    if (MR::isPlayerInBind()) {
        if (MR::testFpViewStartTrigger()) {
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_NG");
                mIsCameraNG = true;
            }
        }
    }

    if (MR::isPressedDown(PLAYER_1)) {
        if (isPlayableCameraSE(false)) {
            MR::startSystemSE("SE_SY_CAMERA_NG");
            mIsCameraNG = true;
        }
    }
}

void CameraDirector::removeEndEventAtLanding(s32 zoneID, const char* pName) {
    if (mEventNum == 0) {
        return;
    }

    for (s32 idx = 0; idx < mEventNum; idx++) {
        if (mEvents[idx].mZoneID == zoneID && MR::isEqualString(mEvents[idx].mName, pName)) {
            if (mEventNum - 1 == idx) {
                mEventNum = 0;
                return;
            }

            mEvents[idx].mZoneID = mEvents[mEventNum - 1].mZoneID;
            mEvents[idx].mName = mEvents[mEventNum - 1].mName;
            mEvents[idx].mFrame = mEvents[mEventNum - 1].mFrame;
            mEventNum--;
            return;
        }
    }
}

void CameraDirector::calcViewMtxFromPoseParam(Pos3f* pMtx, const CameraPoseParam* pParam) {
    Vec3f front = pParam->mWatchPos - pParam->mPos;
    MR::normalizeOrZero(&front);
    Vec3f side = pParam->mUpVec.cross(front);
    MR::normalizeOrZero(&side);
    Vec3f up = front.cross(side);
    MR::normalizeOrZero(&up);

    pMtx->setXDir(-side);
    pMtx->setYDir(up);
    pMtx->setZDir(-front);
    pMtx->setTrans(pParam->mPos);

    Pos3f rot;
    rot.makeRotate(Vec3f(0.0f, 0.0f, 1.0f), pParam->mRoll);
    pMtx->concat(*pMtx, rot);
}

bool CameraDirector::isPlayableCameraSE(bool checkSubjective) {
    if (MR::isDemoActive()) {
        return false;
    }

    if (MR::isPowerStarGetDemoActive()) {
        return false;
    }

    if (MR::isStageStateScenarioOpeningCamera()) {
        return false;
    }

    if (MR::isExecScenarioStarter()) {
        return false;
    }

    if (MR::isPlayerDead()) {
        return false;
    }

    if (MR::isEqualStageName("FileSelect")) {
        return false;
    }

    if (MR::isEqualStageName("EpilogueDemoStage")) {
        return false;
    }

    if (checkSubjective && mIsSubjectiveCamera) {
        return false;
    }

    return true;
}

void CameraDirector::resetCameraMan() {
    setInterpolation(0);

    CameraTargetObj* target1;
    CameraTargetObj* target2;
    CameraTargetObj* target3;
    CameraMan* man = getCurrentCameraMan();

    target1 = mTargetHolder->get();
    target2 = mTargetHolder->get();
    target3 = mTargetHolder->get();

    Vec3f newPos = target3->getPosition() - target2->getFrontVec() * 800.0f + target1->getUpVec() * 300.0f;

    CameraLocalUtil::setPos(man, newPos);
    CameraLocalUtil::setWatchPos(man, mTargetHolder->get()->getPosition());
    CameraLocalUtil::setUpVec(man, mTargetHolder->get()->getUpVec());
    CameraLocalUtil::setWatchUpVec(man, mTargetHolder->get()->getUpVec());

    man->deactivate();
    man->activate();

    mOnlyCamera->mIsResetting = true;
}

void CameraDirector::createStartAnimCamera() {
    void* data = nullptr;
    s32 size = 0;
    MR::getCurrentScenarioStartAnimCameraData(&data, &size);

    if (size > 0) {
        ActorCameraInfo info = ActorCameraInfo();
        MR::declareEventCameraAnim(&info, ::sStartAnimCameraName, data);
        mStartCameraCreated = true;
    }
}

void CameraDirector::createSubjectiveCamera() {
    declareEvent(0, ::sSubjectiveCameraName);

    CameraParamChunkEvent* chunk = getEventParameter(0, ::sSubjectiveCameraName);
    if (chunk != nullptr) {
        chunk->setCameraType("CAM_TYPE_SUBJECTIVE", mHolder);
        chunk->mIsCodeChunk = true;
    }
}

CameraDirector* MR::getCameraDirector() {
    return MR::getSceneObj< CameraDirector >(SceneObj_CameraDirector);
}
