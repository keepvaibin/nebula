#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class LiveActor;
class JointController;
struct JointControllerInfo;

class JointCtrlRate {
public:
    JointCtrlRate();

    void update();
    void startCtrl(s32);
    void endCtrl(s32);

    f32 mRate;
    s32 mTime;
    s32 mStartTime;
    s32 mEndTime;
};

struct DynamicJointCtrlParam {
    f32 mGravity;
    f32 mFriction;
    f32 mAccelRatetoBckPos;
    f32 mBendMaxDegree;
    s32 mCtrlJointNumMax;
};

class DynamicJointCtrlNode;

class DynamicJointCtrl {
public:
    DynamicJointCtrl(LiveActor*, const char*, const DynamicJointCtrlParam*);

    void init();
    void update();
    void reset();
    void setCallBackFunction();

    LiveActor* mActor;
    const char* mName;
    s32 mNodeNum;
    DynamicJointCtrlNode** mCtrlNodes;
    const DynamicJointCtrlParam* mParams;
    JointCtrlRate* mControlRate;
};

class DynamicJointCtrlNode {
public:
    DynamicJointCtrlNode(const DynamicJointCtrl*, MtxPtr, f32);

    void update(LiveActor*, const DynamicJointCtrlNode*);
    void reset();
    bool updateJointMtxCallBack(Pos3f*, const JointControllerInfo&);

    f32 mLength;
    Vec3f mPos;
    Vec3f mVelocity;
    Vec3f mDir;
    MtxPtr mMtx;
    JointController* mJointController;
    DynamicJointCtrlNode* mChild;
    const DynamicJointCtrl* mParentControl;
};

class DynamicJointCtrlKeeper {
public:
    DynamicJointCtrlKeeper(LiveActor*);

    void update();
    void setCallBackFunction();
    void startCtrl(const char*, s32);
    void endCtrl(const char*, s32);
    void reset();
    DynamicJointCtrl* findJointCtrl(const char*);

    LiveActor* mActor;
    s32 mControlNum;
    DynamicJointCtrl** mControls;
};