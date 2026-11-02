#pragma once

#include "Game/Camera/CameraTargetArg.hpp"

class ActorCameraInfo;

class MultiEventCamera {
public:
    enum CameraType {
        CameraType_Soon,
        CameraType_AtLanding,
    };

    /// @brief Creates a new `MultiEventCamera`.
    MultiEventCamera();

    bool isEnd() const;
    void setUp(const char*, const ActorCameraInfo*, s32);
    void setEndCameraTypeSoon();
    void setEndCameraTypeAtLanding();
    void start(s32);
    void start(const CameraTargetArg&, s32);
    void update();
    void nextForce();
    void endForce();
    void endForceSoon();
    void endForceAtLanding();
    void changeTargetPlayer();
    bool isActive(s32) const;
    void declareEventCamera(s32);
    void startCamera();
    void endCamera(s32);
    void changeTarget(const CameraTargetArg&);

    const char* mName;
    s32 mCameraNum;
    s32 _8;
    s32 _C;
    s32 _10;
    s32 mCameraType;
    const ActorCameraInfo* mCameraInfo;
    CameraTargetArg mTarget;
};
