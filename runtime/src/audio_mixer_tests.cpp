#include "galaxy/audio_mixer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using galaxy::audio::AudioMixer;

int g_failures = 0;

void expect(bool condition, const char* what, double actual = 0.0) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s (actual %g)\n", what, actual);
        ++g_failures;
    }
}

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

constexpr double kPi = 3.14159265358979323846;

// The game's side: 64 voices, 4 effect lines, a sound memory of big-endian waves.
struct Game {
    std::vector<NebulaAudioVoice> voices = std::vector<NebulaAudioVoice>(64);
    std::vector<NebulaAudioEffect> effects = std::vector<NebulaAudioEffect>(4);
    std::vector<std::byte> sound = std::vector<std::byte>(1u << 20);

    void put16(std::uint32_t offset, std::int16_t value) {
        sound[offset] = static_cast<std::byte>((static_cast<std::uint16_t>(value) >> 8u) & 0xFFu);
        sound[offset + 1u] = static_cast<std::byte>(static_cast<std::uint16_t>(value) & 0xFFu);
    }
    void attach(AudioMixer& mixer) {
        mixer.setup(voices.data(), 64u, effects.data(), sound.data(), static_cast<std::uint32_t>(sound.size()));
    }
    // A PCM16 voice over `samples` samples at offset `wave`, one send to `bus` at `volume`.
    NebulaAudioVoice& pcm16(std::uint32_t index, std::uint32_t wave, std::uint32_t samples, std::uint16_t bus,
                            std::int16_t volume, bool loop = false) {
        NebulaAudioVoice& v = voices[index];
        v = {};
        v.active = 1u;
        v.starting = 1u;
        v.pitch = 0x1000u;
        v.format = NEBULA_AUDIO_PCM16;
        v.wave = wave;
        v.end = samples;
        v.loop = loop ? 1u : 0u;
        v.sends[0] = {bus, volume, volume, 0u};
        return v;
    }
};

// Runs `subframes` subframes as one frame; returns the output (left, right).
std::vector<float> run(AudioMixer& mixer, std::uint32_t subframes, float gain = 1.0f) {
    for (std::uint32_t i = 0; i < subframes; ++i) {
        mixer.mix(gain);
    }
    const std::span<const float> out = mixer.end_frame(nullptr, 0u);
    return {out.begin(), out.end()};
}

void test_subframe_lengths() {
    Game game;
    AudioMixer mixer48(48000u);
    game.attach(mixer48);
    expect(run(mixer48, 7u).size() == 7u * 120u * 2u, "48 kHz: 120 samples a subframe");
    AudioMixer mixer44(44100u);
    game.attach(mixer44);
    expect(run(mixer44, 4u).size() == 441u * 2u, "44.1 kHz: 441 samples every 4 subframes");
}

// A constant through a send of 32767 (half the voice) at gain 4: the level the game's mix was made at.
void test_gain_chain() {
    Game game;
    for (std::uint32_t i = 0; i < 4096u; ++i) {
        game.put16(i * 2u, 8192);  // 0.25
    }
    game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_LEFT, 32767);
    AudioMixer mixer(48000u);
    game.attach(mixer);
    const std::vector<float> out = run(mixer, 7u, 4.0f);
    const double expected = 0.25 * 32767.0 / 65536.0 * 4.0;
    expect(near(out[2u * 400u], expected, 1e-4), "left level", out[2u * 400u]);
    expect(near(out[2u * 400u + 1u], 0.0, 1e-6), "right silent", out[2u * 400u + 1u]);
    // 7 subframes of pitch 1.0 read 560 source samples.
    expect(game.voices[0].left == 4096u - 560u, "PCM position", game.voices[0].left);
}

// A send's volume ramps from its current value to its target by 65/80 of the subframe, then holds.
void test_ramp() {
    Game game;
    for (std::uint32_t i = 0; i < 4096u; ++i) {
        game.put16(i * 2u, 16384);
    }
    NebulaAudioVoice& v = game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_LEFT, 0);
    AudioMixer mixer(48000u);
    game.attach(mixer);
    (void)run(mixer, 2u);
    v.sends[0].target = 16384;
    const std::vector<float> out = run(mixer, 1u);
    expect(near(out[0], 0.0, 1e-4), "ramp starts at the current volume", out[0]);
    const double full = 0.5 * 16384.0 / 65536.0;
    expect(near(out[2u * 100u], full, 1e-4), "ramp done by 65/80", out[2u * 100u]);
    expect(near(out[2u * 48u], full * (48.0 * 80.0 / 120.0 - 1.0) / 64.0, 2e-3), "ramp linear", out[2u * 48u]);
    expect(v.sends[0].current == 16384, "current = target after the subframe", v.sends[0].current);
}

// The interpolation kernel keeps the response the game's sound was made with: at a sample position the weights
// are about (0.096, 0.802, 0.104, -0.001), and every phase sums to one.
void test_kernel() {
    const galaxy::audio::InterpolationKernel kernel(4u, 0.753, 7.05, 64u);
    const float* w = kernel.weights(0.0);
    expect(near(w[0], 0.0955, 0.002) && near(w[1], 0.8022, 0.002) && near(w[2], 0.1037, 0.002) &&
               near(w[3], -0.0010, 0.002),
           "kernel at fraction 0", w[1]);
    const float* below = kernel.weights(31.5 / 64.0);
    const float* above = kernel.weights(32.5 / 64.0);
    expect(near(below[0], above[3], 1e-6) && near(below[1], above[2], 1e-6), "kernel symmetric about one half",
           below[1] - above[2]);
    for (double f = 0.0; f < 1.0; f += 0.01) {
        const float* p = kernel.weights(f);
        expect(near(p[0] + p[1] + p[2] + p[3], 1.0, 1e-5), "kernel unity gain", f);
    }
}

// A 1 kHz sine at 32 kHz, played at pitch 1, comes out as 1 kHz at 48 kHz.
void test_resampled_sine() {
    Game game;
    for (std::uint32_t i = 0; i < 32000u; ++i) {
        game.put16(i * 2u, static_cast<std::int16_t>(std::lround(16384.0 * std::sin(2.0 * kPi * 1000.0 * i / 32000.0))));
    }
    game.pcm16(0u, 0u, 32000u, NEBULA_AUDIO_BUS_LEFT, 32767);
    AudioMixer mixer(48000u);
    game.attach(mixer);
    const std::vector<float> out = run(mixer, 70u, 2.0f);
    double re = 0.0;
    double im = 0.0;
    double energy = 0.0;
    const std::size_t from = 2400u;
    const std::size_t count = 4800u;
    for (std::size_t i = from; i < from + count; ++i) {
        const double x = out[i * 2u];
        re += x * std::cos(2.0 * kPi * 1000.0 * i / 48000.0);
        im += x * std::sin(2.0 * kPi * 1000.0 * i / 48000.0);
        energy += x * x;
    }
    const double amplitude = 2.0 * std::sqrt(re * re + im * im) / count;
    const double expected = 0.5 * 32767.0 / 65536.0 * 2.0;
    expect(near(amplitude, expected, expected * 0.01), "1 kHz amplitude", amplitude);
    const double tone_energy = amplitude * amplitude / 2.0 * count;
    expect(energy - tone_energy < tone_energy * 1e-3, "no other content", (energy - tone_energy) / tone_energy);
}

// ADPCM: the record's bookkeeping (next block, samples left, leftover in the block) as the stream player reads it.
void test_adpcm_bookkeeping() {
    Game game;
    for (std::uint32_t b = 0; b < 64u; ++b) {
        game.sound[b * 9u] = std::byte{0x00};  // scale 0, predictor 0: samples are the nibbles
        for (std::uint32_t k = 1; k < 9u; ++k) {
            game.sound[b * 9u + k] = std::byte{0x11};
        }
    }
    NebulaAudioVoice& v = game.voices[0];
    v = {};
    v.active = 1u;
    v.starting = 1u;
    v.pitch = 0x1000u;
    v.format = NEBULA_AUDIO_ADPCM4;
    v.wave = 0u;
    v.end = 1000u;
    v.leftover = 16u;
    v.sends[0] = {NEBULA_AUDIO_BUS_LEFT, 32767, 32767, 0u};
    AudioMixer mixer(48000u);
    game.attach(mixer);
    (void)run(mixer, 1u);
    // 80 samples: 5 blocks.
    expect(v.address == 45u, "next block", v.address);
    expect(v.left == 1000u - 80u, "samples left", v.left);
    expect(v.leftover == 0u, "no leftover", v.leftover);
    v.pitch = 0x0800u;  // 40 samples: two blocks and a half
    (void)run(mixer, 1u);
    expect(v.address == 45u + 27u, "three more blocks", v.address);
    expect(v.leftover == 8u, "half a block left", v.leftover);
    expect(v.left == 1000u - 80u - 48u, "samples left after a partial block", v.left);
    (void)run(mixer, 30u);
    expect(v.finished == 1u, "finishes at the end", v.finished);
}

// A looping PCM voice plays on from its loop start.
void test_pcm_loop() {
    Game game;
    for (std::uint32_t i = 0; i < 100u; ++i) {
        game.put16(i * 2u, 1000);
    }
    NebulaAudioVoice& v = game.pcm16(0u, 0u, 100u, NEBULA_AUDIO_BUS_LEFT, 32767, true);
    v.loop_start = 20u;
    AudioMixer mixer(48000u);
    game.attach(mixer);
    (void)run(mixer, 20u);
    expect(v.finished == 0u, "a loop never finishes", v.finished);
    expect(v.left <= 80u, "inside the loop", v.left);
}

// An effect line: what its input bus gets comes back after its delay, sent to left at its volume, and again
// (through the FIR's feedback) one delay later.
void test_effect_delay() {
    Game game;
    for (std::uint32_t i = 0; i < 4096u; ++i) {
        game.put16(i * 2u, i < 80u ? 8192 : 0);
    }
    game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_EFFECT0, 32767);
    NebulaAudioEffect& e = game.effects[0];
    e.filter = 1u;
    e.subframes = 4u;
    e.bus_a = NEBULA_AUDIO_BUS_LEFT;
    e.volume_a = 16384;
    e.fir[7] = 16384;  // one sample back, half
    e.generation = 1u;
    AudioMixer mixer(48000u);
    game.attach(mixer);
    const std::vector<float> out = run(mixer, 12u);
    double first = 0.0;
    double second = 0.0;
    double before = 0.0;
    for (std::size_t i = 0; i < 480u; ++i) {
        before += std::fabs(out[i * 2u]);
    }
    for (std::size_t i = 480u; i < 960u; ++i) {
        first += std::fabs(out[i * 2u]);
    }
    for (std::size_t i = 960u; i < 1440u; ++i) {
        second += std::fabs(out[i * 2u]);
    }
    expect(before < 1e-6, "nothing before the delay", before);
    expect(first > 0.0, "the echo after the delay", first);
    expect(near(second / first, 0.5, 0.05), "the second echo at the feedback's gain", second / first);
}

// The panner: hard left, front, gives the left bus only; rear content reaches left and right through the surround
// matrix one subframe later.
void test_panner() {
    Game game;
    for (std::uint32_t i = 0; i < 4096u; ++i) {
        game.put16(i * 2u, 16384);
    }
    NebulaAudioVoice& v = game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_NONE, 0);
    v.panner = 1u;
    v.pan = 0u;
    v.rear = 0u;
    v.panner_volume = 16384;
    v.panner_target = 16384;
    AudioMixer mixer(48000u);
    game.attach(mixer);
    std::vector<float> out = run(mixer, 2u);
    const double gain = std::sin(kPi * 127.0 / 256.0) * std::sin(kPi * 127.0 / 256.0);
    expect(near(out[2u * 200u], 0.5 * gain * 16384.0 / 65536.0, 1e-4), "pan left: left", out[2u * 200u]);
    expect(near(out[2u * 200u + 1u], 0.0, 1e-6), "pan left: right silent", out[2u * 200u + 1u]);
    v.rear = 127u;
    out = run(mixer, 3u);
    const double rear = 0.5 * gain * 16384.0 / 65536.0;
    expect(near(out[2u * 300u], rear * 26500.0 / 32768.0 * 32767.0 / 32768.0, 2e-4), "rear left on left",
           out[2u * 300u]);
    expect(near(out[2u * 300u + 1u], -rear * 26500.0 / 32768.0 * 18400.0 / 32768.0, 2e-4), "rear left on right",
           out[2u * 300u + 1u]);
}

// FIR {1 at 8 samples back}: a delay of 8 samples at 32 kHz, 12 at 48 kHz.
void test_fir_delay() {
    Game game;
    for (std::uint32_t i = 0; i < 4096u; ++i) {
        game.put16(i * 2u, i >= 200u ? 16384 : 0);
    }
    NebulaAudioVoice& plain = game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_LEFT, 32767);
    NebulaAudioVoice& filtered = game.pcm16(1u, 0u, 4096u, NEBULA_AUDIO_BUS_RIGHT, 32767);
    filtered.filter = 8u;
    filtered.fir[0] = 32767;
    (void)plain;
    AudioMixer mixer(48000u);
    game.attach(mixer);
    const std::vector<float> out = run(mixer, 7u);
    std::size_t rise_left = 0;
    std::size_t rise_right = 0;
    for (std::size_t i = 0; i < 840u; ++i) {
        if (rise_left == 0 && out[i * 2u] > 0.06f) rise_left = i;
        if (rise_right == 0 && out[i * 2u + 1u] > 0.06f) rise_right = i;
    }
    expect(rise_right == rise_left + 12u, "FIR delay at 48 kHz", static_cast<double>(rise_right) - rise_left);
}

// IIR {0.5, 0.5, 0, 0}: DC passes; the top of the source's band (16 kHz at 32 kHz, its zero) loses more than half.
void test_iir() {
    Game game;
    for (std::uint32_t i = 0; i < 4096u; ++i) {
        game.put16(i * 2u, 16384);
        game.put16(16384u + i * 2u, (i & 1u) != 0u ? 16384 : -16384);
    }
    NebulaAudioVoice& dc = game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_LEFT, 32767);
    NebulaAudioVoice& nyquist = game.pcm16(1u, 16384u, 4096u, NEBULA_AUDIO_BUS_RIGHT, 32767);
    for (NebulaAudioVoice* v : {&dc, &nyquist}) {
        v->filter = 0x20u;
        v->iir[0] = 16384;
        v->iir[1] = 16384;
    }
    AudioMixer mixer(48000u);
    game.attach(mixer);
    const std::vector<float> out = run(mixer, 7u);
    expect(near(out[2u * 600u], 0.5 * 32767.0 / 65536.0, 1e-3), "IIR passes DC", out[2u * 600u]);
    double high = 0.0;
    for (std::size_t i = 400u; i < 800u; ++i) {
        high = std::max(high, std::fabs(static_cast<double>(out[i * 2u + 1u])));
    }
    expect(high < 0.25 * 0.5, "IIR lowers the top", high);
}

// Oscillators: a square of pitch p has p / 131072 cycles per 32 kHz sample.
void test_square() {
    Game game;
    NebulaAudioVoice& v = game.voices[0];
    v = {};
    v.active = 1u;
    v.starting = 1u;
    v.pitch = 0x2000u;  // 0.0625 cycles a 32 kHz sample: 2 kHz
    v.format = NEBULA_AUDIO_OSCILLATOR;
    v.oscillator = NEBULA_AUDIO_SQUARE;
    v.sends[0] = {NEBULA_AUDIO_BUS_LEFT, 32767, 32767, 0u};
    AudioMixer mixer(48000u);
    game.attach(mixer);
    const std::vector<float> out = run(mixer, 10u);
    int changes = 0;
    for (std::size_t i = 1; i < 1200u; ++i) {
        if ((out[i * 2u] > 0.0f) != (out[(i - 1u) * 2u] > 0.0f)) ++changes;
    }
    // 25 ms at 2 kHz: 50 cycles, 100 edges.
    expect(changes >= 99 && changes <= 100, "square frequency", changes);
}

// Volume categories: a muted music category silences a PCM16 voice no owner names.
void test_categories() {
    Game game;
    for (std::uint32_t i = 0; i < 4096u; ++i) {
        game.put16(i * 2u, 16384);
    }
    game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_LEFT, 32767);
    AudioMixer mixer(48000u);
    game.attach(mixer);
    mixer.set_category_gains({1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f});
    const std::vector<float> out = run(mixer, 2u);
    expect(near(out[2u * 100u], 0.0, 1e-6), "muted music", out[2u * 100u]);
    expect(mixer.voice_category(0u).decision.bus == galaxy::audio::NativeAudioBus::Music, "PCM16 is music");
}

// The movie player's 32 kHz stream: a constant comes out at its level, at 48 kHz.
void test_stream() {
    Game game;
    AudioMixer mixer(48000u);
    game.attach(mixer);
    std::vector<std::int16_t> stream(560u * 2u);
    for (std::size_t i = 0; i < 560u; ++i) {
        stream[i * 2u] = 8192;        // right
        stream[i * 2u + 1u] = -8192;  // left
    }
    std::vector<float> out;
    for (int f = 0; f < 3; ++f) {
        for (int s = 0; s < 7; ++s) mixer.mix(1.0f);
        const std::span<const float> frame = mixer.end_frame(stream.data(), 560u);
        out.assign(frame.begin(), frame.end());
    }
    expect(out.size() == 840u * 2u, "stream frame length", static_cast<double>(out.size()));
    expect(near(out[2u * 400u], -0.25, 1e-3), "stream left", out[2u * 400u]);
    expect(near(out[2u * 400u + 1u], 0.25, 1e-3), "stream right", out[2u * 400u + 1u]);
}

// A forced stop halves the volume each subframe and finishes the voice.
void test_forced_stop() {
    Game game;
    for (std::uint32_t i = 0; i < 40960u; ++i) {
        game.put16(i * 2u, 1000);
    }
    NebulaAudioVoice& v = game.pcm16(0u, 0u, 40960u, NEBULA_AUDIO_BUS_LEFT, 12000);
    AudioMixer mixer(48000u);
    game.attach(mixer);
    (void)run(mixer, 1u);
    v.forced_stop = 1u;
    (void)run(mixer, 20u);
    expect(v.finished == 1u, "forced stop finishes", v.finished);
}

}  // namespace

int main() {
    test_subframe_lengths();
    test_gain_chain();
    test_ramp();
    test_kernel();
    test_resampled_sine();
    test_adpcm_bookkeeping();
    test_pcm_loop();
    test_effect_delay();
    test_panner();
    test_fir_delay();
    test_iir();
    test_square();
    test_categories();
    test_stream();
    test_forced_stop();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("audio mixer tests passed");
    return 0;
}
