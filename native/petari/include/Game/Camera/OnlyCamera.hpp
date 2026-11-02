#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class CameraMan;
class CameraPoseParam;

class OnlyCamera : public NameObj {
public:
    OnlyCamera(const char*);

    void calcPose(CameraMan*);
    void calcStartPose(CameraMan*);
    void calcSafePose(CameraMan*);
    void moveToIdealPosition(TVec3f*);

    CameraPoseParam* mPoseParam;
    bool mStartPose;
    bool mCalcIdeal;
    TVec3f mPos;
    f32 mSpeed;
    s32 _24;
    u32 _28;
    bool _2C;
    bool _2D;
    TVec3f _30;
    bool mIsZeroFrameMoveOff;
    bool mIsResetting;
};
