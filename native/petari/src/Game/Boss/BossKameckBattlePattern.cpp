#include "Game/Boss/BossKameckBattlePattern.hpp"

BossKameckBattlePattern::BossKameckBattlePattern(const s32* pPattern, bool canSummon)
    : mPattern(), mPatternNum(), mIndex(), mCanSummon(canSummon) {
    initPattern(pPattern);
}

void BossKameckBattlePattern::initPattern(const s32* pPattern) {
    mPattern = pPattern;

    s32 count = 0;
    while (mPattern[count] != cPatternEnd) {
        count++;
    }

    mPatternNum = count;
    mIndex = 0;
}

s32 BossKameckBattlePattern::goNextPattern() {
    s32 next = mPattern[mIndex];

    mIndex++;

    if (mPatternNum <= mIndex) {
        mIndex = 0;
    }

    return next;
}
