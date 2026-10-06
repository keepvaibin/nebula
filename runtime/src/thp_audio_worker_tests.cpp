#include "galaxy/thp_audio_worker.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using galaxy::host::ThpAudioWorker;
using namespace std::chrono_literals;

bool expect(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "THP audio worker: %s\n", description);
    }
    return condition;
}

bool decode_one_frame(std::span<const std::byte> encoded,
                      std::vector<std::int16_t>& pcm) {
    if (encoded.size() != 1u) {
        return false;
    }
    const auto sample = static_cast<std::int16_t>(
        std::to_integer<unsigned>(encoded[0]));
    pcm = {sample, static_cast<std::int16_t>(-sample)};
    return true;
}

std::atomic<bool> release_first_decode{false};

bool decode_after_copy_proof(std::span<const std::byte> encoded,
                             std::vector<std::int16_t>& pcm) {
    while (!release_first_decode.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    return decode_one_frame(encoded, pcm);
}

bool reject_frame(std::span<const std::byte>,
                  std::vector<std::int16_t>&) {
    return false;
}

bool decode_two_frames(std::span<const std::byte> encoded,
                       std::vector<std::int16_t>& pcm) {
    if (encoded.size() != 1u) {
        return false;
    }
    const auto sample = static_cast<std::int16_t>(
        std::to_integer<unsigned>(encoded[0]));
    pcm = {sample, static_cast<std::int16_t>(-sample),
           static_cast<std::int16_t>(sample + 10),
           static_cast<std::int16_t>(-sample - 10)};
    return true;
}

bool owns_input_and_preserves_twenty_slot_order() {
    release_first_decode.store(false, std::memory_order_release);
    ThpAudioWorker worker(&decode_after_copy_proof, 1u, 1u);
    bool ok = true;
    std::array<std::byte, 1> input{std::byte{1}};
    ok &= expect(worker.enqueue(0u, input) ==
                     ThpAudioWorker::EnqueueStatus::Accepted,
                 "first encoded frame accepted");
    input[0] = std::byte{99};  // Worker must only see the owned copy.
    for (std::uint64_t sequence = 1u;
         sequence < ThpAudioWorker::kGalaxyMovieSlots; ++sequence) {
        const std::array<std::byte, 1> next{
            static_cast<std::byte>(sequence + 1u)};
        ok &= expect(worker.enqueue(sequence, next) ==
                         ThpAudioWorker::EnqueueStatus::Accepted,
                     "frame accepted in order");
    }
    const std::array<std::byte, 1> extra{std::byte{21}};
    ok &= expect(worker.enqueue(20u, extra) ==
                     ThpAudioWorker::EnqueueStatus::Full,
                 "twenty occupied movie slots reject another frame");
    release_first_decode.store(true, std::memory_order_release);
    ok &= expect(worker.wait_for_decoded_frames(20u, 2s),
                 "all twenty owned frames decoded");

    std::array<std::int16_t, 2> output{};
    for (std::uint64_t callback = 0u; callback < 20u; ++callback) {
        output = {-1, -1};
        const auto result = worker.drain_callback(callback, output);
        const auto expected = static_cast<std::int16_t>(callback + 1u);
        ok &= expect(result.status == ThpAudioWorker::DrainStatus::Complete &&
                         result.stereo_frames_copied == 1u &&
                         output[0] == expected && output[1] == -expected,
                     "decoded movie frames retain source order and input copy");
        if (callback == 0u) {
            output = {123, 456};
            const auto duplicate = worker.drain_callback(0u, output);
            ok &= expect(duplicate.status ==
                             ThpAudioWorker::DrainStatus::WrongCallback &&
                             output[0] == 123 && output[1] == 456,
                         "same callback cannot mix the movie frame twice");
        }
    }
    ok &= expect(worker.enqueue(20u, extra) ==
                     ThpAudioWorker::EnqueueStatus::Accepted,
                 "consumed slot admits next ordered frame");
    ok &= expect(worker.wait_for_decoded_frames(1u, 2s),
                 "replacement frame decoded");
    const auto final = worker.drain_callback(20u, output);
    ok &= expect(final.status == ThpAudioWorker::DrainStatus::Complete &&
                     output[0] == 21 && output[1] == -21,
                 "twenty-first frame follows the original ring order");
    const auto stats = worker.stats();
    ok &= expect(stats.enqueued == 21u && stats.decoded == 21u &&
                     stats.completed_frames == 21u && stats.callbacks == 21u &&
                     stats.wrong_callbacks == 1u && stats.outstanding == 0u,
                 "queue accounting is exact");
    return ok;
}

bool fails_closed_on_underrun_and_decode_error() {
    bool ok = true;
    {
        ThpAudioWorker worker(&decode_one_frame, 1u, 1u);
        std::array<std::int16_t, 2> output{123, 456};
        const auto result = worker.drain_callback(0u, output);
        ok &= expect(result.status == ThpAudioWorker::DrainStatus::Starved &&
                         result.stereo_frames_copied == 0u &&
                         output[0] == 0 && output[1] == 0,
                     "empty decoded queue gives explicit silence and starvation");
        const std::array<std::byte, 1> frame{std::byte{1}};
        ok &= expect(worker.enqueue(0u, frame) ==
                         ThpAudioWorker::EnqueueStatus::Failed,
                     "old movie frame cannot play after an underrun");
    }
    {
        ThpAudioWorker worker(&reject_frame, 1u, 1u);
        const std::array<std::byte, 1> frame{std::byte{1}};
        ok &= expect(worker.enqueue(0u, frame) ==
                         ThpAudioWorker::EnqueueStatus::Accepted,
                     "malformed compressed frame reaches owned decoder");
        ok &= expect(!worker.wait_for_decoded_frames(1u, 2s),
                     "decode failure prevents PCM publication");
        std::array<std::int16_t, 2> output{123, 456};
        const auto result = worker.drain_callback(0u, output);
        ok &= expect(result.status == ThpAudioWorker::DrainStatus::DecodeFailed &&
                         output[0] == 0 && output[1] == 0,
                     "decode failure cannot leak stale PCM");
    }
    return ok;
}

bool drains_partial_movie_frames_once_across_callbacks() {
    ThpAudioWorker worker(&decode_two_frames, 1u, 2u);
    const std::array<std::byte, 1> first{std::byte{1}};
    const std::array<std::byte, 1> second{std::byte{2}};
    bool ok = worker.enqueue(0u, first) ==
                  ThpAudioWorker::EnqueueStatus::Accepted &&
              worker.enqueue(1u, second) ==
                  ThpAudioWorker::EnqueueStatus::Accepted &&
              worker.wait_for_decoded_frames(2u, 2s);
    std::array<std::int16_t, 6> first_output{};
    const auto first_result = worker.drain_callback(0u, first_output);
    ok &= expect(first_result.status == ThpAudioWorker::DrainStatus::Complete &&
                     first_result.stereo_frames_copied == 3u &&
                     first_output == std::array<std::int16_t, 6>{
                         1, -1, 11, -11, 2, -2},
                 "callback crosses movie frame boundary in source order");
    std::array<std::int16_t, 2> second_output{};
    const auto second_result = worker.drain_callback(1u, second_output);
    ok &= expect(second_result.status == ThpAudioWorker::DrainStatus::Complete &&
                     second_output == std::array<std::int16_t, 2>{12, -12},
                 "next callback consumes only previously unplayed PCM");
    return ok;
}

}  // namespace

int main() {
    const bool ok = owns_input_and_preserves_twenty_slot_order() &
        fails_closed_on_underrun_and_decode_error() &
        drains_partial_movie_frames_once_across_callbacks();
    return ok ? 0 : 1;
}
