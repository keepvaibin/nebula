#pragma once

#include <nebula_geometry.hpp>

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
    bool mIsBckPlaying;
    bool mIsActive;
};

class ActorPadAndCameraCtrl {
public:
    ActorPadAndCameraCtrl(const ModelManager*, const Vec3f*);

    static ActorPadAndCameraCtrl* tryCreate(const ModelManager*, const Vec3f*);

    void update();
    void updateInfoBck();
    void updatePadAndCamera(const ActorPadAndCameraCtrlInfo*);

    const ModelManager* mModelManager;
    const Vec3f* mPosition;
    const ResourceHolder* mResourceHolder;
    const char* mPlayingBckName;
    s32 mInfoNum;
    ActorPadAndCameraCtrlInfo* mInfo;
};
