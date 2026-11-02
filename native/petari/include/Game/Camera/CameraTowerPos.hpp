#pragma once

#include "Game/Camera/CameraTowerBase.hpp"

class CameraTowerPos : public CameraTowerBase {
public:
    CameraTowerPos(const char* pName = "\223\203\203\112\203\201\203\211\201\151\203\124\203\165\203\136\201\133\203\121\203\142\203\147\225\164\202\253\201\152" /* 塔カメラ（サブターゲット付き） */);
    virtual ~CameraTowerPos();

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isEnableToReset() const {
        return true;
    }
    virtual CamTranslatorBase* createTranslator();

    void setParam(const TVec3f& wPoint, const TVec3f& axis, f32 dist, const TVec2f& angle, f32 angleXRate) {
        mWPoint.set(wPoint);
        mAxis.set(axis);
        mDist = dist;
        mAngleYMaxSpeed = angle.y;
        mAngleX = angle.x;
        mAngleXRate = angleXRate;
    }

    void calcIdealPose(bool);
    void arrangeHorizontalPose(const TVec3f&, TVec3f&);
    void arrangeWatchPos(TVec3f&, const TVec3f&);

    TVec3f mWPoint;
    TVec3f mAxis;
    f32 mAngleYMaxSpeed;
    f32 mAngleX;
    f32 mDist;
    f32 mAngleXRate;
    TVec3f mPos;
};
