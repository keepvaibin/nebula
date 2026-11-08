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

    void startAppearCamera(LiveActor*, const Vec3f&, const Vec3f&, const Vec3f&);
    void endAppearCamera(LiveActor*);
    s32 getAppearCameraFrames() const;
    void start(const Vec3f&, const Vec3f&, const Vec3f&);
    void update(const Vec3f&, const Vec3f&);
    void cancel();
    void end();
    void updateTargetMatrix(const Vec3f&, const Vec3f&);
    bool isUseAppearCamera(LiveActor*) const;
    void init(const TableIter&, LiveActor*);
    void initAppearCamera(const TableIter&, LiveActor*);

    MultiEventCamera* mCamera;
    CameraTargetMtx* mTargetMtx;
    ActorCameraInfo* mCameraInfo;
    s32 mAppearCameraFrame;
};
