#include "galaxy/speaker_law.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace galaxy::audio {

const float kRearGain =
    static_cast<float>(static_cast<double>(kFold) * std::sqrt(static_cast<double>(kUnit) * kUnit + static_cast<double>(kMatrix) * kMatrix));

namespace {

constexpr double kPi = 3.14159265358979323846;

std::atomic<std::uint8_t> s_device_speakers{kSpeakersFront};

constexpr SpeakerChoice kChoices[] = {SpeakerChoice::Auto,     SpeakerChoice::Mono,     SpeakerChoice::Stereo,
                                      SpeakerChoice::Encoded,  SpeakerChoice::Layout31, SpeakerChoice::Layout51,
                                      SpeakerChoice::Layout71};

[[nodiscard]] bool has(std::uint8_t kinds, std::uint8_t wanted) noexcept {
    return (kinds & wanted) == wanted;
}

}  // namespace

std::uint8_t speaker_kinds(std::span<const Speaker> speakers) noexcept {
    const auto present = [speakers](Speaker wanted) {
        return std::find(speakers.begin(), speakers.end(), wanted) != speakers.end();
    };
    std::uint8_t kinds = 0u;
    if (present(Speaker::Left) && present(Speaker::Right)) kinds |= kSpeakersFront;
    if (present(Speaker::Center)) kinds |= kSpeakersCentre;
    if (present(Speaker::SideLeft) && present(Speaker::SideRight)) kinds |= kSpeakersSides;
    if (present(Speaker::BackLeft) && present(Speaker::BackRight)) kinds |= kSpeakersBacks;
    if (present(Speaker::LowFrequency)) kinds |= kSpeakersLowFrequency;
    return kinds;
}

bool speakers_offered(SpeakerChoice choice, std::uint8_t kinds) noexcept {
    constexpr std::uint8_t k31 = kSpeakersFront | kSpeakersCentre | kSpeakersLowFrequency;
    switch (choice) {
    case SpeakerChoice::Layout31: return has(kinds, k31);
    case SpeakerChoice::Layout51: return has(kinds, k31) && (has(kinds, kSpeakersSides) || has(kinds, kSpeakersBacks));
    case SpeakerChoice::Layout71: return has(kinds, k31 | kSpeakersSides | kSpeakersBacks);
    default: return true;
    }
}

SpeakerChoice next_offered_speakers(SpeakerChoice from, int delta, std::uint8_t kinds) noexcept {
    constexpr int count = static_cast<int>(std::size(kChoices));
    int at = static_cast<int>(from);
    for (int step = 0; step < count; ++step) {
        at = (at + (delta < 0 ? count - 1 : 1)) % count;
        if (speakers_offered(kChoices[at], kinds)) {
            return kChoices[at];
        }
    }
    return from;
}

PlayedLayout resolve_speakers(SpeakerChoice choice, std::uint8_t kinds) noexcept {
    if (!speakers_offered(choice, kinds)) {
        choice = SpeakerChoice::Auto;
    }
    PlayedLayout played;
    switch (choice) {
    case SpeakerChoice::Mono: played.two = TwoChannel::Mono; return played;
    case SpeakerChoice::Stereo: played.two = TwoChannel::Stereo; return played;
    case SpeakerChoice::Encoded: played.two = TwoChannel::Encoded; return played;
    case SpeakerChoice::Layout31:
        played.two = TwoChannel::Placed;
        played.centre = true;
        return played;
    case SpeakerChoice::Layout51:
        played.two = TwoChannel::Placed;
        played.centre = true;
        played.sides = has(kinds, kSpeakersSides);
        played.backs = !played.sides;
        return played;
    case SpeakerChoice::Layout71:
        played.two = TwoChannel::Placed;
        played.centre = played.sides = played.backs = true;
        return played;
    default:
        break;
    }
    // Auto: every speaker the game has content for; the front pair alone is Stereo.
    played.centre = has(kinds, kSpeakersCentre);
    played.sides = has(kinds, kSpeakersSides);
    played.backs = has(kinds, kSpeakersBacks);
    played.two = played.centre || played.sides || played.backs ? TwoChannel::Placed : TwoChannel::Stereo;
    return played;
}

std::uint32_t game_sound_mode(const PlayedLayout& played) noexcept {
    switch (played.two) {
    case TwoChannel::Mono: return 0u;
    case TwoChannel::Stereo: return 1u;
    default: return 2u;
    }
}

void publish_device_speakers(std::uint8_t kinds) noexcept {
    s_device_speakers.store(kinds, std::memory_order_relaxed);
}

std::uint8_t device_speakers() noexcept {
    return s_device_speakers.load(std::memory_order_relaxed);
}

PannerGains panner_gains(float p, float r, bool rear_speakers) noexcept {
    p = std::clamp(p, 0.0f, 1.0f);
    r = std::clamp(r, 0.0f, 1.0f);
    const double left = std::cos(kPi * p / 2.0);
    const double right = std::sin(kPi * p / 2.0);
    const double front = std::cos(kPi * r / 2.0);
    const double back = std::sin(kPi * r / 2.0);
    PannerGains gains;
    if (rear_speakers) {
        gains.front_left = static_cast<float>(left * front);
        gains.front_right = static_cast<float>(right * front);
        gains.rear_left = static_cast<float>(left * back);
        gains.rear_right = static_cast<float>(right * back);
        return gains;
    }
    // No rear speaker: sqrt(front gain^2 + (kRearGain * rear gain)^2) on the front of each side.
    const double rear = static_cast<double>(kRearGain) * back;
    const double folded = std::sqrt(front * front + rear * rear);
    gains.front_left = static_cast<float>(left * folded);
    gains.front_right = static_cast<float>(right * folded);
    return gains;
}

std::array<float, kMixBuses> placed_speaker_gains(Speaker speaker, const PlayedLayout& played, std::uint8_t opened) noexcept {
    std::array<float, kMixBuses> gains{};
    const bool centre = played.centre && has(opened, kSpeakersCentre);
    const bool sides = played.sides && has(opened, kSpeakersSides);
    const bool backs = played.backs && has(opened, kSpeakersBacks);
    const float centre_fold = centre ? 0.0f : kHalfPower;
    const float rear_fold = sides || backs ? 0.0f : kRearGain;
    const float rear = sides && backs ? kRearGain * kHalfPower : kRearGain;
    switch (speaker) {
    case Speaker::Left:
        gains[kBusLeft] = 1.0f;
        gains[kBusCentre] = centre_fold;
        gains[kBusRearLeft] = rear_fold;
        break;
    case Speaker::Right:
        gains[kBusRight] = 1.0f;
        gains[kBusCentre] = centre_fold;
        gains[kBusRearRight] = rear_fold;
        break;
    case Speaker::Center:
        gains[kBusCentre] = centre ? 1.0f : 0.0f;
        break;
    case Speaker::SideLeft: gains[kBusRearLeft] = sides ? rear : 0.0f; break;
    case Speaker::SideRight: gains[kBusRearRight] = sides ? rear : 0.0f; break;
    case Speaker::BackLeft: gains[kBusRearLeft] = backs ? rear : 0.0f; break;
    case Speaker::BackRight: gains[kBusRearRight] = backs ? rear : 0.0f; break;
    default: break;
    }
    return gains;
}

PlayedLayout played_on(SpeakerChoice choice, std::uint8_t reported, std::uint8_t opened) noexcept {
    PlayedLayout played = resolve_speakers(choice, reported);
    played.centre = played.centre && has(opened, kSpeakersCentre);
    played.sides = played.sides && has(opened, kSpeakersSides);
    played.backs = played.backs && has(opened, kSpeakersBacks);
    return played;
}

bool SpeakerStage::same_mix(const PlayedLayout& a, const PlayedLayout& b) noexcept {
    return a.two == b.two && a.rear() == b.rear();
}

std::size_t SpeakerStage::fade_frames(std::uint32_t rate) noexcept {
    return std::max<std::size_t>(rate / 100u, 1u);
}

bool SpeakerStage::set_form(const PlayedLayout& played, std::uint32_t rate) {
    fade_frames_ = fade_frames(rate);
    hold_frames_ = (rate + 399u) / 400u;
    if (!form_set_) {
        form_ = played;
        form_set_ = true;
        return false;
    }
    if (played == form_) {
        return false;
    }
    const bool same = same_mix(played, form_);
    form_ = played;
    if (same) {
        return false;
    }
    // The frames waiting fade out over at most 10 ms; the rest of them is dropped.
    const std::size_t total = std::min(waiting(), fade_frames_);
    std::size_t offset = 0u;
    for (Block& block : blocks_) {
        const std::size_t keep = std::min(block.end - block.read, total - offset);
        block.end = block.read + keep;
        block.fade_out = total;
        block.fade_out_from = block.read;
        block.fade_out_offset = offset;
        offset += keep;
    }
    pending_fade_in_ = fade_frames_;
    return true;
}

void SpeakerStage::push(std::span<const float> front, std::span<const float> apart) {
    const std::size_t frames = front.size() / 2u;
    if (frames == 0u) {
        return;
    }
    if (blocks_.empty() || blocks_.back().form != form_ || pending_fade_in_ != 0u || blocks_.back().fade_out != 0u) {
        Block block;
        block.form = form_;
        if (pending_fade_in_ != 0u) {
            block.fade_in_hold = hold_frames_;
            block.fade_in = hold_frames_ + pending_fade_in_;
        }
        pending_fade_in_ = 0u;
        blocks_.push_back(std::move(block));
    }
    Block& block = blocks_.back();
    block.front.insert(block.front.end(), front.begin(), front.begin() + static_cast<std::ptrdiff_t>(frames * 2u));
    if (form_.placed()) {
        const std::size_t at = block.apart.size();
        block.apart.resize(at + frames * 3u, 0.0f);
        std::copy_n(apart.begin(), std::min(apart.size(), frames * 3u), block.apart.begin() + static_cast<std::ptrdiff_t>(at));
    }
    block.end += frames;
}

std::size_t SpeakerStage::waiting() const noexcept {
    std::size_t frames = 0u;
    for (const Block& block : blocks_) {
        frames += block.end - block.read;
    }
    return frames;
}

void SpeakerStage::clear() {
    blocks_.clear();
    pending_fade_in_ = fade_frames_;
}

void SpeakerStage::make_gains(const PlayedLayout& form, std::span<const Speaker> speakers, std::uint8_t opened,
                              std::vector<float>& into) const {
    into.assign(speakers.size() * kMixBuses, 0.0f);
    for (std::size_t channel = 0; channel < speakers.size(); ++channel) {
        const auto gains = placed_speaker_gains(speakers[channel], form, opened);
        std::copy(gains.begin(), gains.end(), into.begin() + static_cast<std::ptrdiff_t>(channel * kMixBuses));
    }
}

std::size_t SpeakerStage::pull(float* out, std::span<const Speaker> speakers, std::size_t frames) {
    const std::size_t channels = speakers.size();
    std::size_t written = 0u;
    while (written < frames && !blocks_.empty()) {
        Block& block = blocks_.front();
        if (block.read >= block.end) {
            if (blocks_.size() == 1u && block.fade_out == 0u) {
                // The block frames are pushed into: emptied for the next ones, its room kept.
                block.fade_in_done = std::min(block.fade_in, block.fade_in_done + block.end);
                block.front.clear();
                block.apart.clear();
                block.read = block.end = 0u;
                break;
            }
            blocks_.erase(blocks_.begin());
            continue;
        }
        // Frames fading out keep their own form's gains on the speakers still there (what went to a speaker that is
        // gone goes with it, nothing folds onto the others); the rest play in the current form, which mixes the same
        // way as theirs.
        const bool fading = block.fade_out != 0u;
        const PlayedLayout& form = fading ? block.form : form_;
        const bool speakers_changed = !std::equal(speakers.begin(), speakers.end(), speakers_.begin(), speakers_.end());
        if (block.form.placed() &&
            (!gains_valid_ || form != gains_form_ || fading != gains_fading_ || speakers_changed)) {
            // The new gains; a block that does not fade in or out moves to them over 10 ms from the gains each speaker
            // had (0 for a speaker that was not there).
            std::vector<float> from(channels * kMixBuses, 0.0f);
            for (std::size_t channel = 0; channel < channels && gains_valid_; ++channel) {
                const auto was = std::find(speakers_.begin(), speakers_.end(), speakers[channel]);
                if (was != speakers_.end()) {
                    const auto at = static_cast<std::size_t>(was - speakers_.begin()) * kMixBuses;
                    std::copy_n(gains_.begin() + static_cast<std::ptrdiff_t>(at), kMixBuses,
                                from.begin() + static_cast<std::ptrdiff_t>(channel * kMixBuses));
                }
            }
            const bool move = gains_valid_ && gains_form_.placed() && !fading && !gains_fading_ &&
                              block.fade_in_done + block.read >= block.fade_in;
            make_gains(form, speakers, fading ? std::uint8_t{0xFFu} : speaker_kinds(speakers), gains_);
            gains_from_ = std::move(from);
            move_left_ = move ? fade_frames_ : 0u;
            gains_form_ = form;
            gains_fading_ = fading;
            gains_valid_ = true;
        }
        if (speakers_changed) {
            speakers_.assign(speakers.begin(), speakers.end());
        }
        const std::size_t take = std::min(block.end - block.read, frames - written);
        const float* front = block.front.data() + block.read * 2u;
        const float* apart = block.form.placed() ? block.apart.data() + block.read * 3u : nullptr;
        float* to = out + written * channels;
        for (std::size_t i = 0; i < take; ++i, front += 2, to += channels) {
            const std::size_t at = block.read + i;
            float envelope = 1.0f;
            const std::size_t into = block.fade_in_done + at;
            if (into < block.fade_in) {
                envelope = into < block.fade_in_hold ? 0.0f
                                                     : static_cast<float>(into - block.fade_in_hold + 1u) /
                                                           static_cast<float>(block.fade_in - block.fade_in_hold + 1u);
            }
            if (block.fade_out != 0u) {
                const std::size_t done = block.fade_out_offset + (at - block.fade_out_from);
                envelope *= static_cast<float>(block.fade_out - done) / static_cast<float>(block.fade_out + 1u);
            }
            if (!block.form.placed()) {
                // The two-channel forms: left and right as made.
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    float value = 0.0f;
                    if (speakers[channel] == Speaker::Left) {
                        value = front[0];
                    } else if (speakers[channel] == Speaker::Right) {
                        value = front[1];
                    }
                    if (envelope != 1.0f) {
                        value *= envelope;
                    }
                    to[channel] = std::clamp(value, -1.0f, 1.0f);
                }
                continue;
            }
            const float buses[kMixBuses] = {front[0], front[1], apart[0], apart[1], apart[2]};
            apart += 3;
            const float moved = move_left_ != 0u ? static_cast<float>(move_left_) / static_cast<float>(fade_frames_ + 1u) : 0.0f;
            if (move_left_ != 0u) {
                --move_left_;
            }
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const float* gain = gains_.data() + channel * kMixBuses;
                const float* was = gains_from_.data() + channel * kMixBuses;
                float value = 0.0f;
                for (std::size_t bus = 0; bus < kMixBuses; ++bus) {
                    value += (gain[bus] + (was[bus] - gain[bus]) * moved) * buses[bus];
                }
                to[channel] = std::clamp(value * envelope, -1.0f, 1.0f);
            }
        }
        block.read += take;
        written += take;
    }
    return written;
}

}  // namespace galaxy::audio
