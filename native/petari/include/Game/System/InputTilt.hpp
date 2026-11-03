#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class PlayerInput;

class InputTilt {
public:
    InputTilt(const PlayerInput* pPad, u32 device);

    bool getAcceleration(TVec3f* pOut) const;
    bool getPastAcceleration(TVec3f* pOut, s32 index) const;
    void update();

    /* 0x000 */ const PlayerInput* mPad;
    /* 0x004 */ u32 _4;
    /* 0x024 */ TVec3f mHistory[128];
    /* 0x624 */ s32 _624;
    /* 0x628 */ s32 _628;
};
