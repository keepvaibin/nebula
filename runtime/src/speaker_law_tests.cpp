// The surround law's tests (docs/engine/CONSOLE-FREE.md section 4, points 1 to 11; step G6.7's tests 10 to 12): the law
// over a 101 by 101 grid of pan and rear for every layout, known sounds rendered through the real mixer and the speaker
// stage to a file for every Speakers value on every device layout, and a device that changes between two render calls.
#include "galaxy/audio_mixer.h"
#include "galaxy/speaker_law.h"
#include "galaxy/test_temp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

using galaxy::SpeakerChoice;
using galaxy::audio::AudioMixer;
using galaxy::audio::PlayedLayout;
using galaxy::audio::Speaker;
using galaxy::audio::SpeakerStage;

int g_failures = 0;

[[maybe_unused]] void expect(bool condition, const std::string& what, double actual = 0.0) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s (actual %.9g)\n", what.c_str(), actual);
        ++g_failures;
    }
}

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint32_t kRate = 48000u;

// The scene of test 10 c: a sends voice with an effect send, a panner voice with a rear amount and echo, and sends to the
// surround buses, the centre and the inverted right; 48 subframes at 48 kHz. Its two channels are hashed.
std::uint64_t two_channel_hash(bool mono) {
    std::vector<NebulaVoice> voices(64);
    std::vector<NebulaEffectLine> effects(4);
    std::vector<std::int16_t> sound(32000);
    for (std::size_t i = 0; i < sound.size(); ++i) {
        sound[i] = static_cast<std::int16_t>(std::lround(12000.0 * std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) / 32000.0)));
    }
    const auto voice = [&](std::uint32_t index) -> NebulaVoice& {
        NebulaVoice& v = voices[index];
        v = {};
        v.active = 1u;
        v.starting = 1u;
        v.rate = 32000.0f;
        v.format = NEBULA_AUDIO_PCM16;
        v.wave = reinterpret_cast<const std::uint8_t*>(sound.data());
        v.wave_end = reinterpret_cast<const std::uint8_t*>(sound.data() + sound.size());
        v.end = static_cast<std::uint32_t>(sound.size());
        v.loop = 1u;
        return v;
    };
    NebulaVoice& sends = voice(0u);
    sends.sends[0] = {NEBULA_AUDIO_BUS_LEFT, 0.3f, 0.3f};
    sends.sends[1] = {NEBULA_AUDIO_BUS_RIGHT, 0.2f, 0.2f};
    sends.sends[2] = {NEBULA_AUDIO_BUS_EFFECT0, 0.1f, 0.1f};
    NebulaVoice& panner = voice(1u);
    panner.panner = 1u;
    panner.pan = 0.3f;
    panner.rear = 0.6f;
    panner.panner_volume = 0.4f;
    panner.panner_target = 0.4f;
    panner.panner_send = 0.25f;
    NebulaVoice& surround = voice(2u);
    surround.rate = 24000.0f;
    surround.sends[0] = {NEBULA_AUDIO_BUS_SURROUND_LEFT, 0.2f, 0.2f};
    surround.sends[1] = {NEBULA_AUDIO_BUS_CENTER, 0.15f, 0.15f};
    surround.sends[2] = {NEBULA_AUDIO_BUS_RIGHT_INVERTED, 0.1f, 0.1f};
    surround.sends[3] = {NEBULA_AUDIO_BUS_SURROUND_RIGHT, 0.05f, 0.05f};
    effects[0] = {1u, 4u, NEBULA_AUDIO_BUS_LEFT, NEBULA_AUDIO_BUS_RIGHT, 0.5f, 0.25f, {0, 0, 0, 0, 0, 0, 0, 0.5f}, 1u};
    effects[2] = {1u, 3u, NEBULA_AUDIO_BUS_LEFT, NEBULA_AUDIO_BUS_RIGHT, 0.875f, 0.0f, {0, 0, 0, 0, 0, 0, 0.25f, 0}, 1u};
    effects[3] = {1u, 3u, NEBULA_AUDIO_BUS_LEFT, NEBULA_AUDIO_BUS_RIGHT, 0.0f, 0.875f, {0, 0, 0, 0, 0, 0, 0.25f, 0}, 1u};
    AudioMixer mixer(kRate);
    mixer.setup(voices.data(), 64u, effects.data());
    std::uint64_t hash = 1469598103934665603ull;
    for (int frame = 0; frame < 6; ++frame) {
        for (int i = 0; i < 8; ++i) {
            mixer.mix(1.0f);
        }
        const std::span<const float> out = mixer.end_frame();
        for (std::size_t f = 0; f < out.size() / 2u; ++f) {
            float pair[2] = {out[f * 2u], out[f * 2u + 1u]};
            if (mono) {
                const float both = 0.5f * (pair[0] + pair[1]);
                pair[0] = both;
                pair[1] = both;
            }
            for (float value : pair) {
                value = std::clamp(value, -1.0f, 1.0f);
                std::uint32_t bits = 0u;
                std::memcpy(&bits, &value, 4u);
                for (int k = 0; k < 4; ++k) {
                    hash = (hash ^ ((bits >> (8 * k)) & 0xFFu)) * 1099511628211ull;
                }
            }
        }
    }
    return hash;
}

#if !defined(SPEAKER_LAW_REFERENCE)


// The rear gain written out from the matrix's own numbers, apart from the library's.
const double kRear = 26500.0 / 32768.0 * std::sqrt((32767.0 / 32768.0) * (32767.0 / 32768.0) + (18400.0 / 32768.0) * (18400.0 / 32768.0));

struct Layout {
    const char* name;
    std::vector<Speaker> speakers;  // in the device's order (a Windows mask's bit order)
};

const std::vector<Layout>& layouts() {
    constexpr Speaker L = Speaker::Left, R = Speaker::Right, C = Speaker::Center, LFE = Speaker::LowFrequency,
                      SL = Speaker::SideLeft, SR = Speaker::SideRight, BL = Speaker::BackLeft, BR = Speaker::BackRight,
                      O = Speaker::Other;
    static const std::vector<Layout> all = {
        {"2.0", {L, R}},
        {"3.0", {L, R, C}},
        {"4.0", {L, R, BL, BR}},
        {"5.1 side", {L, R, C, LFE, SL, SR}},
        {"5.1 back", {L, R, C, LFE, BL, BR}},
        {"7.1", {L, R, C, LFE, BL, BR, SL, SR}},
        {"7.1.4", {L, R, C, LFE, BL, BR, SL, SR, O, O, O, O}},
    };
    return all;
}

constexpr SpeakerChoice kChoices[] = {SpeakerChoice::Auto,     SpeakerChoice::Mono,     SpeakerChoice::Stereo,
                                      SpeakerChoice::Encoded,  SpeakerChoice::Layout31, SpeakerChoice::Layout51,
                                      SpeakerChoice::Layout71};
const char* const kChoiceNames[] = {"Auto", "Mono", "Stereo", "Encoded stereo", "3.1", "5.1", "7.1"};

// The card's table: which values each layout offers (a layout needs every speaker it names, its low frequency too).
bool offered_by_card(std::size_t layout, std::size_t choice) {
    static const bool table[7][7] = {
        {1, 1, 1, 1, 0, 0, 0},  // 2.0
        {1, 1, 1, 1, 0, 0, 0},  // 3.0: no low frequency
        {1, 1, 1, 1, 0, 0, 0},  // 4.0: no centre
        {1, 1, 1, 1, 1, 1, 0},  // 5.1 side
        {1, 1, 1, 1, 1, 1, 0},  // 5.1 back
        {1, 1, 1, 1, 1, 1, 1},  // 7.1
        {1, 1, 1, 1, 1, 1, 1},  // 7.1.4
    };
    return table[layout][choice];
}

// The speakers that carry sound for a choice on a layout, as the card's "what each plays" names them.
std::vector<Speaker> played_by_card(std::size_t layout, std::size_t choice) {
    constexpr Speaker L = Speaker::Left, R = Speaker::Right, C = Speaker::Center, SL = Speaker::SideLeft,
                      SR = Speaker::SideRight, BL = Speaker::BackLeft, BR = Speaker::BackRight;
    if (!offered_by_card(layout, choice)) {
        choice = 0u;  // played as Auto
    }
    switch (choice) {
    case 1:
    case 2:
    case 3: return {L, R};
    case 4: return {L, R, C};
    case 5: return layout == 4u ? std::vector<Speaker>{L, R, C, BL, BR} : std::vector<Speaker>{L, R, C, SL, SR};
    case 6: return {L, R, C, SL, SR, BL, BR};
    default: break;
    }
    static const std::vector<std::vector<Speaker>> automatic = {
        {L, R}, {L, R, C}, {L, R, BL, BR}, {L, R, C, SL, SR}, {L, R, C, BL, BR}, {L, R, C, SL, SR, BL, BR},
        {L, R, C, SL, SR, BL, BR}};
    return automatic[layout];
}

bool contains(const std::vector<Speaker>& set, Speaker speaker) {
    return std::find(set.begin(), set.end(), speaker) != set.end();
}

// A speaker's gain for a panner sound at pan p and rear r, written out from the law's words (points 3, 4, 11).
double law_gain(Speaker speaker, const std::vector<Speaker>& played, double p, double r) {
    const bool sides = contains(played, Speaker::SideLeft);
    const bool backs = contains(played, Speaker::BackLeft);
    const double left = std::cos(kPi * p / 2.0);
    const double right = std::sin(kPi * p / 2.0);
    const double front = std::cos(kPi * r / 2.0);
    const double back = std::sin(kPi * r / 2.0);
    const double front_of_side = sides || backs ? front : std::sqrt(front * front + kRear * kRear * back * back);
    const double rear = kRear * back * (sides && backs ? 1.0 / std::sqrt(2.0) : 1.0);
    switch (speaker) {
    case Speaker::Left: return left * front_of_side;
    case Speaker::Right: return right * front_of_side;
    case Speaker::SideLeft: return sides ? left * rear : 0.0;
    case Speaker::SideRight: return sides ? right * rear : 0.0;
    case Speaker::BackLeft: return backs ? left * rear : 0.0;
    case Speaker::BackRight: return backs ? right * rear : 0.0;
    default: return 0.0;
    }
}

// A centre send's gain on a speaker: its own speaker, else left and right at 1 / sqrt 2.
double centre_gain(Speaker speaker, const std::vector<Speaker>& played) {
    const bool centre = contains(played, Speaker::Center);
    if (speaker == Speaker::Center) {
        return centre ? 1.0 : 0.0;
    }
    return (speaker == Speaker::Left || speaker == Speaker::Right) && !centre ? 1.0 / std::sqrt(2.0) : 0.0;
}

// The law as the mixer and the stage use it: a speaker's gain for the panner at (p, r).
double stage_gain(Speaker speaker, const PlayedLayout& played, std::uint8_t opened, float p, float r) {
    const galaxy::audio::PannerGains bus = galaxy::audio::panner_gains(p, r, played.rear());
    const auto gains = galaxy::audio::placed_speaker_gains(speaker, played, opened);
    return gains[galaxy::audio::kBusLeft] * bus.front_left + gains[galaxy::audio::kBusRight] * bus.front_right +
           gains[galaxy::audio::kBusRearLeft] * bus.rear_left + gains[galaxy::audio::kBusRearRight] * bus.rear_right;
}

Speaker twin(Speaker speaker) {
    switch (speaker) {
    case Speaker::Left: return Speaker::Right;
    case Speaker::Right: return Speaker::Left;
    case Speaker::SideLeft: return Speaker::SideRight;
    case Speaker::SideRight: return Speaker::SideLeft;
    case Speaker::BackLeft: return Speaker::BackRight;
    case Speaker::BackRight: return Speaker::BackLeft;
    default: return speaker;
    }
}

// Points 2 to 4, 6, 7 and 11 over the 101 by 101 grid, for each placed value on each layout (test 10 b and e).
void test_law_grid() {
    expect(std::fabs(galaxy::audio::kRearGain - 0.92747) < 5e-6, "the rear gain is 0.92747", galaxy::audio::kRearGain);
    expect(std::fabs(galaxy::audio::kRearGain * galaxy::audio::kHalfPower - 0.65582) < 5e-6,
           "a rear pair of two gets 0.65582", galaxy::audio::kRearGain * galaxy::audio::kHalfPower);
    for (std::size_t li = 0; li < layouts().size(); ++li) {
        const Layout& layout = layouts()[li];
        const std::uint8_t kinds = galaxy::audio::speaker_kinds(layout.speakers);
        for (std::size_t ci = 0; ci < std::size(kChoices); ++ci) {
            const PlayedLayout played = galaxy::audio::played_on(kChoices[ci], kinds, kinds);
            if (!played.placed()) {
                continue;
            }
            const std::vector<Speaker> named = played_by_card(li, ci);
            const std::string at = std::string(kChoiceNames[ci]) + " on " + layout.name + ": ";
            std::vector<std::vector<double>> previous_row;
            double worst_power = 0.0, worst_mirror = 0.0, worst_step = 0.0, worst_law = 0.0, silent = 0.0;
            for (int pi = 0; pi <= 100; ++pi) {
                std::vector<std::vector<double>> row;
                for (int ri = 0; ri <= 100; ++ri) {
                    const float p = static_cast<float>(pi) / 100.0f;
                    const float r = static_cast<float>(ri) / 100.0f;
                    std::vector<double> gains;
                    double power = 0.0;
                    for (const Speaker speaker : layout.speakers) {
                        const double gain = stage_gain(speaker, played, kinds, p, r);
                        gains.push_back(gain);
                        power += gain * gain;
                        worst_law = std::max(worst_law, std::fabs(gain - law_gain(speaker, named, p, r)));
                        if (speaker == Speaker::LowFrequency || speaker == Speaker::Other || !contains(named, speaker)) {
                            silent = std::max(silent, std::fabs(gain));
                        }
                        const double mirrored = stage_gain(twin(speaker), played, kinds, 1.0f - p, r);
                        worst_mirror = std::max(worst_mirror, std::fabs(gain - mirrored));
                    }
                    // On two channels the matrix gives a rear bus kRear^2 of its power, the front pair 1.
                    const double front = std::cos(kPi * r / 2.0);
                    const double back = std::sin(kPi * r / 2.0);
                    worst_power = std::max(worst_power, std::fabs(power - (front * front + kRear * kRear * back * back)));
                    if (ri > 0) {
                        for (std::size_t k = 0; k < gains.size(); ++k) {
                            worst_step = std::max(worst_step, std::fabs(gains[k] - row.back()[k]));
                        }
                    }
                    if (pi > 0) {
                        for (std::size_t k = 0; k < gains.size(); ++k) {
                            worst_step = std::max(worst_step, std::fabs(gains[k] - previous_row[static_cast<std::size_t>(ri)][k]));
                        }
                    }
                    row.push_back(std::move(gains));
                }
                previous_row = std::move(row);
            }
            expect(worst_power < 2e-6, at + "power equals two channels' over the grid", worst_power);
            expect(worst_mirror < 2e-6, at + "left and right mirror", worst_mirror);
            // A quarter sine moves at most pi/2 per unit: 0.0157 a grid step.
            expect(worst_step < 0.0158, at + "no jump between grid points", worst_step);
            expect(worst_law < 2e-6, at + "the gains are the written law's", worst_law);
            expect(silent == 0.0, at + "low frequency, heights and unplayed speakers silent", silent);
        }
    }
}

// What a device offers, what plays, the row's cycle and the game's mode (tests 10 d, 11 and 12's modes).
void test_choices() {
    for (std::size_t li = 0; li < layouts().size(); ++li) {
        const Layout& layout = layouts()[li];
        const std::uint8_t kinds = galaxy::audio::speaker_kinds(layout.speakers);
        std::vector<SpeakerChoice> offered;
        for (std::size_t ci = 0; ci < std::size(kChoices); ++ci) {
            const bool is = galaxy::audio::speakers_offered(kChoices[ci], kinds);
            expect(is == offered_by_card(li, ci), std::string(kChoiceNames[ci]) + " offered on " + layout.name + " as the card says");
            if (is) {
                offered.push_back(kChoices[ci]);
            } else {
                expect(galaxy::audio::resolve_speakers(kChoices[ci], kinds) ==
                           galaxy::audio::resolve_speakers(SpeakerChoice::Auto, kinds),
                       std::string("a stored ") + kChoiceNames[ci] + " plays as Auto on " + layout.name);
            }
        }
        // The cycle from each value visits the offered ones in order, both ways, skipping the rest.
        for (std::size_t ci = 0; ci < std::size(kChoices); ++ci) {
            SpeakerChoice forward = kChoices[ci];
            SpeakerChoice back = kChoices[ci];
            std::vector<SpeakerChoice> seen_forward, seen_back;
            for (std::size_t k = 0; k < offered.size(); ++k) {
                forward = galaxy::audio::next_offered_speakers(forward, 1, kinds);
                back = galaxy::audio::next_offered_speakers(back, -1, kinds);
                seen_forward.push_back(forward);
                seen_back.push_back(back);
            }
            std::vector<SpeakerChoice> sorted_forward = seen_forward, sorted_back = seen_back;
            std::sort(sorted_forward.begin(), sorted_forward.end());
            std::sort(sorted_back.begin(), sorted_back.end());
            expect(sorted_forward == offered && sorted_back == offered,
                   std::string("the row's cycle from ") + kChoiceNames[ci] + " on " + layout.name + " is the offered values");
            if (galaxy::audio::speakers_offered(kChoices[ci], kinds)) {
                expect(seen_forward.back() == kChoices[ci] && seen_back.back() == kChoices[ci],
                       std::string("the row's cycle comes back to ") + kChoiceNames[ci] + " on " + layout.name);
            }
        }
    }
    // The game's mode on two channels and on 5.1 (test 12).
    const std::uint8_t two = galaxy::audio::speaker_kinds(layouts()[0].speakers);
    const std::uint8_t five = galaxy::audio::speaker_kinds(layouts()[3].speakers);
    const std::uint32_t on_two[] = {1u, 0u, 1u, 2u, 1u, 1u, 1u};
    const std::uint32_t on_five[] = {2u, 0u, 1u, 2u, 2u, 2u, 2u};
    for (std::size_t ci = 0; ci < std::size(kChoices); ++ci) {
        expect(galaxy::audio::game_sound_mode(galaxy::audio::resolve_speakers(kChoices[ci], two)) == on_two[ci],
               std::string("the game's mode for ") + kChoiceNames[ci] + " on two channels");
        expect(galaxy::audio::game_sound_mode(galaxy::audio::resolve_speakers(kChoices[ci], five)) == on_five[ci],
               std::string("the game's mode for ") + kChoiceNames[ci] + " on 5.1");
    }
}

// The known sounds: a constant through the panner at (pan, rear) and a constant on the centre bus, with the game's
// lines the panner's effect send feeds (when `echo`).
struct Scene {
    std::vector<NebulaVoice> voices = std::vector<NebulaVoice>(64);
    std::vector<NebulaEffectLine> effects = std::vector<NebulaEffectLine>(4);
    std::vector<std::int16_t> sound = std::vector<std::int16_t>(1u << 17, 16384);  // 0.5

    NebulaVoice& voice(std::uint32_t index) {
        NebulaVoice& v = voices[index];
        v = {};
        v.active = 1u;
        v.starting = 1u;
        v.rate = 32000.0f;
        v.format = NEBULA_AUDIO_PCM16;
        v.wave = reinterpret_cast<const std::uint8_t*>(sound.data());
        v.wave_end = reinterpret_cast<const std::uint8_t*>(sound.data() + sound.size());
        v.end = static_cast<std::uint32_t>(sound.size());
        v.loop = 1u;
        return v;
    }
    void panner(float pan, float rear, float volume) {
        NebulaVoice& v = voice(0u);
        v.panner = NEBULA_VOICE_PANNER;
        v.pan = pan;
        v.rear = rear;
        v.panner_volume = volume;
        v.panner_target = volume;
    }
    void centre(float volume) {
        NebulaVoice& v = voice(1u);
        v.sends[0] = {NEBULA_AUDIO_BUS_CENTER, volume, volume};
    }
};

// One game frame (8 subframes) into the stage.
bool make_frame(AudioMixer& mixer, SpeakerStage& stage) {
    for (int i = 0; i < 8; ++i) {
        mixer.mix(1.0f);
    }
    const std::span<const float> front = mixer.end_frame();
    stage.push(front, mixer.surround());
    return !front.empty();
}

std::vector<float> render(Scene& scene, const Layout& layout, SpeakerChoice choice, std::size_t calls, std::size_t frames) {
    AudioMixer mixer(kRate);
    mixer.setup(scene.voices.data(), static_cast<std::uint32_t>(scene.voices.size()), scene.effects.data());
    SpeakerStage stage;
    const std::uint8_t kinds = galaxy::audio::speaker_kinds(layout.speakers);
    std::vector<float> out(calls * frames * layout.speakers.size());
    for (std::size_t call = 0; call < calls; ++call) {
        stage.render(out.data() + call * frames * layout.speakers.size(), layout.speakers, kinds, choice, frames, kRate,
                     [&] { return make_frame(mixer, stage); },
                     [&](const PlayedLayout& played) { mixer.set_layout(played.placed(), played.rear()); });
    }
    return out;
}

// A float WAV of the render, read back for the checks: what is checked is the file.
std::vector<float> through_file(const std::vector<float>& samples, std::size_t channels, const std::string& name) {
    const std::filesystem::path folder = galaxy::test_temp_path("nebula-speaker-law");
    std::filesystem::create_directories(folder);
    const std::filesystem::path path = folder / (name + ".wav");
    {
        std::ofstream file(path, std::ios::binary);
        const auto u32 = [&file](std::uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
        const auto u16 = [&file](std::uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
        const auto bytes = static_cast<std::uint32_t>(samples.size() * 4u);
        file.write("RIFF", 4);
        u32(36u + bytes);
        file.write("WAVEfmt ", 8);
        u32(16u);
        u16(3u);  // IEEE float
        u16(static_cast<std::uint16_t>(channels));
        u32(kRate);
        u32(kRate * static_cast<std::uint32_t>(channels) * 4u);
        u16(static_cast<std::uint16_t>(channels * 4u));
        u16(32u);
        file.write("data", 4);
        u32(bytes);
        file.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(bytes));
    }
    std::ifstream file(path, std::ios::binary);
    std::vector<char> whole((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<float> back((whole.size() - 44u) / 4u);
    std::memcpy(back.data(), whole.data() + 44, back.size() * 4u);
    file.close();
    std::filesystem::remove(path);
    return back;
}

// Test 10 a to c and points 5 and 9: every value on every layout, rendered through the real mixer to a file.
void test_renders() {
    // The constant 0.5 through the panner at 0.25 and on the centre at 0.2: a = 0.125, b = 0.1 on the speakers.
    const double a = 0.125;
    const double b = 0.1;
    const float points[][2] = {{0.5f, 0.5f}, {0.0f, 0.0f}, {0.25f, 0.75f}, {1.0f, 1.0f}, {0.8f, 0.3f}};
    for (std::size_t li = 0; li < layouts().size(); ++li) {
        const Layout& layout = layouts()[li];
        const std::size_t channels = layout.speakers.size();
        for (std::size_t ci = 0; ci < std::size(kChoices); ++ci) {
            const std::vector<Speaker> named = played_by_card(li, ci);
            // Only the two-channel forms play on the front pair alone (a placed layout has a centre or a rear).
            const bool two = named.size() == 2u;
            for (std::size_t pi = 0; pi < std::size(points); ++pi) {
                const float* point = points[pi];
                Scene scene;
                scene.panner(point[0], point[1], 0.25f);
                scene.centre(0.2f);
                const std::string at = std::string(kChoiceNames[ci]) + " on " + layout.name + " at pan " +
                                       std::to_string(point[0]) + " rear " + std::to_string(point[1]) + ": ";
                std::vector<float> out = render(scene, layout, kChoices[ci], 4u, 960u);
                if (pi == 0u) {
                    out = through_file(out, channels, std::string("render-") + std::to_string(li) + "-" + std::to_string(ci));
                }
                // Every channel the card does not name is all zero samples.
                for (std::size_t k = 0; k < channels; ++k) {
                    if (contains(named, layout.speakers[k])) {
                        continue;
                    }
                    double most = 0.0;
                    for (std::size_t f = 0; f < out.size() / channels; ++f) {
                        most = std::max(most, static_cast<double>(std::fabs(out[f * channels + k])));
                    }
                    expect(most == 0.0, at + "an unplayed channel is all zero samples", most);
                }
                if (two) {
                    // The two-channel forms: the front pair is the mixer's two channels as made (the matrix): checked
                    // bit for bit by test_two_channels; here they carry sound.
                    const std::size_t f = 3000u;
                    expect(out[f * channels] != 0.0f || out[f * channels + 1u] != 0.0f, at + "the front pair carries sound");
                    continue;
                }
                // Placed: each channel's level and sign (its phase: no inversion) against the written law; each sound's
                // total power against its two channels' power.
                const std::size_t f = 3000u;  // past the start, the constant settled
                for (std::size_t k = 0; k < channels; ++k) {
                    const Speaker speaker = layout.speakers[k];
                    const double want = 0.5 * 0.25 * law_gain(speaker, named, point[0], point[1]) + 0.5 * 0.2 * centre_gain(speaker, named);
                    const double got = out[f * channels + k];
                    expect(std::fabs(got - want) < 2e-4, at + "channel " + std::to_string(k) + " level and sign", got - want);
                    if (pi == 0u && contains(named, speaker)) {
                        expect(got > 1e-3, at + "a played channel carries sound", got);
                    }
                }
                // Each sound rendered alone (the two are the same constant, so together a folded centre adds to the
                // panner's left and right in phase and their powers do not sum).
                const auto power_alone = [&](float panner_volume, float centre_volume) {
                    Scene alone;
                    alone.panner(point[0], point[1], panner_volume);
                    alone.centre(centre_volume);
                    const std::vector<float> played = render(alone, layout, kChoices[ci], 4u, 960u);
                    double power = 0.0;
                    for (std::size_t k = 0; k < channels; ++k) {
                        power += static_cast<double>(played[f * channels + k]) * played[f * channels + k];
                    }
                    return power;
                };
                const double front = std::cos(kPi * point[1] / 2.0);
                const double back = std::sin(kPi * point[1] / 2.0);
                // Two channels: the panner's power with the rear at kRear^2; the centre's b^2 (kCenterGain^2 on each).
                const double panner_two = a * a * (front * front + kRear * kRear * back * back);
                const double panner_power = power_alone(0.25f, 0.0f);
                expect(std::fabs(panner_power - panner_two) < 2e-4, at + "the panned sound's power equals two channels'",
                       panner_power - panner_two);
                const double centre_power = power_alone(0.0f, 0.2f);
                expect(std::fabs(centre_power - b * b) < 2e-4, at + "the centre's power equals two channels'",
                       centre_power - b * b);
            }
        }
    }
}

// The same scene's two channels from the mixer as it was before the surround law (G6.2's tree,
// runtime/src/audio_mixer.cpp at c8820d8be, built alone with this function): stereo, and its mean for mono.
// Recorded with MSVC x64 and the mixer's own flags (/O2 /arch:AVX2): another compiler or processor may round the same
// code differently, so the recorded numbers are checked where they were made; everywhere the stage's copy of the
// mixer's two channels is checked bit for bit.
constexpr std::uint64_t kTwoChannelsBefore = 0xd87b50e3aab53cfeull;
constexpr std::uint64_t kMonoBefore = 0x1f4148eaa86e7dffull;

// Test 10 c: the two-channel forms, bit for bit as before, through the stage onto the front pair of every layout.
void test_two_channels() {
#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64)
    expect(two_channel_hash(false) == kTwoChannelsBefore, "the mixer's two channels are as they were, bit for bit");
    expect(two_channel_hash(true) == kMonoBefore, "mono, the mean of the two, is as it was, bit for bit");
#endif
    for (std::size_t li = 0; li < layouts().size(); ++li) {
        const Layout& layout = layouts()[li];
        for (const SpeakerChoice choice : {SpeakerChoice::Mono, SpeakerChoice::Stereo, SpeakerChoice::Encoded}) {
            Scene scene;
            scene.panner(0.3f, 0.6f, 0.25f);
            scene.centre(0.2f);
            // The mixer's own two channels for the same scene.
            AudioMixer reference(kRate);
            Scene same = scene;
            reference.setup(same.voices.data(), 64u, same.effects.data());
            std::vector<float> made;
            for (int frame = 0; frame < 4; ++frame) {
                for (int i = 0; i < 8; ++i) {
                    reference.mix(1.0f);
                }
                const std::span<const float> out = reference.end_frame();
                made.insert(made.end(), out.begin(), out.end());
            }
            const std::vector<float> out = render(scene, layout, choice, 4u, 960u);
            const std::size_t channels = layout.speakers.size();
            bool same_bits = true;
            for (std::size_t f = 0; f < made.size() / 2u; ++f) {
                for (std::size_t side = 0; side < 2u; ++side) {
                    const float want = std::clamp(made[f * 2u + side], -1.0f, 1.0f);
                    const float got = out[f * channels + side];
                    same_bits = same_bits && std::memcmp(&want, &got, 4u) == 0;
                }
            }
            expect(same_bits, std::string("the stage copies the two channels bit for bit on ") + layout.name);
        }
    }
}

// Point 5: no inversion on a speaker of its own. A sound on the inverted right bus reaches the right speaker in phase,
// at its level, on every placed layout (on two channels it is subtracted, the matrix as made).
void test_no_inversion() {
    for (std::size_t li = 1; li < layouts().size(); ++li) {
        const Layout& layout = layouts()[li];
        Scene scene;
        NebulaVoice& v = scene.voice(2u);
        v.sends[0] = {NEBULA_AUDIO_BUS_RIGHT_INVERTED, 0.2f, 0.2f};
        const std::vector<float> out = render(scene, layout, SpeakerChoice::Auto, 4u, 960u);
        const std::size_t channels = layout.speakers.size();
        const std::size_t f = 3000u;
        for (std::size_t k = 0; k < channels; ++k) {
            const double want = layout.speakers[k] == Speaker::Right ? 0.1 : 0.0;
            expect(std::fabs(out[f * channels + k] - want) < 1e-5,
                   std::string("the inverted right on ") + layout.name + ": channel " + std::to_string(k) +
                       " in phase on the right, nothing elsewhere",
                   out[f * channels + k] - want);
        }
    }
}

// Point 7 (a change of pan or rear ramps) and point 8 (channels named, never by position).
void test_ramp_and_order() {
    const Layout& seven = layouts()[5];
    Scene scene;
    scene.panner(0.0f, 0.0f, 0.25f);
    AudioMixer mixer(kRate);
    mixer.setup(scene.voices.data(), 64u, scene.effects.data());
    mixer.set_layout(true, true);
    for (int i = 0; i < 8; ++i) {
        mixer.mix(1.0f);
    }
    (void)mixer.end_frame();
    scene.voices[0].pan = 1.0f;
    scene.voices[0].rear = 1.0f;
    mixer.mix(1.0f);
    const std::span<const float> front = mixer.end_frame();
    double step = 0.0;
    for (std::size_t f = 1; f < front.size() / 2u; ++f) {
        step = std::max(step, static_cast<double>(std::fabs(front[f * 2u] - front[(f - 1u) * 2u])));
    }
    // The volume ramp: 0.125 over 65 samples of 32 kHz (97.5 at 48 kHz).
    expect(step < 0.125 / 90.0, "a jump of pan and rear ramps over the subframe", step);
    // Point 8: the same layout in another channel order plays each speaker the same.
    Layout shuffled{"7.1 shuffled", {Speaker::SideRight, Speaker::Left, Speaker::LowFrequency, Speaker::BackLeft, Speaker::Center,
                                     Speaker::Right, Speaker::SideLeft, Speaker::BackRight}};
    Scene a, b;
    a.panner(0.3f, 0.7f, 0.25f);
    b.panner(0.3f, 0.7f, 0.25f);
    const std::vector<float> standard = render(a, seven, SpeakerChoice::Auto, 3u, 960u);
    const std::vector<float> other = render(b, shuffled, SpeakerChoice::Auto, 3u, 960u);
    bool same = true;
    for (std::size_t f = 0; f < standard.size() / 8u; ++f) {
        for (std::size_t k = 0; k < 8u; ++k) {
            const auto where = static_cast<std::size_t>(
                std::find(seven.speakers.begin(), seven.speakers.end(), shuffled.speakers[k]) - seven.speakers.begin());
            same = same && other[f * 8u + k] == standard[f * 8u + where];
        }
    }
    expect(same, "each channel is named from its speaker, never by position");
}

// Test 12: a device that goes 5.1, then 2.0, then 7.1 between two render calls: the played layout changes at once and no
// sample steps by more than the 10 ms fade allows.
void test_device_change() {
    Scene scene;
    scene.panner(0.3f, 0.4f, 0.25f);
    AudioMixer mixer(kRate);
    mixer.setup(scene.voices.data(), 64u, scene.effects.data());
    SpeakerStage stage;
    const std::size_t fade = SpeakerStage::fade_frames(kRate);
    const double bound = 0.25 / static_cast<double>(fade + 1u) * 1.05 + 1e-6;
    const Layout* steps[] = {&layouts()[3], &layouts()[0], &layouts()[5]};
    std::map<Speaker, float> last;
    double worst = 0.0;
    bool first = true;
    const SpeakerChoice choice = SpeakerChoice::Auto;
    for (const Layout* layout : steps) {
        const std::uint8_t kinds = galaxy::audio::speaker_kinds(layout->speakers);
        const std::size_t channels = layout->speakers.size();
        for (int call = 0; call < 6; ++call) {
            std::vector<float> out(480u * channels);
            stage.render(out.data(), layout->speakers, kinds, choice, 480u, kRate, [&] { return make_frame(mixer, stage); },
                         [&](const PlayedLayout& played) { mixer.set_layout(played.placed(), played.rear()); });
            expect(stage.form() == galaxy::audio::played_on(choice, kinds, kinds),
                   std::string("the layout follows the device at once: ") + layout->name);
            for (std::size_t f = 0; f < 480u; ++f) {
                for (std::size_t k = 0; k < channels; ++k) {
                    const Speaker speaker = layout->speakers[k];
                    const float value = out[f * channels + k];
                    const auto was = last.find(speaker);
                    const float before = was != last.end() ? was->second : 0.0f;
                    if (!first) {
                        worst = std::max(worst, static_cast<double>(std::fabs(value - before)));
                    }
                    last[speaker] = value;
                }
                // A speaker the device no longer has starts from silence if it comes back.
                for (auto& [speaker, value] : last) {
                    if (std::find(layout->speakers.begin(), layout->speakers.end(), speaker) == layout->speakers.end()) {
                        value = 0.0f;
                    }
                }
            }
            first = first && call < 1;  // the sound's own start (the resampler filling) is not a layout change
        }
    }
    expect(worst <= bound, "no sample steps by more than the 10 ms fade allows across 5.1, 2.0 and 7.1", worst);
    expect(choice == SpeakerChoice::Auto, "the stored choice is the same after");
}

#endif  // SPEAKER_LAW_REFERENCE

}  // namespace

#if !defined(SPEAKER_LAW_REFERENCE)
int main() {
    test_law_grid();
    test_choices();
    test_renders();
    test_two_channels();
    test_no_inversion();
    test_ramp_and_order();
    test_device_change();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    std::printf("speaker law tests passed\n");
    return 0;
}
#endif
