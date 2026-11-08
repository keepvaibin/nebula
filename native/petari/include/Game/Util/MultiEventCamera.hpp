#pragma once

#include "Game/Camera/CameraTargetArg.hpp"

class ActorCameraInfo;

class MultiEventCamera {
public:
    enum CameraType {
        CameraType_Soon,
        CameraType_AtLanding,
    };

    MultiEventCamera();

    bool isEnd() const;
    void setUp(const char*, const ActorCameraInfo*, s32);
    void setEndCameraTypeSoon();
    void setEndCameraTypeAtLanding();
    void start(s32 cameraIndex);
    void start(const CameraTargetArg&, s32 cameraIndex);
    void update();
    void nextForce();
    void endForce();
    void endForceSoon();
    void endForceAtLanding();
    void changeTargetPlayer();
    bool isActive(s32 cameraIndex) const;
    void declareEventCamera(s32 cameraIndex);
    void startCamera();
    void endCamera(s32);
    void changeTarget(const CameraTargetArg&);

    const char* mName;
    s32 mCameraNum;
    s32 mCameraIndex;
    s32 mFrame;
    s32 mFrameNum;
    s32 mCameraType;
    const ActorCameraInfo* mCameraInfo;
    CameraTargetArg mTarget;
};
