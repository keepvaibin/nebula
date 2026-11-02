#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class GamePad;

class GamePadAcceleration {
public:
    GamePadAcceleration(const GamePad* pPad, u32 device);

    bool getAcceleration(TVec3f* pOut) const;
    bool getPastAcceleration(TVec3f* pOut, s32 index) const;
    bool isStationary() const;
    void update();
    void updateRotate();
    void updateAccAverage();
    void updateIsStable();

    const GamePad* mPad;
    u32 _4;
    f32 _8;
    f32 _C;
    TVec3f _10;
    f32 _1C;
    bool _20;
    TVec3f mHistory[128];
    s32 _624;
    s32 _628;
    TVec3f _62C;
    TVec3f _638;
    s32 _644;
    s32 _648;
    s32 _64C;
};
