#include "Game/Boss/BossKameckBattlePattarn.hpp"

BossKameckBattlePattarn::BossKameckBattlePattarn(s32* a1, bool a2) : mPattern(), mPatternNum(), mIndex(), mCanSummon(a2) {
    initPattarn(a1);
}

void BossKameckBattlePattarn::initPattarn(s32* a1) {
    mPattern = a1;

    u32 v2 = 0;

    for (s32 i = 0; mPattern[i] != -1; i++) {
        v2++;
    }

    mPatternNum = v2;
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