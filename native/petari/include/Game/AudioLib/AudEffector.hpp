#pragma once

#include <nebula_types.h>

struct EffectLineCommand;

class AudEffector {
public:
    AudEffector();

    void initFxLine();
    void setFxSend(f32);
    void setCutoff(s32);
    void setSpecialFx(s32);
    void initParams();

    // The four effect lines: echo to the sends, as the game sets them up once.
    static const EffectLineCommand cFxLine[4];

    f32 mFxSend;
    s32 mCutoff;
    s32 mSpecialFx;
};
