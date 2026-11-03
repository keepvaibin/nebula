#pragma once

#include <JSystem/JGeometry.hpp>

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

    bool updateJointMtxCallBack(TPos3f* pMtx, const JointControllerInfo& rInfo);

    LiveActor* mHostActor;
    f32 mDegreeMax;
    TVec3f _8;
    TVec3f _14;
    JointController* mJointController;
    JointCtrlRate* mJointCtrlRate;
};
