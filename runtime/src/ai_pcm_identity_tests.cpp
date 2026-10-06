#include "galaxy/ai_pcm_identity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>

namespace {

using galaxy::audio::AiPcmIdentityTracker;
using galaxy::audio::analyze_pcm16_be_stereo;

template <std::size_t Size>
std::span<const std::byte> bytes(const std::array<std::uint8_t, Size>& value) {
    return std::as_bytes(std::span(value));
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "ai_pcm_identity_tests: " << message << '\n';
    }
    return condition;
}

bool test_pcm_analysis() {
    const std::array<std::uint8_t, 8> pcm{
        0x00, 0x00, 0x7f, 0xff, 0x80, 0x00, 0xff, 0xff};
    const auto analysis = analyze_pcm16_be_stereo(bytes(pcm));
    return expect(analysis.sample_count == 4u, "sample count") &&
           expect(analysis.nonzero_samples == 3u, "nonzero count") &&
           expect(analysis.peak == 32768u, "peak") &&
           expect(analysis.mean_abs == 16384u, "mean absolute") &&
           expect(analysis.first_left == 0x0000u, "first left") &&
           expect(analysis.first_right == 0x7fffu, "first right") &&
           expect(analysis.content_hash != 0u, "content hash");
}

bool test_analysis_gate() {
    return expect(
               galaxy::audio::should_analyze_ai_pcm(false, false, false),
               "AI PCM analysis is retained before the first audible buffer") &&
           expect(
               !galaxy::audio::should_analyze_ai_pcm(false, false, true),
               "ordinary audible playback skips diagnostic PCM analysis") &&
           expect(
               galaxy::audio::should_analyze_ai_pcm(true, false, true),
               "identity measurement retains complete PCM analysis") &&
           expect(
               galaxy::audio::should_analyze_ai_pcm(false, true, true),
               "PCM tracing retains complete PCM analysis");
}

bool test_exact_window_identity() {
    const std::array<std::uint8_t, 8> a{
        0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04};
    const std::array<std::uint8_t, 8> b{
        0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x05};
    const std::array<std::uint8_t, 8> silence{};
    const auto analysis_a = analyze_pcm16_be_stereo(bytes(a));
    const auto analysis_b = analyze_pcm16_be_stereo(bytes(b));
    const auto analysis_silence = analyze_pcm16_be_stereo(bytes(silence));

    AiPcmIdentityTracker tracker;
    tracker.observe(0x807ad040u, 32'000u, bytes(a), analysis_a);
    tracker.observe(0x807ad040u, 32'000u, bytes(a), analysis_a);
    tracker.observe(0x807ae040u, 32'000u, bytes(a), analysis_a);
    tracker.observe(0x807ae040u, 32'000u, bytes(b), analysis_b);
    tracker.observe(0x807ae040u, 32'000u, bytes(silence), analysis_silence);
    tracker.observe(0x807ae040u, 32'000u, bytes(a), analysis_a);
    tracker.record_sink_submit();

    const auto& stats = tracker.stats();
    const std::uint64_t three_frame_duration_ns =
        (3u * 2u * 1'000'000'000ull + 32'000u - 1u) / 32'000u;
    return expect(stats.buffers == 6u, "buffer count") &&
           expect(stats.stereo_frames == 12u, "stereo frame count") &&
           expect(stats.audible_buffers == 5u, "audible buffer count") &&
           expect(stats.nonzero_samples == 20u, "window nonzero samples") &&
           expect(stats.peak_window == 5u, "window peak") &&
           expect(stats.content_change_events == 3u,
                  "content change count") &&
           expect(stats.distinct_content_identities == 3u,
                  "collision-resolved distinct count") &&
           expect(stats.address_change_events == 1u,
                  "address change count") &&
           expect(stats.identical_audible_run_max == 3u,
                  "longest repeated audible run") &&
           expect(stats.identical_audible_run_max_duration_ns ==
                      three_frame_duration_ns,
                  "longest repeated audible duration") &&
           expect(stats.repeated_known_content_run_max == 2u,
                  "longest already-known content sequence") &&
           expect(stats.sink_submit_buffers == 1u, "sink submit count");
}

bool test_alternating_freeze_and_pure_silence() {
    const std::array<std::uint8_t, 8> a{
        0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04};
    const std::array<std::uint8_t, 8> b{
        0x00, 0x05, 0x00, 0x06, 0x00, 0x07, 0x00, 0x08};
    const std::array<std::uint8_t, 8> silence{};
    const auto analysis_a = analyze_pcm16_be_stereo(bytes(a));
    const auto analysis_b = analyze_pcm16_be_stereo(bytes(b));
    const auto analysis_silence = analyze_pcm16_be_stereo(bytes(silence));

    AiPcmIdentityTracker alternating;
    alternating.observe(0x807ad040u, 32'000u, bytes(a), analysis_a);
    alternating.observe(0x807ae040u, 32'000u, bytes(b), analysis_b);
    alternating.observe(0x807ad040u, 32'000u, bytes(a), analysis_a);
    alternating.observe(0x807ae040u, 32'000u, bytes(b), analysis_b);
    alternating.observe(0x807ad040u, 32'000u, bytes(a), analysis_a);
    alternating.observe(0x807ae040u, 32'000u, bytes(b), analysis_b);
    const auto& alternating_stats = alternating.stats();

    AiPcmIdentityTracker pure_silence;
    for (unsigned index = 0; index < 8u; ++index) {
        pure_silence.observe(
            0x807ad040u,
            32'000u,
            bytes(silence),
            analysis_silence);
    }
    const auto& silence_stats = pure_silence.stats();
    const std::uint64_t four_buffer_duration_ns =
        (4u * 2u * 1'000'000'000ull + 32'000u - 1u) / 32'000u;
    return expect(
               alternating_stats.identical_audible_run_max == 1u,
               "alternating freeze defeats only an identical-adjacent check") &&
           expect(
               alternating_stats.repeated_known_content_run_max == 4u,
               "alternating A/B freeze is an exact already-known sequence") &&
           expect(
               alternating_stats
                       .repeated_known_content_run_max_duration_ns ==
                   four_buffer_duration_ns,
               "alternating freeze duration") &&
           expect(
               silence_stats.repeated_known_content_run_max == 0u,
               "pure digital silence is not reported as a stuck audible loop");
}

bool test_reset_is_window_local() {
    const std::array<std::uint8_t, 4> pcm{0x00, 0x01, 0x00, 0x02};
    const auto analysis = analyze_pcm16_be_stereo(bytes(pcm));
    AiPcmIdentityTracker tracker;
    tracker.observe(0x80001000u, 48'000u, bytes(pcm), analysis);
    tracker.record_sink_submit();
    tracker.reset();
    const auto& stats = tracker.stats();
    return expect(stats.buffers == 0u, "reset buffer count") &&
           expect(stats.distinct_content_identities == 0u,
                  "reset distinct count") &&
           expect(stats.identical_audible_run_max == 0u,
                  "reset run count") &&
           expect(stats.repeated_known_content_run_max == 0u,
                  "reset known-content run count") &&
           expect(stats.sink_submit_buffers == 0u, "reset sink count");
}

bool test_exact_native_rate_ledger() {
    const std::array<std::uint8_t, 8> two_frames{
        0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04};
    const std::array<std::uint8_t, 4> one_frame{
        0x00, 0x05, 0x00, 0x06};
    const auto two_frame_analysis =
        analyze_pcm16_be_stereo(bytes(two_frames));
    const auto one_frame_analysis = analyze_pcm16_be_stereo(bytes(one_frame));

    AiPcmIdentityTracker tracker;
    tracker.observe(
        0x80001000u,
        32'000u,
        bytes(two_frames),
        two_frame_analysis);
    tracker.observe(
        0x80002000u,
        48'000u,
        bytes(two_frames),
        two_frame_analysis);
    tracker.observe(
        0x80003000u,
        32'000u,
        bytes(one_frame),
        one_frame_analysis);

    const auto& stats = tracker.stats();
    constexpr std::uint64_t duration_32_two_frames = 62'500u;
    constexpr std::uint64_t duration_48_two_frames = 41'667u;
    constexpr std::uint64_t duration_32_one_frame = 31'250u;
    bool passed = expect(stats.buffers == 3u, "rate ledger buffer total") &&
        expect(stats.stereo_frames == 5u, "rate ledger frame total") &&
        expect(stats.buffers_32000 == 2u, "32 kHz buffer count") &&
        expect(stats.stereo_frames_32000 == 3u, "32 kHz frame count") &&
        expect(stats.buffers_48000 == 1u, "48 kHz buffer count") &&
        expect(stats.stereo_frames_48000 == 2u, "48 kHz frame count") &&
        expect(stats.sample_rate_change_events == 2u,
               "native rate transition count") &&
        expect(stats.total_duration_ns ==
                   duration_32_two_frames + duration_48_two_frames +
                       duration_32_one_frame,
               "mixed-rate duration is the exact per-buffer sum") &&
        expect(stats.min_buffer_duration_ns == duration_32_one_frame,
               "mixed-rate minimum buffer duration") &&
        expect(stats.max_buffer_duration_ns == duration_32_two_frames,
               "mixed-rate maximum buffer duration");

    bool unsupported_failed = false;
    try {
        tracker.observe(
            0x80004000u,
            44'100u,
            bytes(one_frame),
            one_frame_analysis);
    } catch (const std::invalid_argument&) {
        unsupported_failed = true;
    }
    passed &= expect(
        unsupported_failed,
        "non-native AI sample rate hard-fails identity accounting");
    return passed;
}

}  // namespace

int main() {
    if (!test_pcm_analysis() || !test_analysis_gate() ||
        !test_exact_window_identity() ||
        !test_alternating_freeze_and_pure_silence() ||
        !test_reset_is_window_local() || !test_exact_native_rate_ledger()) {
        return 1;
    }
    std::cout << "ai_pcm_identity_tests: PASS\n";
    return 0;
}
