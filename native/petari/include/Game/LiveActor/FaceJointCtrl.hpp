#pragma once

#include <nebula_geometry.hpp>

class JointController;
struct JointControllerInfo;
class JointCtrlRate;
class LiveActor;

class FaceJointCtrl {
public:
    FaceJointCtrl(LiveActor*);

    void init();
    void startCtrl(s32);
    void endCtrl(s32);
    void update();
    void setCallBackFunction();

    bool updateJointMtxCallBack(Pos3f* pMtx, const JointControllerInfo& rInfo);

    LiveActor* mHostActor;
    f32 mDegreeMax;
    JointController* mJointController;
    JointCtrlRate* mJointCtrlRate;
};
