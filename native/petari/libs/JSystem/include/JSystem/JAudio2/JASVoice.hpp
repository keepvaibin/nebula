#pragma once

#include <cstddef>

#include <nebula_game_api.h>

struct JASWaveInfo;

struct JASEffectLine : NebulaEffectLine {};

// A voice of the host's mixer (nebula_game_api.h voices_*): the game sets its source, pitch, volumes and filters here. There are 64;
// four effect lines take what the voices send them.
struct JASVoice : NebulaVoice {
    // Room for every note the game asks for: stealing a voice then only happens past this.
    static const int cVoices = 256;

    // An effect line's settings as the game's sound system keeps them (AudEffector::mFxLine).
    struct EffectConfig {
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

    void init();
    void playStart();
    void playStop();
    void replyFinishRequest();
    void forceStop();
    bool isFinish() const;
    void setWaveInfo(JASWaveInfo const&, const u8*, u32);
    void setOscInfo(u32);
    void initAutoMixer();
    void setAutoMixer(f32, f32, f32, f32);
    void setPitch(f32);
    void setMixerInitVolume(u8, f32);
    void setMixerVolume(u8, f32);
    void setPauseFlag(u8);
    void initFilter();
    void setFilterMode(u16);
    void setIIRFilterParam(s16*);
    void setFIR8FilterParam(s16*);
    void setBusConnect(u8, u8);

    static void mixSubframe();
    static void setOutputLevel(f32);
    static f32 getOutputLevel();
    static JASVoice* getVoice(int);
    static void initAll();
    // With `restart` the line's delay starts silent; with no settings it stops.
    static int setEffectLine(u8, bool restart, EffectConfig*);

    static JASVoice* sVoices;
    static JASEffectLine* sEffects;
    static f32 sOutputLevel;
};
