#pragma once

#include <cstddef>
#include <revolution/types.h>

#include <nebula_game_api.h>

struct JASWaveInfo;

// The game's side of the host's mixer (nebula_game_api.h audio_*): the 64 voices and the four effect lines it plays.
namespace JASDsp {
    // An effect line's settings as the game's sound system keeps them (AudEffector::mFxLine).
    struct FxlineConfig_ {
        u8 _0;       // filter: FIR before (1) or after (2) the sends
        u8 _1;
        u16 _2;      // send A's destination (an index of the effect destinations)
        s16 _4;      // send A's volume
        u16 _6;      // send B's destination
        s16 _8;      // send B's volume
        u16 _A;
        u32 _C;      // delay in subframes
        s16 _10[8];  // FIR
    };

    struct FxBuf : NebulaAudioEffect {};

    struct TChannel : NebulaAudioVoice {
        void init();
        void playStart();
        void playStop();
        void replyFinishRequest();
        void forceStop();
        bool isActive() const;
        bool isFinish() const;
        void setWaveInfo(JASWaveInfo const&, const u8*, u32);
        void setOscInfo(u32);
        void initAutoMixer();
        void setAutoMixer(u16, u8, u8, u8, u8);
        void setPitch(u16);
        void setMixerInitVolume(u8, s16);
        void setMixerVolume(u8, s16);
        void setPauseFlag(u8);
        void initFilter();
        void setFilterMode(u16);
        void setIIRFilterParam(s16*);
        void setFIR8FilterParam(s16*);
        void setBusConnect(u8, u8);
    };

    void mixSubframe();
    void setDSPMixerLevel(f32);
    f32 getDSPMixerLevel();
    TChannel* getDSPHandle(int);
    void initBuffer();
    int setFXLine(u8, s16*, JASDsp::FxlineConfig_*);

    extern TChannel* sVoices;
    extern FxBuf* sEffects;
    extern f32 sDSPVolume;
};  // namespace JASDsp
