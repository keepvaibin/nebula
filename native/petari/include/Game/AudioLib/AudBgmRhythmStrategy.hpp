#pragma once


class AudBgm;
class SoundHandle;

class AudBgmRhythmStrategy {
public:
    AudBgmRhythmStrategy() : mBgmIdx(-1), mBgm() {};

    virtual bool set(AudBgm*, s32);

    void reject();
    bool setDominant();
    bool isDominant() const;

    s32 mBgmIdx;
    AudBgm* mBgm;
};
