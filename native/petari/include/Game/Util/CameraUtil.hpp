#pragma once

#include <nebula_geometry.hpp>
#include "Game/LumaMarks.hpp"

class LiveActor;
class ActorCameraInfo;
class CameraTargetArg;
class NameObj;
class TableIter;

namespace MR {
    bool calcScreenPosition(LUMA_OUT Vec2f*, const Vec3f&);
    bool calcScreenPosition(LUMA_OUT Vec3f*, const Vec3f&);
    bool calcNormalizedScreenPosition(Vec3f*, const Vec3f&);
    bool calcNormalizedScreenPositionFromView(Vec3f*, const Vec3f&);
    bool calcWorldPositionFromScreen(Vec3f*, const Vec2f&, f32);
    bool calcWorldPositionFromCenterScreen(Vec3f*, const Vec2f&, f32);
    bool calcWorldRayDirectionFromScreen(LUMA_OUT Vec3f*, const Vec2f&);

    f32 calcCameraDistanceZ(const Vec3f&);
    void loadProjectionMtx();
    // The scene's opaque surfaces are drawn: tells the renderer, with the camera's projection and the screen.
    void sceneOpaqueEnd();
    void loadViewMtx();
    const Pos3f& getCameraViewMtx();
    const Pos3f& getCameraInvViewMtx();
    const Proj3f& getCameraProjectionMtx();

    f32 getAspect();
    f32 getNearZ();
    // The near plane drawn with: a plugin's (set_camera_near), else getNearZ.
    f32 getRenderNearZ();
    f32 getFarZ();
    f32 getFovy();
    f32 getRenderFovy();  // CameraContext::getRenderFovy
    void setCameraViewMtx(const Pos3f& rMtx);
    void setNearZ(f32);
    void setFovy(f32);
    void setShakeOffset(f32, f32);
    const Vec3f getCamPos();
    Vec3f getCamXdir();
    Vec3f getCamYdir();
    Vec3f getCamZdir();

    void createMirrorCamera();
    bool isExistMirrorCamera();
    const Pos3f& getMirrorCameraViewMtx();
    const Pos3f& getMirrorModelTexMtx();

    void completeCameraParameters();
    void resetCameraMan();
    void startCameraInterpolation(u32);

    void declareEventCamera(const ActorCameraInfo*, const char*);
    void endEventCamera(const ActorCameraInfo*, const char*, bool, s32 = -1);
    void endEventCameraAtLanding(const ActorCameraInfo*, const char*, s32);
    bool isEventCameraActive();
    bool isEventCameraActive(const ActorCameraInfo*, const char*);
    void declareGlobalEventCamera(const char*);
    void endGlobalEventCamera(const char*, s32, bool);
    void declareGlobalEventCameraAbyss(const char*);
    void declareGlobalEventCameraFixedThere(const char*, bool, f32);
    void declareGlobalEventCameraDead(const char*, f32, s32, s32);
    void declareEventCameraAnim(const ActorCameraInfo*, const char*, void*);

    bool isAnimCameraEnd(const ActorCameraInfo*, const char*);
    s32 getAnimCameraFrame(const ActorCameraInfo*, const char*);
    void pauseOnAnimCamera(const ActorCameraInfo*, const char*);
    void pauseOffAnimCamera(const ActorCameraInfo*, const char*);

    void declareBlackHoleCamera(const char*);
    void startBlackHoleCamera(const char*, const Vec3f&, const Vec3f&);

    void declareLauncherCamera();
    void endLauncherCamera();
    void setLauncherCameraAngle(f32, f32, f32, f32);
    void declareLauncherFlightCamera();
    void endLauncherFlightCamera();
    bool isActiveLauncherCamera();
    bool isActiveLauncherFlightCamera();

    void startSubjectiveCamera(s32);
    void endSubjectiveCamera(s32 endType);

    void declareEventCameraProgrammable(const char*);
    void setProgrammableCameraParam(const char*, const Vec3f&, const Vec3f&, const Vec3f&, bool);
    void setProgrammableCameraParamFovy(const char*, f32);

    u32 getEventCameraFrames(const ActorCameraInfo*, const char*);

    void declareCameraRegisterMtx(const NameObj*, u32, MtxPtr);
    void declareCameraRegisterVec(const NameObj*, u32, Vec3f*);

    void startStartPosCamera(bool);
    void endStartPosCamera();
    bool isStartPosCameraEnd();
    bool hasStartAnimCamera();
    void startStartAnimCamera();
    bool isStartAnimCameraEnd();
    s32 getStartAnimCameraFrame();
    void endStartAnimCamera();

    bool isCameraInterpolatingNearlyEnd();
    bool isFirstPersonCamera();
    bool isPossibleToShiftToFirstPersonCamera();
    bool isCameraPossibleToRoundLeft();
    bool isCameraPossibleToRoundRight();
    bool isCameraControlNG();

    void startTalkCamera(const Vec3f&, const Vec3f&, f32, f32, s32);
    void endTalkCamera(bool, s32);

    void pauseOnCameraDirector();
    void pauseOffCameraDirector();

    const Vec3f& getCameraWatchPos();

    void zoomInTargetGameCamera();
    void zoomOutTargetGameCamera();

    bool isCameraInWater();

    void resetCameraLocalOffset();
    void overlayWithPreviousScreen(u32);

    bool isSubjectiveCameraOnForObjClipping();

    void setGameCameraTargetToPlayer();
    void setGameCameraTarget(const CameraTargetArg&);

    void changeEventCameraTarget(const ActorCameraInfo*, const char*, const CameraTargetArg&);
    void startEventCameraNoTarget(const ActorCameraInfo*, const char*, s32 = -1);
    void startEventCameraTargetPlayer(const ActorCameraInfo*, const char*, s32 = -1);
    void startEventCamera(const ActorCameraInfo*, const char*, const CameraTargetArg&, s32 = -1);
    void startGlobalEventCameraNoTarget(const char*, s32 = -1);
    void startGlobalEventCameraTargetPlayer(const char*, s32);
    void startGlobalEventCamera(const char*, const CameraTargetArg&, s32);
    void startEventCameraAnim(const ActorCameraInfo*, const char*, const CameraTargetArg&, s32, f32);

    void startLauncherCamera(const CameraTargetArg&);
    void startLauncherFlightCamera(s32);

    void cleanEventCameraTarget_temporally();
};  // namespace MR
