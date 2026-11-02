#pragma once

#include <JSystem/JGeometry/TMatrix.hpp>

class LiveActor;
class JointController;
class JointCtrlRate;
struct JointControllerInfo;

class TurnJointCtrl {
public:
    enum AXIS { X = 0, Y = 1, Z = 2 };
    struct Ctrl {
        bool mEnabled;
        JointController* mJointController;
        f32 mBlendRate;
        AXIS mFrontAxis;
        AXIS mVerticalTurnAxis;
        AXIS mHorizontalTurnAxis;
    };

    TurnJointCtrl(LiveActor*);

    void init(f32, f32, f32);
    void addFace(const char*, f32, AXIS, AXIS, AXIS);
    void addWaist(const char*, f32, AXIS, AXIS, AXIS);
    void startCtrl(s32);
    void endCtrl(s32);
    void validate();
    void invalidate();
    void setStarePos(const TVec3f&);
    void update();
    void setCallBackFunction();
    bool updateJointMtxCallBackFace(TPos3f*, const JointControllerInfo&);
    bool updateJointMtxCallBackWaist(TPos3f*, const JointControllerInfo&);
    void getMtxDir(TVec3f*, const TPos3f*, AXIS);
    bool updateJointMtxCallBack(TPos3f*, const Ctrl&);

    LiveActor* mActor;
    Ctrl mFace;
    Ctrl mWaist;
    JointCtrlRate* mCtrlRate;
    f32 mHorizontalDegreeMax;
    f32 mPositiveDegreeMax;
    f32 mNegativeDegreeMax;
    TVec3f mStarePos;
    TVec3f _50;
    f32 _5C;
    f32 _60;
    s32 mCtrlFrames;
    bool mEnabled;
};
