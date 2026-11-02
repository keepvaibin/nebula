#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class Camera;
class CameraPoseParam;
class CameraTargetObj;

class CameraHeightArrange : public NameObj {
public:
    enum JumpState {
        JumpState_NoMove = 0,
        JumpState_Rise = 1,
        JumpState_Fall = 2,
    };

    CameraHeightArrange(Camera*);

    virtual ~CameraHeightArrange();

    void updateJump();
    void calcWatchPos(CameraTargetObj*);
    void arrange();
    void resetJump();
    void reset();
    void resetParameter();
    void checkState();
    void arrangeHeight();
    void arrangeVPan();
    void calcPose();
    void updateUpper();
    void updateLower();
    f32 calcOffset(f32);
    void chase();
    void updateHeightAndOffset();
    const TVec3f& getGlobalAxis();

    Camera* mCamera;
    bool mIsJumping;
    bool mIsStartJump;
    bool mIsEndJump;
    CameraPoseParam* mNextParam;
    CameraPoseParam* mCurrParam;
    u32 _1C;
    u32 _20;
    u32 _24;
    f32 mCameraWatchPosBaseHeight;
    f32 mCameraTargetPosHeight;
    f32 mCameraWatchPosHeight;
    f32 mCameraPosHeight;
    f32 mCameraWatchPosOffset;
    f32 mCameraPosOffset;
    u32 mJumpState;
    s32 mStateTime;
    s32 _48;
    bool mIsCatchup;
    s32 mChaseTime;
    s32 mRiseTime;
    s32 mDropTime;
    s32 mRiseEaseTime;
    bool mUpdateGlobalAxis;
    TVec3f mGlobalAxis;
    f32 mOffsetScale;
    f32 mOffsetScaleMax;
    f32 mFocalScaleUpper;
    f32 mFocalScaleLower;
    s32 mGndInt;
    f32 mPosOffsetMinRiseLag;
    f32 mPosOffsetMinDropLag;
    s32 mRiseDelay;
    s32 mDropDelay;
    s32 mMaxRiseEaseTime;
    bool mVPanUse;
    TVec3f mVPanAxis;
};
