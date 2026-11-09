#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class CameraMan;
class CameraPoseParam;

class OnlyCamera : public NameObj {
public:
    OnlyCamera(const char*);

    void calcPose(CameraMan*);
    void calcStartPose(CameraMan*);
    void calcSafePose(CameraMan*);
    void moveToIdealPosition(Vec3f*);

    CameraPoseParam* mPoseParam;
    bool mStartPose;
    bool mCalcIdeal;
    Vec3f mPos;
    f32 mSpeed;
    bool mIsResetting;
};
