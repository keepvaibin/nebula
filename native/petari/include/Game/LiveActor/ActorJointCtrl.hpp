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
    ActorJointCtrl(LiveActor*);

    void startDynamicCtrl(const char*, s32);
    void endDynamicCtrl(const char*, s32);
    void resetDynamicCtrl();
    void startFaceCtrl(s32);
    void endFaceCtrl(s32);
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
