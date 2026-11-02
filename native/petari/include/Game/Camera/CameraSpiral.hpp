#pragma once

#include "Game/Camera/Camera.hpp"

class CameraSpiral : public Camera {
public:
    enum EaseType {
        EaseType_Linear = 0,
        EaseType_InOut = 1,
    };
    CameraSpiral(const char* pName = "\227\206\220\371\203\112\203\201\203\211\201\151\203\146\203\202\227\160\201\152" /* 螺旋カメラ（デモ用） */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(s32 easeType, s32 endTime, s32 startTime, f32 offYStart, f32 offYEnd, f32 distStart, f32 distEnd, f32 angleStart, f32 angleEnd) {
        mEndTime = endTime;
        mStartTime = startTime;
        mEaseType = easeType;
        mOffsetYStart = offYStart;
        mOffsetYEnd = offYEnd;
        mDistStart = distStart;
        mDistEnd = distEnd;
        mAngleStart = angleStart;
        mAngleEnd = angleEnd;
    }

    inline s32 getInterval() const {
        return (mEndTime - mStartTime);
    }

    s32 mEndTime;
    s32 mTimer;
    s32 mStartTime;
    s32 mEaseType;
    f32 mOffsetYStart;
    f32 mOffsetYEnd;
    f32 mDistStart;
    f32 mDistEnd;
    f32 mAngleStart;
    f32 mAngleEnd;
};
