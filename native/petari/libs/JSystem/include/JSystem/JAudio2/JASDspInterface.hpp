#pragma once

#include <cstddef>
#include <revolution/dsp.h>

struct JASWaveInfo;

namespace JASDsp {
    struct FxlineConfig_ {
        u8 _0;
        u8 _1;
        u16 _2;
        s16 _4;
        u16 _6;
        s16 _8;
        u16 _A;
        u32 _C;
        s16 _10[8];
    };

    // FX_BUF and CH_BUF are the DSP program's memory: natively in the console's byte order and layout, as the host's
    // DSP program reads them (NEBULA_BE is the plain type for CodeWarrior).
    typedef struct {
        NEBULA_BE(u16) _0;
        NEBULA_BE(u16) _2;
#ifdef TARGET_PC
        NEBULA_BE(u32) _4;  // the delay line's physical address: the DSP's view of the console's pointer
#else
        s16* _4;
#endif
        NEBULA_BE(u16) _8;
        NEBULA_BE(s16) _A;
        NEBULA_BE(u16) _C;
        NEBULA_BE(s16) _E;
        NEBULA_BE(u16) _10[8];
    } FxBuf;

    struct TChannel {
        void init();
        void playStart();
        void playStop();
        void replyFinishRequest();
        void forceStop();
        bool isActive() const;
        bool isFinish() const;
        void setWaveInfo(JASWaveInfo const&, u32, u32);
        void setOscInfo(u32);
        void initAutoMixer();
        void setAutoMixer(u16, u8, u8, u8, u8);
        void setPitch(u16);
        void setMixerInitVolume(u8, s16);
        void setMixerVolume(u8, s16);
        void setPauseFlag(u8);
        void flush();
        void initFilter();
        void setFilterMode(u16);
        void setIIRFilterParam(s16*);
        void setFIR8FilterParam(s16*);
        void setDistFilter(s16);
        void setBusConnect(u8, u8);

        /* 0x000 */ NEBULA_BE(u16) mIsActive;
        /* 0x002 */ NEBULA_BE(u16) mIsFinished;
        /* 0x004 */ NEBULA_BE(u16) mPitch;
        /* 0x006 */ NEBULA_BE(short) _6;
        /* 0x008 */ NEBULA_BE(u16) _8;
        /* 0x00A */ u8 _A[0x00C - 0x00A];
        /* 0x00C */ NEBULA_BE(s16) mPauseFlag;
        /* 0x00E */ NEBULA_BE(short) _E;
#ifdef TARGET_PC
        // The six bus records (connect, target, current, delay): setBusConnect and the volume setters index all six.
        /* 0x010 */ NEBULA_BE(u16) _10[6][4];
        /* 0x040 */ u8 _18[0x050 - 0x040];
#else
        /* 0x010 */ u16 _10[1][4];  // array size unknown
        /* 0x018 */ u8 _18[0x050 - 0x018];
#endif
        /* 0x050 */ NEBULA_BE(u16) _50;
        /* 0x052 */ NEBULA_BE(u16) _52;
        /* 0x054 */ NEBULA_BE(u16) _54;
        /* 0x056 */ NEBULA_BE(u16) _56;
        /* 0x058 */ NEBULA_BE(u16) _58;
        /* 0x05A */ u8 _5A[0x060 - 0x05A];
        /* 0x060 */ NEBULA_BE(short) _60;
        /* 0x062 */ u8 _62[0x064 - 0x062];
        /* 0x064 */ NEBULA_BE(u16) _64;
        /* 0x066 */ NEBULA_BE(short) _66;
        /* 0x068 */ NEBULA_BE(int) _68;
        /* 0x06C */ u8 _6C[0x070 - 0x06C];
        /* 0x070 */ NEBULA_BE(int) _70;
        /* 0x074 */ NEBULA_BE(int) _74;
        /* 0x078 */ NEBULA_BE(short) _78[4];
        /* 0x080 */ NEBULA_BE(short) _80[20];
        /* 0x0A8 */ NEBULA_BE(short) _A8[4];
        /* 0x0B0 */ NEBULA_BE(u16) _B0[16];
        /* 0x0D0 */ u8 _D0[0x100 - 0x0D0];
        /* 0x100 */ NEBULA_BE(u16) _100;
        /* 0x102 */ NEBULA_BE(u16) _102;
        /* 0x104 */ NEBULA_BE(s16) _104;
        /* 0x106 */ NEBULA_BE(s16) _106;
        /* 0x108 */ NEBULA_BE(u16) mFilterMode;
        /* 0x10A */ NEBULA_BE(u16) mForcedStop;
        /* 0x10C */ NEBULA_BE(int) _10C;
        /* 0x110 */ NEBULA_BE(u32) _110;
        /* 0x114 */ NEBULA_BE(u32) _114;
        /* 0x118 */ NEBULA_BE(u32) _118;
        /* 0x11C */ NEBULA_BE(int) _11C;
        /* 0x120 */ NEBULA_BE(s16) fir_filter_params[8];
        /* 0x130 */ u8 _130[0x148 - 0x130];
        /* 0x148 */ NEBULA_BE(s16) iir_filter_params[8];
        /* 0x158 */ u8 _158[0x180 - 0x158];
    };

    void boot(void (*)(void*));
    void releaseHalt(u32);
    void finishWork(u16);
    void syncFrame(u32, uintptr_t, uintptr_t);
    void setDSPMixerLevel(f32);
    f32 getDSPMixerLevel();
    TChannel* getDSPHandle(int);
    TChannel* getDSPHandleNc(int);
    void setFilterTable(NEBULA_BE(s16)*, s16*, u32);
    void flushBuffer();
    void invalChannelAll();
    void initBuffer();
    int setFXLine(u8, s16*, JASDsp::FxlineConfig_*);
    BOOL changeFXLineParam(u8, u8, uintptr_t);

    extern u8 const DSPADPCM_FILTER[64];
#ifdef TARGET_PC
    extern u8 const DSPRES_FILTER[1280];  // the console's bytes of its 320 words (DSP data)
#else
    extern u32 const DSPRES_FILTER[320];
#endif
    extern u16 SEND_TABLE[];
    extern TChannel* CH_BUF;
    extern FxBuf* FX_BUF;
    extern f32 sDSPVolume;
};  // namespace JASDsp

u16 DSP_CreateMap2(u32 msg);
