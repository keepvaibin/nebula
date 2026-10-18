#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFooFighterPlanet : public Camera {
public:
    CameraFooFighterPlanet(const char* pName = "\203\164\201\133\203\164\203\100\203\103\203\136\201\133\203\166\203\211\203\154\203\142\203\147\203\112\203\201\203\211" /* フーファイタープラネットカメラ */);
    virtual ~CameraFooFighterPlanet();

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void goRoundBehind(TVec3f&, TVec3f&, TVec3f&);

    void setParam(f32 distMin, f32 distMax, f32 pitchMax) {
        mDistMin = distMin;
        mDistMax = distMax;
        mPitchMax = pitchMax;
    }

    /* 0x4C */ f32 mDistMin;
    /* 0x50 */ f32 mDistMax;
    /* 0x54 */ f32 mPitchMax;
};
