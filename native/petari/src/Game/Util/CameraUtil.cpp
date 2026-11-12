#include "Game/Util/CameraUtil.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Camera/CameraAnim.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraContext.hpp"
#include "Game/Camera/CameraPointer.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Camera/CameraHolder.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraPoseParam.hpp"
#include "Game/Camera/CameraRegisterHolder.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/MirrorCamera.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    static const char* sLauncherCameraName = "大砲";
    static const char* sLauncherFlightCameraName = "大砲飛行";

    CameraContext* getCameraContext() {
        return MR::getSceneObj< CameraContext >(SceneObj_CameraContext);
    }

    void calcNormalizedScreenPosToScreenPos(Vec3f* pScreenPos, const Vec3f& rNormalizedPos) {
        f32 width = MR::getScreenWidth();
        f32 height = MR::getScreenHeight();

        pScreenPos->set< f32 >(width * 0.5f + rNormalizedPos.x * width * 0.5f, height * 0.5f + rNormalizedPos.y * height * 0.5f, rNormalizedPos.z);
    }

};  // namespace

namespace MR {
    bool calcScreenPosition(Vec2f* pScreenPos, const Vec3f& rPos) {
        Vec3f normalizedScreenPos;
        Vec3f screenPos;
        bool isOnScreen = calcNormalizedScreenPosition(&normalizedScreenPos, rPos);
        ::calcNormalizedScreenPosToScreenPos(&screenPos, normalizedScreenPos);
        pScreenPos->x = screenPos.x;
        pScreenPos->y = screenPos.y;
        return isOnScreen;
    }

    bool calcScreenPosition(Vec3f* pScreenPos, const Vec3f& rPos) {
        Vec3f normalizedScreenPos;
        bool isOnScreen = calcNormalizedScreenPosition(&normalizedScreenPos, rPos);
        ::calcNormalizedScreenPosToScreenPos(pScreenPos, normalizedScreenPos);
        return isOnScreen;
    }

    bool calcNormalizedScreenPosition(Vec3f* pScreenPos, const Vec3f& rPos) {
        Vec3f pos;
        ::getCameraContext()->getViewMtx().mult(rPos, pos);

        return calcNormalizedScreenPositionFromView(pScreenPos, pos);
    }

    bool calcNormalizedScreenPositionFromView(Vec3f* pScreenPos, const Vec3f& rPos) {
        Proj3f proj;
        proj.set(::getCameraContext()->mProjection);
        Vec4f pos(rPos.x * proj.toMtx44()[0][0] + rPos.z * proj.toMtx44()[0][2], rPos.y * proj.toMtx44()[1][1] + rPos.z * proj.toMtx44()[1][2],
                   rPos.z * proj.toMtx44()[2][2] + proj.toMtx44()[2][3], -rPos.z);
        pScreenPos->scale(1.0f / pos.w, *pos.toVec3());
        pScreenPos->y = -pScreenPos->y;

        // A point on the camera's plane divides by zero: not finite is not on screen.
        if (!(MR::abs(pScreenPos->x) <= 1.0f) || !(MR::abs(pScreenPos->y) <= 1.0f)) {
            return false;
        }

        // In front of the near plane.
        return !(0.0f < pScreenPos->z);
    }

    bool calcWorldPositionFromScreen(Vec3f* pPos, const Vec2f& rScreenPos, f32 distZ) {
        f32 width = MR::getScreenWidth();
        f32 height = MR::getScreenHeight();

        Vec2f center(rScreenPos.x - width * 0.5f, rScreenPos.y - height * 0.5f);

        return calcWorldPositionFromCenterScreen(pPos, center, distZ);
    }

    bool calcWorldPositionFromCenterScreen(Vec3f* pPos, const Vec2f& rScreenPos, f32 distZ) {
        f32 height = MR::getScreenHeight();
        f32 focalLength = height * 0.5f / MR::tan(MR::toRadian(::getCameraContext()->mFovy) * 0.5f);

        f32 dist = (distZ >= 0.0f ? distZ : focalLength);
        f32 ratio = dist / focalLength;

        Vec3f viewPos(rScreenPos.x * ratio, -rScreenPos.y * ratio, -dist);

        Pos3f inv = getCameraInvViewMtx();
        Vec3f pos;
        inv.mult(viewPos, pos);

        if (pPos != nullptr) {
            *pPos = pos;
        }

        return true;
    }

    bool calcWorldRayDirectionFromScreen(Vec3f* pDir, const Vec2f& rScreenPos) {
        bool isComputed = calcWorldPositionFromScreen(pDir, rScreenPos, -1.0f);
        pDir->sub(getCamPos());
        return isComputed;
    }

    f32 calcCameraDistanceZ(const Vec3f& rPos) {
        const Pos3f& viewMtx = getCameraViewMtx();
        return MR::abs(rPos.x * viewMtx.get(2, 0) + rPos.y * viewMtx.get(2, 1) + rPos.z * viewMtx.get(2, 2) + 1.0f * viewMtx.get(2, 3));
    }

    void sceneOpaqueEnd() {
        f32 view[3][4];
        Frame::hostView(::getCameraContext()->mProjection, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, &view[0][0]);
        Frame::opaqueEnd(view);
    }

    void loadProjectionMtx() {
        Frame::setPerspective(::getCameraContext()->mProjection);
    }

    void loadViewMtx() {
        NebulaMtxCopy(::getCameraContext()->getViewMtx(), ModelSys::mViewMtx);
        ModelSys::mViewRole = NEBULA_CAMERA_SCENE;
    }

    const Pos3f& getCameraViewMtx() {
        return ::getCameraContext()->getViewMtx();
    }

    const Pos3f& getCameraInvViewMtx() {
        return ::getCameraContext()->getInvViewMtx();
    }

    const Proj3f& getCameraProjectionMtx() {
        return ::getCameraContext()->mProjection;
    }

    void setCameraViewMtx(const Pos3f& rMtx) {
        ::getCameraContext()->setViewMtx(rMtx);
    }

    f32 getAspect() {
        return ::getCameraContext()->getAspect();
    }

    f32 getNearZ() {
        return ::getCameraContext()->mNearZ;
    }

    f32 getRenderNearZ() {
        return NebulaMods::cameraNear(::getCameraContext()->mNearZ);
    }

    f32 getFarZ() {
        return ::getCameraContext()->mFarZ;
    }

    f32 getFovy() {
        return ::getCameraContext()->mFovy;
    }

    f32 getRenderFovy() {
        return ::getCameraContext()->getRenderFovy();
    }

    void setNearZ(f32 nearZ) {
        ::getCameraContext()->setNearZ(nearZ);
    }

    void setFovy(f32 fovy) {
        ::getCameraContext()->setFovy(fovy);
    }

    void setShakeOffset(f32 offsetX, f32 offsetY) {
        ::getCameraContext()->setShakeOffset(offsetX, offsetY);
    }

    const Vec3f getCamPos() {
        Pos3f viewMtx = ::getCameraContext()->getInvViewMtx();
        Vec3f pos;
        MR::extractMtxTrans(viewMtx, &pos);
        return pos;
    }

    Vec3f getCamXdir() {
        Pos3f viewMtx = ::getCameraContext()->getInvViewMtx();
        Vec3f dir;
        viewMtx.getXDir(dir);
        MR::normalizeOrZero(&dir);
        return dir;
    }

    Vec3f getCamYdir() {
        Pos3f viewMtx = ::getCameraContext()->getInvViewMtx();
        Vec3f dir;
        viewMtx.getYDir(dir);
        MR::normalizeOrZero(&dir);
        return dir;
    }

    Vec3f getCamZdir() {
        Pos3f viewMtx = ::getCameraContext()->getInvViewMtx();
        Vec3f dir;
        viewMtx.getZDir(dir);
        MR::normalizeOrZero(&dir);
        return -dir;
    }

    void createMirrorCamera() {
        MR::createSceneObj(SceneObj_MirrorCamera);
    }

    bool isExistMirrorCamera() {
        return MR::isExistSceneObj(SceneObj_MirrorCamera);
    }

    const Pos3f& getMirrorCameraViewMtx() {
        return getMirrorCamera()->mViewMtx;
    }

    const Pos3f& getMirrorModelTexMtx() {
        return getMirrorCamera()->mModelTexMtx;
    }

    CameraHolder* getCameraHolder() {
        return getCameraDirector()->getHolder();
    }

    void completeCameraParameters() {
        MR::getCameraDirector()->closeCreatingCameraChunk();
    }

    void resetCameraMan() {
        MR::getCameraDirector()->requestToResetCameraMan();
    }

    void startCameraInterpolation(u32 time) {
        MR::getCameraDirector()->setInterpolation(time);
    }

    void declareEventCamera(const ActorCameraInfo* pInfo, const char* pEventName) {
        MR::getCameraDirector()->declareEvent(pInfo->mZoneID, pEventName);
    }

    void endEventCamera(const ActorCameraInfo* pInfo, const char* pEventName, bool resetView, s32 frame) {
        MR::getCameraDirector()->endEvent(pInfo->mZoneID, pEventName, resetView, frame);
    }

    void endEventCameraAtLanding(const ActorCameraInfo* pInfo, const char* pName, s32 frame) {
        getCameraDirector()->endEventAtLanding(pInfo->mZoneID, pName, frame);
    }

    void declareGlobalEventCameraAbyss(const char* pEventName) {
        declareGlobalEventCamera(pEventName);
        CameraParamChunkEvent* chunk = MR::getCameraDirector()->getEventParameter(0, pEventName);

        if (chunk != nullptr) {
            chunk->setCameraType("CAM_TYPE_EYEPOS_FIX_THERE", MR::getCameraDirector()->mHolder);
            chunk->mGeneralParam->mNum1 = 1;
            chunk->mIsCodeChunk = true;
        }
    }

    bool isEventCameraActive() {
        return getCameraDirector()->isEventCameraActive();
    }

    bool isEventCameraActive(const ActorCameraInfo* pInfo, const char* pEventName) {
        return getCameraDirector()->isEventCameraActive(pInfo->mZoneID, pEventName);
    }

    bool isGlobalEventCameraActive(const char* pEventName) {
        return getCameraDirector()->isEventCameraActive(0, pEventName);
    }

    void declareGlobalEventCamera(const char* pEventName) {
        getCameraDirector()->declareEvent(0, pEventName);
    }

    void endGlobalEventCamera(const char* pEventName, s32 frame, bool resetView) {
        getCameraDirector()->endEvent(0, pEventName, resetView, frame);
    }

    void declareGlobalEventCameraFixedThere(const char* pEventName, bool mDisableRoll, f32 localOffsetV) {
        declareGlobalEventCamera(pEventName);
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(0, pEventName);
        if (chunk != nullptr) {
            chunk->setCameraType("CAM_TYPE_EYEPOS_FIX_THERE", getCameraDirector()->mHolder);
            chunk->mExParam.mLOffsetV = localOffsetV;
            if (mDisableRoll) {
                chunk->mGeneralParam->mNum1 = 1;
            }

            chunk->mIsCodeChunk = true;
        }
    }

    void declareGlobalEventCameraDead(const char* pEventName, f32 dist, s32 time, s32 type) {
        declareGlobalEventCamera(pEventName);
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(0, pEventName);
        if (chunk != nullptr) {
            chunk->setCameraType("CAM_TYPE_DEAD", getCameraDirector()->mHolder);
            chunk->mGeneralParam->mDist = dist;
            chunk->mGeneralParam->mNum1 = time;
            chunk->mGeneralParam->mNum2 = type;
            chunk->setLOfsErpOff(true);
            chunk->mExParam.setWOffset(Vec3f(0.0f, 0.0f, 0.0f));
            chunk->mExParam.mLOffsetV = 100.0f;
            chunk->mIsCodeChunk = true;
        }
    }

    void declareEventCameraAnim(const ActorCameraInfo* pCamInfo, const char* pAnimName, void* pAnimData) {
        declareEventCamera(pCamInfo, pAnimName);
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(pCamInfo->mZoneID, pAnimName);
        if (chunk != nullptr) {
            chunk->setCameraType("CAM_TYPE_ANIM", getCameraDirector()->mHolder);
            chunk->mGeneralParam->mAnimData = reinterpret_cast< u8* >(pAnimData);
            chunk->mGeneralParam->mDist = 1.0f;
            chunk->getGeneralParam()->mNum2 = CameraAnim::getAnimFrame(reinterpret_cast< u8* >(pAnimData));
            chunk->mIsCodeChunk = true;
        }
    }

    bool isAnimCameraEnd(const ActorCameraInfo* pInfo, const char* pAnimName) {
        return getCameraDirector()->isAnimCameraEnd(pInfo->mZoneID, pAnimName);
    }

    inline bool isCameraType(CameraParamChunkEvent* pChunk, const char* pType) {
        return pChunk->getCameraTypeIndex() == getCameraHolder()->getIndexOf(pType);
    }

    s32 getAnimCameraFrame(const ActorCameraInfo* pCamInfo, const char* pAnimName) {
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(pCamInfo->mZoneID, pAnimName);
        if (chunk != nullptr) {
            if (isCameraType(chunk, "CAM_TYPE_ANIM")) {
                return chunk->mGeneralParam->mNum2;
            }
        }

        return 0;
    }

    void pauseOnAnimCamera(const ActorCameraInfo* pInfo, const char* pName) {
        getCameraDirector()->pauseOnAnimCamera(pInfo->mZoneID, pName);
    }

    void pauseOffAnimCamera(const ActorCameraInfo* pInfo, const char* pName) {
        getCameraDirector()->pauseOffAnimCamera(pInfo->mZoneID, pName);
    }

    void declareBlackHoleCamera(const char* pEventName) {
        declareGlobalEventCamera(pEventName);
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(0, pEventName);
        if (chunk != nullptr) {
            chunk->setCameraType("CAM_TYPE_BLACK_HOLE", getCameraDirector()->mHolder);
            chunk->mIsCodeChunk = true;
            chunk->mEnableErpFrame = true;
            chunk->mExParam.mCamInt = 240;
            chunk->setCollisionOff(true);
        }
    }

    void startBlackHoleCamera(const char* pEventName, const Vec3f& rWPoint, const Vec3f& rPos) {
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(0, pEventName);
        if (chunk != nullptr) {
            chunk->mGeneralParam->mWPoint.set(rWPoint);
            chunk->mGeneralParam->mAxis.set(rPos);
            startGlobalEventCameraNoTarget(pEventName);
        }
    }

    void declareLauncherCamera() {
        if (getCameraDirector()->getEventParameter(0, ::sLauncherCameraName) != nullptr) {
            return;
        }

        declareGlobalEventCamera(::sLauncherCameraName);
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(0, ::sLauncherCameraName);
        if (chunk != nullptr) {
            chunk->setCameraType("CAM_TYPE_DPD", getCameraDirector()->mHolder);
            chunk->mGeneralParam->mDist = 120.0f;
            chunk->mGeneralParam->mNum1 = CameraPointer::CameraType_UpdateWithTarget;
            chunk->mGeneralParam->mAngleA = MR::pi() / 3.0f;
            chunk->mGeneralParam->mAngleB = MR::pi() / 6.0f;
            chunk->mGeneralParam->mWPoint.z = 0.0f;
            chunk->mGeneralParam->mWPoint.x = 0.05f;
            chunk->mGeneralParam->mWPoint.y = 0.99f;
            chunk->mGeneralParam->mUp.zero();
            chunk->mGeneralParam->mNum2 = 0;
            chunk->mIsCodeChunk = true;
        }
    }

    void endLauncherCamera() {
        endGlobalEventCamera(::sLauncherCameraName, -1, true);
    }

    // A negative upX leaves the camera's up direction off.
    void setLauncherCameraAngle(f32 angleY, f32 angleX, f32 elevation, f32 upX) {
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(0, ::sLauncherCameraName);
        if (chunk != nullptr) {
            chunk->mGeneralParam->mAngleA = angleX;
            chunk->mGeneralParam->mAngleB = angleY;
            chunk->mGeneralParam->mWPoint.z = elevation;
            if (upX < 0.0f) {
                chunk->mGeneralParam->mNum2 = 0;
            } else {
                chunk->mGeneralParam->mNum2 = 1;
                chunk->mGeneralParam->mUp.x = upX;
            }
        }
    }

    void declareLauncherFlightCamera() {
        if (getCameraDirector()->getEventParameter(0, ::sLauncherFlightCameraName) != nullptr) {
            return;
        }

        declareGlobalEventCamera(::sLauncherFlightCameraName);
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(0, ::sLauncherFlightCameraName);
        if (chunk != nullptr) {
            chunk->setCameraType("CAM_TYPE_OBJ_PARALLEL", getCameraDirector()->mHolder);
            chunk->mExParam.setWOffset(Vec3f(0.0f, 0.0f, 0.0f));
            chunk->mExParam.mLOffset = 0.0f;
            chunk->mGeneralParam->mDist = 900.0f;
            chunk->mGeneralParam->mAngleA = 1.4f;
            chunk->mGeneralParam->mAngleB = MR::pi();
            chunk->setCollisionOff(true);
            chunk->mIsCodeChunk = true;
        }
    }

    void endLauncherFlightCamera() {
        endGlobalEventCamera(::sLauncherFlightCameraName, -1, true);
    }

    bool isActiveLauncherCamera() {
        return isGlobalEventCameraActive(::sLauncherCameraName);
    }

    bool isActiveLauncherFlightCamera() {
        return isGlobalEventCameraActive(::sLauncherFlightCameraName);
    }

    void startSubjectiveCamera(s32 camType) {
        getCameraDirector()->startSubjectiveCamera(camType);
    }

    void endSubjectiveCamera(s32 endType) {
        getCameraDirector()->endSubjectiveCamera(endType);
    }

    void declareEventCameraProgrammable(const char* pEventName) {
        declareGlobalEventCamera(pEventName);
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(0, pEventName);
        if (chunk != nullptr) {
            chunk->setCameraType("CAM_TYPE_POINT_FIX", getCameraDirector()->mHolder);
            chunk->mIsCodeChunk = true;
        }
    }

    void setProgrammableCameraParam(const char* pEventName, const Vec3f& rWPoint, const Vec3f& rPos, const Vec3f& rUpVec, bool isLOfsErpOff) {
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(0, pEventName);
        if (chunk != nullptr) {
            chunk->mGeneralParam->mWPoint.set(rWPoint);
            MR::crossToPolar(rWPoint, rPos, &chunk->mGeneralParam->mDist, &chunk->mGeneralParam->mAxis.x, &chunk->mGeneralParam->mAxis.y);
            chunk->mGeneralParam->mUp.set(rUpVec);
            chunk->setLOfsErpOff(isLOfsErpOff);
            if (!isLOfsErpOff) {
                chunk->mExParam.mWOffset.zero();
            }
        }
    }

    void setProgrammableCameraParamFovy(const char* pEventName, f32 fovy) {
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(0, pEventName);
        if (chunk != nullptr) {
            chunk->mExParam.mFovy = fovy;
            chunk->setUseFovy(true);
        }
    }

    u32 getEventCameraFrames(const ActorCameraInfo* pInfo, const char* pEventName) {
        CameraParamChunkEvent* chunk = getCameraDirector()->getEventParameter(pInfo->mZoneID, pEventName);
        if (chunk != nullptr) {
            return chunk->mEvFrame;
        }

        return 0;
    }
};  // namespace MR

namespace {
    char* createRegisterName(const NameObj* pNameObj, u32 id) {
        MR::FormattedName buff("%s-%d", pNameObj->getName(), id);

        const size_t size = strlen(buff.c_str()) + 1;
        char* out = new char[size];
        memcpy(out, buff.c_str(), size);
        return out;
    }
};  // namespace

namespace MR {

    void declareCameraRegisterMtx(const NameObj* pObj, u32 id, MtxPtr mtx) {
        getCameraDirector()->mRegisterHolder->declareMtxReg(::createRegisterName(pObj, id), mtx);
    }

    void declareCameraRegisterVec(const NameObj* pObj, u32 id, Vec3f* pVec) {
        getCameraDirector()->mRegisterHolder->declareVecReg(::createRegisterName(pObj, id), pVec);
    }

    void startStartPosCamera(bool interpolate) {
        getCameraDirector()->startStartPosCamera(interpolate);
    }

    void endStartPosCamera() {
        getCameraDirector()->started();
    }

    bool isStartPosCameraEnd() {
        return !getCameraDirector()->mIsStartCameraActive;
    }

    bool hasStartAnimCamera() {
        return getCameraDirector()->mStartCameraCreated;
    }

    void startStartAnimCamera() {
        getCameraDirector()->startStartAnimCamera();
    }

    bool isStartAnimCameraEnd() {
        return getCameraDirector()->isStartAnimCameraEnd();
    }

    s32 getStartAnimCameraFrame() {
        return getCameraDirector()->getStartAnimCameraFrame();
    }

    void endStartAnimCamera() {
        getCameraDirector()->endStartAnimCamera();
    }

    bool isCameraInterpolatingNearlyEnd() {
        return getCameraDirector()->isInterpolatingNearlyEnd();
    }

    bool isFirstPersonCamera() {
        return getCameraDirector()->isSubjectiveCamera();
    }

    bool isFirstPersonCameraOK() {
        return !MR::isPlayerDisableFpView() && getCameraDirector()->isEnableToControl();
    }

    bool isPossibleToShiftToFirstPersonCamera() {
        return isFirstPersonCameraOK() && !getCameraDirector()->mIsStartCameraActive;
    }

    bool isCameraPossibleToRoundLeft() {
        return getCameraDirector()->isEnableToRoundLeft();
    }

    bool isCameraPossibleToRoundRight() {
        return getCameraDirector()->isEnableToRoundRight();
    }

    bool isCameraControlNG() {
        return getCameraDirector()->mIsCameraNG || isFpViewChangingFailure();
    }

    void startTalkCamera(const Vec3f& rPosition, const Vec3f& rUp, f32 axisX, f32 axisY, s32 frame) {
        getCameraDirector()->startTalkCamera(rPosition, rUp, axisX, axisY, frame);
    }

    void endTalkCamera(bool resetView, s32 frame) {
        getCameraDirector()->endTalkCamera(resetView, frame);
    }

    void pauseOnCameraDirector() {
        requestMovementOff(getCameraDirector());
    }

    void pauseOffCameraDirector() {
        requestMovementOn(getCameraDirector());
    }

    const Vec3f& getCameraWatchPos() {
        return getCameraDirector()->mPoseParam1->mWatchPos;
    }

    void zoomInTargetGameCamera() {
        getCameraDirector()->zoomInGameCamera();
    }

    void zoomOutTargetGameCamera() {
        getCameraDirector()->zoomOutGameCamera();
    }

    bool isCameraInWater() {
        return WaterAreaFunction::isCameraInWaterForCameraUtil();
    }

    void resetCameraLocalOffset() {
        getCameraDirector()->requestLocalOffsetReset();
    }

    void overlayWithPreviousScreen(u32 time) {
        getCameraDirector()->cover(time);
    }

    bool isSubjectiveCameraOnForObjClipping() {
        return getCameraDirector()->mSubjectiveFrame > 0;
    }

    void setGameCameraTargetToPlayer() {
        CameraTargetArg camTarget = CameraTargetArg();
        setCameraTargetToPlayer(&camTarget);
        setGameCameraTarget(camTarget);
    }

    void setGameCameraTarget(const CameraTargetArg& rCamTarget) {
        rCamTarget.setTarget();
    }

    void changeEventCameraTarget(const ActorCameraInfo* /*pInfo*/, const char* /*pEvetName*/, const CameraTargetArg& rCamTarget) {
        rCamTarget.setTarget();
    }

    void startEventCameraNoTarget(const ActorCameraInfo* pInfo, const char* pName, s32 frame) {
        startEventCamera(pInfo, pName, CameraTargetArg(), frame);
    }

    void startEventCameraTargetPlayer(const ActorCameraInfo* pInfo, const char* pName, s32 frame) {
        CameraTargetArg camTarget = CameraTargetArg();
        setCameraTargetToPlayer(&camTarget);
        startEventCamera(pInfo, pName, camTarget, frame);
    }

    void startEventCamera(const ActorCameraInfo* pInfo, const char* pName, const CameraTargetArg& rCamTarget, s32 frame) {
        getCameraDirector()->startEvent(pInfo->mZoneID, pName, rCamTarget, frame);
    }

    void startGlobalEventCameraNoTarget(const char* pName, s32 frame) {
        startGlobalEventCamera(pName, CameraTargetArg(), frame);
    }

    void startGlobalEventCameraTargetPlayer(const char* pName, s32 frame) {
        CameraTargetArg camTarget = CameraTargetArg();
        setCameraTargetToPlayer(&camTarget);
        startGlobalEventCamera(pName, camTarget, frame);
    }

    void startGlobalEventCamera(const char* pName, const CameraTargetArg& rCamTarget, s32 frame) {
        getCameraDirector()->startEvent(0, pName, rCamTarget, frame);
    }

    void startEventCameraAnim(const ActorCameraInfo* pInfo, const char* pEventName, const CameraTargetArg& rCamTarget, s32 frame, f32 speed) {
        CameraParamChunkEvent* pChunk = getCameraDirector()->getEventParameter(pInfo->mZoneID, pEventName);
        if (pChunk != nullptr) {
            pChunk->mGeneralParam->mDist = speed;
        }

        startEventCamera(pInfo, pEventName, rCamTarget, frame);
    }

    void startLauncherCamera(const CameraTargetArg& rTargetArg) {
        startGlobalEventCamera(::sLauncherCameraName, rTargetArg, 0);
    }

    void startLauncherFlightCamera(s32 time) {
        startGlobalEventCameraTargetPlayer(::sLauncherFlightCameraName, time);
    }

    void cleanEventCameraTarget_temporally() {
        CameraTargetArg camTarget = CameraTargetArg();
        setCameraTargetToPlayer(&camTarget);
        camTarget.setTarget();
    }
};  // namespace MR

