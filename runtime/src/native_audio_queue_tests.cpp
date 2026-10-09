#include "galaxy/native_audio_queue.h"
#include "galaxy/native_audio_adpcm.h"
#include "galaxy/native_audio_dusk_dsp.h"
#include "galaxy/native_audio_anomaly.h"
#include "galaxy/native_audio_bus.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

struct FailureNotificationProbe {
    std::uint32_t calls{};
};

void record_failure_notification(void* user) noexcept {
    auto* const probe = static_cast<FailureNotificationProbe*>(user);
    if (probe != nullptr) {
        ++probe->calls;
    }
}

// Model the two legal orderings at a source-buffer boundary.  The callback
// observes the queue at the exact end boundary, so a replacement submitted
// before completion is continuous playback while a replacement submitted
// afterward cannot erase the empty-boundary evidence.
class InterleavingVoice final {
public:
    explicit InterleavingVoice(
        galaxy::host::NativeAudioCallbackLedger& callbacks)
        : callbacks_(callbacks) {}

    void queue_replacement() noexcept {
        ++queued_buffers_;
    }

    [[nodiscard]] bool finish_current_buffer() noexcept {
        if (queued_buffers_ == 0u) {
            return false;
        }
        --queued_buffers_;
        callbacks_.record_buffer_end(
            queued_buffers_ == 0u
                ? galaxy::host::NativeAudioBufferEndQueueEvidence::
                      EmptyAtBoundary
                : galaxy::host::NativeAudioBufferEndQueueEvidence::
                      SuccessorQueued);
        return true;
    }

    [[nodiscard]] bool submit_while_previous_buffer_ends() noexcept {
        queue_replacement();
        return finish_current_buffer();
    }

    [[nodiscard]] std::uint32_t get_state() const noexcept {
        return queued_buffers_;
    }

private:
    galaxy::host::NativeAudioCallbackLedger& callbacks_;
    std::uint32_t queued_buffers_{1};
};

}  // namespace

int main() {
    bool passed = true;
    {
        galaxy::host::NativeAudioRecoveryTask task;
        std::atomic_bool entered{false}, release{false};
        unsigned published = 0;
        task.start([&] {
            entered.store(true, std::memory_order_release);
            entered.notify_one();
            release.wait(false, std::memory_order_acquire);
            published = 73;
        });
        entered.wait(false, std::memory_order_acquire);
        bool duplicate_rejected = false;
        try { task.start([] {}); }
        catch (const std::logic_error&) { duplicate_rejected = true; }
        bool nonblocking = task.active();
        for (unsigned i = 0; i < 10000; ++i) nonblocking &= !task.finish_ready();
        passed &= expect(nonblocking && duplicate_rejected,
            "blocked endpoint job never blocks health polling or permits overlapping ownership");
        release.store(true, std::memory_order_release);
        release.notify_one();
        task.finish();
        passed &= expect(!task.active() && task.finish_ready() && published == 73,
            "joining completed recovery publishes backend ownership and writes");
        task.start([] { throw std::runtime_error("recovery failure"); });
        bool failure_reported = false;
        try { task.finish(); }
        catch (const std::runtime_error&) { failure_reported = true; }
        passed &= expect(failure_reported && !task.active() && task.finish_ready(),
            "recovery exceptions are reported once after joining");
        task.start([&] { published = 91; });
        task.finish();
        passed &= expect(published == 91, "recovery can retry after an acknowledged failure");
    }
    {
        unsigned joined_at_destruction = 0;
        { galaxy::host::NativeAudioRecoveryTask task;
          task.start([&] { joined_at_destruction = 1; }); }
        passed &= expect(joined_at_destruction == 1,
            "recovery destruction joins before backend storage can be released");
    }

    for (const auto error : {0x80070490u, 0x88960004u, 0x88890004u}) {
        passed &= expect(galaxy::host::native_audio_endpoint_error_recoverable(error, false),
            "only known detached-endpoint errors can recover");
        passed &= expect(!galaxy::host::native_audio_endpoint_error_recoverable(error, true),
            "strict audio proof never accepts an unavailable endpoint");
    }
    for (const auto error : {0u, 0x80004005u, 0x8007000eu, 0x80070057u, 0x88960001u}) {
        passed &= expect(!galaxy::host::native_audio_endpoint_error_recoverable(error, false),
            "unknown, invalid-call and resource failures remain fatal");
    }
    passed &= expect(!galaxy::host::native_audio_endpoint_retry_due(999'999'999ull, 0u) &&
        galaxy::host::native_audio_endpoint_retry_due(1'000'000'000ull, 0u) &&
        !galaxy::host::native_audio_endpoint_retry_due(100u, 200u),
        "endpoint retry is bounded to one per second without unsigned wrap");
    galaxy::host::NativeAudioEmptyBoundaryTracker recreated_voice;
    recreated_voice.reset_after_voice_destruction(8u);
    passed &= expect(recreated_voice.observe(8u) == galaxy::host::NativeAudioEmptyBoundaryDisposition::None &&
        recreated_voice.observe(9u) == galaxy::host::NativeAudioEmptyBoundaryDisposition::RecoverUnderrun &&
        recreated_voice.observe(11u) == galaxy::host::NativeAudioEmptyBoundaryDisposition::FatalCounterDiscontinuity,
        "a new voice starts after quiesced flush events but retains live discontinuity checks");

    {
        using galaxy::host::native_audio_dusk_sync_frame_shape_supported;
        passed &= expect(
            native_audio_dusk_sync_frame_shape_supported(
                0x82074000u, 0u, 0u, 0u) &&
                native_audio_dusk_sync_frame_shape_supported(
                    0x82072ee0u, 6u, 0u, 0u) &&
                native_audio_dusk_sync_frame_shape_supported(
                    0x82070000u, 3u, 0u, 0u),
            "DsyncFrame2ch lower half is a variable global mixer level");
        passed &= expect(
            !native_audio_dusk_sync_frame_shape_supported(
                0x82074000u, 7u, 0u, 0u) &&
                !native_audio_dusk_sync_frame_shape_supported(
                    0x82094000u, 0u, 0u, 0u) &&
                !native_audio_dusk_sync_frame_shape_supported(
                    0x82078001u, 0u, 0u, 0u) &&
                !native_audio_dusk_sync_frame_shape_supported(
                    0x82074000u, 0u, 0x81000000u, 0u) &&
                !native_audio_dusk_sync_frame_shape_supported(
                    0x82074000u, 0u, 0u, 0x81000000u),
            "Dusk HLE rejects unsupported frame count, gain, or auxiliary output");
    }

    {
        // First active transplant kernel: check Dusklight's one-subframe
        // interpolation and the channel state that a guest adapter must write
        // back after rendering a 32-kHz PCM16 voice.
        std::array<std::uint8_t, 200> pcm_be{};
        for (std::size_t i = 0; i < 100; ++i) {
            pcm_be[i * 2] = 0x40;
        }
        galaxy::host::NativeDuskDspChannel channel{};
        channel.active = true;
        channel.reset = true;
        channel.pitch = 0x1000;
        channel.samples_per_block = 1;
        channel.bytes_per_block = 16;
        channel.end_sample = 100;
        channel.wave = pcm_be;
        galaxy::host::NativeDuskDspChannelAux aux{};
        std::array<float, galaxy::host::kNativeDuskDspSubframeSamples> out{};
        const auto result = galaxy::host::native_audio_dusk_render_channel_32k(
            channel, aux, out);
        passed &= expect(
            result == galaxy::host::NativeDuskDspRenderResult::Rendered &&
                out[0] == 0.0f && out[1] == 0.5f && out[79] == 0.5f &&
                channel.sample_position == 82u &&
                channel.aram_stream_position == 164u &&
                channel.samples_left == 18u &&
                aux.decoded_samples == 2u && aux.reset_count == 1u,
            "Dusklight PCM16 subframe preserves sample and feedback state");

        channel.filter_mode = 0x20u;
        passed &= expect(
            galaxy::host::native_audio_dusk_render_channel_32k(
                channel, aux, out) ==
                galaxy::host::NativeDuskDspRenderResult::UnsupportedFormat,
            "unported IIR filtering is explicitly rejected");
    }

    {
        // Dusklight routes source-zero channels through RenderOscChannel;
        // their 16-bit phase persists across DSP subframes and resets only
        // when the guest's channel reset flag is set.
        galaxy::host::NativeDuskDspChannel channel{};
        channel.active = true;
        channel.reset = true;
        channel.pitch = 0x1000u;
        channel.bytes_per_block = 0u;
        galaxy::host::NativeDuskDspChannelAux aux{};
        std::array<float, galaxy::host::kNativeDuskDspSubframeSamples> out{};
        using galaxy::host::NativeDuskDspRenderResult;
        using galaxy::host::native_audio_dusk_render_oscillator_32k;
        passed &= expect(
            native_audio_dusk_render_oscillator_32k(channel, aux, out) ==
                NativeDuskDspRenderResult::Rendered &&
                out[0] == 0.5f && out[16] == -0.5f &&
                out[32] == 0.5f && aux.oscillator_phase == 0x8000u &&
                aux.reset_count == 1u && !channel.reset,
            "Dusklight square oscillator preserves 16-bit phase and reset state");
        passed &= expect(
            native_audio_dusk_render_oscillator_32k(channel, aux, out) ==
                NativeDuskDspRenderResult::Rendered &&
                out[0] == -0.5f && aux.oscillator_phase == 0u,
            "oscillator phase continues across subframes");
        channel.bytes_per_block = 1u;
        passed &= expect(
            native_audio_dusk_render_oscillator_32k(channel, aux, out) ==
                NativeDuskDspRenderResult::Rendered &&
                out[0] == 0.0f && out[16] == -1.0f,
            "saw oscillator uses signed 16-bit phase");
        channel.bytes_per_block = 11u;
        const auto rejected_phase = aux.oscillator_phase;
        passed &= expect(
            native_audio_dusk_render_oscillator_32k(channel, aux, out) ==
                NativeDuskDspRenderResult::UnsupportedFormat &&
                aux.oscillator_phase == rejected_phase,
            "unported evolving harmonic oscillator fails closed");
        channel.bytes_per_block = 7u;
        channel.reset = true;
        passed &= expect(
            native_audio_dusk_render_oscillator_32k(channel, aux, out) ==
                NativeDuskDspRenderResult::Rendered &&
                out[0] == 0.0f &&
                std::fabs(out[8] - 0.5f) < 1.0e-6f &&
                aux.reset_count == 2u,
            "sine oscillator resets its phase with the guest channel");
    }

    {
        // Dusk's stereo routing must preserve both channel separation and
        // the 80-sample start-to-target gain ramp before any guest writeback.
        galaxy::host::NativeDuskDspStereoMix mix{};
        mix.buses[0] = {0x0d00u, 0x2ee0u, 0u};
        mix.buses[1] = {0x0d60u, 0u, 0x2ee0u};
        galaxy::host::NativeDuskDspChannelAux aux{};
        std::array<float, galaxy::host::kNativeDuskDspSubframeSamples> mono{};
        std::array<float, galaxy::host::kNativeDuskDspSubframeSamples> left{};
        std::array<float, galaxy::host::kNativeDuskDspSubframeSamples> right{};
        mono.fill(1.0f);
        const auto result = galaxy::host::native_audio_dusk_mix_stereo(
            mix, aux, mono, left, right);
        passed &= expect(
            result == galaxy::host::NativeDuskDspRenderResult::Rendered &&
                left[0] == 0.0f &&
                std::abs(left[79] - 79.0f / 80.0f) < 1.0e-6f &&
                right[0] == 1.0f &&
                std::abs(right[79] - 1.0f / 80.0f) < 1.0e-6f,
            "Dusklight stereo routing ramps independent guest buses");
        const auto expected_left = left;
        const auto expected_right = right;
        // The source mixer dispatches only connected records. Stale gain in
        // a disconnected slot must not invalidate otherwise supported stereo.
        mix.buses[2] = {0u, 0x2ee0u, 0x2ee0u};
        aux = {};
        left.fill(0.0f);
        right.fill(0.0f);
        passed &= expect(
            galaxy::host::native_audio_dusk_mix_stereo(
                mix, aux, mono, left, right) ==
                galaxy::host::NativeDuskDspRenderResult::Rendered &&
                left == expected_left && right == expected_right,
            "disconnected DSP bus ignores stale gain without changing stereo output");
        mix.effect_mix = 1u;
        passed &= expect(
            galaxy::host::native_audio_dusk_mix_stereo(
                mix, aux, mono, left, right) ==
                galaxy::host::NativeDuskDspRenderResult::UnsupportedRouting,
            "unported reverb routing fails closed");

        mix.effect_mix = 0u;
        mix.buses[2] = {0x0dc0u, 0u, 0u};
        mix.buses[3] = {0x0e20u, 0u, 0u};
        aux = {};
        left.fill(0.0f);
        right.fill(0.0f);
        passed &= expect(
            galaxy::host::native_audio_dusk_mix_stereo(
                mix, aux, mono, left, right) ==
                galaxy::host::NativeDuskDspRenderResult::Rendered &&
                left == expected_left && right == expected_right,
            "real RMGE01 connected zero-gain auxiliary buses preserve all stereo samples");
        mix.buses[2].target_volume = 1u;
        passed &= expect(
            galaxy::host::native_audio_dusk_mix_stereo(
                mix, aux, mono, left, right) ==
                galaxy::host::NativeDuskDspRenderResult::UnsupportedRouting,
            "an audible auxiliary target remains unsupported");
        mix.buses[2] = {0x0dc0u, 0u, 1u};
        passed &= expect(
            galaxy::host::native_audio_dusk_mix_stereo(
                mix, aux, mono, left, right) ==
                galaxy::host::NativeDuskDspRenderResult::UnsupportedRouting,
            "an auxiliary ramp starting audible remains unsupported");
        mix.buses[2] = {0x0352u, 0u, 0u};
        passed &= expect(
            galaxy::host::native_audio_dusk_mix_stereo(
                mix, aux, mono, left, right) ==
                galaxy::host::NativeDuskDspRenderResult::UnsupportedRouting,
            "an unknown zero-gain DSP address still fails closed");
        mix.buses[2] = {};
        mix.buses[3] = {};
        mix.buses[1].connect = 0x0352u;
        passed &= expect(
            galaxy::host::native_audio_dusk_mix_stereo(
                mix, aux, mono, left, right) ==
                galaxy::host::NativeDuskDspRenderResult::UnsupportedRouting,
            "unported auxiliary bus routing fails closed");

        mix = {};
        mix.auto_mixer = true;
        mix.pan_dolby = 127u << 8u;
        mix.auto_init_volume = 0x2ee0u;
        mix.auto_volume = 0x2ee0u;
        aux = {};
        left.fill(0.0f);
        right.fill(0.0f);
        passed &= expect(
            galaxy::host::native_audio_dusk_mix_stereo(
                mix, aux, mono, left, right) ==
                galaxy::host::NativeDuskDspRenderResult::Rendered &&
                left[0] == 0.0f && right[0] == 1.0f,
            "Dusklight auto mixer pans fully right");
    }

    {
        std::array<std::uint8_t, 54> adpcm{};
        for (std::size_t frame = 0; frame < 6; ++frame) {
            for (std::size_t packed = 1; packed < 9; ++packed) {
                adpcm[frame * 9 + packed] = 0x11;
            }
        }
        galaxy::host::NativeDuskDspChannel channel{};
        channel.active = true;
        channel.reset = true;
        channel.pitch = 0x1000;
        channel.samples_per_block = 16;
        channel.bytes_per_block = 9;
        channel.end_sample = 96;
        channel.wave = adpcm;
        galaxy::host::NativeDuskDspChannelAux aux{};
        std::array<float, galaxy::host::kNativeDuskDspSubframeSamples> out{};
        const auto result = galaxy::host::native_audio_dusk_render_channel_32k(
            channel, aux, out);
        passed &= expect(
            result == galaxy::host::NativeDuskDspRenderResult::Rendered &&
                out[0] == 0.0f && out[1] == (1.0f / 32768.0f) &&
                channel.sample_position == 96u &&
                channel.aram_stream_position == 54u &&
                aux.history_newer == 1 && aux.decoded_samples == 16u,
            "Dusklight AFC subframe decodes source and publishes channel feedback");
        const auto unfiltered_samples = out;
        channel.sample_position = 0u;
        channel.finished = false;
        channel.reset = true;
        channel.filter_mode = 0x20u;
        channel.iir_coefficients[0] = 0x7fff;
        aux = {};
        passed &= expect(
            galaxy::host::native_audio_dusk_render_channel_32k(
                channel, aux, out) ==
                galaxy::host::NativeDuskDspRenderResult::Rendered &&
                out == unfiltered_samples &&
                channel.sample_position == 96u &&
                channel.aram_stream_position == 54u,
            "real RMGE01 identity-IIR flag preserves every AFC sample and feedback");
        channel.iir_coefficients[1] = 1;
        const auto rejected_position = channel.sample_position;
        const auto rejected_reset_count = aux.reset_count;
        passed &= expect(
            galaxy::host::native_audio_dusk_render_channel_32k(
                channel, aux, out) ==
                galaxy::host::NativeDuskDspRenderResult::UnsupportedFormat &&
                channel.sample_position == rejected_position &&
                aux.reset_count == rejected_reset_count,
            "a nontrivial IIR remains unsupported without advancing its voice");
        channel.iir_coefficients[1] = 0;
        channel.filter_mode = 0x28u;
        passed &= expect(
            galaxy::host::native_audio_dusk_render_channel_32k(
                channel, aux, out) ==
                galaxy::host::NativeDuskDspRenderResult::UnsupportedFormat,
            "identity IIR cannot bypass an independently enabled FIR");
    }

    {
        // Dusklight's CC0 AFC decoder must agree with the RMGE01 predictor
        // and history order before it can be considered for live audio.
        std::array<std::uint8_t, 18> adpcm{};
        adpcm[0] = 0x10;  // Predictor 0, scale 2.
        adpcm[9] = 0x01;  // Predictor 1, scale 1, uses previous sample.
        for (std::size_t index = 1; index < 9; ++index) {
            adpcm[index] = 0x1f;
        }
        for (std::size_t index = 10; index < 18; ++index) {
            adpcm[index] = 0x11;
        }
        std::array<std::int16_t, 32> pcm{};
        std::int16_t older = 0;
        std::int16_t newer = 0;
        passed &= expect(galaxy::host::native_audio_decode_dusk_adpcm4(
            adpcm, pcm, older, newer),
            "Dusklight AFC decoder accepts two complete RMGE01 frames");
        bool samples_match = true;
        for (std::size_t index = 0; index < 16; ++index) {
            samples_match &= pcm[index] == (index % 2 == 0 ? 2 : -2);
            samples_match &= pcm[16 + index] ==
                static_cast<std::int16_t>(index - 1);
        }
        passed &= expect(samples_match && older == 13 && newer == 14,
            "Dusklight AFC samples and cross-frame predictor history match expected values");
        passed &= expect(!galaxy::host::native_audio_decode_dusk_adpcm4(
            std::span<const std::uint8_t>(adpcm).first(17), pcm,
            older, newer), "Dusklight AFC decoder rejects truncated frames");

        std::array<std::uint8_t, 9> clipped_frame{};
        clipped_frame[0] = 0xf0;
        clipped_frame[1] = 0x80;
        std::array<std::int16_t, 1> clipped_pcm{};
        older = 0;
        newer = 0;
        passed &= expect(galaxy::host::native_audio_decode_dusk_adpcm4(
            clipped_frame, clipped_pcm, older, newer) &&
            clipped_pcm[0] == -0x8000,
            "Dusklight negative saturation differs from Galaxy's current -0x7fff DSP policy");
    }

    {
        // Wii AI frame order is BE right/left; output order is LE left/right.
        const std::array<std::byte, 8> ai{{
            std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78},
            std::byte{0x9a}, std::byte{0xbc}, std::byte{0xde}, std::byte{0xf0}}};
        std::array<std::byte, 8> pcm{};
        passed &= expect(galaxy::host::native_audio_convert_wii_ai_pcm16(ai, pcm),
            "Wii AI PCM channel conversion accepts complete stereo frames");
        passed &= expect(pcm == std::array<std::byte, 8>{{
            std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12},
            std::byte{0xf0}, std::byte{0xde}, std::byte{0xbc}, std::byte{0x9a}}},
            "Wii AI PCM channel conversion preserves samples and stereo placement");
        passed &= expect(!galaxy::host::native_audio_convert_wii_ai_pcm16(
            std::span<const std::byte>(ai).first(4), pcm),
            "Wii AI PCM channel conversion rejects size mismatch");
    }

    {
        // Publishing writers race a reader. A successful read must never mix
        // payload fields from different producers or expose an unfinished slot.
        galaxy::host::NativeAudioAnomalyLedger<128u> ledger;
        std::atomic_uint32_t finished{0u};
        std::array<std::thread, 4u> writers;
        for (std::size_t producer = 0u; producer < writers.size(); ++producer) {
            writers[producer] = std::thread([&, producer] {
                for (std::uint64_t i = 0u; i < 32u; ++i) {
                    galaxy::host::NativeAudioAnomalyRecord record{};
                    record.kind = galaxy::host::NativeAudioAnomalyKind::Submitted;
                    record.buffer_sequence = producer * 32u + i + 1u;
                    record.host_ns = record.buffer_sequence * 1'000u;
                    record.accepted_ns = record.host_ns - 100u;
                    record.dequeued_ns = record.host_ns - 50u;
                    record.guest_ticks = record.buffer_sequence ^ 0x12345678u;
                    (void)ledger.record(record);
                }
                finished.fetch_add(1u, std::memory_order_release);
            });
        }
        bool coherent = true;
        do {
            for (std::size_t i = 0u; i < ledger.capacity(); ++i) {
                galaxy::host::NativeAudioAnomalyRecord record{};
                if (ledger.read(i, record)) {
                    coherent &= record.buffer_sequence >= 1u &&
                        record.buffer_sequence <= ledger.capacity() &&
                        record.host_ns == record.buffer_sequence * 1'000u &&
                        record.accepted_ns + 100u == record.host_ns &&
                        record.dequeued_ns + 50u == record.host_ns &&
                        record.guest_ticks == (record.buffer_sequence ^ 0x12345678u);
                }
            }
        } while (finished.load(std::memory_order_acquire) != writers.size());
        for (auto& writer : writers) {
            writer.join();
        }
        std::array<bool, 128u> seen{};
        for (std::size_t i = 0u; i < ledger.capacity(); ++i) {
            galaxy::host::NativeAudioAnomalyRecord record{};
            const bool available = ledger.read(i, record);
            coherent &= available && record.buffer_sequence >= 1u &&
                record.buffer_sequence <= ledger.capacity();
            if (available && record.buffer_sequence >= 1u &&
                record.buffer_sequence <= ledger.capacity()) {
                coherent &= !seen[record.buffer_sequence - 1u];
                seen[record.buffer_sequence - 1u] = true;
            }
        }
        passed &= expect(coherent && ledger.attempted() == 128u,
            "concurrent audio anomaly producers publish complete unique records");
        galaxy::host::NativeAudioAnomalyRecord before{};
        galaxy::host::NativeAudioAnomalyRecord after{};
        passed &= expect(ledger.read(0u, before),
            "the oldest audio anomaly remains readable");
        passed &= expect(!ledger.record({}) && !ledger.record({}) &&
            ledger.attempted() == 130u && ledger.read(0u, after) &&
            before.buffer_sequence == after.buffer_sequence &&
            before.host_ns == after.host_ns && !ledger.read(128u, after),
            "bounded audio overflow is explicit and never overwrites early evidence");
    }

    {
        using galaxy::audio::NativeAudioBus;
        using galaxy::audio::NativeAudioBusControls;

        // Category gain is a DSP-sample transformation. It must remain
        // completely outside the XAudio queue, playback, rate, and callback
        // timing state machines that establish underrun evidence.
        galaxy::host::NativeAudioCallbackLedger callbacks;
        callbacks.record_buffer_end(
            galaxy::host::NativeAudioBufferEndQueueEvidence::SuccessorQueued);
        galaxy::host::NativeAudioEmptyBoundaryTracker empty_boundaries;
        galaxy::host::NativeAudioPlaybackLifecycle playback;
        playback.mark_started();
        galaxy::host::NativeAudioRateLedger rates;
        const bool rate_recorded = rates.record_submission(32'000u, 1'280u);

        const std::uint64_t completed_before = callbacks.completed_buffers();
        const std::uint64_t nonempty_before =
            callbacks.nonempty_buffer_end_boundaries();
        const std::uint64_t empty_before =
            callbacks.empty_buffer_end_boundaries();
        const std::uint64_t event_before = callbacks.event_sequence();
        const std::uint64_t failure_before = callbacks.failure_sequence();
        const auto ratio_before = callbacks.frequency_ratio_snapshot();
        const auto rate_before = rates.snapshot();
        const auto playback_before = playback.state();
        const std::uint64_t handled_empty_before =
            empty_boundaries.handled_empty_boundaries();

        NativeAudioBusControls controls;
        std::array<NativeAudioBus, NativeAudioBusControls::kChannelCount>
            buses{};
        buses.fill(NativeAudioBus::Sfx);
        buses[0] = NativeAudioBus::Music;
        buses[1] = NativeAudioBus::Voice;
        buses[2] = NativeAudioBus::Sfx;
        buses[3] = NativeAudioBus::Ambience;
        controls.publish({1.0f, 0.25f, 0.5f, 0.75f, 1.0f}, buses);

        galaxy::audio::NativeDspAudioBusState bus_state{};
        std::array<std::int16_t, 4u> routed{};
        for (std::size_t channel = 0u; channel < routed.size(); ++channel) {
            galaxy::audio::native_dsp_audio_bus_begin_channel(
                bus_state, static_cast<std::uint16_t>(channel), &controls);
            routed[channel] = galaxy::audio::native_dsp_audio_bus_apply_sample(
                bus_state, 16'000);
        }
        passed &= expect(
            routed == std::array<std::int16_t, 4u>{4'000, 8'000, 12'000, 16'000},
            "music, voice, SFX, and ambience channels receive independent gains");

        // Updating only music starts only its channel-local ramp. Selecting
        // the other channels again must retain their exact prior gains.
        controls.publish({1.0f, 0.0f, 0.5f, 0.75f, 1.0f}, buses);
        galaxy::audio::native_dsp_audio_bus_begin_channel(
            bus_state, 0u, &controls);
        passed &= expect(
            bus_state.ramps[0].remaining ==
                    galaxy::audio::kNativeAudioBusRampSamples &&
                bus_state.ramps[1].remaining == 0u &&
                bus_state.ramps[2].remaining == 0u &&
                bus_state.ramps[3].remaining == 0u,
            "changing music does not start voice, SFX, or ambience ramps");
        for (std::size_t channel = 1u; channel < routed.size(); ++channel) {
            galaxy::audio::native_dsp_audio_bus_begin_channel(
                bus_state, static_cast<std::uint16_t>(channel), &controls);
            routed[channel] = galaxy::audio::native_dsp_audio_bus_apply_sample(
                bus_state, 16'000);
        }
        passed &= expect(
            routed[1] == 8'000 && routed[2] == 12'000 &&
                routed[3] == 16'000,
            "a music-only update leaves voice, SFX, and ambience samples unchanged");

        const auto ratio_after = callbacks.frequency_ratio_snapshot();
        const auto rate_after = rates.snapshot();
        passed &= expect(
            rate_recorded && callbacks.completed_buffers() == completed_before &&
                callbacks.nonempty_buffer_end_boundaries() == nonempty_before &&
                callbacks.empty_buffer_end_boundaries() == empty_before &&
                callbacks.event_sequence() == event_before &&
                callbacks.failure_sequence() == failure_before &&
                ratio_after.apply_calls == ratio_before.apply_calls &&
                ratio_after.change_actions == ratio_before.change_actions &&
                ratio_after.apply_errors == ratio_before.apply_errors &&
                rate_after.submitted_buffers_32000 ==
                    rate_before.submitted_buffers_32000 &&
                rate_after.submitted_stereo_frames_32000 ==
                    rate_before.submitted_stereo_frames_32000 &&
                rate_after.submitted_buffers_48000 ==
                    rate_before.submitted_buffers_48000 &&
                rate_after.submitted_stereo_frames_48000 ==
                    rate_before.submitted_stereo_frames_48000 &&
                rate_after.sample_rate_change_events ==
                    rate_before.sample_rate_change_events &&
                playback.state() == playback_before &&
                empty_boundaries.handled_empty_boundaries() ==
                    handled_empty_before,
            "category routing leaves queue, underrun, playback, rate, and callback timing state untouched");
    }

    using galaxy::host::NativeAudioInitializationGate;
    using galaxy::host::NativeAudioInitializationState;
    using galaxy::host::NativeAudioEmptyBoundaryDisposition;
    using galaxy::host::NativeAudioEmptyBoundaryTracker;
    using galaxy::host::NativeAudioPreinitializeMode;
    using galaxy::host::NativeAudioPlaybackAction;
    using galaxy::host::NativeAudioPlaybackLifecycle;
    using galaxy::host::NativeAudioPlaybackState;
    passed &= expect(
        galaxy::host::native_audio_preinitialize_mode(false, false) ==
            NativeAudioPreinitializeMode::NativeOutput,
        "normal output eagerly initializes the native endpoint");
    passed &= expect(
        galaxy::host::native_audio_preinitialize_mode(true, false) ==
            NativeAudioPreinitializeMode::SkipDisabled,
        "explicit audio disable skips endpoint initialization");
    passed &= expect(
        galaxy::host::native_audio_preinitialize_mode(false, true) ==
            NativeAudioPreinitializeMode::SkipWavDumpOnly &&
            galaxy::host::native_audio_preinitialize_mode(true, true) ==
                NativeAudioPreinitializeMode::SkipWavDumpOnly,
        "WAV-dump-only mode skips and retains sink submission precedence");

    NativeAudioInitializationGate preinitialized_gate;
    std::uint32_t ready_initializer_calls = 0;
    passed &= expect(
        preinitialized_gate.preinitialize([&] {
            ++ready_initializer_calls;
            return true;
        }),
        "pre-guest native audio initialization succeeds");
    auto initialization = preinitialized_gate.snapshot();
    passed &= expect(
        initialization.state == NativeAudioInitializationState::Ready &&
            initialization.initialization_attempts == 1u &&
            initialization.preinitialize_calls == 1u &&
            initialization.preinitialized &&
            ready_initializer_calls == 1u,
        "successful pre-initialization records one ready backend");
    passed &= expect(
        preinitialized_gate.ensure_ready_for_submit([&] {
            ++ready_initializer_calls;
            return false;
        }),
        "first PCM submit observes the pre-initialized backend");
    initialization = preinitialized_gate.snapshot();
    passed &= expect(
        ready_initializer_calls == 1u &&
            initialization.initialization_attempts == 1u &&
            initialization.submit_checks == 1u &&
            initialization.submit_initialization_attempts == 0u,
        "first PCM submit cannot initialize or retry a ready backend");

    NativeAudioInitializationGate failed_gate;
    std::uint32_t failed_initializer_calls = 0;
    passed &= expect(
        !failed_gate.preinitialize([&] {
            ++failed_initializer_calls;
            return false;
        }),
        "pre-guest initialization failure is reported");
    passed &= expect(
        !failed_gate.ensure_ready_for_submit([&] {
            ++failed_initializer_calls;
            return true;
        }),
        "failed pre-initialization is sticky at submit time");
    initialization = failed_gate.snapshot();
    passed &= expect(
        failed_initializer_calls == 1u &&
            initialization.state == NativeAudioInitializationState::Failed &&
            initialization.initialization_attempts == 1u &&
            initialization.submit_checks == 1u &&
            initialization.submit_initialization_attempts == 0u,
        "failed backend is never silently retried on the live AI path");

    NativeAudioInitializationGate cold_submit_gate;
    std::uint32_t cold_initializer_calls = 0;
    passed &= expect(
        cold_submit_gate.ensure_ready_for_submit([&] {
            ++cold_initializer_calls;
            return true;
        }),
        "cold-submit path remains explicitly measurable for regression tests");
    initialization = cold_submit_gate.snapshot();
    passed &= expect(
        cold_initializer_calls == 1u &&
            initialization.initialization_attempts == 1u &&
            initialization.submit_initialization_attempts == 1u &&
            !initialization.preinitialized,
        "cold submit records the forbidden live-path backend initialization");

    constexpr auto ratio_32000 =
        galaxy::host::native_audio_frequency_ratio_for_sample_rate(32'000u);
    constexpr auto ratio_48000 =
        galaxy::host::native_audio_frequency_ratio_for_sample_rate(48'000u);
    constexpr auto ratio_unsupported =
        galaxy::host::native_audio_frequency_ratio_for_sample_rate(44'100u);
    passed &= expect(
        ratio_32000.valid && ratio_32000.ratio == 1.0f &&
            static_cast<float>(
                galaxy::host::kNativeAudioVoiceBaseSampleRate) *
                    ratio_32000.ratio ==
                32'000.0f,
        "32 kHz buffers use the fixed voice at an exact 1.0 ratio");
    passed &= expect(
        ratio_48000.valid && ratio_48000.ratio == 1.5f &&
            ratio_48000.ratio <=
                galaxy::host::kNativeAudioMaxFrequencyRatio &&
            static_cast<float>(
                galaxy::host::kNativeAudioVoiceBaseSampleRate) *
                    ratio_48000.ratio ==
                48'000.0f,
        "48 kHz buffers use an exact 1.5 ratio on the same 32 kHz voice");
    passed &= expect(
        !ratio_unsupported.valid && ratio_unsupported.ratio == 0.0f,
        "the fixed voice rejects every non-Wii AI sample rate");
    constexpr auto first_32000_boundary =
        galaxy::host::native_audio_frequency_ratio_at_boundary(0u, 32'000u);
    constexpr auto first_48000_boundary =
        galaxy::host::native_audio_frequency_ratio_at_boundary(0u, 48'000u);
    constexpr auto same_32000_boundary =
        galaxy::host::native_audio_frequency_ratio_at_boundary(
            32'000u, 32'000u);
    constexpr auto same_48000_boundary =
        galaxy::host::native_audio_frequency_ratio_at_boundary(
            48'000u, 48'000u);
    constexpr auto upsample_boundary =
        galaxy::host::native_audio_frequency_ratio_at_boundary(
            32'000u, 48'000u);
    constexpr auto downsample_boundary =
        galaxy::host::native_audio_frequency_ratio_at_boundary(
            48'000u, 32'000u);
    passed &= expect(
        first_32000_boundary.valid &&
            first_32000_boundary.apply_required &&
            first_32000_boundary.ratio == 1.0f &&
            first_48000_boundary.valid &&
            first_48000_boundary.apply_required &&
            first_48000_boundary.ratio == 1.5f,
        "the first buffer explicitly establishes either exact voice ratio");
    passed &= expect(
        same_32000_boundary.valid &&
            !same_32000_boundary.apply_required &&
            same_32000_boundary.ratio == 1.0f &&
            same_48000_boundary.valid &&
            !same_48000_boundary.apply_required &&
            same_48000_boundary.ratio == 1.5f,
        "same-rate buffers inherit the last successful exact voice ratio");
    passed &= expect(
        upsample_boundary.valid && upsample_boundary.apply_required &&
            upsample_boundary.ratio == 1.5f &&
            downsample_boundary.valid &&
            downsample_boundary.apply_required &&
            downsample_boundary.ratio == 1.0f,
        "each real 32/48 kHz boundary requires one native ratio action");
    passed &= expect(
        galaxy::host::native_audio_pcm_duration_ms_ceil(
            32'000u * 4u,
            32'000u) == 1000u &&
            galaxy::host::native_audio_pcm_duration_ms_ceil(
                48'000u * 4u,
            48'000u) == 1000u,
        "per-rate PCM duration remains exact without a voice drain");

    NativeAudioEmptyBoundaryTracker empty_boundary_tracker;
    NativeAudioPlaybackLifecycle rate_boundary_playback;
    rate_boundary_playback.mark_started();
    passed &= expect(
        empty_boundary_tracker.observe(0u) ==
                NativeAudioEmptyBoundaryDisposition::None &&
            empty_boundary_tracker.observe(1u) ==
            NativeAudioEmptyBoundaryDisposition::RecoverUnderrun,
        "the one-voice path classifies its first empty boundary as an underrun");
    passed &= expect(
        empty_boundary_tracker.observe(1u) ==
                NativeAudioEmptyBoundaryDisposition::None &&
            empty_boundary_tracker.observe(2u) ==
                NativeAudioEmptyBoundaryDisposition::RecoverUnderrun,
        "rate changes have no special empty-boundary suppression path");
    passed &= expect(
        rate_boundary_playback.observe_queue(0u) ==
                NativeAudioPlaybackAction::StopVoiceForRecovery &&
            rate_boundary_playback.recovering(),
        "real underrun still enters stopped prebuffer recovery");
    passed &= expect(
        empty_boundary_tracker.observe(4u) ==
            NativeAudioEmptyBoundaryDisposition::FatalCounterDiscontinuity,
        "multiple unseen empty boundaries hard-fail instead of being suppressed");

    galaxy::host::NativeAudioRateLedger rate_ledger;
    passed &= expect(
        rate_ledger.record_submission(32'000u, 320u * 4u) &&
            rate_ledger.record_submission(32'000u, 160u * 4u) &&
            rate_ledger.record_submission(48'000u, 480u * 4u) &&
            rate_ledger.record_submission(48'000u, 240u * 4u) &&
            rate_ledger.record_submission(32'000u, 80u * 4u),
        "per-rate sink ledger accepts exact Wii AI formats");
    auto rate_snapshot = rate_ledger.snapshot();
    passed &= expect(
        rate_snapshot.submitted_buffers_32000 == 3u &&
            rate_snapshot.submitted_stereo_frames_32000 == 560u &&
            rate_snapshot.submitted_buffers_48000 == 2u &&
            rate_snapshot.submitted_stereo_frames_48000 == 720u &&
            rate_snapshot.sample_rate_change_events == 2u,
        "mixed-rate sink ledger preserves separate buffers, frames, and transitions");
    passed &= expect(
        !rate_ledger.record_submission(44'100u, 441u * 4u) &&
            !rate_ledger.record_submission(32'000u, 3u),
        "sink rate ledger rejects unsupported or non-stereo-frame submissions");
    const auto rate_snapshot_after_rejection = rate_ledger.snapshot();
    passed &= expect(
        rate_snapshot_after_rejection.submitted_buffers_32000 ==
                rate_snapshot.submitted_buffers_32000 &&
            rate_snapshot_after_rejection.submitted_stereo_frames_32000 ==
                rate_snapshot.submitted_stereo_frames_32000 &&
            rate_snapshot_after_rejection.submitted_buffers_48000 ==
                rate_snapshot.submitted_buffers_48000 &&
            rate_snapshot_after_rejection.submitted_stereo_frames_48000 ==
                rate_snapshot.submitted_stereo_frames_48000 &&
            rate_snapshot_after_rejection.sample_rate_change_events ==
                rate_snapshot.sample_rate_change_events,
        "rejected sink-rate evidence cannot mutate the proof ledger");
    rate_ledger.reset();
    rate_snapshot = rate_ledger.snapshot();
    passed &= expect(
        rate_snapshot.submitted_buffers_32000 == 0u &&
            rate_snapshot.submitted_stereo_frames_32000 == 0u &&
            rate_snapshot.submitted_buffers_48000 == 0u &&
            rate_snapshot.submitted_stereo_frames_48000 == 0u &&
            rate_snapshot.sample_rate_change_events == 0u,
        "measurement reset clears the complete per-rate sink ledger");

    galaxy::host::NativeAudioSubmissionWindowLedger submission_window;
    std::atomic_bool submission_writer_finished{false};
    std::atomic_bool submission_writer_failed{false};
    std::thread submission_writer([&] {
        constexpr std::uint64_t kBufferBytes = 560u * 4u;
        for (std::uint32_t index = 0u; index < 4096u; ++index) {
            if (!submission_window.record_submission(
                    32'000u,
                    kBufferBytes,
                    index != 0u,
                    index != 0u ? 17'500u : 0u,
                    index != 0u ? 17'500u : 0u)) {
                submission_writer_failed.store(
                    true, std::memory_order_release);
                break;
            }
        }
        submission_writer_finished.store(true, std::memory_order_release);
    });
    while (!submission_writer_finished.load(std::memory_order_acquire)) {
        const auto submission_snapshot = submission_window.snapshot();
        const std::uint64_t per_rate_buffers =
            submission_snapshot.rate_ledger.submitted_buffers_32000 +
            submission_snapshot.rate_ledger.submitted_buffers_48000;
        const std::uint64_t expected_deltas =
            submission_snapshot.submitted_buffers == 0u
            ? 0u
            : submission_snapshot.submitted_buffers - 1u;
        passed &= expect(
            per_rate_buffers == submission_snapshot.submitted_buffers &&
                submission_snapshot.submit_delta_events == expected_deltas,
            "concurrent snapshots cannot split one XAudio submission from its cadence interval");
    }
    submission_writer.join();
    const auto final_submission_snapshot = submission_window.snapshot();
    passed &= expect(
        !submission_writer_failed.load(std::memory_order_acquire) &&
            final_submission_snapshot.submitted_buffers == 4096u &&
            final_submission_snapshot.submit_delta_events == 4095u &&
            final_submission_snapshot.total_submit_delta_us ==
                4095u * 17'500u &&
            final_submission_snapshot.max_submit_delta_us == 17'500u &&
            final_submission_snapshot.submit_delta_over_expected == 0u &&
            final_submission_snapshot.submit_delta_over_25_ms == 0u,
        "coherent submission ledger retains every buffer and exact cadence interval");
    passed &= expect(
        !submission_window.record_submission(
            32'000u, 560u * 4u, false, 0u, 0u),
        "a non-first submission cannot omit its same-window cadence predecessor");
    submission_window.reset();
    const auto reset_submission_snapshot = submission_window.snapshot();
    passed &= expect(
        reset_submission_snapshot.submitted_buffers == 0u &&
            reset_submission_snapshot.submit_delta_events == 0u &&
            reset_submission_snapshot.rate_ledger.submitted_buffers_32000 ==
                0u,
        "coherent submission reset clears both queue and cadence facts");

    galaxy::host::NativeAudioStartedWindowLedger started_window;
    passed &= expect(
        started_window.record_start(
            48'000u,
            560u * 4u,
            false,
            false,
            1.5f,
            1.5f,
            false,
            true,
            false,
            5'000u) &&
            started_window.record_start(
                48'000u,
                560u * 4u,
                false,
                false,
                1.5f,
                1.5f,
                false,
                true,
                false,
                6'000u) &&
            started_window.record_start(
                32'000u,
                560u * 4u,
                true,
                true,
                1.0f,
                1.0f,
                true,
                false,
                true,
                8'000u) &&
            started_window.record_start(
                32'000u,
                560u * 4u,
                false,
                false,
                1.0f,
                1.0f,
                false,
                true,
                false,
                7'000u) &&
            started_window.record_start(
                48'000u,
                560u * 4u,
                true,
                true,
                1.5f,
                1.5f,
                true,
                false,
                true,
                9'000u),
        "a window beginning mid-run at 48 kHz inherits, then owns exact transitions");
    auto started_snapshot = started_window.snapshot();
    passed &= expect(
        started_snapshot.started_buffers == 5u &&
            started_snapshot.rate_ledger.sample_rate_change_events == 2u &&
            started_snapshot.frequency_ratio_apply_calls == 2u &&
            started_snapshot.frequency_ratio_inherited_buffers == 3u &&
            started_snapshot.frequency_ratio_change_actions == 2u &&
            started_snapshot.frequency_ratio_apply_errors == 0u &&
            started_snapshot.buffer_start_callback_core_work_count == 5u &&
            started_snapshot.buffer_start_callback_core_work_total_ns ==
                35'000u &&
            started_snapshot.buffer_start_callback_core_work_max_ns ==
                9'000u &&
            started_snapshot.buffer_start_callback_core_work_over_1_ms == 0u &&
            started_snapshot.buffer_start_callback_core_work_over_2_ms == 0u,
        "ratio apply plus inheritance conserves every start and callback sample");
    galaxy::host::NativeAudioStartedWindowLedger midrun_32000_window;
    passed &= expect(
        midrun_32000_window.record_start(
            32'000u,
            560u * 4u,
            false,
            false,
            1.0f,
            1.0f,
            false,
            true,
            false,
            4'000u) &&
            midrun_32000_window.record_start(
                32'000u,
                560u * 4u,
                false,
                false,
                1.0f,
                1.0f,
                false,
                true,
                false,
                4'500u) &&
            midrun_32000_window.record_start(
                48'000u,
                560u * 4u,
                true,
                true,
                1.5f,
                1.5f,
                true,
                false,
                true,
                7'500u),
        "a window beginning mid-run at 32 kHz also proves inheritance and transition ownership");
    const auto midrun_32000_snapshot = midrun_32000_window.snapshot();
    passed &= expect(
        midrun_32000_snapshot.started_buffers == 3u &&
            midrun_32000_snapshot.frequency_ratio_apply_calls == 1u &&
            midrun_32000_snapshot.frequency_ratio_inherited_buffers == 2u &&
            midrun_32000_snapshot.frequency_ratio_change_actions == 1u &&
            midrun_32000_snapshot.frequency_ratio_apply_errors == 0u,
        "32 kHz mid-run proof has no redundant native ratio call");
    galaxy::host::NativeAudioStartedWindowLedger first_voice_window;
    passed &= expect(
        first_voice_window.record_start(
            32'000u,
            560u * 4u,
            true,
            true,
            1.0f,
            1.0f,
            true,
            false,
            true,
            6'000u),
        "the first buffer in a source-voice lifetime explicitly sets ratio 1.0");
    const auto first_voice_snapshot = first_voice_window.snapshot();
    passed &= expect(
        first_voice_snapshot.started_buffers == 1u &&
            first_voice_snapshot.frequency_ratio_apply_calls == 1u &&
            first_voice_snapshot.frequency_ratio_inherited_buffers == 0u &&
            first_voice_snapshot.frequency_ratio_change_actions == 0u,
        "first-voice initialization is allowed as the one extra window ratio action");
    galaxy::host::NativeAudioStartedWindowLedger missing_transition_window;
    passed &= expect(
        missing_transition_window.record_start(
            48'000u,
            560u * 4u,
            false,
            false,
            1.5f,
            1.5f,
            false,
            true,
            false,
            5'000u) &&
            !missing_transition_window.record_start(
                32'000u,
                560u * 4u,
                false,
                false,
                1.0f,
                1.0f,
                false,
                true,
                false,
                5'000u),
        "a missing callback rate transition hard-fails exact started evidence");
    started_snapshot = missing_transition_window.snapshot();
    passed &= expect(
        started_snapshot.started_buffers == 2u &&
            started_snapshot.rate_ledger.sample_rate_change_events == 1u &&
            started_snapshot.frequency_ratio_apply_calls == 0u &&
            started_snapshot.frequency_ratio_inherited_buffers == 2u &&
            started_snapshot.frequency_ratio_change_actions == 0u &&
            started_snapshot.frequency_ratio_apply_errors == 1u,
        "failed transition evidence stays visible instead of being normalized");
    galaxy::host::NativeAudioStartedWindowLedger skipped_apply_window;
    passed &= expect(
        !skipped_apply_window.record_start(
            32'000u,
            560u * 4u,
            true,
            false,
            1.0f,
            0.0f,
            false,
            false,
            false,
            5'000u),
        "a required first-voice SetFrequencyRatio invocation cannot be skipped");
    started_snapshot = skipped_apply_window.snapshot();
    passed &= expect(
        started_snapshot.started_buffers == 1u &&
            started_snapshot.frequency_ratio_apply_calls == 0u &&
            started_snapshot.frequency_ratio_inherited_buffers == 0u &&
            started_snapshot.frequency_ratio_apply_errors == 1u,
        "skipped boundary ratio application cannot masquerade as inheritance");
    galaxy::host::NativeAudioStartedWindowLedger slow_callback_window;
    passed &= expect(
        slow_callback_window.record_start(
            32'000u,
            560u * 4u,
            false,
            false,
            1.0f,
            1.0f,
            false,
            true,
            false,
            1'000'001u) &&
            slow_callback_window.record_start(
                32'000u,
                560u * 4u,
                false,
                false,
                1.0f,
                1.0f,
                false,
                true,
                false,
                2'000'001u),
        "callback timing evidence remains conserved even when a proof threshold is exceeded");
    const auto slow_callback_snapshot = slow_callback_window.snapshot();
    passed &= expect(
        slow_callback_snapshot.buffer_start_callback_core_work_count == 2u &&
            slow_callback_snapshot.buffer_start_callback_core_work_total_ns ==
                3'000'002u &&
            slow_callback_snapshot.buffer_start_callback_core_work_max_ns ==
                2'000'001u &&
            slow_callback_snapshot.buffer_start_callback_core_work_over_1_ms ==
                2u &&
            slow_callback_snapshot.buffer_start_callback_core_work_over_2_ms ==
                1u,
        "callback ledger counts exact >1 ms and >2 ms violations");
    started_window.reset();
    started_snapshot = started_window.snapshot();
    passed &= expect(
        started_snapshot.started_buffers == 0u &&
            started_snapshot.frequency_ratio_apply_calls == 0u &&
            started_snapshot.frequency_ratio_inherited_buffers == 0u &&
            started_snapshot.frequency_ratio_change_actions == 0u &&
            started_snapshot.frequency_ratio_apply_errors == 0u &&
            started_snapshot.buffer_start_callback_core_work_count == 0u &&
            started_snapshot.buffer_start_callback_core_work_total_ns == 0u &&
            started_snapshot.buffer_start_callback_core_work_max_ns == 0u &&
            started_snapshot.buffer_start_callback_core_work_over_1_ms == 0u &&
            started_snapshot.buffer_start_callback_core_work_over_2_ms == 0u,
        "started evidence reset clears rate and ratio ownership together");

    galaxy::host::NativeAudioFailureNotifier failure_notifier;
    FailureNotificationProbe failure_probe;
    failure_notifier.set(&record_failure_notification, &failure_probe);
    std::thread asynchronous_failure_thread(
        [&failure_notifier] { failure_notifier.publish(); });
    asynchronous_failure_thread.join();
    failure_notifier.publish();
    passed &= expect(
        failure_notifier.published() && failure_probe.calls == 1u,
        "asynchronous fatal audio failure notifies exactly once without another DMA");
    galaxy::host::NativeAudioFailureNotifier early_failure_notifier;
    FailureNotificationProbe late_registration_probe;
    early_failure_notifier.publish();
    passed &= expect(
        early_failure_notifier.published() &&
            late_registration_probe.calls == 0u,
        "failure publication is sticky before a notifier is registered");
    early_failure_notifier.set(
        &record_failure_notification, &late_registration_probe);
    passed &= expect(
        late_registration_probe.calls == 1u,
        "late notifier registration immediately receives the sticky failure");

    NativeAudioPlaybackLifecycle playback;
    constexpr std::uint32_t kPrebufferBuffers = 3u;
    std::uint32_t accepted_buffers = 0u;
    std::uint32_t completed_buffers = 0u;
    std::uint32_t queued_buffers = 0u;
    std::uint32_t stop_actions = 0u;
    std::uint32_t start_actions = 0u;
    passed &= expect(
        playback.state() == NativeAudioPlaybackState::Stopped &&
            playback.observe_queue(0u) == NativeAudioPlaybackAction::None,
        "an unstarted empty voice is not an underrun");
    for (queued_buffers = 1u; queued_buffers <= kPrebufferBuffers;
         ++queued_buffers) {
        ++accepted_buffers;
        const NativeAudioPlaybackAction action = playback.start_action(
            queued_buffers, kPrebufferBuffers);
        if (action == NativeAudioPlaybackAction::StartVoice) {
            ++start_actions;
            playback.mark_started();
        }
        passed &= expect(
            (queued_buffers < kPrebufferBuffers) ==
                (action == NativeAudioPlaybackAction::None),
            "initial playback starts exactly at the prebuffer threshold");
    }
    --queued_buffers;
    passed &= expect(
        playback.started() && start_actions == 1u &&
            accepted_buffers == queued_buffers,
        "initial prebuffer starts once without consuming accepted PCM");
    passed &= expect(
        playback.observe_queue(queued_buffers) ==
                NativeAudioPlaybackAction::None &&
            playback.started(),
        "a nonempty running voice cannot enter underrun recovery");

    completed_buffers += queued_buffers;
    queued_buffers = 0u;
    for (std::uint32_t observation = 0u; observation < 3u; ++observation) {
        if (playback.observe_queue(queued_buffers) ==
            NativeAudioPlaybackAction::StopVoiceForRecovery) {
            ++stop_actions;
        }
    }
    passed &= expect(
        playback.recovering() && stop_actions == 1u,
        "repeated observations of one empty queue request exactly one stop");

    for (queued_buffers = 1u; queued_buffers <= kPrebufferBuffers;
         ++queued_buffers) {
        ++accepted_buffers;
        const NativeAudioPlaybackAction action = playback.start_action(
            queued_buffers, kPrebufferBuffers);
        if (action == NativeAudioPlaybackAction::StartVoice) {
            passed &= expect(
                playback.recovering(),
                "recovery remains active until voice restart succeeds");
            ++start_actions;
            playback.mark_started();
        } else {
            passed &= expect(
                playback.recovering(),
                "partial recovery prebuffer remains stopped and recoverable");
        }
        passed &= expect(
            (queued_buffers < kPrebufferBuffers) ==
                (action == NativeAudioPlaybackAction::None),
            "recovery waits for every configured prebuffer slot");
    }
    --queued_buffers;
    passed &= expect(
        playback.started() && start_actions == 2u && stop_actions == 1u &&
            accepted_buffers == completed_buffers + queued_buffers,
        "recovery restarts once and preserves every accepted PCM buffer");
    // Two buffers queued before a measurement reset may both reach XAudio
    // afterward. Neither their mutual delta nor the old->new transition is a
    // cadence sample for the new generation; only two new-window buffers are.
    constexpr std::uint64_t kOldGeneration = 6u;
    constexpr std::uint64_t kWindowGeneration = 7u;
    passed &= expect(
        !galaxy::host::native_audio_submit_delta_in_window(
            kOldGeneration, kOldGeneration, kWindowGeneration),
        "two pre-window pending buffers cannot contaminate cadence");
    passed &= expect(
        !galaxy::host::native_audio_submit_delta_in_window(
            kOldGeneration, kWindowGeneration, kWindowGeneration),
        "the pre-window to in-window boundary is not a cadence sample");
    passed &= expect(
        galaxy::host::native_audio_submit_delta_in_window(
            kWindowGeneration, kWindowGeneration, kWindowGeneration),
        "two current-generation buffers form one window-local cadence sample");

    galaxy::host::NativeAudioCallbackLedger callbacks;
    galaxy::host::NativeAudioQueueMirror queue;
    InterleavingVoice voice(callbacks);

    passed &= expect(
        voice.submit_while_previous_buffer_ends(),
        "the continuously queued boundary completes its current buffer");
    passed &= expect(
        callbacks.completed_buffers() == 1u &&
            callbacks.empty_buffer_end_boundaries() == 0u &&
            callbacks.nonempty_buffer_end_boundaries() == 1u &&
            callbacks.unobserved_buffer_end_boundaries() == 0u,
        "callback proves a queued successor existed at the exact boundary");
    passed &= expect(
        callbacks.event_sequence() == 1u,
        "completion signals one worker event");
    passed &= expect(
        queue.queued_buffers() == 0,
        "completion callback cannot publish or decrement queue depth");

    queue.publish_from_worker(voice.get_state());
    passed &= expect(
        queue.queued_buffers() == 1,
        "worker GetState remains authoritative across submit/end interleave");
    passed &= expect(
        queue.max_queued_buffers() == 1,
        "authoritative maximum tracks worker observations");

    galaxy::host::NativeAudioCallbackLedger late_replacement_callbacks;
    InterleavingVoice late_replacement_voice(late_replacement_callbacks);
    NativeAudioPlaybackLifecycle late_replacement_playback;
    late_replacement_playback.mark_started();
    const std::uint32_t stale_queue_observation =
        late_replacement_voice.get_state();
    passed &= expect(
        late_replacement_voice.finish_current_buffer(),
        "the final old buffer can end after an earlier nonempty observation");
    passed &= expect(
        late_replacement_voice.get_state() == 0u &&
            late_replacement_callbacks.empty_buffer_end_boundaries() == 1u &&
            late_replacement_callbacks.nonempty_buffer_end_boundaries() == 0u,
        "callback records the N-to-zero transition at the exact boundary");
    late_replacement_voice.queue_replacement();
    passed &= expect(
        stale_queue_observation == 1u &&
            late_replacement_voice.get_state() == 1u &&
            late_replacement_callbacks.empty_buffer_end_boundaries() == 1u,
        "a concurrent replacement cannot erase prior empty-boundary evidence");
    passed &= expect(
        late_replacement_playback.observe_queue(0u) ==
                NativeAudioPlaybackAction::StopVoiceForRecovery &&
            late_replacement_playback.recovering(),
        "sticky callback evidence defeats a stale pre-submit nonempty snapshot");

    galaxy::host::NativeAudioCallbackLedger unwired_callbacks;
    unwired_callbacks.record_buffer_end();
    passed &= expect(
        unwired_callbacks.completed_buffers() == 1u &&
            unwired_callbacks.empty_buffer_end_boundaries() == 0u &&
            unwired_callbacks.nonempty_buffer_end_boundaries() == 0u &&
            unwired_callbacks.unobserved_buffer_end_boundaries() == 1u,
        "missing boundary observation is explicit and never false evidence");

    constexpr std::int32_t kVoiceError = -123;
    const std::uint64_t failure_sequence_before_voice_error =
        callbacks.failure_sequence();
    callbacks.record_voice_error(kVoiceError);
    passed &= expect(
        callbacks.voice_errors() == 1u &&
            callbacks.last_voice_error() == kVoiceError &&
            callbacks.failure_sequence() == 1u && callbacks.has_failure() &&
            callbacks.failure_since(failure_sequence_before_voice_error) &&
            callbacks.event_sequence() == 2u,
        "voice callback records failure data without queue mutation");
    passed &= expect(
        queue.queued_buffers() == 1,
        "voice failure callback cannot mutate worker queue state");

    constexpr std::int32_t kEngineCriticalError = -456;
    const std::uint64_t failure_sequence_before_engine_error =
        callbacks.failure_sequence();
    const std::uint64_t event_sequence_before_engine_error =
        callbacks.event_sequence();
    callbacks.record_engine_critical_error(kEngineCriticalError);
    passed &= expect(
        callbacks.engine_critical_errors() == 1u &&
            callbacks.last_engine_critical_error() == kEngineCriticalError &&
            callbacks.voice_errors() == 1u &&
            callbacks.failure_since(failure_sequence_before_engine_error) &&
            callbacks.event_sequence() ==
                event_sequence_before_engine_error + 1u,
        "engine critical error wakes observers without requiring another DMA");
    const std::uint64_t current_failure_sequence =
        callbacks.failure_sequence();
    passed &= expect(
        current_failure_sequence == 2u &&
            !callbacks.failure_since(current_failure_sequence) &&
            callbacks.has_failure(),
        "acknowledging a failure sequence cannot clear the sticky fatal state");
    constexpr std::int32_t kCallbackWakeError = -789;
    const std::uint64_t event_sequence_before_wake_error =
        callbacks.event_sequence();
    callbacks.record_callback_wake_error(kCallbackWakeError);
    passed &= expect(
        callbacks.callback_wake_errors() == 1u &&
            callbacks.last_callback_wake_error() == kCallbackWakeError &&
            callbacks.engine_critical_errors() == 1u &&
            callbacks.voice_errors() == 1u &&
            callbacks.failure_sequence() == 3u &&
            callbacks.event_sequence() ==
                event_sequence_before_wake_error + 1u,
        "failed callback wake is independently sticky and worker-pollable");
    passed &= expect(
        queue.queued_buffers() == 1u,
        "engine failure callback cannot mutate worker queue state");

    galaxy::host::NativeAudioCallbackLedger ratio_callbacks;
    ratio_callbacks.record_frequency_ratio_apply(false, true, 0);
    ratio_callbacks.record_frequency_ratio_apply(true, true, 0);
    auto ratio_snapshot = ratio_callbacks.frequency_ratio_snapshot();
    passed &= expect(
        ratio_callbacks.frequency_ratio_apply_calls() == 2u &&
            ratio_callbacks.frequency_ratio_change_actions() == 1u &&
            ratio_callbacks.frequency_ratio_apply_errors() == 0u &&
            ratio_snapshot.apply_calls == 2u &&
            ratio_snapshot.change_actions == 1u &&
            ratio_snapshot.apply_errors == 0u &&
            ratio_callbacks.failure_sequence() == 0u &&
            ratio_callbacks.event_sequence() == 2u,
        "every actual boundary ratio apply is counted and successful changes are distinct");
    constexpr std::int32_t kFrequencyRatioError = -987;
    ratio_callbacks.record_frequency_ratio_apply(
        true, false, kFrequencyRatioError);
    ratio_snapshot = ratio_callbacks.frequency_ratio_snapshot();
    passed &= expect(
        ratio_callbacks.frequency_ratio_apply_calls() == 3u &&
            ratio_callbacks.frequency_ratio_change_actions() == 1u &&
            ratio_callbacks.frequency_ratio_apply_errors() == 1u &&
            ratio_callbacks.last_frequency_ratio_apply_error() ==
                kFrequencyRatioError &&
            ratio_snapshot.apply_calls == 3u &&
            ratio_snapshot.change_actions == 1u &&
            ratio_snapshot.apply_errors == 1u &&
            ratio_callbacks.failure_sequence() == 1u &&
            ratio_callbacks.event_sequence() == 3u,
        "a failed OnBufferStart ratio apply is sticky and never counted as a change");

    galaxy::host::NativeAudioCallbackLedger concurrent_ratio_callbacks;
    std::atomic_bool ratio_writer_finished{false};
    std::thread ratio_writer([&] {
        for (std::uint32_t index = 0u; index < 4096u; ++index) {
            concurrent_ratio_callbacks.record_frequency_ratio_apply(
                (index & 1u) != 0u, true, 0);
        }
        ratio_writer_finished.store(true, std::memory_order_release);
    });
    while (!ratio_writer_finished.load(std::memory_order_acquire)) {
        const auto concurrent_snapshot =
            concurrent_ratio_callbacks.frequency_ratio_snapshot();
        passed &= expect(
            concurrent_snapshot.change_actions <=
                concurrent_snapshot.apply_calls,
            "concurrent ratio snapshots never split one callback update");
    }
    ratio_writer.join();
    ratio_snapshot = concurrent_ratio_callbacks.frequency_ratio_snapshot();
    passed &= expect(
        ratio_snapshot.apply_calls == 4096u &&
            ratio_snapshot.change_actions == 2048u &&
            ratio_snapshot.apply_errors == 0u,
        "ratio snapshot retains every serialized callback update");

    // Publish start and end between the two observer operations. Reading
    // start first would produce the impossible live-playback pair {true,false}.
    std::atomic_bool observed_start{false};
    std::atomic_bool observed_end{false};
    const auto interleaved = galaxy::host::observe_callback_buffer_state(
        [&] {
            observed_start.store(true, std::memory_order_release);
            observed_end.store(true, std::memory_order_release);
            return observed_end.load(std::memory_order_acquire);
        },
        [&] { return observed_start.load(std::memory_order_acquire); });
    passed &= expect(interleaved.completed && interleaved.started,
        "completion observation cannot combine an old start with a new end");
    const auto unstarted_flush = galaxy::host::observe_callback_buffer_state(
        [] { return true; }, [] { return false; });
    passed &= expect(unstarted_flush.completed && !unstarted_flush.started,
        "observation retains an unstarted end for explicit flush-boundary policy");

    queue.publish_from_worker(0);
    passed &= expect(
        queue.queued_buffers() == 0 && queue.max_queued_buffers() == 1,
        "worker alone publishes a real empty state");

    if (!passed) {
        return 1;
    }
    std::cout << "Native audio queue ownership tests passed\n";
    return 0;
}
