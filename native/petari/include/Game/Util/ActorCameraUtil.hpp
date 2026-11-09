#pragma once

#include <nebula_geometry.hpp>

class ActorCameraInfo;
class CameraTargetArg;
class TableIter;
class LiveActor;

namespace MR {
    void initAnimCamera(const LiveActor*, const ActorCameraInfo*, const char*);
    ActorCameraInfo* createActorCameraInfo(const TableIter&);
    bool createActorCameraInfoIfExist(const TableIter&, ActorCameraInfo**);
    bool initActorCamera(const LiveActor*, const TableIter&, ActorCameraInfo**);
    bool initMultiActorCamera(const LiveActor*, const TableIter&, ActorCameraInfo**, const char*);
    bool initMultiActorCameraNoInit(const LiveActor*, ActorCameraInfo*, const char*);
    void initActorCameraProgrammable(const LiveActor*);
    bool startActorCameraNoTarget(const LiveActor*, const ActorCameraInfo*, s32 = -1);
    bool startActorCameraTargetPlayer(const LiveActor*, const ActorCameraInfo*, s32 = -1);
    bool startActorCameraTargetSelf(const LiveActor*, const ActorCameraInfo*, s32 = -1);
    bool startActorCameraTargetOther(const LiveActor*, const ActorCameraInfo*, const CameraTargetArg&, s32);
    bool startMultiActorCameraNoTarget(const LiveActor*, const ActorCameraInfo*, const char*, s32 = -1);
    bool startMultiActorCameraTargetPlayer(const LiveActor*, const ActorCameraInfo*, const char*, s32 = -1);
    bool startMultiActorCameraTargetSelf(const LiveActor*, const ActorCameraInfo*, const char*, s32 = -1);
    bool startMultiActorCameraTargetOther(const LiveActor*, const ActorCameraInfo*, const char*, const CameraTargetArg&, s32 = -1);
    void startAnimCameraTargetPlayer(const LiveActor*, const ActorCameraInfo*, const char*, s32, f32);
    void startAnimCameraTargetSelf(const LiveActor*, const ActorCameraInfo*, const char*, s32, f32);
    void startAnimCameraTargetOther(const LiveActor*, const ActorCameraInfo*, const char*, const CameraTargetArg&, s32, f32);
    void startActorCameraProgrammable(const LiveActor*, s32 = -1);
    bool endActorCamera(const LiveActor*, const ActorCameraInfo*, bool, s32 = -1);
    bool endMultiActorCamera(const LiveActor*, const ActorCameraInfo*, const char*, bool, s32 = -1);
    bool endActorCameraAtLanding(const LiveActor*, const ActorCameraInfo*, s32 = -1);
    bool endMultiActorCameraAtLanding(const LiveActor*, const ActorCameraInfo*, const char*, s32 = -1);
    void endActorCameraProgrammable(const LiveActor*, s32, bool);
    bool isActiveMultiActorCamera(const LiveActor*, const ActorCameraInfo*, const char*);
    void setProgrammableCameraParam(const LiveActor*, const Vec3f&, const Vec3f&, const Vec3f&);
    void setProgrammableCameraParamFovy(const LiveActor*, f32);
    void initAnimCamera(const LiveActor*, const ActorCameraInfo*, const char*, const char*);
    void endAnimCamera(const LiveActor*, const ActorCameraInfo*, const char*, s32, bool);
    s32 getAnimCameraFrame(const LiveActor*, const ActorCameraInfo*, const char*);
    bool isAnimCameraEnd(const LiveActor*, const ActorCameraInfo*, const char*);
    s32 getActorCameraFrames(const LiveActor*, const ActorCameraInfo*);
    s32 getMultiActorCameraFrames(const LiveActor*, const ActorCameraInfo*, const char*);
    bool isExistActorCamera(const ActorCameraInfo*);
    void startRumbleWithShakeCameraWeak(const LiveActor*, const char*, const char*, f32, f32);
    void startRumbleWithShakeCameraNormalWeak(const LiveActor*, const char*, const char*, f32, f32);
    void startRumbleWithShakeCameraStrong(const LiveActor*, const char*, const char*, f32, f32);
};  // namespace MR
