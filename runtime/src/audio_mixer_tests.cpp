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
    std::vector<NebulaVoice> voices = std::vector<NebulaVoice>(64);
    std::vector<NebulaEffectLine> effects = std::vector<NebulaEffectLine>(4);
    std::vector<std::byte> sound = std::vector<std::byte>(1u << 20);

    void put16(std::uint32_t offset, std::int16_t value) {
        std::memcpy(&sound[offset], &value, sizeof(value));
    }
    void attach(AudioMixer& mixer) {
        mixer.setup(voices.data(), 64u, effects.data());
    }
    // A PCM16 voice over `samples` samples at offset `wave`, one send to `bus` at `volume`.
    NebulaVoice& pcm16(std::uint32_t index, std::uint32_t wave, std::uint32_t samples, std::uint16_t bus,
                            std::int16_t volume, bool loop = false) {
        NebulaVoice& v = voices[index];
        v = {};
        v.active = 1u;
        v.starting = 1u;
        v.rate = 32000.0f;
        v.format = NEBULA_AUDIO_PCM16;
        v.wave = reinterpret_cast<const std::uint8_t*>(sound.data()) + wave;
        v.wave_end = reinterpret_cast<const std::uint8_t*>(sound.data()) + sound.size();
        v.end = samples;
        v.loop = loop ? 1u : 0u;
        v.sends[0] = {bus, volume / 65536.0f, volume / 65536.0f};
        return v;
    }
};

// Runs `subframes` subframes as one frame; returns the output (left, right).
std::vector<float> run(AudioMixer& mixer, std::uint32_t subframes, float gain = 1.0f) {
    for (std::uint32_t i = 0; i < subframes; ++i) {
        mixer.mix(gain);
    }
    const std::span<const float> out = mixer.end_frame();
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
    NebulaVoice& v = game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_LEFT, 0);
    AudioMixer mixer(48000u);
    game.attach(mixer);
    (void)run(mixer, 2u);
    v.sends[0].target = 0.25f;
    const std::vector<float> out = run(mixer, 1u);
    expect(near(out[0], 0.0, 1e-4), "ramp starts at the current volume", out[0]);
    const double full = 0.5 * 16384.0 / 65536.0;
    expect(near(out[2u * 100u], full, 1e-4), "ramp done by 65/80", out[2u * 100u]);
    expect(near(out[2u * 48u], full * (48.0 * 80.0 / 120.0 - 1.0) / 64.0, 2e-3), "ramp linear", out[2u * 48u]);
    expect(v.sends[0].current == 0.25f, "current = target after the subframe", v.sends[0].current);
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

void test_pcm_loop() {
    Game game;
    for (std::uint32_t i = 0; i < 100u; ++i) {
        game.put16(i * 2u, 1000);
    }
    NebulaVoice& v = game.pcm16(0u, 0u, 100u, NEBULA_AUDIO_BUS_LEFT, 32767, true);
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
    NebulaEffectLine& e = game.effects[0];
    e.filter = 1u;
    e.subframes = 4u;
    e.bus_a = NEBULA_AUDIO_BUS_LEFT;
    e.volume_a = 0.5f;
    e.fir[7] = 0.5f;  // one sample back, half
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
    NebulaVoice& v = game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_NONE, 0);
    v.panner = 1u;
    v.pan = 0.0f;
    v.rear = 0.0f;
    v.panner_volume = 0.25f;
    v.panner_target = 0.25f;
    AudioMixer mixer(48000u);
    game.attach(mixer);
    std::vector<float> out = run(mixer, 2u);
    const double gain = std::sin(kPi * 127.0 / 256.0) * std::sin(kPi * 127.0 / 256.0);
    expect(near(out[2u * 200u], 0.5 * gain * 16384.0 / 65536.0, 1e-4), "pan left: left", out[2u * 200u]);
    expect(near(out[2u * 200u + 1u], 0.0, 1e-6), "pan left: right silent", out[2u * 200u + 1u]);
    v.rear = 1.0f;
    out = run(mixer, 3u);
    const double rear = 0.5 * gain * 16384.0 / 65536.0;
    expect(near(out[2u * 300u], rear * 26500.0 / 32768.0 * 32767.0 / 32768.0, 2e-4), "rear left on left",
           out[2u * 300u]);
    expect(near(out[2u * 300u + 1u], -rear * 26500.0 / 32768.0 * 18400.0 / 32768.0, 2e-4), "rear left on right",
           out[2u * 300u + 1u]);
}

// Its own speakers: a rear voice leaves left and right silent and comes out of surround left, one subframe late.
void test_discrete_surround() {
    Game game;
    for (std::uint32_t i = 0; i < 4096u; ++i) {
        game.put16(i * 2u, 16384);
    }
    NebulaVoice& v = game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_NONE, 0);
    v.panner = 1u;
    v.pan = 0.0f;
    v.rear = 1.0f;
    v.panner_volume = 0.25f;
    v.panner_target = 0.25f;
    AudioMixer mixer(48000u);
    game.attach(mixer);
    mixer.set_discrete(true);
    for (int i = 0; i < 3; ++i) {
        mixer.mix(1.0f);
    }
    const std::span<const float> front = mixer.end_frame();
    const std::span<const float> rear = mixer.surround();
    expect(rear.size() == front.size() / 2u * 3u, "three more channels a frame", static_cast<double>(rear.size()));
    const double gain = std::sin(kPi * 127.0 / 256.0) * std::sin(kPi * 127.0 / 256.0);
    const double level = 0.5 * gain * 16384.0 / 65536.0;
    expect(near(front[2u * 300u], 0.0, 1e-6), "rear voice: left silent", front[2u * 300u]);
    expect(near(front[2u * 300u + 1u], 0.0, 1e-6), "rear voice: right silent", front[2u * 300u + 1u]);
    expect(near(rear[3u * 300u], 0.0, 1e-6), "rear voice: center silent", rear[3u * 300u]);
    expect(near(rear[3u * 300u + 1u], level, 2e-4), "rear voice: surround left", rear[3u * 300u + 1u]);
    expect(near(rear[3u * 300u + 2u], 0.0, 1e-6), "rear voice: surround right silent", rear[3u * 300u + 2u]);
    mixer.set_discrete(false);
    mixer.mix(1.0f);
    (void)mixer.end_frame();
    expect(mixer.surround().empty(), "matrix again: nothing apart", static_cast<double>(mixer.surround().size()));
}

// FIR {1 at 8 samples back}: a delay of 8 samples at 32 kHz, 12 at 48 kHz.
void test_fir_delay() {
    Game game;
    for (std::uint32_t i = 0; i < 4096u; ++i) {
        game.put16(i * 2u, i >= 200u ? 16384 : 0);
    }
    NebulaVoice& plain = game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_LEFT, 32767);
    NebulaVoice& filtered = game.pcm16(1u, 0u, 4096u, NEBULA_AUDIO_BUS_RIGHT, 32767);
    filtered.fir_taps = 8u;
    filtered.fir[0] = 32767.0f / 32768.0f;
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
    NebulaVoice& dc = game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_LEFT, 32767);
    NebulaVoice& nyquist = game.pcm16(1u, 16384u, 4096u, NEBULA_AUDIO_BUS_RIGHT, 32767);
    for (NebulaVoice* v : {&dc, &nyquist}) {
        v->iir_on = 1u;
        v->iir[0] = 0.5f;
        v->iir[1] = 0.5f;
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

// Oscillators: `rate` is the frequency.
void test_square() {
    Game game;
    NebulaVoice& v = game.voices[0];
    v = {};
    v.active = 1u;
    v.starting = 1u;
    v.rate = 2000.0f;
    v.format = NEBULA_AUDIO_OSCILLATOR;
    v.oscillator = NEBULA_AUDIO_SQUARE;
    v.sends[0] = {NEBULA_AUDIO_BUS_LEFT, 32767 / 65536.0f, 32767 / 65536.0f};
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

// Volume categories: a voice follows the setting the game names.
void test_categories() {
    Game game;
    for (std::uint32_t i = 0; i < 4096u; ++i) {
        game.put16(i * 2u, 16384);
    }
    game.pcm16(0u, 0u, 4096u, NEBULA_AUDIO_BUS_LEFT, 32767).category = NEBULA_SOUND_MUSIC;
    AudioMixer mixer(48000u);
    game.attach(mixer);
    mixer.set_category_gains({1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f});
    const std::vector<float> out = run(mixer, 2u);
    expect(near(out[2u * 100u], 0.0, 1e-6), "muted music", out[2u * 100u]);
    // A voice in the voices category follows that gain; music ramps back up over 10 ms.
    NebulaVoice& named = game.pcm16(1u, 0u, 4096u, NEBULA_AUDIO_BUS_RIGHT, 32767);
    named.category = NEBULA_SOUND_VOICE;
    mixer.set_category_gains({1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f});
    const std::vector<float> named_out = run(mixer, 7u);
    expect(near(named_out[2u * 100u + 1u], 0.0, 1e-6), "muted voices", named_out[2u * 100u + 1u]);
    expect(near(named_out[2u * 800u], 0.5 * 32767.0 / 65536.0, 1e-3), "music back on", named_out[2u * 800u]);
}

// A movie frame's audio record: its stereo sample count, then a constant left and right.
std::vector<std::uint8_t> thp_record(std::uint32_t samples, std::int16_t left, std::int16_t right) {
    std::vector<std::uint8_t> record(4u + samples * 4u);
    std::memcpy(record.data(), &samples, sizeof(samples));
    for (std::uint32_t i = 0; i < samples; ++i) {
        std::memcpy(record.data() + 4u + i * 4u, &left, sizeof(left));
        std::memcpy(record.data() + 6u + i * 4u, &right, sizeof(right));
    }
    return record;
}

// A movie record: its stereo sample count, then the samples.
void test_movie_record() {
    galaxy::audio::MovieAudio movie;
    movie.open(false);
    const std::uint32_t samples = 2u;
    std::array<std::uint8_t, 4u + 8u> record{};
    std::memcpy(record.data(), &samples, sizeof(samples));
    const std::array<std::int16_t, 4> pcm{16384, -16384, 8192, -8192};
    std::memcpy(record.data() + 4u, pcm.data(), sizeof(pcm));
    expect(movie.push(record.data(), record.size()), "a record queues");
    expect(movie.push(record.data(), record.size()), "the next queues");
    expect(!movie.push(record.data(), 8u), "a short record is refused");
    std::array<float, 4> out{};
    movie.pull(out.data(), 2u);
    expect(near(out[0], 0.5, 1e-4), "left", out[0]);
    expect(near(out[1], -0.5, 1e-4), "right", out[1]);
    expect(near(out[2], 0.25, 1e-4), "left, next", out[2]);
}

// The room the player reads its sound ahead by; two frames start it; after that any frame plays, so the last plays out.
void test_movie_room() {
    galaxy::audio::MovieAudio movie;
    movie.open(false);
    const std::vector<std::uint8_t> record = thp_record(100u, 2048, -2048);
    expect(movie.room() == galaxy::audio::MovieAudio::kQueueFrames, "an empty queue has all its room");
    expect(movie.push(record.data(), record.size()), "the first frame queues");
    expect(movie.room() == galaxy::audio::MovieAudio::kQueueFrames - 1u, "one frame less room");
    std::vector<float> out(2u * 150u, 0.0f);
    movie.pull(out.data(), 150u);
    expect(out[0] == 0.0f, "one frame waiting: silent", out[0]);
    expect(movie.push(record.data(), record.size()), "the second frame queues");
    movie.pull(out.data(), 150u);
    expect(out[0] != 0.0f, "two frames waiting: it starts", out[0]);
    std::fill(out.begin(), out.end(), 0.0f);
    movie.pull(out.data(), 50u);
    expect(out[0] != 0.0f, "the last frame plays out", out[0]);
    expect(movie.room() == galaxy::audio::MovieAudio::kQueueFrames, "the queue is dry");
    movie.set_unplayed(150u);
    expect(near(movie.position(), 0.5, 1e-9), "half of the first frame played", movie.position());
    movie.set_unplayed(100u);
    expect(near(movie.position(), 1.0, 1e-9), "the first frame played", movie.position());
    movie.set_unplayed(25u);
    expect(near(movie.position(), 1.75, 1e-9), "three quarters of the second", movie.position());
    movie.set_unplayed(0u);
    expect(near(movie.position(), 2.0, 1e-9), "both played", movie.position());
    expect(movie.push(record.data(), record.size()), "another frame queues");
    std::fill(out.begin(), out.end(), 0.0f);
    movie.pull(out.data(), 40u);
    expect(out[0] != 0.0f, "a frame after a dry moment plays at once", out[0]);
    for (std::size_t i = movie.room(); i != 0u; --i) {
        expect(movie.push(record.data(), record.size()), "room for a frame");
    }
    expect(movie.room() == 0u && !movie.push(nullptr, 0u), "a full queue has no room");
}

// Output continues at a fixed latency after the last movie sample. The final picture must reach the end even
// though the device keeps asking for silence. A later frame after an underrun waits for that silence to pass too.
void test_movie_output_clock() {
    galaxy::audio::MovieAudio movie;
    movie.open(false);
    const auto record = thp_record(100u, 2048, -2048);
    expect(movie.push(record.data(), record.size()), "clock: first frame queues");
    expect(movie.push(record.data(), record.size()), "clock: second frame queues");
    std::vector<float> out(2u * 320u, 0.0f);
    movie.set_unplayed(80u);
    movie.pull(out.data(), 200u);
    expect(near(movie.position(), 1.2, 1e-9), "clock: last frame is still in output", movie.position());
    movie.pull(out.data(), 80u);
    expect(near(movie.position(), 2.0, 1e-9), "clock: silence drains the final output delay", movie.position());
    movie.pull(out.data(), 320u);
    expect(near(movie.position(), 2.0, 1e-9), "clock: silence adds no movie frames", movie.position());

    expect(movie.push(record.data(), record.size()), "clock: frame after underrun queues");
    movie.set_unplayed(100u);
    movie.pull(out.data(), 50u);
    expect(near(movie.position(), 2.0, 1e-9), "clock: underrun silence precedes the new frame", movie.position());
    movie.pull(out.data(), 50u);
    expect(near(movie.position(), 2.0, 1e-9), "clock: new frame has just reached output", movie.position());
    movie.pull(out.data(), 50u);
    expect(near(movie.position(), 2.5, 1e-9), "clock: half the new frame is heard", movie.position());
    movie.pull(out.data(), 50u);
    expect(near(movie.position(), 3.0, 1e-9), "clock: new frame finishes", movie.position());

    movie.reset();
    expect(near(movie.position(), 0.0, 1e-9), "clock: reset starts a new timeline", movie.position());
    expect(movie.push(record.data(), record.size()), "clock: first frame after reset queues");
    movie.pull(out.data(), 320u);
    expect(near(movie.position(), 0.0, 1e-9), "clock: startup still waits for two frames", movie.position());
    expect(movie.push(record.data(), record.size()), "clock: second frame after reset queues");
    movie.pull(out.data(), 320u);
    expect(near(movie.position(), 2.0, 1e-9), "clock: final partial block drains", movie.position());
}

// The movie's sound at 48 kHz: its level through the player's volume (a square law), and silent until the frame
// after the current one is decoded too.
void test_stream() {
    Game game;
    AudioMixer mixer(48000u);
    game.attach(mixer);
    galaxy::audio::MovieAudio& movie = mixer.movie();
    movie.open(false);
    const std::vector<std::uint8_t> record = thp_record(560u, 2048, -2048);
    std::vector<float> out;
    expect(movie.push(record.data(), record.size()), "room for a frame");
    for (int s = 0; s < 7; ++s) mixer.mix(1.0f);
    const std::span<const float> first = mixer.end_frame();
    expect(near(first[2u * 400u], 0.0, 1e-6), "one frame waiting: silent", first[2u * 400u]);
    for (int f = 0; f < 3; ++f) {
        expect(movie.push(record.data(), record.size()), "room for a frame");
        for (int s = 0; s < 7; ++s) mixer.mix(1.0f);
        const std::span<const float> frame = mixer.end_frame();
        out.assign(frame.begin(), frame.end());
    }
    expect(out.size() == 840u * 2u, "stream frame length", static_cast<double>(out.size()));
    expect(near(out[2u * 400u], 0.0625, 1e-3), "stream left", out[2u * 400u]);
    expect(near(out[2u * 400u + 1u], -0.0625, 1e-3), "stream right", out[2u * 400u + 1u]);
    movie.set_volume(64, 0);
    for (int f = 0; f < 2; ++f) {
        movie.push(record.data(), record.size());
        for (int s = 0; s < 7; ++s) mixer.mix(1.0f);
        const std::span<const float> frame = mixer.end_frame();
        out.assign(frame.begin(), frame.end());
    }
    expect(near(out[2u * 400u], 0.0625 * 8321.0 / 32768.0, 1e-3), "volume 64", out[2u * 400u]);
    for (int f = 0; f < 30; ++f) {
        if (!movie.push(record.data(), record.size())) break;
    }
    expect(!movie.push(nullptr, 0u), "20 frames waiting: no room");
}

// A forced stop halves the volume each subframe and finishes the voice.
void test_forced_stop() {
    Game game;
    for (std::uint32_t i = 0; i < 40960u; ++i) {
        game.put16(i * 2u, 1000);
    }
    NebulaVoice& v = game.pcm16(0u, 0u, 40960u, NEBULA_AUDIO_BUS_LEFT, 12000);
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
    test_pcm_loop();
    test_effect_delay();
    test_panner();
    test_discrete_surround();
    test_fir_delay();
    test_iir();
    test_square();
    test_categories();
    test_movie_record();
    test_movie_room();
    test_movie_output_clock();
    test_stream();
    test_forced_stop();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("audio mixer tests passed");
    return 0;
}
