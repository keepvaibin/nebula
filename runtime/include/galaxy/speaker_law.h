// The surround law (docs/engine/CONSOLE-FREE.md section 4, "The law", points 1 to 11): which speakers the game's sound
// plays on, chosen by the player's Speakers setting against what the device reports, and the gains that place a sound on
// them. Two channels keep the game's matrix as made; on speakers of their own a sound is placed by its pan and rear with
// the panner's constant-power law, at the power it has on two channels.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "galaxy/runtime_settings.h"

namespace galaxy::audio {

// The two-channel matrix (15-bit fractions as made): a surround bus reaches left and right through kFold (kUnit, kMatrix),
// the centre at kCenterGain.
constexpr float kUnit = 32767.0f / 32768.0f;
constexpr float kFold = 26500.0f / 32768.0f;
constexpr float kMatrix = 18400.0f / 32768.0f;
constexpr float kCenterGain = 23170.0f / 32768.0f;

// A rear bus on a speaker of its own: the amplitude that gives it the power the matrix gives it on two channels,
// kFold sqrt(kUnit^2 + kMatrix^2) = 0.92747 (point 4).
extern const float kRearGain;
// 1 / sqrt 2: a centre with no speaker on left and right, a rear bus split over sides and backs.
constexpr float kHalfPower = 0.70710678f;

// What each of a device's channels is, named from its bit or the device's order. LowFrequency and Other stay silent.
enum class Speaker : std::uint8_t { Left, Right, Center, SideLeft, SideRight, BackLeft, BackRight, LowFrequency, Other };

// The speakers a device has, as kinds.
enum SpeakerKinds : std::uint8_t {
    kSpeakersFront = 1,         // left and right
    kSpeakersCentre = 2,
    kSpeakersSides = 4,         // side left and right
    kSpeakersBacks = 8,         // back left and right
    kSpeakersLowFrequency = 16,
};
[[nodiscard]] std::uint8_t speaker_kinds(std::span<const Speaker> speakers) noexcept;

// What the mixer makes and the speakers it plays on: one of the two-channel forms on the front pair, or the sound placed
// on the centre and rear speakers named (the front pair always).
enum class TwoChannel : std::uint8_t { Placed, Mono, Stereo, Encoded };
struct PlayedLayout {
    TwoChannel two = TwoChannel::Stereo;
    bool centre = false;
    bool sides = false;
    bool backs = false;

    [[nodiscard]] bool placed() const noexcept { return two == TwoChannel::Placed; }
    [[nodiscard]] bool rear() const noexcept { return placed() && (sides || backs); }
    friend bool operator==(const PlayedLayout&, const PlayedLayout&) = default;
};

// A layout is offered when the device has every speaker it names (3.1: front, centre and low frequency; 5.1: and a
// side or back pair; 7.1: both pairs). Auto and the two-channel values always.
[[nodiscard]] bool speakers_offered(SpeakerChoice choice, std::uint8_t kinds) noexcept;
// The choice a step of the Speakers row reaches from `from` (delta < 0 back), past the values the device does not offer.
[[nodiscard]] SpeakerChoice next_offered_speakers(SpeakerChoice from, int delta, std::uint8_t kinds) noexcept;
// What plays: a choice the device cannot play plays as Auto; Auto is every speaker the game has content for (Stereo when
// the device has nothing past the front pair).
[[nodiscard]] PlayedLayout resolve_speakers(SpeakerChoice choice, std::uint8_t kinds) noexcept;
// What the output plays: resolve_speakers on the device's report, less the speakers the device opened without (a
// request for more not yet answered).
[[nodiscard]] PlayedLayout played_on(SpeakerChoice choice, std::uint8_t reported, std::uint8_t opened) noexcept;
// The game's sound mode (its outputMode): 0 mono, 1 stereo, 2 with a rear amount (Encoded stereo and every placed layout).
[[nodiscard]] std::uint32_t game_sound_mode(const PlayedLayout& played) noexcept;

// The device's last report (its speakers as kinds), published by the output for the settings row and the game's mode; any
// thread. Before any device: the front pair.
void publish_device_speakers(std::uint8_t kinds) noexcept;
[[nodiscard]] std::uint8_t device_speakers() noexcept;

// The panner's gains for pan p and rear r (0..1) on the placed output's buses: front left and right, rear left and right
// (point 3). With no rear speaker the rear part joins the front gain of its side as a power sum at kRearGain (point 11).
struct PannerGains {
    float front_left = 0.0f;
    float front_right = 0.0f;
    float rear_left = 0.0f;
    float rear_right = 0.0f;
};
[[nodiscard]] PannerGains panner_gains(float p, float r, bool rear_speakers) noexcept;

// The mixer's buses as the speaker stage takes them: left and right, then (placed) centre, rear left and rear right.
enum MixBus : std::uint8_t { kBusLeft, kBusRight, kBusCentre, kBusRearLeft, kBusRearRight, kMixBuses };
// A speaker's gain for each bus in the placed form (point 4 and 11): front at 1; a centre with no speaker to left and
// right at 1 / sqrt 2; a rear bus at kRearGain on its pair, kRearGain / sqrt 2 on each of two pairs, or folded to the front
// of its side at kRearGain when no rear speaker plays. Speakers the layout does not play are 0.
[[nodiscard]] std::array<float, kMixBuses> placed_speaker_gains(Speaker speaker, const PlayedLayout& played,
                                                                std::uint8_t opened) noexcept;

// The mixer's frames onto the device's channels. Frames wait here in the form they were made in; the two-channel forms
// copy left and right to the front pair as they are (bit for bit), every other channel silent. A change of the mixer's
// output ramps (point 7): frames of the old form still waiting fade out over 10 ms (the rest of them is dropped), the new
// form fades in over 10 ms after 2.5 ms of silence; a change of the speakers alone, the mixer's output kept, moves each
// gain over 10 ms.
class SpeakerStage {
public:
    // The same mixer output: the same two-channel form, or placed with or without rear speakers in both.
    [[nodiscard]] static bool same_mix(const PlayedLayout& a, const PlayedLayout& b) noexcept;
    // 10 ms at `rate`.
    [[nodiscard]] static std::size_t fade_frames(std::uint32_t rate) noexcept;

    // A render call of the device (its thread): the form for `choice` on a device that reports `reported` and opened
    // `opened` is settled first; when the mixer's output changes with it, `make()` mixes game frames in the old form until
    // 10 ms of them wait (to fade out), then `relayout(form)` puts the mixer in the new one. Then `frames` frames go to
    // `out`, `make()` mixing more as needed (false: nothing came, the game is not running); the rest is silent. Returns
    // the frames that carried sound.
    template <class Make, class Relayout>
    std::size_t render(float* out, std::span<const Speaker> opened, std::uint8_t reported, SpeakerChoice choice,
                       std::size_t frames, std::uint32_t rate, Make&& make, Relayout&& relayout) {
        const PlayedLayout played = played_on(choice, reported, speaker_kinds(opened));
        if (!form_set_ || played != form_) {
            if (form_set_ && !same_mix(played, form_)) {
                while (waiting() < fade_frames(rate) && make()) {
                }
            }
            set_form(played, rate);
            relayout(played);
        }
        std::size_t filled = 0u;
        while (filled < frames) {
            if (waiting() == 0u && !make()) {
                break;
            }
            filled += pull(out + filled * opened.size(), opened, frames - filled);
        }
        std::fill(out + filled * opened.size(), out + frames * opened.size(), 0.0f);
        return filled;
    }

    // The form the next frames are made in. True when the mixer's output changes with it: the frames waiting fade out.
    bool set_form(const PlayedLayout& played, std::uint32_t rate);
    [[nodiscard]] const PlayedLayout& form() const noexcept { return form_; }
    // A frame from the mixer in the current form: front (left, right) interleaved and, placed, apart (centre, rear left,
    // rear right) interleaved.
    void push(std::span<const float> front, std::span<const float> apart);
    // Frames waiting.
    [[nodiscard]] std::size_t waiting() const noexcept;
    // Up to `frames` frames onto `speakers` (interleaved), clamped to -1..1; returns the frames written.
    std::size_t pull(float* out, std::span<const Speaker> speakers, std::size_t frames);
    void clear();

private:
    // Frames of one form, played in order.
    struct Block {
        PlayedLayout form;
        std::vector<float> front;  // (left, right) from frame 0
        std::vector<float> apart;  // placed: (centre, rear left, rear right) from frame 0
        std::size_t read = 0;
        std::size_t end = 0;
        std::size_t fade_in = 0;          // nonzero: the fade in's length; the block's frame 0 is its fade_in_done-th
        std::size_t fade_in_done = 0;
        std::size_t fade_in_hold = 0;     // the fade in's first frames, silent
        std::size_t fade_out = 0;         // nonzero: the fade out's length; frame fade_out_from is its fade_out_offset-th
        std::size_t fade_out_from = 0;
        std::size_t fade_out_offset = 0;
    };
    void make_gains(const PlayedLayout& form, std::span<const Speaker> speakers, std::uint8_t opened,
                    std::vector<float>& into) const;

    PlayedLayout form_{};
    bool form_set_ = false;
    std::size_t fade_frames_ = 480u;
    // A new form starts with one subframe (2.5 ms) of silence before its fade in: the two-channel matrix brings the
    // surround buses that much late, and that start is inside it.
    std::size_t hold_frames_ = 120u;
    std::size_t pending_fade_in_ = 0;  // the next block pushed starts with this fade in
    std::vector<Block> blocks_;
    // The placed form's speaker gains as last used, per channel and bus, and the move to new ones (move_left_ frames
    // from gains_from_) when the speakers or the layout change and the mixer's output does not.
    std::vector<Speaker> speakers_;
    PlayedLayout gains_form_{};
    bool gains_fading_ = false;
    bool gains_valid_ = false;
    std::vector<float> gains_, gains_from_;
    std::size_t move_left_ = 0;
};

}  // namespace galaxy::audio
