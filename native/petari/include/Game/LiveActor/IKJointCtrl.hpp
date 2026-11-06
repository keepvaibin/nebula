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

class IKJointCtrl {
public:
    IKJointCtrl(LiveActor*);

    void init(const IKJointCtrlParam&);
    void setEndPosition(const Vec3f&, f32);
    void setEndDirection(const Vec3f&, f32);
    void endCtrl();
    void setCallBackFunction();
    bool updateRootJointCallBack(Pos3f*, const JointControllerInfo&);
    bool updateMiddleJointCallBack(Pos3f*, const JointControllerInfo&);
    bool updateEndJointCallBack(Pos3f*, const JointControllerInfo&);

    void disableCallBack() {
        _B6 = false;
    }

    void enableCallBack() {
        _B6 = true;
    }

    const char* mName;
    Mtx34f _4;
    Mtx34f _34;
    LiveActor* mActor;
    IKJoint* mJoint;
    JointController* mRootCtrl;
    JointController* mMiddleCtrl;
    JointController* mEndCtrl;
    Vec3f _78;
    Vec3f mEndLocalDir;
    Vec3f _90;
    f32 mNearLimitRate;
    f32 mFarLimitRate;
    f32 mEndDirMaxDegree;
    f32 _A8;
    f32 _AC;
    u32 _B0;
    u32 _B2;
    u32 _B4;
    u8 _B6;
};

class IKJointCtrlHolder {
public:
    IKJointCtrlHolder(LiveActor*, u32);

    void addCtrl(const IKJointCtrlParam&);

    void setEndPosition(const char*, const Vec3f&, f32);
    void setEndDirection(const char*, const Vec3f&, f32);

    void startUpdate();
    void endUpdate();
    void endCtrlAll();
    void setCallBackFunction();
    IKJointCtrl* findIKJointCtrl(const char*);

    IKJointCtrl** mControls;
    s32 mNumControls;
    s32 _8;
    LiveActor* mActor;
    u8 _10;
};

class ActorJoint {
public:
    static IKJointCtrlHolder* createIKJointCtrlHolder(LiveActor*);
};