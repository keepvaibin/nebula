#pragma once

#include <JSystem/JGeometry.hpp>
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

    f32 _0;
    s32 _4;
    s32 _8;
    s32 _C;
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
    s32 _8;
    DynamicJointCtrlNode** mCtrlNodes;
    const DynamicJointCtrlParam* mParams;
    JointCtrlRate* mControlRate;
};

class DynamicJointCtrlNode {
public:
    DynamicJointCtrlNode(const DynamicJointCtrl*, MtxPtr, f32);

    void update(LiveActor*, const DynamicJointCtrlNode*);
    void reset();
    bool updateJointMtxCallBack(TPos3f*, const JointControllerInfo&);

    f32 _0;
    TVec3f _4;
    TVec3f _10;
    TVec3f _1C;
    MtxPtr _28;
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
    s32 _4;
    DynamicJointCtrl** mControls;
};