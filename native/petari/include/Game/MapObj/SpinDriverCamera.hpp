#pragma once

#include <nebula_geometry.hpp>

class ActorCameraInfo;
class CameraTargetMtx;
class TableIter;
class LiveActor;
class MultiEventCamera;

class SpinDriverCamera {
public:
    SpinDriverCamera();

    void startAppearCamera(LiveActor*, const Vec3f& rUp, const Vec3f& rFront, const Vec3f& rPos);
    void endAppearCamera(LiveActor*);
    s32 getAppearCameraFrames() const;
    void start(const Vec3f& rUp, const Vec3f& rFront, const Vec3f& rPos);
    void update(const Vec3f& rDir, const Vec3f& rPos);
    void cancel();
    void end();
    void updateTargetMatrix(const Vec3f& rDir, const Vec3f& rPos);
    bool isUseAppearCamera(LiveActor*) const;
    void init(const TableIter&, LiveActor*);
    void initAppearCamera(const TableIter&, LiveActor*);

    MultiEventCamera* mCamera;
    CameraTargetMtx* mTargetMtx;
    ActorCameraInfo* mCameraInfo;
    s32 mAppearCameraFrame;
};
