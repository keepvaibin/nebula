#include "Game/Boss/BossKameckBattlePattarn.hpp"

BossKameckBattlePattarn::BossKameckBattlePattarn(const s32* pPattern, bool canSummon)
    : mPattern(), mPatternNum(), mIndex(), mCanSummon(canSummon) {
    initPattarn(pPattern);
}

void BossKameckBattlePattarn::initPattarn(const s32* pPattern) {
    mPattern = pPattern;

    s32 count = 0;
    while (mPattern[count] != cPatternEnd) {
        count++;
    }

    mPatternNum = count;
    mIndex = 0;
}

s32 BossKameckBattlePattarn::goNextPattarn() {
    s32 next = mPattern[mIndex];

    mIndex++;

    if (mPatternNum <= mIndex) {
        mIndex = 0;
    }

    return next;
}
