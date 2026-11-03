#pragma once

#include "Game/Camera/Camera.hpp"
#include "Game/LiveActor/RailRider.hpp"

class CameraTwistedPassage : public Camera {
public:
    CameraTwistedPassage(const char* pName = "\202\313\202\266\202\352\211\361\230\114\203\112\203\201\203\211" /* ねじれ回廊カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(s32, s32, f32, f32);
    void initStartPos();
    f32 calcNearestCoord(const TVec3f&) const;
    f32 calcNearestCoordBetweenTwoRailCoord(f32, f32, const TVec3f&) const;

    RailRider* mRailRider;
    f32 mDistMin;
    f32 mDistMax;
    bool mIsReversed;
};
