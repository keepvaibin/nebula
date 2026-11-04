#include "JSystem/JAudio2/JASVoice.hpp"
#include "JSystem/JAudio2/JASCalc.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "JSystem/JAudio2/JASWaveInfo.hpp"
#include "Game/System/NebulaArena.hpp"

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

static_assert(sizeof(JASVoice) == sizeof(NebulaVoice), "a voice is the API's record");
static_assert(sizeof(JASEffectLine) == sizeof(NebulaEffectLine), "an effect line is the API's record");

namespace {
    // The output gain: four times the game's level (4 before the game first sets it).
    f32 sMixerGain = 4.0f;
    // An effect line has been started.
    bool sEffectStarted[4];

    // The buses an effect line's sends name (FxlineConfig_::_2 and _6).
    const u16 sEffectDestinations[12] = {
        NEBULA_AUDIO_BUS_LEFT,           NEBULA_AUDIO_BUS_RIGHT,          NEBULA_AUDIO_BUS_EFFECT0,
        NEBULA_AUDIO_BUS_EFFECT1,        NEBULA_AUDIO_BUS_EFFECT2,        NEBULA_AUDIO_BUS_EFFECT3,
        NEBULA_AUDIO_BUS_SURROUND_RIGHT, NEBULA_AUDIO_BUS_SURROUND_LEFT,  NEBULA_AUDIO_BUS_RIGHT_INVERTED,
        NEBULA_AUDIO_BUS_DISCARD,        NEBULA_AUDIO_BUS_CENTER,         NEBULA_AUDIO_BUS_NONE,
    };
}  // namespace

JASVoice* JASVoice::sVoices;

JASEffectLine* JASVoice::sEffects;

f32 JASVoice::sOutputLevel;

u32 JASWaveInfo::one = 1;

void JASVoice::mixSubframe(u32 used) {
    const NebulaHostServices* const host = nebula_host();
    host->voices_mix(host->user, sMixerGain, used);
}

void JASVoice::setOutputLevel(f32 dsp_level) {
    sOutputLevel = dsp_level;
    sMixerGain = 4.0f * dsp_level;
}

f32 JASVoice::getOutputLevel() {
    return sOutputLevel;
}

JASVoice* JASVoice::getVoice(int index) {
    return sVoices + index;
}

void JASVoice::initAll() {
    sVoices = new (JASDram, 0x20) JASVoice[cVoices];
    sEffects = new (JASDram, 0x20) JASEffectLine[4];
    JASCalc::bzero(sVoices, sizeof(JASVoice) * cVoices);
    JASCalc::bzero(sEffects, sizeof(JASEffectLine) * 4);
    for (u8 i = 0; i < 4; i++) {
        setEffectLine(i, false, nullptr);
    }
    const NebulaHostServices* const host = nebula_host();
    host->voices_setup(host->user, sVoices, cVoices, sEffects);
}

// Settings change, the line's delay kept; with a buffer the line starts over, silent. Without either the line is off.
int JASVoice::setEffectLine(u8 line, bool restart, EffectConfig* config) {
    JASEffectLine& effect = sEffects[line];
    GameAudioExclusion cs;
    if (config != NULL) {
        effect.volume_a = config->_4 / 32768.0f;
        effect.bus_a = sEffectDestinations[config->_2];
        effect.volume_b = config->_8 / 32768.0f;
        effect.bus_b = sEffectDestinations[config->_6];
        effect.subframes = config->_C;
        for (int i = 0; i < 8; i++) {
            effect.fir[i] = config->_10[i] / 32768.0f;
        }
    }
    if (restart && config != nullptr) {
        sEffectStarted[line] = true;
        effect.generation++;
    } else if (config == nullptr) {
        sEffectStarted[line] = restart;
    }
    effect.filter = sEffectStarted[line] && config != nullptr ? config->_0 : 0;
    return 1;
}

void JASVoice::init() {
    paused = 0;
    finished = 0;
    forced_stop = 0;
    active = 0;
    panner = 0;
    restart = 0;
    category = NEBULA_SOUND_EFFECT;
    named = 0;
    initFilter();
}

void JASVoice::playStart() {
    starting = 1;
    active = 1;
}

void JASVoice::playStop() {
    active = 0;
}

void JASVoice::replyFinishRequest() {
    finished = 0;
    active = 0;
}

void JASVoice::forceStop() {
    forced_stop = 1;
}

bool JASVoice::isFinish() const {
    return finished != 0;
}

// `wave_data`: the wave's data in the sound memory, PCM16; `skip`: samples to start into it (1: from its loop start). A voice nobody
// names follows its wave: a wave that was PCM16 on the disc is music (JASVoiceSlot::nameSound).
void JASVoice::setWaveInfo(JASWaveInfo const& waveInfo, const u8* wave_data, u32 skip) {
    const u8 kind = waveInfo.mFormat & 7;
    wave = wave_data;
    format = kind < 4 ? NEBULA_AUDIO_PCM16 : NEBULA_AUDIO_SILENT;
    category = kind == 3 ? NEBULA_SOUND_MUSIC : NEBULA_SOUND_EFFECT;
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
    } else {
        end = waveInfo.mSampleCount;
    }
    if (skip != 0 && end > skip) {
        restart = skip;
    }
}

void JASVoice::setOscInfo(u32 waveform) {
    wave = nullptr;
    format = NEBULA_AUDIO_OSCILLATOR;
    category = NEBULA_SOUND_EFFECT;
    oscillator = waveform;
}

void JASVoice::initAutoMixer() {
    if (panner) {
        panner_volume = panner_target;
    } else {
        panner_volume = 0;
        panner = 1;
    }
}

void JASVoice::setAutoMixer(f32 volume, f32 pan_position, f32 rear_position, f32 send) {
    pan = pan_position;
    rear = rear_position;
    panner_send = send;
    panner_target = volume;
    panner = 1;
}

// `samples`: source samples a second; an oscillator's cycles a second (a constant's value: this over 8000).
void JASVoice::setRate(f32 samples) {
    rate = format == NEBULA_AUDIO_OSCILLATOR && oscillator == NEBULA_AUDIO_CONSTANT ? samples / 8000.0f : samples;
}

void JASVoice::setMixerInitVolume(u8 send, f32 volume) {
    sends[send].current = volume;
    sends[send].target = volume;
}

void JASVoice::setMixerVolume(u8 send, f32 volume) {
    if (forced_stop == 0) {
        sends[send].target = volume;
    }
}

void JASVoice::setPauseFlag(u8 flag) {
    paused = flag;
}

void JASVoice::initFilter() {
    for (int i = 0; i < 8; i++) {
        fir[i] = 0.0f;
    }
    fir[0] = 32767.0f / 32768.0f;
    for (int i = 0; i < 4; i++) {
        iir[i] = 0.0f;
    }
    iir[0] = 32767.0f / 32768.0f;
}

void JASVoice::setFilterMode(u16 mode) {
    const u8 iir_on = mode & 0x20;
    u8 taps = mode & 0x1f;
    if (taps > (iir_on ? 0x14 : 0x18)) {
        taps = iir_on ? 0x14 : 0x18;
    }
    fir_taps = taps;
    this->iir_on = iir_on != 0;
}

void JASVoice::setIIRFilterParam(s16* coefficients) {
    for (int i = 0; i < 4; i++) {
        iir[i] = coefficients[i] / 32768.0f;
    }
}

void JASVoice::setFIR8FilterParam(s16* coefficients) {
    for (int i = 0; i < 8; i++) {
        fir[i] = coefficients[i] / 32768.0f;
    }
}

// The bus a send feeds: the mix configuration's value is the bus (NEBULA_AUDIO_BUS_*).
void JASVoice::setBusConnect(u8 send, u8 bus) {
    sends[send].bus = bus < NEBULA_AUDIO_BUSES ? bus : static_cast< u32 >(NEBULA_AUDIO_BUS_NONE);
}
