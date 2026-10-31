#include "JSystem/JAudio2/JASDspInterface.hpp"
#include "JSystem/JAudio2/JASCalc.hpp"
#include "JSystem/JAudio2/JASCriticalSection.hpp"
#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "JSystem/JAudio2/JASWaveInfo.hpp"
#include "JSystem/JKernel/JKRSolidHeap.hpp"

#include <nebula_game_api.h>

// The voices and effect lines are records the host's mixer reads and advances (audio_setup, audio_mix): the game
// sets each voice's source, pitch, volumes and filters here, and the mixer plays one subframe per mixSubframe.
extern "C" const NebulaHostServices* nebula_host(void);

static_assert(sizeof(JASDsp::TChannel) == sizeof(NebulaAudioVoice), "a voice is the API's record");
static_assert(sizeof(JASDsp::FxBuf) == sizeof(NebulaAudioEffect), "an effect line is the API's record");

namespace {
    // The output gain: four times the game's DSP level (4 before the game first sets it).
    f32 sMixerGain = 4.0f;
    // An effect line has its delay memory from the game (AudEffector's buffers).
    bool sEffectHasBuffer[4];

    // The buses an effect line's sends name (FxlineConfig_::_2 and _6).
    const u16 sEffectDestinations[12] = {
        NEBULA_AUDIO_BUS_LEFT,           NEBULA_AUDIO_BUS_RIGHT,          NEBULA_AUDIO_BUS_EFFECT0,
        NEBULA_AUDIO_BUS_EFFECT1,        NEBULA_AUDIO_BUS_EFFECT2,        NEBULA_AUDIO_BUS_EFFECT3,
        NEBULA_AUDIO_BUS_SURROUND_RIGHT, NEBULA_AUDIO_BUS_SURROUND_LEFT,  NEBULA_AUDIO_BUS_RIGHT_INVERTED,
        NEBULA_AUDIO_BUS_DISCARD,        NEBULA_AUDIO_BUS_CENTER,         NEBULA_AUDIO_BUS_NONE,
    };
}  // namespace

JASDsp::TChannel* JASDsp::sVoices;

JASDsp::FxBuf* JASDsp::sEffects;

f32 JASDsp::sDSPVolume;

u32 JASWaveInfo::one = 1;

void JASDsp::mixSubframe() {
    const NebulaHostServices* const host = nebula_host();
    host->audio_mix(host->user, sMixerGain);
}

void JASDsp::setDSPMixerLevel(f32 dsp_level) {
    sDSPVolume = dsp_level;
    sMixerGain = 4.0f * dsp_level;
}

f32 JASDsp::getDSPMixerLevel() {
    return sDSPVolume;
}

JASDsp::TChannel* JASDsp::getDSPHandle(int index) {
    return sVoices + index;
}

void JASDsp::initBuffer() {
    sVoices = new (JASDram, 0x20) TChannel[64];
    sEffects = new (JASDram, 0x20) FxBuf[4];
    JASCalc::bzero(sVoices, sizeof(TChannel) * 64);
    JASCalc::bzero(sEffects, sizeof(FxBuf) * 4);
    for (u8 i = 0; i < 4; i++) {
        setFXLine(i, NULL, NULL);
    }
    JASHeap* const sound = JASKernel::getAramHeap();
    const NebulaHostServices* const host = nebula_host();
    host->audio_setup(host->user, sVoices, 64, sEffects, sound->getBase(), sound->getSize());
}

// Settings change, the line's delay kept; with a buffer the line starts over, silent. Without either the line is off.
int JASDsp::setFXLine(u8 line, s16* buffer, JASDsp::FxlineConfig_* config) {
    FxBuf& effect = sEffects[line];
    JASCriticalSection cs;
    if (config != NULL) {
        effect.volume_a = config->_4;
        effect.bus_a = sEffectDestinations[config->_2];
        effect.volume_b = config->_8;
        effect.bus_b = sEffectDestinations[config->_6];
        effect.subframes = config->_C;
        for (int i = 0; i < 8; i++) {
            effect.fir[i] = config->_10[i];
        }
    }
    if (buffer != NULL && config != NULL) {
        sEffectHasBuffer[line] = true;
        effect.generation++;
    } else if (config == NULL) {
        sEffectHasBuffer[line] = buffer != NULL;
    }
    effect.filter = sEffectHasBuffer[line] && config != NULL ? config->_0 : 0;
    return 1;
}

void JASDsp::TChannel::init() {
    paused = 0;
    finished = 0;
    forced_stop = 0;
    active = 0;
    panner = 0;
    restart = 0;
    sound_route = 0;
    sound_id = 0;
    initFilter();
}

void JASDsp::TChannel::playStart() {
    starting = 1;
    active = 1;
}

void JASDsp::TChannel::playStop() {
    active = 0;
}

void JASDsp::TChannel::replyFinishRequest() {
    finished = 0;
    active = 0;
}

void JASDsp::TChannel::forceStop() {
    forced_stop = 1;
}

bool JASDsp::TChannel::isActive() const {
    return active != 0;
}

bool JASDsp::TChannel::isFinish() const {
    return finished != 0;
}

// `wave_data`: the wave's data in the sound memory; `skip`: samples to start into it (1: from its loop start).
void JASDsp::TChannel::setWaveInfo(JASWaveInfo const& waveInfo, const u8* wave_data, u32 skip) {
    static const u16 FORMATS[8] = {
        NEBULA_AUDIO_ADPCM4, NEBULA_AUDIO_ADPCM2, NEBULA_AUDIO_PCM8,   NEBULA_AUDIO_PCM16,
        NEBULA_AUDIO_SILENT, NEBULA_AUDIO_SILENT, NEBULA_AUDIO_SILENT, NEBULA_AUDIO_SILENT,
    };
    static const u8 BLOCK_SAMPLES[8] = {16, 16, 1, 1, 1, 16, 16, 1};
    static const u8 BLOCK_BYTES[8] = {9, 5, 1, 2, 0, 0, 0, 0};
    const u8 kind = waveInfo.mFormat & 7;
    wave = wave_data;
    leftover = BLOCK_SAMPLES[kind];
    format = FORMATS[kind];
    restart = 0;
    if (format == NEBULA_AUDIO_SILENT) {
        return;
    }
    loop = static_cast< u16 >(waveInfo.mLoopFlags);
    if (loop) {
        if (skip == 1) {
            skip = waveInfo.mSampleLoopStart;
        }
        loop_start = waveInfo.mSampleLoopStart;
        end = waveInfo.mSampleLoopEnd;
        loop_history[0] = waveInfo.mpLastSample;
        loop_history[1] = waveInfo.mpPenultSample;
    } else {
        end = waveInfo.mSampleCount;
    }
    if (skip != 0 && end > skip) {
        restart = skip;
        if (format == NEBULA_AUDIO_ADPCM4 || format == NEBULA_AUDIO_ADPCM2) {
            wave += skip * BLOCK_BYTES[kind] >> 4;
            loop_start -= skip;
            end -= skip;
        }
    }
}

void JASDsp::TChannel::setOscInfo(u32 waveform) {
    wave = nullptr;
    leftover = 16;
    format = NEBULA_AUDIO_OSCILLATOR;
    oscillator = waveform;
}

void JASDsp::TChannel::initAutoMixer() {
    if (panner) {
        panner_volume = panner_target;
    } else {
        panner_volume = 0;
        panner = 1;
    }
}

void JASDsp::TChannel::setAutoMixer(u16 volume, u8 pan_position, u8 rear_position, u8 send, u8) {
    pan = pan_position;
    rear = rear_position;
    panner_send = send << 8 | send << 1;
    panner_target = volume;
    panner = 1;
}

void JASDsp::TChannel::setPitch(u16 value) {
    if (value >= 0x7fff) {
        value = 0x7fff;
    }
    pitch = value;
}

void JASDsp::TChannel::setMixerInitVolume(u8 send, s16 volume) {
    sends[send].current = volume;
    sends[send].target = volume;
}

void JASDsp::TChannel::setMixerVolume(u8 send, s16 volume) {
    if (forced_stop == 0) {
        sends[send].target = volume;
    }
}

void JASDsp::TChannel::setPauseFlag(u8 flag) {
    paused = flag;
}

void JASDsp::TChannel::initFilter() {
    for (int i = 0; i < 8; i++) {
        fir[i] = 0;
    }
    fir[0] = 0x7fff;
    for (int i = 0; i < 4; i++) {
        iir[i] = 0;
    }
    iir[0] = 0x7fff;
}

void JASDsp::TChannel::setFilterMode(u16 mode) {
    const u8 iir_on = mode & 0x20;
    u8 taps = mode & 0x1f;
    if (taps > (iir_on ? 0x14 : 0x18)) {
        taps = iir_on ? 0x14 : 0x18;
    }
    filter = iir_on + taps;
}

void JASDsp::TChannel::setIIRFilterParam(s16* coefficients) {
    for (int i = 0; i < 4; i++) {
        iir[i] = coefficients[i];
    }
}

void JASDsp::TChannel::setFIR8FilterParam(s16* coefficients) {
    for (int i = 0; i < 8; i++) {
        fir[i] = coefficients[i];
    }
}

// The bus a send feeds: the mix configuration's value is the bus (NEBULA_AUDIO_BUS_*).
void JASDsp::TChannel::setBusConnect(u8 send, u8 bus) {
    sends[send].bus = bus < NEBULA_AUDIO_BUSES ? bus : NEBULA_AUDIO_BUS_NONE;
}
