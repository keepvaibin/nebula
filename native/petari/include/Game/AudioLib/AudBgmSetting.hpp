#pragma once

#include "Game/Sound/Sound.hpp"

namespace AudBgmSetting {
    struct BgmSettingInfo {
        s32 mMuteStateIndex;
        s32 mExtraChordIndex;
    };

    struct MuteState {
        u8 mMuteStates[8];  // 16 nibbles
        u8 mVolumeA;
        u8 mVolumeB;
    };

    struct MultiBgmSet {
        u32 mSeqId;
        u32 mStreamId;
        f32 mBeatMul;
        u32 mIntroBeats;
        u32 mLoopBeats;
        u32 mLoopStartSamples;  // time (sec) = val / 48000
        u32 mLoopEndSamples;    // time (sec) = val / 48000
    };

    const MuteState* getMuteState(SoundID, s32);
    u16 getExtraChordNum(SoundID, s32);
    u32 getSeqIdForMultiBgm(u32);
    u32 getStreamIdForMultiBgm(u32);
    f32 getBeatMulForMultiBgm(u32);
    u32 getIntroBeatsForMultiBgm(u32);
    u32 getLoopBeatsForMultiBgm(u32);
    u32 getLoopStartSamplesForMultiBgm(u32);
    u32 getLoopEndSamplesForMultiBgm(u32);

};  // namespace AudBgmSetting
