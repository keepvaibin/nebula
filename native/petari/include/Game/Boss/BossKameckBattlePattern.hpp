#pragma once

#include <nebula_types.h>

// Cycles a -1 terminated list of beam types. The index persists for the pattern object's lifetime.
class BossKameckBattlePattern {
public:
    static const s32 cPatternEnd = -1;

    BossKameckBattlePattern(const s32* pPattern, bool canSummon);

    void initPattern(const s32* pPattern);
    s32 goNextPattern();

    const s32* mPattern;
    s32 mPatternNum;
    s32 mIndex;
    bool mCanSummon;
};
