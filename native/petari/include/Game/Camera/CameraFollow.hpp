#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFollow : public Camera {
public:
    CameraFollow(const char* pName = "\203\164\203\110\203\215\201\133\203\112\203\201\203\211" /* フォローカメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isEnableToRoundLeft() const;
    virtual bool isEnableToRoundRight() const;
    virtual bool isEnableToReset() const {
        return true;
    }
    virtual CamTranslatorBase* createTranslator();

    void setParam(f32, f32, f32, f32, f32, bool);
    void calcIdealPos();
    void calcRotPos();
    void constrainDistance(TVec3f*, const TVec3f&);
    void constrainAngleX(f32*);
    void constrainAngleY(f32*, const TVec3f&, f32);
    f32 getMagnitudeOfMovingRound() const;
    void checkShiftStateToRound(const TVec3f&, const TVec3f&);
    void arrangeHeight();
    void reflectRotOffset();
    bool isWallForward() const;
    void arrangeLookUp();
    bool checkAscent(TVec3f*) const;
    void arrangeLookDown();
    bool checkDescent(TVec3f*) const;
    void startRound(f32, f32);
    void checkValidityOfRound(const TVec3f&, const TVec3f&);
    bool isUpsideDownByRoll() const;

    f32 mDistMax;
    f32 mDistMin;
    f32 mAngleXMin;
    f32 mAngleXRange;
    TVec3f mPos;
    TVec3f mWatchPos;
    f32 mAngleYRoundSpeed;  // moving round magnitude (rate?)
    f32 mAngleXSpeed;
    bool mCheckWall;
    bool mIsRounding;
    f32 mAngleYTarget;
    f32 mAngleYSpeed;
    bool mIsRoundLeftValid;
    bool mIsRoundRightValid;
};
