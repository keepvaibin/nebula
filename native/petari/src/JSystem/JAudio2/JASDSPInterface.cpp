#include "JSystem/JAudio2/JASDspInterface.hpp"
#include "JSystem/JAudio2/JASCalc.hpp"
#include "JSystem/JAudio2/JASCriticalSection.hpp"
#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "JSystem/JAudio2/JASWaveInfo.hpp"
#include "JSystem/JKernel/JKRAram.hpp"
#include "JSystem/JKernel/JKRSolidHeap.hpp"
#include <revolution/os.h>

#include <nebula_game_api.h>

// The mixer is the host's (on the console a DSP program the game mailed command words): JASDsp hands it the channel
// records, each frame's output planes and each channel group's readiness through typed calls (jaudio_*).
extern "C" const NebulaHostServices* nebula_host(void);
void JASAudioThread_postDSPMessage();

// FX_BUF is the API's FX line record.
static_assert(sizeof(JASDsp::FxBuf) == sizeof(NebulaJAudioFxLine), "FxBuf layout");
static_assert(offsetof(JASDsp::FxBuf, _4) == offsetof(NebulaJAudioFxLine, buffer), "FxBuf layout");
static_assert(offsetof(JASDsp::FxBuf, _8) == offsetof(NebulaJAudioFxLine, dest_a), "FxBuf layout");
static_assert(offsetof(JASDsp::FxBuf, _E) == offsetof(NebulaJAudioFxLine, volume_b), "FxBuf layout");
static_assert(offsetof(JASDsp::FxBuf, _10) == offsetof(NebulaJAudioFxLine, coefficients), "FxBuf layout");

namespace {
    // The mixer's output level, 4096 = 1 (DSP_MIXERLEVEL; 4 before the game first sets it).
    u16 sMixerLevel = 0x4000;
}  // namespace

JASDsp::TChannel* JASDsp::CH_BUF;

JASDsp::FxBuf* JASDsp::FX_BUF;

f32 JASDsp::sDSPVolume;

u16 ATTRIBUTE_ALIGN(32) JASDsp::SEND_TABLE[] = {
    0x0D00, 0x0D60, 0x0DC8, 0x0E28, 0x0E88, 0x0EE8, 0x0CA0, 0x0F40, 0x0FA0, 0x0B00, 0x09A0, 0x0000,
};

u32 JASWaveInfo::one = 1;

void JASDsp::boot() {
    static bool sDspInit = true;
    if (sDspInit) {
        const NebulaHostServices* const host = nebula_host();
        const JKRAram* const aram = JKRAram::getManager();
        host->jaudio_aux(host->user, reinterpret_cast< const void* >(JKRHeap::getAltAramStartAdr()),
                         aram->getAudioMemory() + aram->getAudioMemSize());
        sDspInit = false;
    }
}

// Group 3's release mixes a subframe; the audio thread then runs the next (AUDIOMSG_DSP, the DSP's mail).
void JASDsp::releaseHalt(u32 group) {
    const NebulaHostServices* const host = nebula_host();
    if (host->jaudio_mix(host->user, group, DSP_CreateMap2(group))) {
        JASAudioThread_postDSPMessage();
    }
}

void JASDsp::syncFrame(u32 subframes, uintptr_t left, uintptr_t right) {
    const NebulaHostServices* const host = nebula_host();
    host->jaudio_frame(host->user, subframes, sMixerLevel, reinterpret_cast< s16* >(left), reinterpret_cast< s16* >(right));
}

void JASDsp::setDSPMixerLevel(f32 dsp_level) {
    sDSPVolume = dsp_level;
    dsp_level *= 4.0f;
    sMixerLevel = static_cast< u16 >(static_cast< s32 >(4096.0f * dsp_level));
}

f32 JASDsp::getDSPMixerLevel() {
    return sDSPVolume;
}

JASDsp::TChannel* JASDsp::getDSPHandle(int param_0) {
    return CH_BUF + param_0;
}

void JASDsp::setFilterTable(s16* param_0, s16* param_1, u32 param_2) {
    for (int i = 0; i < param_2; i++) {
        *param_0++ = *param_1++;
    }
}

void JASDsp::flushBuffer() {
}

void JASDsp::invalChannelAll() {
}

// The ADPCM decoder's coefficient pairs and the resampler's taps, wave tables and pan law, as the mixer reads them.
s16 const ATTRIBUTE_ALIGN(32) JASDsp::DSPADPCM_FILTER[32] = {
    0, 0, 2048, 0, 0, 2048, 1024, 1024, 4096, -2048, 3584, -1536, 3072, -1024, 4608, -2560,
    4200, -2248, 4800, -2300, 5120, -3072, 2048, -2048, 1024, -1024, -1024, 1024, -1024, 0, -2048, 0,
};

s16 const ATTRIBUTE_ALIGN(32) JASDsp::DSPRES_FILTER[640] = {
    3129, 26285, 3398, -33, 2873, 26262, 3679, -40, 2628, 26217, 3971, -48, 2394, 26150, 4276, -56,
    2173, 26061, 4592, -65, 1963, 25950, 4920, -74, 1764, 25817, 5260, -84, 1576, 25663, 5611, -95,
    1399, 25487, 5974, -106, 1233, 25291, 6347, -118, 1077, 25075, 6732, -130, 932, 24838, 7127, -143,
    796, 24583, 7532, -156, 671, 24309, 7947, -170, 554, 24016, 8371, -184, 446, 23706, 8804, -198,
    347, 23379, 9246, -212, 257, 23036, 9696, -226, 174, 22678, 10153, -240, 99, 22304, 10618, -254,
    31, 21917, 11088, -268, -30, 21517, 11564, -280, -84, 21104, 12045, -293, -132, 20679, 12531, -304,
    -173, 20244, 13020, -314, -210, 19799, 13512, -323, -241, 19345, 14006, -330, -267, 18882, 14501, -336,
    -289, 18413, 14997, -340, -306, 17937, 15493, -341, -320, 17456, 15988, -340, -330, 16970, 16480, -337,
    -337, 16480, 16970, -330, -340, 15988, 17456, -320, -341, 15493, 17937, -306, -340, 14997, 18413, -289,
    -336, 14501, 18882, -267, -330, 14006, 19345, -241, -323, 13512, 19799, -210, -314, 13020, 20244, -173,
    -304, 12531, 20679, -132, -293, 12045, 21104, -84, -280, 11564, 21517, -30, -268, 11088, 21917, 31,
    -254, 10618, 22304, 99, -240, 10153, 22678, 174, -226, 9696, 23036, 257, -212, 9246, 23379, 347,
    -198, 8804, 23706, 446, -184, 8371, 24016, 554, -170, 7947, 24309, 671, -156, 7532, 24583, 796,
    -143, 7127, 24838, 932, -130, 6732, 25075, 1077, -118, 6347, 25291, 1233, -106, 5974, 25487, 1399,
    -95, 5611, 25663, 1576, -84, 5260, 25817, 1764, -74, 4920, 25950, 1963, -65, 4592, 26061, 2173,
    -56, 4276, 26150, 2394, -48, 3971, 26217, 2628, -40, 3679, 26262, 2873, -33, 3398, 26285, 3129,
    0, 3211, 6392, 9511, 12539, 15446, 18204, 20787, 23170, 25329, 27245, 28898, 30273, 31356, 32137, 32609,
    32767, 32609, 32137, 31356, 30273, 28898, 27245, 25329, 23170, 20787, 18204, 15446, 12539, 9511, 6392, 3211,
    0, -3211, -6392, -9511, -12539, -15446, -18204, -20787, -23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609,
    -32767, -32609, -32137, -31356, -30273, -28898, -27245, -25329, -23170, -20787, -18204, -15446, -12539, -9511, -6392, -3211,
    0, 2047, 4095, 6143, 8191, 10239, 12287, 14335, 16383, 18431, 20479, 22527, 24575, 26623, 28671, 30719,
    32767, 30720, 28672, 26624, 24576, 22528, 20480, 18432, 16384, 14336, 12288, 10240, 8192, 6144, 4096, 2048,
    0, -2047, -4095, -6143, -8191, -10239, -12287, -14335, -16383, -18431, -20479, -22527, -24575, -26623, -28671, -30719,
    -32767, -30720, -28672, -26624, -24576, -22528, -20480, -18432, -16384, -14336, -12288, -10240, -8192, -6144, -4096, -2048,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8191, 16383,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8191, -16383,
    0, 402, 804, 1206, 1607, 2009, 2410, 2811, 3211, 3611, 4011, 4409, 4807, 5205, 5601, 5997,
    6392, 6786, 7179, 7571, 7961, 8351, 8739, 9126, 9511, 9895, 10278, 10659, 11039, 11416, 11792, 12167,
    12539, 12910, 13278, 13645, 14009, 14372, 14732, 15090, 15446, 15799, 16151, 16499, 16845, 17189, 17530, 17868,
    18204, 18537, 18867, 19195, 19519, 19841, 20159, 20475, 20787, 21096, 21402, 21705, 22005, 22301, 22594, 22884,
    23170, 23452, 23731, 24007, 24279, 24547, 24811, 25072, 25329, 25582, 25832, 26077, 26319, 26556, 26790, 27019,
    27245, 27466, 27683, 27896, 28105, 28310, 28510, 28706, 28898, 29085, 29268, 29447, 29621, 29791, 29956, 30117,
    30273, 30424, 30571, 30714, 30852, 30985, 31113, 31237, 31356, 31470, 31580, 31685, 31785, 31880, 31971, 32057,
    32137, 32213, 32285, 32351, 32412, 32469, 32521, 32567, 32609, 32646, 32678, 32705, 32728, 32745, 32757, 32765,
};

void JASDsp::initBuffer() {
    CH_BUF = new (JASDram, 0x20) TChannel[64];
    FX_BUF = new (JASDram, 0x20) FxBuf[4];
    JASCalc::bzero(CH_BUF, 0x6000);
    JASCalc::bzero(FX_BUF, sizeof(FxBuf) * 4);
    for (u8 i = 0; i < 4; i++) {
        setFXLine(i, NULL, NULL);
    }
    const NebulaHostServices* const host = nebula_host();
    host->jaudio_setup(host->user, 0x40, CH_BUF, DSPRES_FILTER, DSPADPCM_FILTER, reinterpret_cast< NebulaJAudioFxLine* >(FX_BUF));
    flushBuffer();
}

int JASDsp::setFXLine(u8 param_0, s16* buffer, JASDsp::FxlineConfig_* param_2) {
    FxBuf* puVar3 = FX_BUF + param_0;
    JASCriticalSection aJStack_20;
    puVar3->_0 = 0;
    if (param_2 != NULL) {
        puVar3->_A = param_2->_4;
        puVar3->_8 = SEND_TABLE[param_2->_2];
        puVar3->_E = param_2->_8;
        puVar3->_C = SEND_TABLE[param_2->_6];
        puVar3->_2 = param_2->_C;
        setFilterTable((s16*)puVar3->_10, param_2->_10, 8);
    }
    if (buffer != NULL && param_2 != NULL) {
        u32 bufsize = param_2->_C * 0xa0;
        puVar3->_4 = buffer;
        JASCalc::bzero(buffer, bufsize);
    } else if (param_2 == NULL || buffer != NULL) {
        puVar3->_4 = buffer;
    }

    if (puVar3->_4 != 0) {
        puVar3->_0 = param_2->_0;
    } else {
        puVar3->_0 = 0;
    }
    return 1;
}

void JASDsp::TChannel::init() {
    mPauseFlag = 0;
    mIsFinished = 0;
    mForcedStop = 0;
    mIsActive = 0;
    _58 = 0;
    _68 = 0;
    initFilter();
}

void JASDsp::TChannel::playStart() {
    _10C = 0;
    _60 = 0;
    _8 = 1;
    _66 = 0;
    int i;
    for (i = 0; i < 4; i++) {
        _78[i] = 0;
        _A8[i] = 0;
    }
    for (i = 0; i < 20; i++) {
        _80[i] = 0;
    }
    mIsActive = 1;
}

void JASDsp::TChannel::playStop() {
    mIsActive = 0;
}

void JASDsp::TChannel::replyFinishRequest() {
    mIsFinished = 0;
    mIsActive = 0;
}

void JASDsp::TChannel::forceStop() {
    mForcedStop = 1;
}

bool JASDsp::TChannel::isActive() const {
    return mIsActive != 0;
}

bool JASDsp::TChannel::isFinish() const {
    return mIsFinished != 0;
}

void JASDsp::TChannel::setWaveInfo(JASWaveInfo const& waveInfo, u32 param_1, u32 param_2) {
    int i;
    _118 = param_1;
    static const u8 COMP_BLOCKSAMPLES[8] = {0x10, 0x10, 0x01, 0x01, 0x01, 0x10, 0x10, 0x01};
    _64 = COMP_BLOCKSAMPLES[waveInfo.mFormat];
    static const u8 COMP_BLOCKBYTES[8] = {0x09, 0x05, 0x08, 0x10, 0x01, 0x01, 0x01, 0x01};
    _100 = COMP_BLOCKBYTES[waveInfo.mFormat];
    _68 = 0;
    if (_100 >= 4) {
        _11C = waveInfo.mSampleCount;
        _102 = waveInfo.mLoopFlags;
        if (_102 != 0) {
            if (param_2 == 1) {
                param_2 = waveInfo.mSampleLoopStart;
            }
            _110 = waveInfo.mSampleLoopStart;
            _114 = waveInfo.mSampleLoopEnd;
            _104 = waveInfo.mpLastSample;
            _106 = waveInfo.mpPenultSample;
        } else {
            _114 = _11C;
        }
        if (param_2 && _114 > param_2) {
            switch (waveInfo.mFormat) {
            case 0:
            case 1:
                _68 = param_2;
                _118 += param_2 * _100 >> 4;
                _110 -= param_2;
                _114 -= param_2;
                break;
            case 2:
            case 3:
                _68 = param_2;
                break;
            }
        }
        for (i = 0; i < 16; i++) {
            _B0[i] = 0;
        }
    }
}

void JASDsp::TChannel::setOscInfo(u32 param_0) {
    _118 = 0;
    _64 = 16;
    _100 = param_0;
}

void JASDsp::TChannel::initAutoMixer() {
    if (_58) {
        _54 = _56;
    } else {
        _54 = 0;
        _58 = 1;
    }
}

void JASDsp::TChannel::setAutoMixer(u16 param_0, u8 param_1, u8 param_2, u8 param_3, u8 param_4) {
    _50 = (param_1 << 8) | param_2;
    _52 = param_3 << 8 | param_3 << 1;
    _56 = param_0;
    _58 = 1;
}

void JASDsp::TChannel::setPitch(u16 pitch) {
    if (pitch >= 0x7fff) {
        pitch = 0x7fff;
    }
    mPitch = pitch;
}

void JASDsp::TChannel::setMixerInitVolume(u8 param_0, s16 param_1) {
    u16* tmp = _10[param_0];
    tmp[2] = param_1;
    tmp[1] = param_1;
    tmp[3] = 0;
}

void JASDsp::TChannel::setMixerVolume(u8 param_0, s16 param_1) {
    u16* tmp;
    if (mForcedStop == 0) {
        tmp = _10[param_0];
        tmp[1] = param_1;
        tmp[3] &= 0xff;
    }
}

void JASDsp::TChannel::setPauseFlag(u8 flag) {
    mPauseFlag = flag;
}

void JASDsp::TChannel::flush() {
}

void JASDsp::TChannel::initFilter() {
    u32 i;
    for (i = 0; i < 8; i++) {
        fir_filter_params[i] = 0;
    }
    fir_filter_params[0] = 0x7fff;
    for (i = 0; i < 8; i++) {
        iir_filter_params[i] = 0;
    }
    iir_filter_params[0] = 0x7fff;
    iir_filter_params[4] = 0;
}

void JASDsp::TChannel::setFilterMode(u16 param_0) {
    u8 r30 = param_0 & 0x20;
    u8 r31 = param_0 & 0x1f;
    if (r30) {
        if (r31 > 0x14) {
            r31 = 0x14;
        }
    } else {
        if (r31 > 0x18) {
            r31 = 0x18;
        }
    }
    mFilterMode = r30 + r31;
}

void JASDsp::TChannel::setIIRFilterParam(s16* param_0) {
    setFilterTable(iir_filter_params, param_0, 8);
}

void JASDsp::TChannel::setFIR8FilterParam(s16* param_0) {
    setFilterTable(fir_filter_params, param_0, 8);
}

void JASDsp::TChannel::setDistFilter(s16 param_0) {
    iir_filter_params[4] = param_0;
}

void JASDsp::TChannel::setBusConnect(u8 param_0, u8 param_1) {
    u16* tmp = _10[param_0];
    static u16 const connect_table[12] = {
        0x0000, 0x0D00, 0x0D60, 0x0DC0, 0x0E20, 0x0E80, 0x0EE0, 0x0CA0, 0x0F40, 0x0FA0, 0x0B00, 0x09A0,
    };
    u16 r30 = 0;
    r30 = connect_table[param_1];
    tmp[0] = r30;
}

u16 DSP_CreateMap2(u32 param_0) {
    u16 r30 = 0;
    JASDsp::TChannel* channel = &JASDsp::CH_BUF[param_0 << 4];
    for (int i = 0; i < 16; i++) {
        r30 <<= 1;
        if (channel->isActive()) {
            r30 |= 1;
        }
        channel++;
    }
    return r30;
}
