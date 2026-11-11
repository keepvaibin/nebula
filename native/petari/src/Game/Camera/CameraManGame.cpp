#include "Game/Camera/CameraManGame.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/AreaObj/CubeCamera.hpp"
#include "Game/Boss/SkeletalFishGuard.hpp"
#include "Game/Camera/CamHeliEffector.hpp"
#include "Game/Camera/CamKarikariEffector.hpp"
#include "Game/Camera/Camera.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Camera/CameraHeightArrange.hpp"
#include "Game/Camera/CameraHolder.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraParamChunkHolder.hpp"
#include "Game/Camera/CameraParamChunkID.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/MapObj/GCapture.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_zoomIn_f1, "zoomIn.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_zoomIn_f2, "zoomIn.f2", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_zoomIn_f3, "zoomIn.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterCamera_f1, "createDefaultWaterCamera.f1", 170.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterCamera_f2, "createDefaultWaterCamera.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterCamera_f3, "createDefaultWaterCamera.f3", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterCamera_f4, "createDefaultWaterCamera.f4", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterCamera_f5, "createDefaultWaterCamera.f5", 900.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterCamera_f6, "createDefaultWaterCamera.f6", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterSurfaceCamera_f1, "createDefaultWaterSurfaceCamera.f1", 170.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterSurfaceCamera_f2, "createDefaultWaterSurfaceCamera.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterSurfaceCamera_f3, "createDefaultWaterSurfaceCamera.f3", 45.0f);
NEBULA_TUNABLE_NAMED(f64, cNum_createDefaultWaterSurfaceCamera_f4, "createDefaultWaterSurfaceCamera.f4", 900.0);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterSurfaceCamera_f5, "createDefaultWaterSurfaceCamera.f5", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterSurfaceCamera_f6, "createDefaultWaterSurfaceCamera.f6", 0.174533f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterSurfaceCamera_f7, "createDefaultWaterSurfaceCamera.f7", 0.349066f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultWaterSurfaceCamera_f8, "createDefaultWaterSurfaceCamera.f8", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultFooFighterCamera_f1, "createDefaultFooFighterCamera.f1", 170.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultFooFighterCamera_f2, "createDefaultFooFighterCamera.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultFooFighterCamera_f3, "createDefaultFooFighterCamera.f3", 45.0f);
NEBULA_TUNABLE_NAMED(f64, cNum_createDefaultFooFighterCamera_f4, "createDefaultFooFighterCamera.f4", 1200.0);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultFooFighterCamera_f5, "createDefaultFooFighterCamera.f5", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDefaultFooFighterCamera_f6, "createDefaultFooFighterCamera.f6", 0.03f);
NEBULA_TUNABLE_NAMED(f32, cNum_createZoomCamera_f1, "createZoomCamera.f1", 100.0f);

namespace {
    NEBULA_TUNABLE(f32, sMinDistance, 300.0f);
    const char* sThruCamName = "デフォルトカメラ";
    const char* sDefaultWaterCamName = "デフォルト水中カメラ";
    const char* sDefaultWaterSurfaceCamName = "デフォルト水面カメラ";
    const char* sDefaultFooFighterCamName = "デフォルトフーファイターカメラ";
    const char* sStartAnimCamName = "スタートアニメカメラ";
    NEBULA_TUNABLE(f32, sZoomRadius, 100.0f);
    NEBULA_TUNABLE(s32, sInterpolateOffCounterMax, 5);
};  // namespace

CameraManGame::CameraManGame(CameraHolder* pHolder, CameraParamChunkHolder* pChunkHolder, const char* pName)
    : CameraMan(pName), mHolder(pHolder), mChunkHolder(pChunkHolder), mRequestReset(), mKarikari(new CamKarikariEffector()),
      mHeli(new CamHeliEffector()), mTypeState(), mIsStartPosActive(), mInterpolateOffCounter(), mZoomedIn() {
    lumacore::setClass(this, lumacore::kClassId< CameraManGame >);
    mCamera = mHolder->getDefaultCamera();
    mChunk = nullptr;
    CameraLocalUtil::setWatchPos(this, Vec3f(0.0f, 0.0f, ::sMinDistance));
}

void CameraManGame::init(const TableIter& /*rIter*/) {
}

void CameraManGame::calc() {
    selectCameraChunk();
    checkReset();
    CameraLocalUtil::setUsedTarget(this, mCamera->calc());
    setSafePose();
    mKarikari->update(this);
    mHeli->update(this);
}

void CameraManGame::notifyActivate() {
    mRequestReset = true;
}

void CameraManGame::closeCreatingCameraChunk() {
    createDefaultCamera();
    createDefaultWaterCamera();
    createDefaultWaterSurfaceCamera();
    createDefaultFooFighterCamera();
    createStartAnimCamera();
    createZoomCamera();
}

void CameraManGame::startStartPosCamera(bool forceChange) {
    mIsStartPosActive = true;
    mInterpolateOffCounter = forceChange ? 0 : ::sInterpolateOffCounterMax;
}

void CameraManGame::endStartPosCamera() {
    mIsStartPosActive = false;
    mInterpolateOffCounter = 0;
}

void CameraManGame::zoomIn() {
    mZoomedIn = true;

    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createOtherID(0, "ズームカメラ");

    CameraParamChunk* chunk = mChunkHolder->getChunk(chunkID);

    const Vec3f& pos = CameraLocalUtil::getPos(this);
    const Vec3f& watchPos = CameraLocalUtil::getWatchPos(this);

    f32 angle = MR::asin(::sZoomRadius / watchPos.distance(pos));
    angle *= cNum_zoomIn_f1;

    chunk->mExParam.mFovy = (cNum_zoomIn_f2 * (angle * cNum_zoomIn_f3)) / MR::pi();
}

void CameraManGame::zoomOut() {
    mZoomedIn = false;
}

bool CameraManGame::isInterpolationOff() const {
    if (mCamera != nullptr && mCamera->isInterpolationOff()) {
        return true;
    }

    if (mChunk != nullptr && mChunk->isAntiBlurOff()) {
        return true;
    }

    return false;
}

bool CameraManGame::isCollisionOff() const {
    if (mCamera != nullptr && mCamera->isCollisionOff()) {
        return true;
    }

    if (mChunk != nullptr && mChunk->isCollisionOff()) {
        return true;
    }

    return false;
}

bool CameraManGame::isZeroFrameMoveOff() const {
    if (mCamera != nullptr) {
        return mCamera->isZeroFrameMoveOff();
    }

    return false;
}

bool CameraManGame::isSubjectiveCameraOff() const {
    if (mCamera != nullptr && mCamera->isSubjectiveCameraOff()) {
        return true;
    }

    if (mChunk != nullptr && mChunk->isSubjectiveCameraOff()) {
        return true;
    }

    return false;
}

bool CameraManGame::isCorrectingErpPositionOff() const {
    return mCamera != nullptr && mCamera->isCorrectingErpPositionOff();
}

bool CameraManGame::isEnableToRoundLeft() const {
    if (mCamera != nullptr) {
        return mCamera->isEnableToRoundLeft();
    }

    return false;
}

bool CameraManGame::isEnableToRoundRight() const {
    if (mCamera != nullptr) {
        return mCamera->isEnableToRoundRight();
    }

    return false;
}

bool CameraManGame::isEnableToReset() const {
    if (mCamera != nullptr) {
        return mCamera->isEnableToReset();
    }

    return false;
}

void CameraManGame::selectCameraChunk() {
    if (!tryStartPosCamera() && !tryZoomCamera()) {
        checkStateShift();

        switch (mTypeState) {
        case CubeCameraArea::ECategory_Normal:
            updateNormal();
            break;
        case CubeCameraArea::ECategory_Swim:
            updateSwim();
            break;
        case CubeCameraArea::ECategory_WaterSurface:
            updateWaterSurface();
            break;
        case CubeCameraArea::ECategory_GCapture:
            updateGCapture();
            break;
        case CubeCameraArea::ECategory_FooFighter:
            updateFooFighter();
            break;
        case CubeCameraArea::ECategory_None:
            break;
        }
    }
}

void CameraManGame::setChunk(const CameraParamChunkID& rChunk) {
    CameraParamChunk* chunk = mChunkHolder->getChunk(rChunk);

    if (chunk == nullptr) {
        setNullCamera();
    } else {
        chunk = tryToReplaceChunkToDefault(chunk);
        requestResetIfNecessary(chunk);
        const bool isChange = chunk != mChunk;
        replaceCurrentChunkAndCamera(chunk);
        applyParameter();
        if (isChange && NebulaMods::sWanted[NEBULA_MOD_EVENT_CAMERA_CHANGE]) {
            NebulaMods::dispatch(NEBULA_MOD_EVENT_CAMERA_CHANGE, 0, nullptr,
                                 chunk->mParamChunkID != nullptr ? chunk->mParamChunkID->mName : nullptr);
        }
    }
}

void CameraManGame::setNullCamera() {
    mChunk = nullptr;
    mCamera = mHolder->getCameraInner(mHolder->getIndexOfDefault());

    CameraLocalUtil::setFovy(this, mDirector->getDefaultFovy());
    CameraLocalUtil::setRoll(this, 0.0f);

    CameraLocalUtil::setGlobalOffset(this, Vec3f(0.0f, 0.0f, 0.0f));
    CameraLocalUtil::setLocalOffset(this, Vec3f(0.0f, 0.0f, 0.0f));
    CameraLocalUtil::setFrontOffset(this, 0.0f);
    CameraLocalUtil::setUpperOffset(this, 0.0f);

    if (mCamera->doesVPanExist()) {
        mCamera->mVPan->resetParameter();
    }
}

CameraParamChunk* CameraManGame::tryToReplaceChunkToDefault(CameraParamChunk* pChunk) {
    if (!MR::isEqualString(pChunk->getClassName(), "Game")) {
        return pChunk;
    }

    CameraParamChunkGame* gameChunk = static_cast< CameraParamChunkGame* >(pChunk);

    if (gameChunk->mThru != 1) {
        return pChunk;
    }

    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createOtherID(0, ::sThruCamName);

    return mChunkHolder->getChunk(chunkID);
}

void CameraManGame::requestResetIfNecessary(CameraParamChunk* pChunk) {
    if (isNecessaryToReset(pChunk)) {
        mRequestReset = true;

        if (isZoomCamera()) {
            mDirector->setInterpolation(mChunk->mExParam.mCamInt);
        } else if (isCurrentChunkEnableEndInterp()) {
            CameraParamChunkGame* gameChunk = static_cast< CameraParamChunkGame* >(mChunk);
            mDirector->setInterpolation(gameChunk->mCamEndInt);
        } else {
            mDirector->setInterpolation(pChunk->mExParam.mCamInt);
        }
    }
}

bool CameraManGame::isNecessaryToReset(CameraParamChunk* pChunk) const {
    if (mChunk == pChunk) {
        return false;
    }

    u8 cameraTypeIndex = pChunk->mCameraTypeIndex;

    if (cameraTypeIndex != mHolder->getIndexOf(mCamera)) {
        return true;
    }

    if (mChunk != nullptr && !mChunk->isOnNoReset()) {
        return true;
    }

    if (!pChunk->isOnNoReset()) {
        return true;
    }

    return false;
}

bool CameraManGame::isCurrentChunkEnableEndInterp() const {
    bool is;

    if (mChunk != nullptr) {
        is = false;

        if (MR::isEqualString(mChunk->getClassName(), "Game")) {
            CameraParamChunkGame* gameChunk = static_cast< CameraParamChunkGame* >(mChunk);

            if (gameChunk->mEnableEndErpFrame != 0) {
                is = true;
            }
        }
    } else {
        is = false;
    }

    return is;
}

void CameraManGame::replaceCurrentChunkAndCamera(CameraParamChunk* pChunk) {
    mCamera = mHolder->getCameraInner(pChunk->mCameraTypeIndex);
    mChunk = pChunk;
}

void CameraManGame::applyParameter() {
    Camera* camera = mHolder->getCameraInner(mChunk->mCameraTypeIndex);
    camera->setParamFromChunk(mChunk);
    camera->setZoneMtx(mChunk->getZoneID());

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
        camera->mIsLOfsErpOff = true;
    } else {
        camera->mIsLOfsErpOff = false;
    }

    CameraLocalUtil::setRoll(mCamera, mChunk->mExParam.mRoll);

    if (camera->doesVPanExist()) {
        CameraHeightArrange* vPan = camera->mVPan;
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

void CameraManGame::checkReset() {
    if (!mRequestReset || mChunk == nullptr || mCamera == nullptr) {
        return;
    }

    mCamera->mCameraMan = this;
    mCamera->reset();
    mRequestReset = false;
}

void CameraManGame::setSafePose() {
    CameraLocalUtil::calcSafePose(this, mCamera);
}

void CameraManGame::createDefaultCamera() {
    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createOtherID(0, ::sThruCamName);
    mChunkHolder->createChunk(chunkID, nullptr);
}

void CameraManGame::createDefaultWaterCamera() {
    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createOtherID(0, ::sDefaultWaterCamName);
    CameraParamChunk* chunk = mChunkHolder->createChunk(chunkID, nullptr);

    chunk->setCameraType("CAM_TYPE_WATER_FOLLOW", mHolder);

    chunk->mExParam.setWOffset(Vec3f(0.0f, cNum_createDefaultWaterCamera_f1, 0.0f));
    chunk->mExParam.mLOffset = cNum_createDefaultWaterCamera_f2;
    chunk->mExParam.mFovy = cNum_createDefaultWaterCamera_f3;
    chunk->mExParam.mCamInt = 120;
    chunk->mGeneralParam->mAxis.x = cNum_createDefaultWaterCamera_f4;
    chunk->mGeneralParam->mAxis.y = cNum_createDefaultWaterCamera_f5;
    chunk->mGeneralParam->mDist = cNum_createDefaultWaterCamera_f6;
}

void CameraManGame::createDefaultWaterSurfaceCamera() {
    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createOtherID(0, ::sDefaultWaterSurfaceCamName);
    CameraParamChunk* chunk = mChunkHolder->createChunk(chunkID, nullptr);

    chunk->setCameraType("CAM_TYPE_FOLLOW", mHolder);

    chunk->mExParam.setWOffset(Vec3f(0.0f, cNum_createDefaultWaterSurfaceCamera_f1, 0.0f));
    chunk->mExParam.mLOffset = cNum_createDefaultWaterSurfaceCamera_f2;
    chunk->mExParam.mFovy = cNum_createDefaultWaterSurfaceCamera_f3;
    chunk->mExParam.mCamInt = 120;
    chunk->mGeneralParam->mAxis.x = cNum_createDefaultWaterSurfaceCamera_f4;
    chunk->mGeneralParam->mAxis.y = cNum_createDefaultWaterSurfaceCamera_f5;
    chunk->mGeneralParam->mAngleA = cNum_createDefaultWaterSurfaceCamera_f6;
    chunk->mGeneralParam->mAngleB = cNum_createDefaultWaterSurfaceCamera_f7;
    chunk->mGeneralParam->mDist = cNum_createDefaultWaterSurfaceCamera_f8;
    chunk->mGeneralParam->mNum1 = 0;
}

void CameraManGame::createDefaultFooFighterCamera() {
    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createOtherID(0, ::sDefaultFooFighterCamName);
    CameraParamChunk* chunk = mChunkHolder->createChunk(chunkID, nullptr);

    chunk->setCameraType("CAM_TYPE_FOO_FIGHTER", mHolder);

    chunk->mExParam.setWOffset(Vec3f(0.0f, cNum_createDefaultFooFighterCamera_f1, 0.0f));
    chunk->mExParam.mLOffset = cNum_createDefaultFooFighterCamera_f2;
    chunk->mExParam.mFovy = cNum_createDefaultFooFighterCamera_f3;
    chunk->mExParam.mCamInt = 120;
    chunk->mGeneralParam->mAxis.x = cNum_createDefaultFooFighterCamera_f4;
    chunk->mGeneralParam->mAxis.y = cNum_createDefaultFooFighterCamera_f5;
    chunk->mGeneralParam->mDist = cNum_createDefaultFooFighterCamera_f6;
}

void CameraManGame::createStartAnimCamera() {
    void* data;
    s32 size;
    MR::getCurrentScenarioStartAnimCameraData(&data, &size);

    if (size > 0) {
        CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
        chunkID.createOtherID(0, ::sStartAnimCamName);
        CameraParamChunk* chunk = mChunkHolder->createChunk(chunkID, nullptr);

        CameraDirector* director = CameraLocalUtil::getCameraDirector();
        chunk->setCameraType("CAM_TYPE_ANIM", director->mHolder);

        chunk->mGeneralParam->mAnimData = reinterpret_cast< u8* >(data);
        chunk->mIsCodeChunk = true;
    }
}

void CameraManGame::createZoomCamera() {
    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createOtherID(0, "ズームカメラ");
    CameraParamChunk* chunk = mChunkHolder->createChunk(chunkID, nullptr);

    CameraDirector* director = CameraLocalUtil::getCameraDirector();
    chunk->setCameraType("CAM_TYPE_EYEPOS_FIX_THERE", director->mHolder);

    chunk->mExParam.setWOffset(Vec3f(0.0f, 0.0f, 0.0f));
    chunk->mExParam.mLOffset = 0.0f;
    chunk->mExParam.mLOffsetV = cNum_createZoomCamera_f1;

    chunk->setUseFovy(true);
    chunk->setLOfsErpOff(true);

    chunk->mExParam.mCamInt = 0;
    chunk->mGeneralParam->mNum1 = 1;
    chunk->mGeneralParam->mNum2 = 1;
    chunk->mIsCodeChunk = 1;
}

void CameraManGame::checkStateShift() {
    if (!tryShiftToGCapture() && !tryShiftToSwimOrWaterSurface() && !tryShiftToFooFighter()) {
        mTypeState = CubeCameraArea::ECategory_Normal;
    }
}

bool CameraManGame::tryShiftToGCapture() {
    if (MR::isPlayerGCaptured()) {
        mTypeState = CubeCameraArea::ECategory_GCapture;
        return true;
    }

    return false;
}

bool CameraManGame::tryShiftToSwimOrWaterSurface() {
    if (CameraLocalUtil::getTarget(this)->isWaterMode()) {
        if (CameraLocalUtil::getTarget(this)->isOnWaterSurface()) {
            mTypeState = CubeCameraArea::ECategory_WaterSurface;
        } else {
            mTypeState = CubeCameraArea::ECategory_Swim;
        }

        return true;
    }

    return false;
}

bool CameraManGame::tryShiftToFooFighter() {
    if (CameraLocalUtil::getTarget(this)->isFooFighterMode()) {
        mTypeState = CubeCameraArea::ECategory_FooFighter;
        return true;
    }

    return false;
}

void CameraManGame::updateNormal() {
    if (setCubeChunk(CubeCameraArea::ECategory_Normal)) {
        return;
    }

    Triangle* tri = CameraLocalUtil::getTarget(this)->getGroundTriangle();
    if (tri != nullptr && tri->isValid()) {
        u32 id = MR::getCameraID(tri);
        if (id < 0xFF) {
            CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
            chunkID.createGroupID(tri->getHostPlacementZoneID(), tri->getHostName(), id, 0);

            setChunk(chunkID);
            return;
        }
    }

    if (mChunk != nullptr) {
        setChunk(*mChunk->mParamChunkID);
    } else {
        setChunk(CameraParamChunkID());
    }
}

void CameraManGame::updateSwim() {
    if (!setCubeChunk(CubeCameraArea::ECategory_Swim)) {
        CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
        chunkID.createOtherID(0, ::sDefaultWaterCamName);

        setChunk(chunkID);
    }
}

void CameraManGame::updateWaterSurface() {
    if (!setCubeChunk(CubeCameraArea::ECategory_WaterSurface)) {
        CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
        chunkID.createOtherID(0, ::sDefaultWaterSurfaceCamName);

        setChunk(chunkID);
    }
}

void CameraManGame::updateGCapture() {
    CubeCameraArea::setCurrentCategory(CubeCameraArea::ECategory_GCapture);

    CubeCameraArea* area = static_cast< CubeCameraArea* >(MR::getAreaObj("CubeCamera", CameraLocalUtil::getTarget(this)->getPosition()));

    if (area != nullptr) {
        CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
        chunkID.createCubeID(area->mZoneID, static_cast< u16 >(area->mObjArg0));

        setChunk(chunkID);
        CubeCameraArea::setCurrentCategory(CubeCameraArea::ECategory_Normal);
    } else {
        updateNormal();
    }
}

void CameraManGame::updateFooFighter() {
    if (!setCubeChunk(CubeCameraArea::ECategory_FooFighter)) {
        CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
        chunkID.createOtherID(0, ::sDefaultFooFighterCamName);

        setChunk(chunkID);
    }
}

bool CameraManGame::setCubeChunk(CubeCameraArea::ECategory category) {
    CubeCameraArea::setCurrentCategory(static_cast< s32 >(category));
    CameraTargetObj* target = CameraLocalUtil::getTarget(this);
    CubeCameraArea* area = target->getCubeCameraArea();

    if (area != nullptr) {
        CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
        chunkID.createCubeID(area->mZoneID, static_cast< u16 >(area->mObjArg0));

        setChunk(chunkID);
        return true;
    }

    return false;
}

bool CameraManGame::tryStartPosCamera() {
    if (!mIsStartPosActive) {
        return false;
    }

    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    u16 startCameraID = static_cast< u16 >(MR::getCurrentStartCameraId());
    s32 startZoneID = MR::getCurrentStartZoneId();

    chunkID.createStartID(startZoneID, startCameraID);
    setChunk(chunkID);

    if (mInterpolateOffCounter > ::sInterpolateOffCounterMax) {
        mInterpolateOffCounter = ::sInterpolateOffCounterMax;
    }

    if (mInterpolateOffCounter < 0) {
        mInterpolateOffCounter = 0;
    }

    if (mInterpolateOffCounter > 0) {
        mDirector->setInterpolation(0);
    }

    if (mInterpolateOffCounter > 0) {
        mInterpolateOffCounter--;
    }

    return true;
}

bool CameraManGame::tryZoomCamera() {
    if (!mZoomedIn) {
        return false;
    }

    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createOtherID(0, "ズームカメラ");

    setChunk(chunkID);

    return true;
}

bool CameraManGame::isZoomCamera() const {
    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createOtherID(0, "ズームカメラ");

    CameraParamChunk* chunk = mChunkHolder->getChunk(chunkID);

    return mChunk == chunk;
}
