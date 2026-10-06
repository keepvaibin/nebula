#pragma once

// Adapted from Dusklight's CC0 movie-audio queue/decoder ownership:
// src/d/actor/d_a_movie_player.cpp::{daMP_AudioDecoder,
// daMP_PushDecodedAudioBuffer,daMP_PopDecodedAudioBuffer,daMP_MixAudio}
// https://github.com/TwilitRealm/dusklight/tree/ad979d3dae092d0f5cbdaf49eabca7b4f1db4838
// Galaxy's movie owns 20 slots, rather than Dusklight's three. This primitive
// has no guest address or guest-memory API. The producer copies
// compressed input before the worker can see it; the consumer copies 32-kHz
// interleaved PCM exactly once for each strictly ordered audio callback.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace galaxy::host {

class ThpAudioWorker {
public:
    static constexpr std::size_t kGalaxyMovieSlots = 20u;
    using Decode = bool (*)(std::span<const std::byte>,
                           std::vector<std::int16_t>&);

    enum class EnqueueStatus {
        Accepted,
        WrongSequence,
        Full,
        InvalidInput,
        Failed,
    };
    enum class DrainStatus {
        Complete,
        WrongCallback,
        Starved,
        DecodeFailed,
        InvalidOutput,
    };
    struct DrainResult {
        DrainStatus status{};
        std::size_t stereo_frames_copied{};
    };
    struct Stats {
        std::uint64_t enqueued{};
        std::uint64_t decoded{};
        std::uint64_t completed_frames{};
        std::uint64_t callbacks{};
        std::uint64_t wrong_callbacks{};
        std::uint64_t starvation_events{};
        std::size_t outstanding{};
        std::size_t decoded_ready{};
    };

    ThpAudioWorker(Decode decoder, std::size_t max_encoded_bytes,
                   std::size_t max_stereo_frames_per_frame)
        : decoder_(decoder), max_encoded_bytes_(max_encoded_bytes),
          max_stereo_frames_per_frame_(max_stereo_frames_per_frame),
          worker_([this] { worker_loop(); }) {}

    ThpAudioWorker(const ThpAudioWorker&) = delete;
    ThpAudioWorker& operator=(const ThpAudioWorker&) = delete;

    ~ThpAudioWorker() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        work_ready_.notify_all();
        decoded_ready_.notify_all();
        worker_.join();
    }

    // Called only from the guest-owning thread after checking the full encoded
    // source span. No guest pointer survives this copy.
    [[nodiscard]] EnqueueStatus enqueue(
        std::uint64_t sequence, std::span<const std::byte> encoded) {
        if (decoder_ == nullptr || encoded.empty() ||
            encoded.size() > max_encoded_bytes_ ||
            max_stereo_frames_per_frame_ == 0u) {
            return EnqueueStatus::InvalidInput;
        }
        std::vector<std::byte> owned(encoded.begin(), encoded.end());
        {
            std::lock_guard lock(mutex_);
            if (stopping_ || decode_failed_ || starved_) {
                return EnqueueStatus::Failed;
            }
            if (sequence != next_input_sequence_) {
                return EnqueueStatus::WrongSequence;
            }
            if (outstanding_ == kGalaxyMovieSlots) {
                return EnqueueStatus::Full;
            }
            pending_.push_back({sequence, std::move(owned)});
            ++next_input_sequence_;
            ++outstanding_;
            ++stats_.enqueued;
        }
        work_ready_.notify_one();
        return EnqueueStatus::Accepted;
    }

    // The caller mixes the returned PCM into JAudio once. A repeated or
    // skipped callback number is rejected without touching the destination.
    // A genuine underrun returns silence and latches failure, preventing old
    // decoded movie audio from being replayed at a later timestamp.
    [[nodiscard]] DrainResult drain_callback(
        std::uint64_t callback_sequence,
        std::span<std::int16_t> interleaved_stereo) {
        std::lock_guard lock(mutex_);
        if (interleaved_stereo.empty() ||
            (interleaved_stereo.size() & 1u) != 0u) {
            return {DrainStatus::InvalidOutput, 0u};
        }
        if (callback_sequence != next_callback_sequence_) {
            ++stats_.wrong_callbacks;
            return {DrainStatus::WrongCallback, 0u};
        }
        ++next_callback_sequence_;
        ++stats_.callbacks;
        if (decode_failed_) {
            std::fill(interleaved_stereo.begin(), interleaved_stereo.end(), std::int16_t{0});
            return {DrainStatus::DecodeFailed, 0u};
        }
        if (starved_) {
            std::fill(interleaved_stereo.begin(), interleaved_stereo.end(), std::int16_t{0});
            return {DrainStatus::Starved, 0u};
        }
        std::size_t copied_samples = 0u;
        while (copied_samples < interleaved_stereo.size() && !ready_.empty()) {
            ReadyFrame& front = ready_.front();
            if (front.sequence != next_play_sequence_) {
                decode_failed_ = true;
                break;
            }
            const std::size_t remaining = front.pcm.size() - front.sample_offset;
            const std::size_t take = std::min(
                remaining, interleaved_stereo.size() - copied_samples);
            std::copy_n(front.pcm.data() + front.sample_offset, take,
                        interleaved_stereo.data() + copied_samples);
            front.sample_offset += take;
            copied_samples += take;
            if (front.sample_offset == front.pcm.size()) {
                ready_.pop_front();
                --outstanding_;
                ++next_play_sequence_;
                ++stats_.completed_frames;
            }
        }
        if (decode_failed_) {
            std::fill(interleaved_stereo.begin() + copied_samples,
                      interleaved_stereo.end(), std::int16_t{0});
            return {DrainStatus::DecodeFailed, copied_samples / 2u};
        }
        if (copied_samples != interleaved_stereo.size()) {
            std::fill(interleaved_stereo.begin() + copied_samples,
                      interleaved_stereo.end(), std::int16_t{0});
            starved_ = true;
            ++stats_.starvation_events;
            return {DrainStatus::Starved, copied_samples / 2u};
        }
        return {DrainStatus::Complete, copied_samples / 2u};
    }

    // Prebuffer/test gate only. The real-time audio callback must never wait.
    [[nodiscard]] bool wait_for_decoded_frames(
        std::size_t minimum,
        std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        decoded_ready_.wait_for(lock, timeout, [&] {
            return ready_.size() >= minimum || decode_failed_ || stopping_;
        });
        return ready_.size() >= minimum && !decode_failed_;
    }

    [[nodiscard]] Stats stats() const {
        std::lock_guard lock(mutex_);
        Stats result = stats_;
        result.outstanding = outstanding_;
        result.decoded_ready = ready_.size();
        return result;
    }

private:
    struct EncodedFrame {
        std::uint64_t sequence{};
        std::vector<std::byte> bytes;
    };
    struct ReadyFrame {
        std::uint64_t sequence{};
        std::vector<std::int16_t> pcm;
        std::size_t sample_offset{};
    };

    void worker_loop() {
        for (;;) {
            EncodedFrame input;
            {
                std::unique_lock lock(mutex_);
                work_ready_.wait(lock, [&] {
                    return stopping_ || !pending_.empty();
                });
                if (stopping_) {
                    return;
                }
                input = std::move(pending_.front());
                pending_.pop_front();
            }
            std::vector<std::int16_t> pcm;
            bool valid = false;
            try {
                valid = decoder_(input.bytes, pcm);
            } catch (...) {
                valid = false;
            }
            valid = valid && !pcm.empty() && (pcm.size() & 1u) == 0u &&
                pcm.size() / 2u <= max_stereo_frames_per_frame_;
            bool terminal = false;
            {
                std::lock_guard lock(mutex_);
                if (!valid || input.sequence != next_decoded_sequence_) {
                    decode_failed_ = true;
                    terminal = true;
                } else {
                    ready_.push_back({input.sequence, std::move(pcm), 0u});
                    ++next_decoded_sequence_;
                    ++stats_.decoded;
                }
            }
            decoded_ready_.notify_all();
            if (terminal) {
                return;
            }
        }
    }

    Decode decoder_{};
    std::size_t max_encoded_bytes_{};
    std::size_t max_stereo_frames_per_frame_{};
    mutable std::mutex mutex_;
    std::condition_variable work_ready_;
    std::condition_variable decoded_ready_;
    std::deque<EncodedFrame> pending_;
    std::deque<ReadyFrame> ready_;
    std::size_t outstanding_{};
    std::uint64_t next_input_sequence_{};
    std::uint64_t next_decoded_sequence_{};
    std::uint64_t next_play_sequence_{};
    std::uint64_t next_callback_sequence_{};
    bool stopping_{};
    bool decode_failed_{};
    bool starved_{};
    Stats stats_{};
    std::thread worker_;
};

}  // namespace galaxy::host
