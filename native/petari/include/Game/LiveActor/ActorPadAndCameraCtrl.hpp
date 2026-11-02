#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class ModelManager;
class ResourceHolder;

struct ActorPadAndCameraCtrlInfo {
    const char* mBckName;
    f32 mStartFrame;
    f32 mEndFrame;
    const char* mPadRumbleName;
    const char* mCameraShakeName;
    const char* mPadRumbleNameMiddle;
    const char* mCameraShakeNameMiddle;
    const char* mPadRumbleNameFar;
    const char* mCameraShakeNameFar;
    f32 mDistanceNear;
    f32 mDistanceFar;
    f32 mDistanceInvalid;
    bool _30;
    bool _31;
};

class ActorPadAndCameraCtrl {
public:
    ActorPadAndCameraCtrl(const ModelManager*, const TVec3f*);

    static ActorPadAndCameraCtrl* tryCreate(const ModelManager*, const TVec3f*);

    void update();
    void updateInfoBck();
    void updatePadAndCamera(const ActorPadAndCameraCtrlInfo*);

    const ModelManager* _0;
    const TVec3f* _4;
    const ResourceHolder* _8;
    const char* _C;
    s32 mInfoNum;
    ActorPadAndCameraCtrlInfo* mInfo;
};
