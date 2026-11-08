#pragma once

#include <nebula_types.h>

// Cycles a -1 terminated list of beam types. The index persists for the pattern object's lifetime.
class BossKameckBattlePattarn {
public:
    static const s32 cPatternEnd = -1;

    BossKameckBattlePattarn(const s32* pPattern, bool canSummon);

    void initPattarn(const s32* pPattern);
    s32 goNextPattarn();

    const s32* mPattern;
    s32 mPatternNum;
    s32 mIndex;
    bool mCanSummon;
};
