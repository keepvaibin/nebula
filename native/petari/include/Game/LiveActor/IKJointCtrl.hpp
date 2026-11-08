#pragma once

#include <nebula_geometry.hpp>

class IKJoint;
class JointController;
struct JointControllerInfo;
class LiveActor;

struct IKJointCtrlParam {
    IKJointCtrlParam() : mName(""), mNearLimitRate(), mFarLimitRate(1.0f), mEndDirMaxDegree(45.0f), mEndLocalDir(0.0f, 1.0f, 0.0f) {
    }

    const char* mName;
    f32 mNearLimitRate;
    f32 mFarLimitRate;
    f32 mEndDirMaxDegree;
    Vec3f mEndLocalDir;
};

// Two-bone IK on the named end joint, its parent and grandparent; the end joint can also turn toward a direction.
class IKJointCtrl {
public:
    IKJointCtrl(LiveActor*);

    void init(const IKJointCtrlParam&);
    // rate 0..1: blend from the animated pose.
    void setEndPosition(const Vec3f&, f32 rate);
    void setEndDirection(const Vec3f&, f32 rate);
    void endCtrl();
    void setCallBackFunction();
    bool updateRootJointCallBack(Pos3f*, const JointControllerInfo&);
    bool updateMiddleJointCallBack(Pos3f*, const JointControllerInfo&);
    bool updateEndJointCallBack(Pos3f*, const JointControllerInfo&);

    void disableCallBack() {
        mIsCallBackEnabled = false;
    }

    void enableCallBack() {
        mIsCallBackEnabled = true;
    }

    const char* mName;
    LiveActor* mActor;
    IKJoint* mJoint;
    JointController* mRootCtrl;
    JointController* mMiddleCtrl;
    JointController* mEndCtrl;
    Vec3f mEndDir;
    Vec3f mEndLocalDir;
    f32 mEndDirMaxDegree;
    f32 mEndPosRate;
    f32 mEndDirRate;
    u32 mRootJointNo;
    u32 mMiddleJointNo;
    u32 mEndJointNo;
    bool mIsCallBackEnabled;
};

class IKJointCtrlHolder {
public:
    IKJointCtrlHolder(LiveActor*, s32 capacity);

    void addCtrl(const IKJointCtrlParam&);

    void setEndPosition(const char*, const Vec3f&, f32);
    void setEndDirection(const char*, const Vec3f&, f32);

    void startUpdate();
    void endUpdate();
    void endCtrlAll();
    void setCallBackFunction();
    IKJointCtrl* findIKJointCtrl(const char*);

    IKJointCtrl** mControls;
    s32 mCapacity;
    s32 mNumControls;
    LiveActor* mActor;
    bool mIsIdle;  // outside startUpdate .. endUpdate
};

class ActorJoint {
public:
    static IKJointCtrlHolder* createIKJointCtrlHolder(LiveActor*);
};
