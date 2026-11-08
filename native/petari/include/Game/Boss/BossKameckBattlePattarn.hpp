#pragma once


class BossKameckBattlePattarn {
public:
    BossKameckBattlePattarn(s32*, bool);

    void initPattarn(s32*);
    s32 goNextPattarn();

    s32* mPattern;
    s32 mPatternNum;
    s32 mIndex;
    bool mCanSummon;
};