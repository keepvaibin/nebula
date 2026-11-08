#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class DynamicJointCtrlKeeper;
class FaceJointCtrl;
class IKJointCtrlHolder;
class LiveActor;

class ActorJointCtrl {
public:
    ActorJointCtrl(LiveActor* pActor);

    void startDynamicCtrl(const char* pName, s32 time);
    void endDynamicCtrl(const char* pName, s32 time);
    void resetDynamicCtrl();
    void startFaceCtrl(s32 time);
    void endFaceCtrl(s32 time);
    void setIKEndPosition(const char* pName, const Vec3f& rPos, f32 rate);
    void setIKEndDirection(const char* pName, const Vec3f& rDirection, f32 rate);
    void endIKCtrlAll();
    void update();
    void startUpdate();
    void endUpdate();
    void setCallBackFunction();

    LiveActor* mActor;
    DynamicJointCtrlKeeper* mJointCtrlKeeper;
    FaceJointCtrl* mFaceJointCtrl;
    IKJointCtrlHolder* mJointCtrlHolder;
    bool mIsIdle;  // outside startUpdate .. endUpdate
};
