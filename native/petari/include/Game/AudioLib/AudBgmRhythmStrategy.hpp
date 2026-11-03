#pragma once


class AudBgm;
class JAISoundHandle;

class AudBgmRhythmStrategy {
public:
    /// @brief Creates a new `AudBgmRhythmStrategy`.
    AudBgmRhythmStrategy() : mBgmIdx(-1), mBgm() {};

    virtual bool set(AudBgm*, s32);

    void reject();
    bool setDominant();
    bool isDominant() const;

    s32 mBgmIdx;
    AudBgm* mBgm;
};
