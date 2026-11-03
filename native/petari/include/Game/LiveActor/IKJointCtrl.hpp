#pragma once

#include <JSystem/JGeometry/TMatrix.hpp>

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
    TVec3f mEndLocalDir;
};

class IKJointCtrl {
public:
    IKJointCtrl(LiveActor*);

    void init(const IKJointCtrlParam&);
    void setEndPosition(const TVec3f&, f32);
    void setEndDirection(const TVec3f&, f32);
    void endCtrl();
    void setCallBackFunction();
    bool updateRootJointCallBack(TPos3f*, const JointControllerInfo&);
    bool updateMiddleJointCallBack(TPos3f*, const JointControllerInfo&);
    bool updateEndJointCallBack(TPos3f*, const JointControllerInfo&);

    void disableCallBack() {
        _B6 = false;
    }

    void enableCallBack() {
        _B6 = true;
    }

    const char* mName;
    TMtx34f _4;
    TMtx34f _34;
    LiveActor* mActor;
    IKJoint* mJoint;
    JointController* mRootCtrl;
    JointController* mMiddleCtrl;
    JointController* mEndCtrl;
    TVec3f _78;
    TVec3f mEndLocalDir;
    TVec3f _90;
    f32 mNearLimitRate;
    f32 mFarLimitRate;
    f32 mEndDirMaxDegree;
    f32 _A8;
    f32 _AC;
    u16 _B0;
    u16 _B2;
    u16 _B4;
    u8 _B6;
};

class IKJointCtrlHolder {
public:
    IKJointCtrlHolder(LiveActor*, u32);

    void addCtrl(const IKJointCtrlParam&);

    void setEndPosition(const char*, const TVec3f&, f32);
    void setEndDirection(const char*, const TVec3f&, f32);

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