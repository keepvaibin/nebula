#pragma once

#include "galaxy/ai_pcm_identity.h"
#include "galaxy/flat_guest_memory.h"
#include "galaxy/frame_cadence_diagnostics.h"
#include "galaxy/host_pointer_events.h"
#include "galaxy/native_api.h"
#include "galaxy/native_audio_dusk_dsp.h"
#include "galaxy/native_dsp.h"
#include "galaxy/native_input.h"
#include "galaxy/synthetic_kpad_buttons.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace galaxy::host {

// Opt-in fixed phases for the existing bounded slow-input anomaly ledger.
// Native steady-clock diagnostics only; these do not sample or alter Wii time.
enum class NativeInputPollStage : std::size_t {
    Reconnect, Cadence, Script, Focus, Keys, Controller, Pointer,
    PointerRemainder, Report, DeliverySelection, DeliveryCopy, DeliveryClock,
    DeliveryReply, Count
};
struct NativeInputPollBreakdown {
    bool enabled{};
    std::uint64_t serial{};
    std::uint64_t total_ns{};
    std::uint64_t diagnostic_file_ns{}; // Nested in Script; not additive.
    std::array<std::uint64_t, static_cast<std::size_t>(NativeInputPollStage::Count)> stage_ns{};
    std::uint32_t delivery_buffer{};
    std::uint32_t delivery_request{};
    std::uint32_t delivery_bytes{};
};
[[nodiscard]] NativeInputPollBreakdown native_input_poll_breakdown() noexcept;

// Native Bluetooth lifecycle timing uses the same deterministic 60.75 MHz
// checkpoint clock as the rest of the virtual hardware. The HCI accept timeout
// is the Bluetooth Core default (0x1FA0 baseband slots at 0.625 ms each). The
// retry delays and finite L2CAP retry budget are explicit virtual-peripheral
// policy, never host-wall-clock pacing.
inline constexpr std::uint16_t kNativeBluetoothConnectionAcceptTimeoutSlots =
    0x1FA0u;
inline constexpr std::uint64_t kNativeBluetoothConnectionAcceptTimeoutTicks =
    (static_cast<std::uint64_t>(
         kNativeBluetoothConnectionAcceptTimeoutSlots) *
         input::kNativeHidTimelineTicksPerSecond +
     1599u) /
    1600u;
inline constexpr std::uint64_t kNativeBluetoothConnectionRetryDelayTicks =
    (input::kNativeHidTimelineTicksPerSecond * 1500u) / 1000u;
inline constexpr std::uint64_t kNativeBluetoothL2capPostAuthDelayTicks =
    (input::kNativeHidTimelineTicksPerSecond * 250u) / 1000u;
inline constexpr std::uint64_t kNativeBluetoothL2capRetryDelayTicks =
    kNativeBluetoothL2capPostAuthDelayTicks;
// Bluetooth Core, Vol 3, Part A, 6.2: every outstanding signaling request owns
// an RTX timer (1..60 seconds). A Pending response replaces it with ERTX
// (60..300 seconds). The Standard Configuration process may not exceed 120
// seconds. Use the specification minima for prompt, deterministic recovery and
// the mandated maximum for the whole configuration transaction.
inline constexpr std::uint64_t kNativeBluetoothL2capRtxTicks =
    input::kNativeHidTimelineTicksPerSecond;
inline constexpr std::uint64_t kNativeBluetoothL2capErtxTicks =
    input::kNativeHidTimelineTicksPerSecond * 60u;
// An individual Pending response starts ERTX, but repeated Pending responses
// may not renew one connection transaction forever. Bluetooth Core, Vol 3,
// Part A, 6.2.2 caps the elapsed time from the first ERTX start to channel
// termination at 300 seconds.
inline constexpr std::uint64_t kNativeBluetoothL2capErtxMaximumTotalTicks =
    input::kNativeHidTimelineTicksPerSecond * 300u;
inline constexpr std::uint64_t kNativeBluetoothL2capConfigurationTimeoutTicks =
    input::kNativeHidTimelineTicksPerSecond * 120u;
inline constexpr std::uint8_t kNativeBluetoothL2capMaxRetries = 3u;

class NativeAudioSink;
class NativeIosAnomalyLedger;

enum class AiDmaServiceMode : std::uint8_t {
    // Stop at the first boundary that can invoke guest code. This lets the
    // guest interrupt path update the next shadow buffer before hardware
    // autoloads it.
    Interruptible,
    // MSR[EE] is known to be clear. Hardware still advances at one real
    // boundary per service call, while its level interrupt remains latched for
    // later guest delivery.
    ExternalInterruptsMasked,
};

// Internal runtime policy for an AI-related MMIO observation.  This is kept
// separate from AiDmaServiceMode because deferring while translated guest AI
// handler code updates the shadow registers is an event-ordering decision, not
// a hardware service mode.
enum class AiDmaMmioServiceDecision : std::uint8_t {
    DeferForGuestInterruptHandler,
    Interruptible,
    ExternalInterruptsMasked,
};

// The renderer's absolute-position cache and its lossless button-transition
// journal have independent producers and independent counters. A sequence is
// therefore an identity only together with this domain; equal numeric values
// from the two domains must never be treated as the same pointer publication.
enum class HostPointerSequenceDomain : std::uint8_t {
    None,
    AbsolutePosition,
    ButtonTransition,
};

// Stable diagnostic spelling used by the runtime trace and its fail-closed
// proof parsers. Keep these values explicit: the numeric sequence is not an
// identity without its producer domain.
[[nodiscard]] constexpr std::string_view host_pointer_sequence_domain_name(
    HostPointerSequenceDomain domain) noexcept {
    switch (domain) {
    case HostPointerSequenceDomain::None:
        return "none";
    case HostPointerSequenceDomain::AbsolutePosition:
        return "absolute-position";
    case HostPointerSequenceDomain::ButtonTransition:
        return "button-transition";
    }
    return "invalid";
}

struct HostPointerState {
    bool window_focused = false;
    bool inside_client = false;
    bool absolute_valid = false;
    bool left_button = false;
    bool right_button = false;
    int client_x = 0;
    int client_y = 0;
    int client_width = 0;
    int client_height = 0;
    std::uint64_t absolute_sequence = 0;
    // Host GetTickCount64 milliseconds. Keep this clock domain distinct from
    // the Wii time-base ticks used by native HID cadence identities.
    std::uint64_t absolute_acquired_ms = 0;
    std::uint32_t debug_flags = 0;
    std::uint32_t debug_error = 0;
    std::uintptr_t debug_window = 0;
};

// Private host policy only; this does not change the generated-module ABI.
// Host cursor identity is always (domain, sequence, acquired_ms), never just
// sequence. Original virtual-HID/WPAD/KPAD causal tags remain separate.
enum class SyntheticKpadPointerOrigin : std::uint8_t {
    None, PhysicalMouse, KeyboardPointer, Controller, Script, Replay,
};

struct SyntheticKpadPointerSample {
    float x{}, y{};
    bool active{};
    bool refresh_mouse_allowed{};
    SyntheticKpadPointerOrigin origin{SyntheticKpadPointerOrigin::None};
    HostPointerSequenceDomain domain{HostPointerSequenceDomain::None};
    std::uint64_t sequence{}, acquired_ms{}, mode_generation{};
    std::uint64_t production_wii_ticks{};
};

enum class SyntheticKpadPointerSelection : std::uint8_t {
    Stored, FreshAbsolute, KnownOutside, RetainedMousePress,
    InvalidAcquisition, MissingProvider, ModeMismatch, ButtonEdge,
    AmbiguousPressIdentity,
};

struct SyntheticKpadPointerRead {
    SyntheticKpadPointerSample sample{};
    SyntheticKpadPointerSelection selection{SyntheticKpadPointerSelection::Stored};
    // Original 100 Hz cache identity stays available for bounded comparison
    // even when sample above selects a retained press or new absolute point.
    HostPointerSequenceDomain stored_domain{HostPointerSequenceDomain::None};
    std::uint64_t stored_sequence{}, stored_acquired_ms{}, stored_production_wii_ticks{};
    float stored_x{}, stored_y{};
};

struct SyntheticKpadPointerStats {
    std::uint64_t reads{}, fresh_reads{}, outside_reads{}, retained_press_reads{};
    std::uint64_t ambiguous_press_reads{}, mode_mismatch_reads{};
};

using HostPointerProvider = HostPointerState (*)() noexcept;
using HostPointerTransitionProvider = HostPointerTransitionPoll (*)(
    std::uint64_t after_sequence) noexcept;
using HostPointerClock = std::uint64_t (*)() noexcept;
using NativeKeyboardCaptureProvider = bool (*)() noexcept;

// Passive identity emitted only when a queued virtual Wii Remote input report
// crosses the IOS /dev/usb/oh1 bulk-in boundary. Host milliseconds and Wii
// time-base ticks intentionally remain separate fields: they do not share an
// epoch or unit and must never be subtracted from one another.
struct NativeVirtualInputAclDeliveryIdentity {
    std::uint64_t logical_epoch{};
    std::uint64_t sample_sequence{};
    std::uint64_t deadline_wii_ticks{};
    std::uint64_t production_wii_ticks{};
    std::uint64_t delivery_wii_ticks{};
    // Meaningful only together with host_pointer_sequence_domain.
    std::uint64_t host_pointer_sequence{};
    std::uint64_t host_pointer_acquired_ms{};
    std::uint32_t payload_size{};
    std::array<
        std::uint8_t,
        input::kMaxWiimoteInputReportBytes> payload{};
    // FNV-1a64 shorthand for log correlation only. Exact identity is the
    // payload_size plus byte array above; this signature is not collision
    // proof.
    std::uint64_t payload_fingerprint{};
    std::uint32_t ios_request{};
    std::uint8_t report_id{};
    bool host_pointer_sampled{};
    HostPointerSequenceDomain host_pointer_sequence_domain =
        HostPointerSequenceDomain::None;

    bool operator==(
        const NativeVirtualInputAclDeliveryIdentity&) const = default;
};

void install_host_pointer_provider(HostPointerProvider provider) noexcept;
void install_host_pointer_transition_provider(
    HostPointerTransitionProvider provider) noexcept;
// Deterministic clock seam for pointer-visibility integration tests. Passing
// null restores the production GetTickCount64 clock.
void install_host_pointer_clock(HostPointerClock clock) noexcept;
// The native settings overlay owns window-key navigation. Install a host-owned
// visibility callback so independently polled keyboard state cannot also leak
// those keys into the guest while the overlay is open. Null disables capture.
void install_native_keyboard_capture_provider(
    NativeKeyboardCaptureProvider provider) noexcept;
[[nodiscard]] bool native_keyboard_capture_active() noexcept;
void publish_native_window_key_press(std::uint32_t key) noexcept;
void clear_native_window_key_presses() noexcept;

struct AudioSinkStats {
    std::uint64_t initialization_attempts{};
    std::uint64_t preinitialize_calls{};
    std::uint64_t submit_initialization_checks{};
    std::uint64_t submit_initialization_attempts{};
    std::uint64_t preinitialize_elapsed_us{};
    std::uint64_t first_submit_initialization_wait_us{};
    std::uint32_t initialization_thread_id{};
    std::uint32_t first_submit_thread_id{};
    bool initialization_ready{};
    bool preinitialized_before_guest{};
    bool first_submit_observed{};
    bool first_submit_initialized_backend{};
    std::uint64_t submitted_buffers{};
    std::uint64_t completed_buffers{};
    std::uint64_t queue_empty_events{};
    std::uint64_t dropped_buffers{};
    std::uint64_t queue_wait_events{};
    std::uint64_t total_queue_wait_ms{};
    std::uint64_t submit_delta_events{};
    std::uint64_t total_submit_delta_us{};
    std::uint64_t max_submit_delta_us{};
    std::uint64_t measurement_window_max_submit_delta_us{};
    std::uint64_t measurement_window_accepted_buffers{};
    std::uint64_t measurement_window_accepted_buffers_32000{};
    std::uint64_t measurement_window_accepted_stereo_frames_32000{};
    std::uint64_t measurement_window_accepted_buffers_48000{};
    std::uint64_t measurement_window_accepted_stereo_frames_48000{};
    std::uint64_t measurement_window_accepted_sample_rate_change_events{};
    std::uint64_t measurement_window_submitted_buffers{};
    std::uint64_t measurement_window_submitted_buffers_32000{};
    std::uint64_t measurement_window_submitted_stereo_frames_32000{};
    std::uint64_t measurement_window_submitted_buffers_48000{};
    std::uint64_t measurement_window_submitted_stereo_frames_48000{};
    std::uint64_t measurement_window_sample_rate_change_events{};
    std::uint64_t measurement_window_started_buffers{};
    std::uint64_t measurement_window_started_buffers_32000{};
    std::uint64_t measurement_window_started_stereo_frames_32000{};
    std::uint64_t measurement_window_started_buffers_48000{};
    std::uint64_t measurement_window_started_stereo_frames_48000{};
    std::uint64_t measurement_window_started_sample_rate_change_events{};
    std::uint64_t measurement_window_frequency_ratio_apply_calls{};
    std::uint64_t measurement_window_frequency_ratio_inherited_buffers{};
    std::uint64_t measurement_window_frequency_ratio_change_actions{};
    std::uint64_t measurement_window_frequency_ratio_apply_errors{};
    std::uint64_t measurement_window_buffer_start_callback_core_work_count{};
    std::uint64_t measurement_window_buffer_start_callback_core_work_total_ns{};
    std::uint64_t measurement_window_buffer_start_callback_core_work_max_ns{};
    std::uint64_t measurement_window_buffer_start_callback_core_work_over_1_ms{};
    std::uint64_t measurement_window_buffer_start_callback_core_work_over_2_ms{};
    std::uint64_t measurement_window_voice_recreation_events{};
    std::uint64_t measurement_window_rate_drain_events{};
    std::uint64_t measurement_window_empty_buffer_end_boundaries{};
    std::uint64_t measurement_window_unobserved_buffer_end_boundaries{};
    std::uint32_t measurement_window_pending_buffers{};
    std::uint64_t measurement_window_submit_delta_events{};
    std::uint64_t measurement_window_total_submit_delta_us{};
    std::uint64_t measurement_window_submit_delta_over_expected{};
    std::uint64_t measurement_window_submit_delta_over_25_ms{};
    std::uint64_t submit_delta_over_expected{};
    std::uint64_t submit_delta_over_25_ms{};
    std::uint64_t submit_failure_events{};
    std::uint64_t source_start_failure_events{};
    std::uint64_t voice_error_events{};
    std::uint64_t engine_critical_error_events{};
    std::uint64_t callback_wake_error_events{};
    std::uint64_t frequency_ratio_apply_calls{};
    std::uint64_t frequency_ratio_change_actions{};
    std::uint64_t frequency_ratio_apply_error_events{};
    std::uint64_t source_voice_creation_events{};
    std::uint64_t voice_recreation_events{};
    std::uint64_t rate_drain_events{};
    std::uint64_t engine_glitches_since_start{};
    std::uint64_t measurement_window_engine_glitches{};
    std::uint64_t pending_overflow_events{};
    std::uint64_t empty_recovery_events{};
    std::uint64_t total_empty_us{};
    std::uint64_t empty_over_1_ms{};
    std::uint64_t empty_over_5_ms{};
    std::uint32_t queued_buffers{};
    std::uint32_t synthetic_queued_buffers{};
    std::uint32_t pending_buffers{};
    std::uint32_t max_queued_buffers{};
    std::uint32_t max_synthetic_queued_buffers{};
    std::uint32_t max_pending_buffers{};
    std::uint32_t max_queue_depth_delta{};
    std::uint32_t max_queue_wait_ms{};
    std::uint32_t max_empty_us{};
    std::uint32_t queue_limit{};
    std::uint32_t prebuffer_buffers{};
};

struct RealWiimoteInputStats {
    bool source_selected{};
    std::uint64_t completed_reports{};
    std::uint64_t fifo_replacements{};
    std::uint64_t reset_discards{};
    std::uint64_t delivered_reports{};
    std::uint64_t last_delivered_sequence{};
    std::uint64_t max_delivery_age_ticks{};
    bool fifo_occupied{};
};

// Opt-in, benchmark-end-only accounting for the virtual Wii Remote transport.
// This records what crossed the native HID producer and IOS delivery boundary;
// it intentionally does not infer that a particular game screen consumed an
// input.  Keeping the observations in memory avoids perturbing the realtime
// input and VI schedules with per-report logging.
struct VirtualWiimoteInputBenchmarkStats {
    bool enabled{};
    std::uint64_t produced_reports{};
    std::uint64_t delivered_reports{};
    std::uint64_t replay_produced_reports{};
    std::uint64_t replay_delivered_reports{};
    std::uint64_t active_button_produced_reports{};
    std::uint64_t active_button_delivered_reports{};
    std::uint64_t a_button_produced_reports{};
    std::uint64_t a_button_delivered_reports{};
    std::uint64_t ir_active_produced_reports{};
    std::uint64_t ir_active_delivered_reports{};
    std::uint64_t nunchuk_active_produced_reports{};
    std::uint64_t nunchuk_active_delivered_reports{};
    bool saw_active_buttons{};
    bool saw_a_button{};
    bool saw_ir_active{};
    bool saw_nunchuk_active{};
    std::uint64_t last_produced_vi{};
    std::uint64_t last_delivered_vi{};
    std::uint16_t last_produced_buttons{};
    std::uint16_t last_delivered_buttons{};
    std::uint8_t last_report_mode{};
};

struct DspDebugSnapshot {
    std::uint16_t to_mail_high{};
    std::uint16_t to_mail_low{};
    bool from_mail_ready{};
    std::uint16_t from_mail_high{};
    std::uint16_t from_mail_low{};
    std::uint32_t from_mail_raw{};
    std::uint16_t dsp_control{};
    std::size_t from_mail_queue_depth{};
    bool native_dsp_running{};
    bool native_from_mail_present{};
    bool native_from_mail_staged{};
    bool native_from_mail_irq_latched{};
    std::size_t native_from_mail_queue_depth{};
    std::uint16_t native_from_mail_high{};
    std::uint16_t native_from_mail_low{};
    bool dsp_interrupt_pending{};
    bool dsp_task_booted{};
    bool dsp_expect_value_mail{};
    std::uint16_t dsp_mail_to_high{};
    std::uint32_t dsp_last_command{};
    std::uint32_t dsp_cmd_first_word{};
    std::uint32_t dsp_cmd_word_count{};
    std::uint32_t dsp_cmd_words_remaining{};
    std::uint64_t dsp_sync_frame_count{};
    std::uint32_t dsp_channel_table_addr{};
    bool pending_sync_active{};
    std::uint64_t pending_sync_frame_index{};
    std::uint32_t pending_sync_command{};
    std::uint32_t pending_sync_total_subframes{};
    std::uint32_t pending_sync_next_subframe{};
    std::uint32_t ai_dma_shadow_address{};
    std::uint16_t ai_dma_shadow_blocks{};
    std::uint32_t ai_dma_active_address{};
    bool ai_dma_playing{};
    bool ai_dma_completion_armed{};
    std::uint64_t ai_dma_next_event_ticks{};
    std::uint64_t ai_dma_next_interrupt_ticks{};
    std::uint64_t ai_dma_next_completion_ticks{};
    std::uint64_t ai_dma_last_duration_ticks{};
    std::uint16_t ai_dma_active_blocks{};
    std::uint32_t ai_dma_active_sample_rate{};
    std::uint64_t ai_audio_submit_count{};
};

struct DspAudioStats {
    std::uint64_t render_batches{};
    std::uint64_t rendered_samples{};
    std::uint64_t active_channel_visits{};
    std::uint64_t rendered_channel_visits{};
    std::uint64_t adpcm_channel_visits{};
    std::uint64_t pcm8_channel_visits{};
    std::uint64_t pcm16_channel_visits{};
    std::uint64_t music_channel_visits{};
    std::uint64_t effects_voice_channel_visits{};
    std::uint64_t sfx_channel_visits{};
    std::uint64_t voice_channel_visits{};
    std::uint64_t unknown_audio_channel_visits{};
    std::uint64_t music_sample_visits{};
    std::uint64_t effects_voice_sample_visits{};
    std::uint64_t sfx_sample_visits{};
    std::uint64_t voice_sample_visits{};
    std::uint64_t unknown_audio_sample_visits{};
    std::uint64_t music_fullscale_filtered_samples{};
    std::uint64_t effects_voice_fullscale_filtered_samples{};
    std::uint64_t sfx_fullscale_filtered_samples{};
    std::uint64_t voice_fullscale_filtered_samples{};
    std::uint64_t unknown_audio_fullscale_filtered_samples{};
    std::uint64_t music_fullscale_lane_samples{};
    std::uint64_t effects_voice_fullscale_lane_samples{};
    std::uint64_t sfx_fullscale_lane_samples{};
    std::uint64_t voice_fullscale_lane_samples{};
    std::uint64_t unknown_audio_fullscale_lane_samples{};
    std::uint64_t auto_mixer_channel_visits{};
    std::uint64_t muted_unrouted_channel_visits{};
    std::uint64_t skipped_paused_channel_visits{};
    std::uint64_t forced_stop_channel_visits{};
    std::uint64_t fallback_gain_channel_visits{};
    std::uint64_t aram_source_channel_visits{};
    std::uint64_t unsupported_channel_visits{};
    std::uint64_t missing_source_channel_visits{};
    std::uint64_t unknown_connect_buses{};
    std::uint64_t zero_connect_nonzero_gain_buses{};
    std::uint64_t mix_clip_count{};
    std::uint64_t mix_clip_direct_front_count{};
    std::uint64_t mix_clip_auto_mixer_count{};
    std::uint64_t mix_clip_aux_null_fallback_count{};
    std::uint64_t mix_clip_aux_plane_count{};
    std::uint64_t mix_clip_unclassified_count{};
    std::uint64_t mix_clip_zero_connect_count{};
    std::uint64_t adpcm_decode_clamp_count{};
    std::uint64_t adpcm_decode_clamp_high_count{};
    std::uint64_t adpcm_decode_clamp_low_count{};
    std::uint64_t adpcm_decode_clamp_output_count{};
    std::uint64_t adpcm_decode_clamp_output_high_count{};
    std::uint64_t adpcm_decode_clamp_output_low_count{};
    std::uint64_t adpcm_replay_blocks{};
    std::uint64_t adpcm_checkpoint_hits{};
    std::uint64_t max_preclamp_abs{};
    std::uint32_t max_music_peak{};
    std::uint32_t max_effects_voice_peak{};
    std::uint32_t max_sfx_peak{};
    std::uint32_t max_voice_peak{};
    std::uint32_t max_unknown_audio_peak{};
    std::uint32_t max_music_lane_peak{};
    std::uint32_t max_effects_voice_lane_peak{};
    std::uint32_t max_sfx_lane_peak{};
    std::uint32_t max_voice_lane_peak{};
    std::uint32_t max_unknown_audio_lane_peak{};
    std::uint32_t max_adpcm_replay_blocks{};
    std::uint32_t max_rendered_peak{};
};

enum class BootImageSectionKind : std::uint32_t {
    text = 0,
    data = 1,
};

struct BootImageSection {
    std::uint64_t image_offset{};
    std::uint32_t guest_address{};
    std::uint32_t size{};
    BootImageSectionKind kind{};
    std::uint32_t index{};
};

struct BootImage {
    std::uint32_t version{};
    std::string game_id;
    std::string main_dol_sha1;
    std::string digest_sha256;
    std::string file_sha256;
    std::uint32_t entry_point{};
    std::uint32_t bss_address{};
    std::uint32_t bss_size{};
    std::vector<BootImageSection> sections;
    std::vector<std::byte> bytes;
};

struct GamePaths {
    std::filesystem::path content_root;
};

struct IpcDebugSnapshot {
    std::uint32_t ppc_message{};
    std::uint32_t ppc_control{};
    std::uint32_t arm_message{};
    std::uint32_t current_reply{};
    std::uint32_t latched_reply{};
    bool latched_reply_acked{};
    std::uint32_t next_pending_reply{};
    std::size_t pending_reply_count{};
    bool interrupt_pending{};
};

GamePaths locate_game(const std::filesystem::path& input);
std::vector<std::byte> read_binary_file(const std::filesystem::path& path);
std::string sha1_hex(std::span<const std::byte> bytes);
BootImage read_boot_image(const std::filesystem::path& path);

class GuestAddressSpace {
public:
    static constexpr std::uint32_t kMem1Size = 0x01800000;
    static constexpr std::uint32_t kMem2Size = 0x04000000;
    static constexpr std::uint32_t kLockedCacheSize = 0x00004000;
    static constexpr std::uint32_t kHollywoodRegisterSize = 0x00400000;

    explicit GuestAddressSpace(
        input::NativeHidTimingProfile hid_timing_profile =
            input::kNativeVirtualHidTimingProfile,
        bool pe_scan_hints_enabled = false);
    ~GuestAddressSpace();
    GuestAddressSpace(const GuestAddressSpace&) = delete;
    GuestAddressSpace& operator=(const GuestAddressSpace&) = delete;
    GuestAddressSpace(GuestAddressSpace&&) = delete;
    GuestAddressSpace& operator=(GuestAddressSpace&&) = delete;

    // The runtime terminates through ExitProcess, so automatic storage
    // destructors do not run on the normal path. Perform the worker teardown
    // that must precede module unload explicitly. This is a terminal,
    // idempotent operation; true is returned only to the caller that performs
    // the teardown and emits any enabled terminal DSP causal report.
    [[nodiscard]] bool prepare_for_process_exit();

    // Experimental async NAND writes; default off. Wake is notification-only,
    // never a guest callback. Unbinding synchronizes with in-flight wake calls.
    void set_native_ios_completion_wake_callback(
        void (*callback)(void*) noexcept, void* user) noexcept;
    [[nodiscard]] bool native_ios_completion_pending() const noexcept;
    // Never waits for a running write. Committing a ready write may admit the
    // one held filesystem request; legacy non-write handlers retain their
    // synchronous I/O costs. Guest IRQ dispatch remains the runtime's job.
    void service_native_ios_completions();

    using EfbPeekCallback = bool (*)(
        void* user,
        GuestMemoryV1* memory,
        std::uint16_t x,
        std::uint16_t y,
        bool depth,
        std::uint32_t* value);
    using NativeAudioFailureCallback = void (*)(void* user) noexcept;
    using NativeInputFailureCallback = void (*)(void* user) noexcept;
    using NativeDspServiceWakeCallback = bool (*)(void* user) noexcept;
    using NativeInputCadenceArmCallback = void (*)(
        void* user,
        std::uint64_t next_unconsumed_sequence,
        std::uint64_t next_unconsumed_deadline_ticks,
        std::uint64_t period_ticks);
    using NativeVirtualInputAclDeliveryCallback = void (*)(
        void* user,
        const NativeVirtualInputAclDeliveryIdentity& identity) noexcept;

    GuestMemoryV1* guest_memory();
    void set_efb_peek_callback(
        EfbPeekCallback callback,
        void* user) noexcept;
    std::byte* pointer(std::uint32_t address, std::uint32_t size);
    const std::byte* pointer(std::uint32_t address, std::uint32_t size) const;
    // Non-throwing variant — returns nullptr if the address is not mapped.
    std::byte* pointer_or_null(std::uint32_t address, std::uint32_t size);
    const std::byte* pointer_or_null(std::uint32_t address, std::uint32_t size) const;

    void clear(std::uint32_t address, std::uint32_t size);
    void copy(std::uint32_t address, std::span<const std::byte> source);
    void write_u32(std::uint32_t address, std::uint32_t value);
    std::uint32_t read_u32(std::uint32_t address) const;
    // PE (pixel engine) completion latches. GXSetDrawDone submits BP reg 0x45
    // with bit 1; GXSetDrawSync submits BP reg 0x48. The GX backend raises
    // these only after the native GPU fence covering the ordered BP event has
    // completed. They remain private levels until the guest writes the
    // corresponding write-only PE interrupt-control clear strobe.
    void raise_pe_finish() {
        pe_finish_pending_.store(true, std::memory_order_release);
    }
    void raise_pe_token(std::uint16_t token, bool interrupt) {
        pe_token_value_.store(token, std::memory_order_release);
        if (interrupt) {
            pe_token_pending_.store(true, std::memory_order_release);
        }
    }
    // Transitional runtime compatibility: these accessors used to consume a
    // host-side edge. PE interrupts are hardware levels now, so observing one
    // must not acknowledge it; the translated handler clears PE_SR instead.
    bool take_pe_finish() const { return pe_finish_pending(); }
    bool take_pe_token() const { return pe_token_pending(); }
    bool pe_finish_pending() const {
        return pe_finish_pending_.load(std::memory_order_acquire);
    }
    bool pe_token_pending() const {
        return pe_token_pending_.load(std::memory_order_acquire);
    }
    std::uint16_t pe_token_value() const {
        return pe_token_value_.load(std::memory_order_acquire);
    }

    // DSP interrupt: DIRQ latches DSPCR.DSPINT. The processor line is a level
    // gated by DSPCR.DSPINTMSK, and only the translated guest's DSPCR W1C write
    // acknowledges it. The legacy take accessor is deliberately observational.
    bool take_dsp_interrupt() const { return dsp_interrupt_pending(); }
    bool dsp_interrupt_pending() const;

    // Broadway ARAM DMA is a native device transaction over the Wii memory
    // fabric.  The status is level-triggered in DSPCR: completion latches
    // ARINT and the processor line is asserted only while ARINTMSK is set.
    bool aram_dma_interrupt_status() const;
    bool aram_dma_interrupt_pending() const;
    bool aram_dma_active() const { return aram_dma_active_; }
    std::uint64_t aram_dma_next_block_ticks() const {
        return aram_dma_next_block_ticks_;
    }
    void service_aram_dma(std::uint64_t now_ticks);

    // Surface any mail/interrupt the native DSP coprocessor produced on its
    // worker thread into the CPU-visible mailbox and interrupt flag. No-op
    // unless the experimental native DSP path is active; safe to call from the
    // runtime's interrupt-dispatch cadence.
    void poll_native_dsp(
        cadence::DspPollOrigin origin = cadence::DspPollOrigin::Other);
    [[nodiscard]] bool set_native_dsp_service_wake_callback(
        NativeDspServiceWakeCallback callback,
        void* user) noexcept {
        const bool mram =
            dsp_native_mram_transactions_.configure_wake(callback, user);
        const bool aram =
            dsp_native_aram_commit_transactions_.configure_wake(
                callback, user);
        return mram && aram;
    }

    // Drive native Wii Remote reconnect attempts from the host event pump. When
    // the guest's WPAD layer drops the controller it sits at the
    // "communications interrupted" overlay and stops posting BT reads, so the
    // retry cannot ride the guest's own polling; the runtime calls this on its
    // periodic cadence instead. No-op unless the native BT path is gated on.
    void poll_native_bt_reconnect();
    // Runtime-owned periodic service supplies the exact identity atomically
    // drained from DeadlineBroker. The device may use the current clock to
    // measure lateness, but it must not infer a second edge from that clock.
    void poll_native_bt_reconnect(
        const input::NativeHidPublishedBatch& published_batch);
    // Load and validate an opt-in deterministic input replay before the guest
    // can arm its realtime HID cadence. A malformed replay remains a hard
    // startup failure; valid replay reads must never first occur in a periodic
    // input service.
    void preload_native_input_replay_log();
    // Publish the first absolute RuntimeTimeline VI bucket at which translated
    // Mario-control code is observed.  This must use the same timeline epoch
    // as the 100 Hz HID deadline consumer; RuntimeState::vi_retrace_count has a
    // later, relative epoch and is not interchangeable with this value.
    // The route-marker producer and the 100 Hz virtual-input consumer can run
    // on different host threads, so this is a one-shot release/acquire handoff.
    // Later observations must never re-anchor an in-flight diagnostic script.
    void publish_native_input_first_mario_control_vi(
        std::uint64_t vi) noexcept {
        if (native_input_first_mario_control_vi_.load(
                std::memory_order_relaxed) !=
            kNativeInputMarioControlViUnset) {
            return;
        }
        std::uint64_t expected = kNativeInputMarioControlViUnset;
        static_cast<void>(
            native_input_first_mario_control_vi_.compare_exchange_strong(
                expected,
                vi,
                std::memory_order_release,
                std::memory_order_relaxed));
    }
    [[nodiscard]] std::optional<std::uint64_t>
    native_input_first_mario_control_vi() const noexcept {
        const std::uint64_t vi = native_input_first_mario_control_vi_.load(
            std::memory_order_acquire);
        return vi == kNativeInputMarioControlViUnset
            ? std::nullopt
            : std::optional<std::uint64_t>{vi};
    }
    void set_native_input_cadence_arm_callback(
        NativeInputCadenceArmCallback callback,
        void* user) noexcept {
        native_input_cadence_arm_callback_ = callback;
        native_input_cadence_arm_callback_user_ = user;
    }
    void set_native_virtual_input_acl_delivery_callback(
        NativeVirtualInputAclDeliveryCallback callback,
        void* user) noexcept {
        native_virtual_input_acl_delivery_callback_ = callback;
        native_virtual_input_acl_delivery_callback_user_ = user;
    }
    [[nodiscard]] const input::NativeHidCadenceStats&
    native_hid_cadence_stats() const noexcept {
        return bt_virtual_input_cadence_.stats();
    }
    [[nodiscard]] bool native_hid_fifo_occupied() const noexcept {
        return bt_virtual_input_report_.has_value();
    }
    [[nodiscard]] const VirtualWiimoteInputBenchmarkStats&
    native_virtual_input_benchmark_stats() const noexcept {
        return bt_virtual_input_benchmark_stats_;
    }
    RealWiimoteInputStats real_wiimote_input_stats() const;
    void set_native_input_failure_callback(
        NativeInputFailureCallback callback,
        void* user) noexcept;
    void throw_if_native_input_failed() const;

    // One-shot diagnostic: returns true exactly once after the guest's L2CAP
    // layer security-blocks the native Wii Remote's HID channel, so the runtime
    // can dump the guest call history that produced the rejection.
    bool take_native_bt_security_block_capture() {
        const bool v = bt_capture_security_block_;
        bt_capture_security_block_ = false;
        return v;
    }
    bool take_native_bt_disconnect_capture() {
        const bool v = bt_capture_disconnect_;
        bt_capture_disconnect_ = false;
        return v;
    }
    std::uint8_t native_bt_last_disconnect_reason() const {
        return bt_last_disconnect_reason_;
    }

    // AI DMA: true while guest audio DMA runs (AID_LEN 0xCC005036 bit15).
    // The host models the native AI hardware boundary: guest writes update
    // shadow registers, the hardware latches them into an active transfer, and
    // OS interrupt 5 is raised when a transfer starts.
    bool ai_dma_playing() const { return ai_dma_playing_; }

    // The most recent WiimoteInputSnapshot produced by the ~100 Hz native HID
    // sample pipeline (build_native_hid_input_snapshot), cached here so a
    // KPADRead/WPADRead host intercept can read the same already-polled
    // keyboard/mouse/controller/replay state directly instead of
    // re-deriving it (which would double-consume replay samples and diverge
    // from the raw HID report the same sample already produced).
    const galaxy::input::WiimoteInputSnapshot* latest_native_hid_snapshot()
        const {
        return latest_native_hid_snapshot_valid_
                   ? &latest_native_hid_snapshot_
                   : nullptr;
    }
    void set_latest_native_hid_snapshot(
        const galaxy::input::WiimoteInputSnapshot& snapshot,
        float pointer_x,
        float pointer_y,
        bool pointer_active) {
        latest_native_hid_snapshot_ = snapshot;
        latest_native_hid_snapshot_valid_ = true;
        latest_native_pointer_x_ = pointer_x;
        latest_native_pointer_y_ = pointer_y;
        latest_native_pointer_active_ = pointer_active;
        synthetic_kpad_buttons_.capture(
            galaxy::input::synthetic_kpad_button_mask(snapshot));
    }
    galaxy::input::SyntheticKpadButtons consume_synthetic_kpad_buttons(
        bool retain_edges) noexcept {
        return synthetic_kpad_buttons_.consume(retain_edges);
    }
    // Opt-in pointer policy: acquisition records metadata without re-polling
    // or changing the immutable HID snapshot. Only the subsequent game-frame
    // read may acquire fresh geometry, never keys, button edges or replay.
    void capture_synthetic_kpad_pointer(
        const SyntheticKpadPointerSample& sample,
        std::uint32_t owned_mouse_buttons,
        std::uint32_t non_mouse_buttons) noexcept;
    [[nodiscard]] SyntheticKpadPointerRead consume_synthetic_kpad_pointer(
        const input::SyntheticKpadButtons& buttons,
        bool retain_edges,
        std::uint64_t current_mode_generation);
    [[nodiscard]] SyntheticKpadPointerStats synthetic_kpad_pointer_stats() const noexcept {
        return synthetic_kpad_pointer_stats_;
    }
    // Final normalized ([-1,1], +y up) pointer position from the same native
    // HID sample cached above -- the pre-IR-dot-encoding position, i.e. what
    // a real KPADStatus.pos read would resolve to, without needing to
    // reconstruct it from synthesized IR camera dot geometry.
    float latest_native_pointer_x() const { return latest_native_pointer_x_; }
    float latest_native_pointer_y() const { return latest_native_pointer_y_; }
    bool latest_native_pointer_active() const {
        return latest_native_hid_snapshot_valid_ &&
               latest_native_pointer_active_;
    }
    using AiDmaDeadlineCallback = void (*)(
        void* user,
        std::uint64_t next_deadline_ticks);
    using AiDmaMmioServiceDecisionProvider = AiDmaMmioServiceDecision (*)(
        void* user) noexcept;
    void set_ai_dma_deadline_callback(
        AiDmaDeadlineCallback callback,
        void* user) noexcept {
        ai_dma_deadline_callback_ = callback;
        ai_dma_deadline_callback_user_ = user;
    }
    void set_ai_dma_mmio_service_decision_provider(
        AiDmaMmioServiceDecisionProvider provider,
        void* user) noexcept {
        ai_dma_mmio_service_decision_provider_ = provider;
        ai_dma_mmio_service_decision_user_ = user;
    }
    bool ai_dma_completion_armed() const {
        return ai_dma_next_completion_ticks_ != 0 ||
               ai_dma_next_interrupt_ticks_ != 0;
    }
    std::uint64_t ai_dma_next_completion_ticks() const {
        if (ai_dma_next_completion_ticks_ == 0) {
            return ai_dma_next_interrupt_ticks_;
        }
        if (ai_dma_next_interrupt_ticks_ == 0) {
            return ai_dma_next_completion_ticks_;
        }
        return std::min(ai_dma_next_completion_ticks_,
                        ai_dma_next_interrupt_ticks_);
    }
    // Advance the hardware transfer timeline independently of whether the
    // guest currently accepts external interrupts. An interruptible service
    // returns at the first pending AID edge so the guest can update its shadow
    // buffer. A masked service has a bounded catch-up path because present-day
    // guest RAM cannot reconstruct an arbitrary history of past PCM buffers.
    void service_ai_dma(
        std::uint64_t now_ticks,
        AiDmaServiceMode mode);
    bool ai_dma_interrupt_status() const;
    bool ai_dma_interrupt_pending() const {
        return ai_dma_interrupt_status() && ai_dma_interrupt_masked_in();
    }
    std::uint64_t ai_dma_interrupt_pending_since_ticks() const {
        return ai_dma_interrupt_pending_since_ticks_;
    }
    std::uint64_t ai_audio_submit_count() const {
        return ai_audio_submit_count_;
    }
    bool native_audio_sink_started() const {
        return native_audio_ != nullptr;
    }
    // Initialize the native Windows audio backend on the simulation thread,
    // before guest timing and AI DMA deadlines begin. Explicit disable and
    // WAV-dump-only diagnostics are intentional no-endpoint modes and succeed
    // without constructing a sink.
    bool preinitialize_native_audio();
    void set_native_audio_failure_callback(
        NativeAudioFailureCallback callback,
        void* user) noexcept;
    void throw_if_native_audio_failed();
    void set_native_audio_output_enabled(bool enabled) {
        native_audio_output_enabled_.store(
            enabled, std::memory_order_release);
    }
    std::uint64_t ai_dma_last_duration_ticks() const {
        return ai_dma_last_duration_ticks_;
    }
    std::uint16_t ai_dma_active_blocks() const {
        return ai_dma_active_blocks_;
    }
    std::uint32_t ai_dma_active_sample_rate() const {
        return ai_dma_active_sample_rate_;
    }
    std::uint64_t ai_dma_resync_events() const {
        return ai_dma_resync_events_;
    }
    std::uint64_t ai_dma_resync_missed_buffers() const {
        return ai_dma_resync_missed_buffers_;
    }
    AudioSinkStats audio_sink_stats() const;
    // Bounded deferred correlation only; called on the simulation thread
    // after an already-accepted timeline rebase. Never services a device.
    void record_audio_host_pause(
        std::uint64_t vi, std::uint32_t proof,
        std::uint64_t last_guest_ticks, std::uint64_t observed_ticks,
        std::uint64_t qpc_begin, std::uint64_t qpc_end,
        std::uint64_t qpc_frequency,
        std::uint64_t cpu_begin_100ns, std::uint64_t cpu_end_100ns,
        std::uint64_t removed_ticks, std::uint64_t prepare_ticks,
        std::uint32_t boundary_phase) noexcept;
    void reset_audio_measurement_window();
    galaxy::audio::AiPcmIdentityStats ai_pcm_identity_stats() const {
        return ai_pcm_identity_tracker_.stats();
    }
    galaxy::DspNativeTelemetrySnapshot dsp_native_telemetry() const;
    // Observe a generated-DSP hard trap at a terminal runtime boundary. This
    // is intentionally a public probe for the native harness only; it does not
    // service hardware or alter the worker's state.
    void throw_if_native_dsp_failed();
    [[nodiscard]] galaxy::DspMramTransactionBoundary::Snapshot
    dsp_native_mram_transaction_stats() const noexcept {
        return dsp_native_mram_transactions_.snapshot();
    }
    DspDebugSnapshot dsp_debug_snapshot() const;
    DspAudioStats dsp_audio_stats() const { return dsp_audio_stats_; }
    IpcDebugSnapshot ipc_debug_snapshot() const;
    bool ipc_interrupt_pending() const;

    // GX FIFO accumulator — populated by WGPIPE write intercept.
    const std::vector<std::byte>& gx_fifo_data() const { return gx_fifo_; }
    // Transfer one completed producer epoch into durable frame ownership.
    // A fresh vector is installed before any render wait or guest interrupt can
    // unwind the current native stack, so later WGPIPE writes can never mutate
    // the captured frame.
    std::vector<std::byte> take_gx_fifo() {
        std::vector<std::byte> captured = std::move(gx_fifo_);
        gx_fifo_.clear();
        return captured;
    }
    void clear_gx_fifo() { gx_fifo_.clear(); }
    bool gx_fifo_pe_scan_pending(std::size_t scanned_byte_count) const {
        return gx_pe_scan_hint_pending_ &&
            (wgpipe_gather_count_ != 0u ||
             scanned_byte_count < gx_fifo_.size());
    }
    void clear_gx_fifo_pe_scan_hint() {
        gx_pe_scan_hint_pending_ = false;
    }
    void flush_wgpipe_gather_tail_for_frame();
    bool write_gx_fifo_bytes(std::span<const std::byte> bytes);

    // Deterministic guest-tick source (the runtime's checkpoint clock, in
    // 60.75 MHz time-base ticks).  When set, AISCNT — the free-running
    // 48 kHz sample counter — is derived from it instead of the host wall
    // clock, so the boot schedule stays reproducible regardless of host
    // frame cost (renderer, shader compiles, debug layers).
    using TickSource = std::uint64_t (*)(void* user);
    void set_tick_source(TickSource fn, void* user) {
        tick_source_ = fn;
        tick_source_user_ = user;
    }

    // Must be called before the game boots so DVDLowReadDiskID returns a valid
    // disc ID.  Accepts up to 0x20 bytes from the beginning of boot.bin.
    void set_disc_id(std::span<const std::byte> id);
    void set_disc_es_metadata(
        std::vector<std::byte> ticket,
        std::vector<std::byte> tmd);

    // Open and memory-map the game.pak sparse image produced by build-pak.
    // After this call DVDLowRead requests are served in O(1) from the mapped
    // view.  If the file does not exist a warning is printed and disc reads
    // will return EIO until the pak is built.
    void open_game_pak(const std::filesystem::path& path);

    // Read-only, host-side-only, never-fatal: parses the raw FST image (the
    // same bytes already placed in guest memory at boot) into a small table
    // of `files/LayoutData/*.arc` entries so DVDLowRead can recognize, by
    // disc byte-offset, when the guest is streaming the file-select
    // screen's own layout archive. Used purely to gate a cosmetic
    // native-UI hint (see file_select_scene_active()); a parse failure or a
    // different game revision just leaves the hint permanently "unknown"
    // and never affects guest-visible behavior.
    // The live hint itself is process-global (see galaxy/host/scene_hint.h
    // and galaxy::host::file_select_scene_active()) rather than a member,
    // since exactly one GuestAddressSpace exists per process and the
    // renderer -- a separate translation unit that never sees this class --
    // needs to read it without depending on native_host.h.
    void install_fst_scene_table(std::span<const std::byte> fst_bytes);

    // Pre-wire the CP FIFO ring-buffer simulation with the same base/end that
    // the dummy GXFifoObj exposes, so if GX code writes CP_WRITE_POINTER
    // directly, advance_cp_fifo starts draining without waiting for GXInit.
    void pre_wire_cp_fifo(std::uint32_t base, std::uint32_t end);

    // Posts a queued IOS reply only at a runtime dispatch boundary. The IPC
    // handler can clear the previous reply's IRQ flag before it finishes
    // processing ARMMSG, so queued replies must not become visible from inside
    // that same translated interrupt handler pass.
    void service_ipc_reply_queue();
    void begin_ipc_interrupt_handler_pass();
    void end_ipc_interrupt_handler_pass();

private:
    friend struct NativeDspBoundaryTestAccess;
    friend struct NativeBluetoothTestAccess;
    friend struct NativeNandTestAccess;

    std::atomic<bool> process_exit_prepared_{};

    bool ai_dma_interrupt_masked_in() const;
    void raise_ai_dma_interrupt(std::uint64_t boundary_ticks);
    [[nodiscard]] std::uint32_t processor_interrupt_cause() const;
    [[nodiscard]] std::uint16_t pe_interrupt_control() const;
    void latch_dsp_interrupt();
    struct PendingBluetoothRead {
        std::uint32_t request{};
        std::uint32_t buffer{};
        std::uint32_t capacity{};
    };
    struct DeferredIosReply {
        std::uint32_t request{};
        std::uint32_t result{};
    };

    static bool read_device_callback(
        void* user,
        std::uint32_t address,
        std::uint32_t size,
        std::byte* output);
    static bool write_device_callback(
        void* user,
        std::uint32_t address,
        std::uint32_t size,
        const std::byte* input);
    bool read_device(
        std::uint32_t address,
        std::uint32_t size,
        std::byte* output);
    bool write_device(
        std::uint32_t address,
        std::uint32_t size,
        const std::byte* input);
    void write_aram_dma_register_half(
        std::uint32_t offset,
        std::uint16_t value);
    void start_aram_dma();
    void update_aram_dma_registers();
    void complete_ios_request(std::uint32_t physical_request);
    void acknowledge_ios_request();
    void reply_ios_request(
        std::uint32_t physical_request,
        std::uint32_t result);
    bool should_defer_ios_reply(std::uint32_t physical_request) const;
    void flush_deferred_ios_replies();
    // Posts the next queued IOS reply into ARMMSG + PPCCTRL.Y2 if the guest
    // has acknowledged the previous one.  Called from reply_ios_request and
    // from the PPCCTRL write-1-to-clear path.
    void post_next_ipc_reply();
    void release_latched_reply_after_irq_clear();
    bool handle_bluetooth_ioctlv(
        std::uint32_t physical_request,
        std::uint32_t handle);
    void queue_bluetooth_command_complete(
        std::uint16_t opcode,
        const std::uint8_t* command,
        std::uint32_t command_size);
    // Native Wiimote BT data path (gated). Returns true if the opcode is a
    // connection-flow command handled here (Command_Status + later event) rather
    // than via the command-complete path.
    bool handle_bluetooth_connection_command(
        std::uint16_t opcode,
        const std::uint8_t* command,
        std::uint32_t command_size);
    [[nodiscard]] std::uint64_t bluetooth_now_ticks() const;
    void cancel_bluetooth_connection_request(bool include_delivered);
    [[nodiscard]] bool is_wiimote_connection_request_event(
        std::span<const std::byte> event) const noexcept;
    void note_bluetooth_event_delivered(std::span<const std::byte> event);
    void queue_wiimote_connection_complete_failure(std::uint8_t status);
    void reset_wiimote_link_state(
        bool disconnected,
        bool preserve_pairing_requests);
    void reset_bluetooth_controller_lifecycle();
    void schedule_wiimote_connection_retry(std::uint64_t now_ticks);
    void queue_wiimote_acl_teardown(std::uint8_t reason);
    void queue_wiimote_authentication_complete();
    void queue_bluetooth_connection_request();
    // Retry the inbound Wiimote connection while the host is scanning but no
    // Wiimote is connected. The upper WPAD/CSL layer rejects a controller that
    // connects before it is ready (BTM disconnects right after link setup);
    // retrying lets a later, ready host state accept and keep the Wii Remote.
    void maybe_reinject_wiimote_connection();
    // Native Wiimote data path (gated): parse outbound ACL (L2CAP signaling +
    // HID output reports) the game sends to the Wiimote. Returns true if handled.
    bool handle_bluetooth_acl_out(std::uint32_t buffer, std::uint32_t size);
    void queue_bluetooth_acl_completed_packet();
    void queue_wiimote_l2cap_connection_request(std::uint16_t psm);
    void schedule_wiimote_l2cap_retry(std::uint16_t psm);
    void clear_wiimote_l2cap_signaling_transactions() noexcept;
    void recover_wiimote_l2cap_channel(
        std::uint16_t psm,
        const char* reason);
    void service_wiimote_l2cap_timeouts(std::uint64_t now_ticks);
    void note_bluetooth_acl_delivered(std::span<const std::byte> packet);
    void maybe_start_wiimote_l2cap_control_open();
    void queue_wiimote_l2cap_config_request(std::uint16_t remote_cid);
    void advance_wiimote_l2cap_open();
    // Frame an L2CAP payload as an inbound ACL packet and deliver it to a pending
    // Bluetooth read (interrupt for HID input reports, bulk otherwise).
    void send_bluetooth_acl(
        std::uint16_t channel_cid,
        const std::byte* payload,
        std::uint32_t length,
        bool via_interrupt);
    [[nodiscard]] std::vector<std::byte> frame_bluetooth_acl(
        std::uint16_t channel_cid,
        const std::byte* payload,
        std::uint32_t length) const;
    // Advance the virtual device from the shared 60.75 MHz hardware timeline.
    // USB bulk reads only drain its bounded FIFO; they never trigger sampling.
    void poll_wiimote_input_device(
        const input::NativeHidPublishedBatch* published_batch = nullptr);
    void service_virtual_wiimote_input_device(
        const input::NativeHidPublishedBatch* published_batch = nullptr);
    [[nodiscard]] bool poll_real_wiimote_input_report();
    void ensure_real_wiimote_hid_worker_started();
    [[nodiscard]] bool virtual_wiimote_input_delivery_ready() const noexcept;
    void try_deliver_bluetooth_acl();
    void invalidate_virtual_wiimote_input_report(bool begin_logical_epoch);
    void reset_virtual_wiimote_input_device(bool disconnected);
    void deliver_bluetooth_event();
    // Handles IOCTL requests on /dev/di (the DVD drive interface).
    // Returns true if the request was consumed; false if the handle is not a
    // /dev/di handle and the caller should fall through to the default path.
    bool handle_di_ioctl(
        std::uint32_t physical_request,
        std::uint32_t handle);
    // Handles IOCTLV requests on /dev/di (scatter-gather variant used by
    // higher-level SDK DVD functions).  Same semantics as handle_di_ioctl.
    bool handle_di_ioctlv(
        std::uint32_t physical_request,
        std::uint32_t handle);
    // Native host-backed NAND save files (fresh-Wii semantics: a missing
    // file fails IOS_Open with raw ISFS ENOENT so the SDK converts it and the
    // game creates a new save).
    // Files live under nand_root_; every write flushes through to disk.
    struct NandMetadata {
        std::uint32_t owner{};
        std::uint16_t group{};
        std::uint8_t owner_mode{};
        std::uint8_t group_mode{};
        std::uint8_t other_mode{};
        std::uint8_t attribute{};

        bool operator==(const NandMetadata&) const = default;
    };
    struct NandBacking {
        std::string guest_path;
        std::vector<std::byte> data;
        std::optional<NandMetadata> metadata;
        std::uint32_t host_volume_serial{};
        std::uint64_t host_file_index{};
        bool host_identity_valid{};
    };
    struct NandFile {
        std::shared_ptr<NandBacking> backing;
        std::uint32_t position{};
        std::uint8_t mode{};
    };
    // A known-format file may exist briefly with incomplete contents between
    // ISFS_CreateFile and the guest's first complete write (and, for RFL,
    // while NANDSafeOpen replaces that empty original).  This authority binds
    // the exact bytes, metadata, and NTFS object and is mirrored by a durable
    // identity-bearing marker so an interrupted create can be recovered on
    // the next boot.  A pathname alone never bypasses content validation.
    struct ProvisionalNandFile {
        NandMetadata metadata;
        std::vector<std::byte> expected_data;
        std::uint32_t host_volume_serial{};
        std::uint64_t host_file_index{};
    };
    // Returns the new IOS handle, or raw ISFS ENOENT if no host file exists.
    std::uint32_t open_nand_file(
        const std::string& guest_path,
        std::uint8_t mode);
    // Handles ISFS IOCTL/IOCTLV requests on /dev/fs and on open NAND file
    // handles (GetFileStats).  Same consume semantics as handle_di_ioctl.
    bool handle_fs_ioctl(
        std::uint32_t physical_request,
        std::uint32_t handle);
    bool handle_fs_ioctlv(
        std::uint32_t physical_request,
        std::uint32_t handle);
    bool handle_es_ioctlv(
        std::uint32_t physical_request,
        std::uint32_t handle);
    // Provision the exact ES-owned RMGE01 title/data directory chain.  Kept
    // behind a member boundary so the early ES request handler does not need
    // to depend on the private host metadata representation below.
    [[nodiscard]] std::uint32_t ensure_rmge01_data_directory();
    std::filesystem::path nand_host_path(std::string_view guest_path) const;
    // Persist the complete file through a durable same-directory sibling and
    // atomically publish it only after write, flush, and close all succeed.
    // Returns an IOS/ISFS result so callers never report a failed host write
    // as a successful guest write.
    [[nodiscard]] std::uint32_t flush_nand_file(
        NandBacking& file,
        bool replace_existing = true,
        bool publish_durable_provisional = false) const;
    [[nodiscard]] static std::uint32_t persist_nand_file_at(
        NandBacking& file, const std::filesystem::path& root,
        const std::filesystem::path& host, bool replace_existing,
        bool publish_durable_provisional);
    struct AsyncNandState;
    std::unique_ptr<AsyncNandState> async_nand_;
    bool defer_native_ios_filesystem_request(std::uint32_t request);
    bool begin_native_nand_write(
        std::uint32_t request, std::uint32_t handle, std::uint32_t length,
        NandBacking candidate);
    void finish_native_ios_shutdown();
    // Layout stays identical for runtime and test-support consumers. Only the
    // test-support CPP copies this hook into a job; production never calls it.
    void (*native_nand_test_before_persist_)(void*){};
    void* native_nand_test_before_persist_user_{};
    void (*native_nand_test_after_persist_)(void*){};
    void* native_nand_test_after_persist_user_{};
    [[nodiscard]] bool provisional_nand_file_matches(
        const std::string& guest_path,
        const std::filesystem::path& host_path) const;
    struct DiscOverride {
        std::uint64_t disc_offset{};
        std::vector<std::byte> bytes;
        std::filesystem::path source_path;
    };
    struct DiscReadTicket {
        std::uint64_t sequence{};
        std::uint32_t request{};
        std::uint32_t destination{};
        std::uint64_t disc_offset{};
        std::uint32_t size{};
        std::uint32_t source_hash{};
        std::uint32_t destination_hash{};
        bool destination_matches_source{};
        bool ioctlv{};
    };
    void load_disc_mod_overrides_from_env();
    [[nodiscard]] bool disc_read_in_range(
        std::uint64_t disc_offset,
        std::uint32_t size) const noexcept;
    void copy_disc_bytes(
        std::uint32_t guest_dest,
        std::uint64_t disc_offset,
        std::uint32_t size);
    DiscReadTicket make_disc_read_ticket(
        std::uint32_t physical_request,
        std::uint32_t guest_dest,
        std::uint64_t disc_offset,
        std::uint32_t size,
        bool ioctlv);
    void remember_disc_read_ticket(const DiscReadTicket& ticket);
    [[nodiscard]] const DiscReadTicket* disc_read_ticket(
        std::uint32_t physical_request) const noexcept;
    void forget_disc_read_ticket(std::uint32_t physical_request);
    [[nodiscard]] std::byte disc_byte_at(std::uint64_t disc_offset) const;
    [[nodiscard]] const DiscOverride* disc_override_at(
        std::uint64_t disc_offset) const noexcept;
    std::uint32_t ai_dma_raw_address() const;
    std::uint32_t ai_dma_sample_rate() const;
    std::uint64_t ai_dma_duration_ticks(
        std::uint16_t blocks,
        std::uint32_t sample_rate) const;
    void write_ai_dma_len(std::uint16_t value);
    void record_audio_ai_state(std::uint32_t reason, std::uint64_t ticks) noexcept;
    bool latch_ai_dma_transfer(std::uint64_t now_ticks);
    std::optional<AiDmaServiceMode> ai_dma_mmio_service_mode() const;
    void service_ai_dma_before_mmio();
    void service_dsp_control_before_mmio();
    void notify_ai_dma_deadline_changed();
    void submit_ai_dma_audio(
        std::uint32_t raw_address,
        std::uint16_t blocks,
        std::uint32_t sample_rate);
    std::uint16_t ai_dma_blocks_left(std::uint64_t now_ticks) const;
    void on_dsp_mail(std::uint32_t mail);
    // When GALAXY_DSP_DUMP_UCODE names a path, persist the booted IRAM image
    // (big-endian 16-bit words, exactly what `nebula-recomp dsp-lower` consumes)
    // from guest main RAM the first time the boot vector mail arrives.
    void maybe_dump_dsp_ucode(std::uint16_t start_vector);
    void render_dsp_sync_frame(
        std::uint32_t command,
        std::uint32_t left_buffer,
        std::uint32_t right_buffer,
        std::uint32_t aux_a_buffer,
        std::uint32_t aux_b_buffer);
    void render_dsp_sync_subframes(
        std::uint64_t sync_frame_index,
        std::uint32_t command,
        std::uint32_t left_buffer,
        std::uint32_t right_buffer,
        std::uint32_t aux_a_buffer,
        std::uint32_t aux_b_buffer,
        std::uint32_t first_subframe,
        std::uint32_t subframe_count);
    void render_dusk_dsp_sync_subframe(
        const std::uint32_t command,
        std::uint32_t left_buffer,
        std::uint32_t right_buffer,
        std::uint32_t aux_a_buffer,
        std::uint32_t aux_b_buffer,
        std::uint32_t subframe_index);
    void queue_pending_dsp_sync_subframe();
    struct DspSourceMapping {
        std::uint32_t guest_base{};
        std::uint32_t bytes_available{};
        bool uses_aram{};
    };
    DspSourceMapping resolve_dsp_source_mapping(
        std::uint32_t source_addr) const;
    const std::byte* dsp_source_pointer_or_null(
        const DspSourceMapping& mapping,
        std::uint64_t relative_offset,
        std::uint32_t size) const;
    // Pop the next queued from-DSP mail into the mailbox if it is empty.
    void pump_dsp_from_mailbox();

    // --- Native DSP coprocessor ---
    // Boots the real lowered RMGE01 ucode on the free-running coprocessor using
    // the captured boot descriptor. Returns true once the coprocessor owns the
    // DSP. Callers hard-fail on false rather than falling back to the retired
    // transitional mixer.
    bool dsp_native_try_boot();
    // Locate the guest DSPTaskInfo matching the booted native ucode and retain
    // its task start/resume vectors for explicit native HALT resumes.
    void dsp_native_capture_task_vectors(std::uint16_t start_vector);
    // Forward one CPU->DSP mail word into the coprocessor, waiting (bounded) for
    // the previous word to be consumed — the hardware to-DSP handshake.
    void dsp_native_forward_mail(std::uint32_t mail);
    void refresh_dsp_audio_bus_controls();
    [[nodiscard]] std::uint64_t dsp_native_now_ticks() const;
    [[nodiscard]] std::uint64_t dsp_native_sync_completion_interval_ticks() const;
    [[nodiscard]] bool dsp_native_should_hold_sync_completion(
        std::uint16_t high,
        std::uint16_t low);
    [[nodiscard]] bool dsp_native_task_done_suppression_enabled() const;
    [[nodiscard]] bool dsp_native_suppress_task_done_mail(
        std::uint16_t high,
        std::uint16_t low,
        const char* reason);
    void dsp_native_wait_for_sync_task_done(std::uint16_t sync_low);
    // Move any DSP->CPU mail and DIRQ the coprocessor produced into the CPU
    // mailbox path. Called from the CPU thread on mailbox polls.
    void dsp_native_pump();
    void dsp_native_service_mram_transaction();
    void dsp_native_throw_if_failed();
    [[nodiscard]] bool dsp_native_active() const { return dsp_native_running_; }
    void dsp_native_shutdown(bool emit_audio_causal_report);
    [[nodiscard]] bool dsp_native_validate_mram_span(
        std::uint32_t address,
        std::uint32_t size) const;
    [[nodiscard]] bool dsp_native_validate_aram_span(
        std::uint32_t address,
        std::uint32_t size) const;
    [[nodiscard]] bool dsp_native_pin_aram_backing(
        std::uint32_t backing_base);
    void dsp_native_observe_command_mail(
        std::uint32_t mail,
        std::uint64_t generation);
    void dsp_native_stage_aram_updates(std::uint64_t generation);
    void dsp_native_clear_aram_tracking() noexcept;
    [[nodiscard]] bool dsp_native_flush_aram_outbound() noexcept;
    [[nodiscard]] bool dsp_native_commit_aram_span(
        std::uint32_t address,
        const std::uint8_t* source,
        std::uint32_t size) noexcept;
    bool dsp_native_read_mram_span(
        std::uint32_t address,
        std::uint8_t* destination,
        std::uint32_t size);
    bool dsp_native_write_mram_span(
        std::uint32_t address,
        const std::uint8_t* source,
        std::uint32_t size);
    bool dsp_native_read_aram(std::uint32_t address, std::uint8_t* value);
    bool dsp_native_write_aram(std::uint32_t address, std::uint8_t value);
    bool dsp_native_read_aram_u16(
        std::uint32_t address,
        std::uint16_t* value);
    bool dsp_native_write_aram_u16(
        std::uint32_t address,
        std::uint16_t value);
    // Hardware-service trampolines (user == this GuestAddressSpace).
    static void dsp_native_request_interrupt_cb(void* user);
    static bool dsp_native_consume_cpu_mailbox_low_cb(
        void* user,
        std::uint64_t generation,
        std::uint32_t* mailbox,
        std::uint16_t* value,
        bool* consumed_mail);
    static bool dsp_native_before_publication_cb(void* user);
    static bool dsp_native_aram_outbound_cb(
        void* user,
        std::uint64_t generation,
        std::uint32_t address,
        const std::uint8_t* source,
        std::uint32_t size) noexcept;
    static galaxy::DspHardwareServices
    dsp_native_hardware_services() noexcept;
    static bool dsp_native_mram_span_cb(
        void* user, std::uint32_t address, std::uint32_t size);
    static bool dsp_native_mram_read_span_cb(
        void* user,
        std::uint32_t address,
        std::uint8_t* destination,
        std::uint32_t size);
    static bool dsp_native_mram_write_span_cb(
        void* user,
        std::uint32_t address,
        const std::uint8_t* source,
        std::uint32_t size);
    static bool dsp_native_service_mram_read_cb(
        void* user,
        std::uint32_t address,
        std::uint8_t* destination,
        std::uint32_t size) noexcept;
    static bool dsp_native_service_mram_write_cb(
        void* user,
        std::uint32_t address,
        const std::uint8_t* source,
        std::uint32_t size) noexcept;
    static bool dsp_native_service_aram_commit_write_cb(
        void* user,
        std::uint32_t address,
        const std::uint8_t* source,
        std::uint32_t size) noexcept;
    static bool dsp_native_aram_span_cb(
        void* user, std::uint32_t address, std::uint32_t size);
    static bool dsp_native_aram_read_cb(
        void* user, std::uint32_t address, std::uint8_t* value);
    static bool dsp_native_aram_write_cb(
        void* user, std::uint32_t address, std::uint8_t value);
    static bool dsp_native_aram_read_u16_cb(
        void* user, std::uint32_t address, std::uint16_t* value);
    static bool dsp_native_aram_write_u16_cb(
        void* user, std::uint32_t address, std::uint16_t value);
    static void dsp_native_accel_exc_cb(
        void* user, galaxy::DspAcceleratorException exception);

    void initialize_devices();
    void add_region(
        std::uint32_t guest_base,
        std::span<std::byte> storage,
        std::uint32_t size);
    void notify_guest_write(
        std::uint32_t address,
        std::uint32_t size) noexcept;
    void notify_guest_write_renderer_only(
        std::uint32_t address,
        std::uint32_t size) noexcept;

    std::vector<std::byte> mem1_;
    std::vector<std::byte> mem2_;
    flat_guest_memory::OwnerLease flat_guest_owner_;
    std::span<std::byte> mem1_view_;
    std::span<std::byte> mem2_view_;
    std::vector<std::byte> locked_cache_;
    std::vector<std::byte> broadway_registers_;
    std::vector<std::byte> hollywood_registers_;
    std::vector<GuestMemoryRegionV1> regions_;
    EfbPeekCallback efb_peek_callback_ = nullptr;
    void* efb_peek_user_ = nullptr;
    std::uint32_t next_ios_handle_{1};
    std::uint32_t ios_event_hook_handle_{};
    std::uint32_t ios_event_hook_pending_request_{};
    std::unordered_map<std::uint32_t, std::string> ios_paths_;
    std::unordered_map<std::uint32_t, NandFile> nand_files_;
    std::unordered_map<std::string, ProvisionalNandFile>
        provisional_nand_files_;
    // Host directory backing the guest NAND.  Resolved on first use to
    // %LOCALAPPDATA%\SuperMarioGalaxy\nand (per-user production data home).
    mutable std::filesystem::path nand_root_;
    std::optional<PendingBluetoothRead> bluetooth_interrupt_read_;
    std::optional<PendingBluetoothRead> bluetooth_bulk_read_;
    std::deque<std::vector<std::byte>> bluetooth_events_;
    // Ordered L2CAP signaling and HID command-response packets waiting for a
    // pending bulk-in read. Periodic HID input lives only in the bounded
    // source-specific FIFOs below; HCI events use bluetooth_events_.
    std::deque<std::vector<std::byte>> bluetooth_acl_events_;
    struct QueuedVirtualWiimoteInputReport {
        std::vector<std::byte> packet;
        input::NativeHidCadenceSample cadence;
        std::uint64_t production_wii_ticks{};
        // Generation of the coherent runtime input-mode snapshot used to
        // acquire and encode this report. A source change invalidates the
        // report before it can cross the IOS bulk-in boundary.
        std::uint64_t input_mode_generation{};
        // Exact coordinate-publication identity is the domain/sequence pair.
        std::uint64_t host_pointer_sequence{};
        std::uint64_t host_pointer_acquired_ms{};
        bool host_pointer_sampled{};
        HostPointerSequenceDomain host_pointer_sequence_domain =
            HostPointerSequenceDomain::None;
        // Retains the compact-proof selection latch until this exact report
        // either crosses the ACL boundary or is superseded. It is diagnostic
        // only and never participates in report construction or scheduling.
        bool trace_selected_cursor_poll_event{};
        // End-only benchmark accounting fields. These are zero unless the
        // explicit diagnostic is enabled, and never participate in report
        // construction or delivery scheduling.
        std::uint16_t benchmark_buttons{};
        bool benchmark_ir_active{};
        bool benchmark_nunchuk_active{};
        bool benchmark_replay_source{};
        std::uint64_t benchmark_vi{};
    };
    // A real Wii Remote and the BT controller both have finite buffering. Both
    // native sources intentionally keep one newest report rather than creating
    // an unbounded queue when the guest stops posting USB reads.
    std::optional<QueuedVirtualWiimoteInputReport>
        bt_virtual_input_report_;
    std::unique_ptr<input::NativeHidIoWorker> bt_real_input_worker_;
    static constexpr std::uint64_t kNativeInputMarioControlViUnset =
        ~std::uint64_t{0};
    std::atomic<std::uint64_t> native_input_first_mario_control_vi_{
        kNativeInputMarioControlViUnset};
    // The opt-in route script publishes one bounded proof record at the first
    // HID sample suppressed by the live Mario-control marker.  All later
    // samples retain a terminal A/B-source invariant, but never emit another
    // record from the realtime input path.
    bool bt_autopress_script_live_stop_recorded_{};
    NativeInputFailureCallback native_input_failure_callback_{};
    void* native_input_failure_callback_user_{};
    input::NativeHidReportCadence bt_virtual_input_cadence_;
    VirtualWiimoteInputBenchmarkStats bt_virtual_input_benchmark_stats_{};
    // Last source generation incorporated into the virtual device timeline.
    // Source changes begin a logical HID epoch without rephasing its hardware
    // deadline clock.
    std::uint64_t bt_virtual_input_mode_generation_{};
    NativeInputCadenceArmCallback native_input_cadence_arm_callback_{};
    void* native_input_cadence_arm_callback_user_{};
    NativeVirtualInputAclDeliveryCallback
        native_virtual_input_acl_delivery_callback_{};
    void* native_virtual_input_acl_delivery_callback_user_{};
    // Native Wiimote Bluetooth data path (gated by GALAXY_NATIVE_BT_WIIMOTE,
    // default off). Inbound/peripheral-initiated model observed in [bt] traces:
    // after the game enables scanning we inject an HCI Connection_Request; on the
    // game's Accept_Connection_Request we emit Command_Status + Connection_Complete.
    // This is bring-up scaffolding for the no-HLE input path; the L2CAP/HID report
    // data path layers on top (see docs/INPUT_BT_NATIVE_COMPLETION.md).
    bool bt_wiimote_scan_enabled_ = false;
    bool bt_wiimote_connection_requested_ = false;
    // Connection_Accept_Timeout begins only after the HCI event crosses the
    // interrupt-in boundary. A request can therefore be queued while false.
    bool bt_wiimote_connection_request_delivered_ = false;
    // Time-base tick of the last inbound-connection injection, for retry pacing.
    std::uint64_t bt_last_connection_attempt_ticks_ = 0;
    std::uint64_t bt_connection_request_deadline_ticks_ = 0;
    std::uint64_t bt_next_connection_attempt_ticks_ = 0;
    // Set when the guest L2CAP layer rejects the HID channel (security block);
    // consumed once by the runtime to dump the offending guest call history.
    bool bt_capture_security_block_ = false;
    bool bt_capture_disconnect_ = false;
    std::uint8_t bt_last_disconnect_reason_ = 0;
    bool bt_wiimote_connected_ = false;
    bool bt_wiimote_sniff_mode_ = false;
    std::uint16_t bt_wiimote_sniff_interval_ = 0;
    bool bt_wiimote_link_key_stored_ = true;
    bool bt_wiimote_link_key_requested_ = false;
    bool bt_wiimote_pin_requested_ = false;
    bool bt_wiimote_authenticated_ = false;
    std::uint64_t bt_wiimote_authenticated_at_ticks_ = 0;
    bool bt_wiimote_encrypted_ = false;
    bool bt_l2cap_control_connect_pending_ = false;
    bool bt_l2cap_post_auth_wait_logged_ = false;
    std::uint16_t bt_l2cap_retry_psm_ = 0;
    std::uint64_t bt_l2cap_retry_deadline_ticks_ = 0;
    std::uint8_t bt_l2cap_control_retry_count_ = 0;
    std::uint8_t bt_l2cap_interrupt_retry_count_ = 0;
    // Exactly one HID channel is established/configured at a time. Signal IDs
    // are retained until the matching response arrives; zero means no
    // outstanding request. Deadlines remain zero until queued ACL bytes are
    // actually delivered through the bulk-in boundary.
    std::uint16_t bt_l2cap_connection_psm_ = 0;
    std::uint8_t bt_l2cap_connection_signal_id_ = 0;
    std::uint64_t bt_l2cap_connection_response_deadline_ticks_ = 0;
    bool bt_l2cap_connection_response_pending_ = false;
    // Non-renewable ceiling measured from the first Connection Pending
    // response. Zero until the outstanding request first enters ERTX.
    std::uint64_t bt_l2cap_connection_ertx_maximum_deadline_ticks_ = 0;
    std::uint16_t bt_l2cap_configuration_psm_ = 0;
    std::uint8_t bt_l2cap_config_signal_id_ = 0;
    std::uint64_t bt_l2cap_config_response_deadline_ticks_ = 0;
    bool bt_l2cap_config_response_pending_ = false;
    std::uint64_t bt_l2cap_configuration_deadline_ticks_ = 0;
    std::uint16_t bt_wiimote_handle_ = 0x0100;
    // L2CAP HID channels (host-initiated: the console opens PSM 0x11 control and
    // 0x13 interrupt to the Wiimote; we respond). Local CIDs we assign; remote
    // CIDs the host assigned. 0 = closed.
    std::uint16_t bt_l2cap_control_local_cid_ = 0;
    std::uint16_t bt_l2cap_control_remote_cid_ = 0;
    std::uint16_t bt_l2cap_interrupt_local_cid_ = 0;
    std::uint16_t bt_l2cap_interrupt_remote_cid_ = 0;
    std::uint16_t bt_l2cap_next_local_cid_ = 0x0040;
    std::uint8_t bt_l2cap_next_signal_id_ = 0x02;
    bool bt_l2cap_control_config_request_seen_ = false;
    bool bt_l2cap_control_config_response_seen_ = false;
    bool bt_l2cap_interrupt_config_request_seen_ = false;
    bool bt_l2cap_interrupt_config_response_seen_ = false;
    std::uint8_t bt_wiimote_report_mode_ = 0x30;  // core buttons until set
    bool bt_wiimote_reporting_enabled_ = false;
    bool bt_wiimote_init_complete_ = false;
    bool bt_wiimote_init_delay_logged_ = false;
    std::uint8_t bt_wiimote_led_mask_ = 0x10;
    bool bt_wiimote_rumble_ = false;
    bool bt_wiimote_speaker_enabled_ = false;
    bool bt_wiimote_speaker_muted_ = true;
    bool bt_wiimote_ir_enabled_ = false;
    bool bt_wiimote_ir_report13_enabled_ = false;
    bool bt_wiimote_ir_report1a_enabled_ = false;
    bool bt_wiimote_ir_sensitivity_block1_seen_ = false;
    bool bt_wiimote_ir_sensitivity_block2_seen_ = false;
    bool bt_wiimote_ir_latch_seen_ = false;
    std::uint8_t bt_wiimote_ir_mode_ = 0;
    galaxy::input::WiimoteInputSnapshot latest_native_hid_snapshot_{};
    bool latest_native_hid_snapshot_valid_ = false;
    galaxy::input::SyntheticKpadButtonState synthetic_kpad_buttons_{};
    float latest_native_pointer_x_ = 0.0f;
    float latest_native_pointer_y_ = 0.0f;
    bool latest_native_pointer_active_ = false;
    // All fields below are guest-thread owned and touched only by the opt-in
    // pointer policy. Two fixed slots retain the first A and B press per game
    // read, matching the existing at-most-one-edge-per-bit consumer contract.
    SyntheticKpadPointerSample synthetic_kpad_pointer_sample_{};
    std::array<SyntheticKpadPointerSample, 2> synthetic_kpad_mouse_press_{};
    std::uint32_t synthetic_kpad_mouse_press_mask_{};
    std::uint32_t synthetic_kpad_last_mouse_buttons_{};
    std::uint32_t synthetic_kpad_last_non_mouse_buttons_{};
    std::uint32_t synthetic_kpad_pending_non_mouse_press_{};
    SyntheticKpadPointerStats synthetic_kpad_pointer_stats_{};
    std::optional<SyntheticKpadPointerSample> synthetic_kpad_known_outside_{};
    bool bt_wiimote_extension_init_seen_ = false;
    bool bt_wiimote_extension_initialized_ = false;
    bool bt_wiimote_interrupt_open_ = false;
    std::deque<DeferredIosReply> deferred_ios_replies_;
    // Completed IOS requests (physical addresses) waiting for the guest to
    // acknowledge the currently posted reply.  See post_next_ipc_reply().
    std::deque<std::uint32_t> ipc_pending_replies_;
    // Physical request addresses of posted IOS replies.  The retail IPC
    // handler W1C-acks PPCCTRL.Y2 before it processes ARMMSG, so the host must
    // keep the posted reply stable until the following IPC IRQ flag W1C.
    std::set<std::uint32_t> latched_reply_requests_;
    std::uint32_t latched_reply_ = 0;
    bool latched_reply_acked_ = false;
    bool latched_reply_read_by_handler_ = false;
    bool latched_reply_release_deferred_ = false;
    std::uint32_t ipc_interrupt_handler_pass_depth_ = 0;
    std::uint64_t disc_read_ticket_sequence_ = 0;
    std::unordered_map<std::uint32_t, DiscReadTicket> disc_read_tickets_;
    std::vector<std::byte> gx_fifo_;
    // CP FIFO ring-buffer simulation.  When the game writes to the CP write-
    // pointer registers (0x0C000034/36), we read the newly produced bytes from
    // MEM1 into gx_fifo_ so render_frame sees them without needing real CP DMA.
    //
    // CP register layout (all offsets from 0x0C000000, 16-bit writes):
    //   0x0020 = CP_BASE_LO (lower 16 bits of base addr, written FIRST by SDK)
    //   0x0022 = CP_BASE_HI (upper 16 bits of base addr, written SECOND → triggers)
    //   0x0024 = CP_END_LO  (lower 16 bits of end addr,  written FIRST)
    //   0x0026 = CP_END_HI  (upper 16 bits of end addr,  written SECOND → triggers)
    //   0x0034 = CP_WRITE_PTR_LO (lower 16 bits of write ptr, written FIRST)
    //   0x0036 = CP_WRITE_PTR_HI (upper 16 bits of write ptr, written SECOND → advance)
    //   0x0038 = CP_READ_PTR_LO  (lower 16 bits — written back by advance_cp_fifo)
    //   0x003A = CP_READ_PTR_HI  (upper 16 bits — written back by advance_cp_fifo)
    std::uint16_t cp_base_lo_{0};  // saved lower 16 bits (from offset 0x0020)
    std::uint16_t cp_end_lo_{0};   // saved lower 16 bits (from offset 0x0024)
    std::uint16_t cp_wr_lo_{0};    // saved lower 16 bits (from offset 0x0034)
    std::uint32_t cp_fifo_base_{0};
    std::uint32_t cp_fifo_end_{0};
    std::uint32_t cp_rd_ptr_{0};
    void advance_cp_fifo(std::uint32_t new_wr_ptr);
    bool cpu_fifo_is_live() const;
    bool wgpipe_feeds_live_fifo() const;
    enum class WgpipePeOwnershipTraceKind : std::uint8_t {
        PeCommand,
        PiCpuFifoRegister,
        CpFifoRegister,
    };
    struct WgpipePeOwnershipTraceRecord {
        std::uint64_t sequence{};
        std::uint64_t ticks{};
        WgpipePeOwnershipTraceKind kind{};
        std::uint32_t register_offset{};
        std::uint32_t previous_value{};
        std::uint32_t current_value{};
        std::uint32_t write_bytes{};
        std::uint32_t gather_before{};
        std::uint32_t gather_after{};
        bool live_before{};
        bool live_after{};
        std::uint32_t cpu_base{};
        std::uint32_t cpu_top{};
        std::uint32_t cpu_write{};
        std::uint32_t cp_base{};
        std::uint32_t cp_end{};
        std::uint32_t cp_read{};
        std::size_t gx_fifo_bytes{};
    };
    void trace_wgpipe_pe_commands(
        std::span<const std::byte> bytes,
        std::uint32_t gather_before,
        bool live_before);
    void record_wgpipe_ownership_transition(
        WgpipePeOwnershipTraceKind kind,
        std::uint32_t register_offset,
        std::uint32_t previous_value,
        std::uint32_t current_value,
        std::uint32_t write_bytes,
        std::uint32_t gather_before,
        bool live_before);
    void append_wgpipe_pe_ownership_trace(
        WgpipePeOwnershipTraceRecord record);
    void dump_wgpipe_pe_ownership_trace() const;
    void flush_wgpipe_gather_burst(std::span<const std::byte, 32> bytes);
    void trace_gx_fifo_append(
        const char* source,
        std::size_t append_start,
        std::size_t append_size);
    void record_wgpipe_pe_scan_hint(std::span<const std::byte> bytes);
    void scan_direct_wgpipe_pe_events(std::span<const std::byte> bytes);
    bool write_wgpipe_bytes(std::span<const std::byte> bytes);
    std::uint32_t cpu_fifo_base_{0};
    std::uint32_t cpu_fifo_top_{0};
    std::uint32_t cpu_fifo_wr_ptr_{0};
    bool cpu_fifo_wrap_{false};
    std::array<std::byte, 32> wgpipe_gather_{};
    std::uint32_t wgpipe_gather_count_{0};
    static constexpr std::size_t kWgpipePeTraceCapacity = 512u;
    static constexpr std::size_t kWgpipeOwnershipTraceCapacity = 8192u;
    bool wgpipe_pe_scan_hint_enabled_{};
    bool wgpipe_pe_ownership_trace_enabled_{};
    std::array<std::uint8_t, 5> wgpipe_pe_ownership_recent_bytes_{};
    std::uint32_t wgpipe_pe_ownership_recent_count_{};
    std::uint64_t wgpipe_pe_ownership_trace_sequence_{};
    std::vector<WgpipePeOwnershipTraceRecord> wgpipe_pe_trace_records_;
    std::vector<WgpipePeOwnershipTraceRecord> wgpipe_ownership_trace_records_;
    std::size_t wgpipe_pe_trace_next_{};
    std::size_t wgpipe_ownership_trace_next_{};
    std::uint64_t wgpipe_pe_trace_overwritten_{};
    std::uint64_t wgpipe_ownership_trace_overwritten_{};
    // Disc ID returned by DVDLowReadDiskID — the first 0x20 bytes of boot.bin.
    std::array<std::byte, 0x20> disc_id_{};
    std::vector<std::byte> disc_ticket_;
    std::vector<std::byte> disc_tmd_;
    // PI_INTSR combines device-owned live levels with PI-owned latched
    // sources. Device levels are acknowledged at their device registers;
    // exact 32-bit W1C writes to PI_INTSR clear only this PI-owned latch.
    std::uint32_t pi_interrupt_cause_latch_{};
    std::atomic_bool pe_finish_pending_{false};
    std::atomic_bool pe_token_pending_{false};
    std::atomic<std::uint16_t> pe_token_value_{0};
    // Native DSP-interface mailbox state: to-DSP mails arrive as a 16-bit high write
    // (0x5000) followed by a 16-bit low write (0x5002).  The DSP boot-task
    // protocol sends (command, value) mail pairs; command 0x80F3D001's value
    // is the start vector — the "DSP" then replies DSP_INIT (0xDCD10000).
    // Task-level mails after boot are answered with DSP_RESUME (0xDCD10001).
    bool aram_dma_active_{};
    bool aram_dma_aram_to_mram_{};
    std::uint32_t aram_dma_mram_address_{};
    std::uint32_t aram_dma_aram_address_{};
    std::uint32_t aram_dma_total_blocks_{};
    std::uint32_t aram_dma_completed_blocks_{};
    std::uint64_t aram_dma_start_ticks_{};
    std::uint64_t aram_dma_next_block_ticks_{};
    bool ai_dma_playing_{};
    std::uint32_t ai_dma_shadow_address_{};
    std::uint16_t ai_dma_shadow_blocks_{};
    std::uint32_t ai_dma_active_address_{};
    std::uint16_t ai_dma_active_blocks_{};
    std::uint32_t ai_dma_active_sample_rate_{};
    std::uint64_t ai_dma_active_start_ticks_{};
    std::uint64_t ai_dma_next_completion_ticks_{};
    std::uint64_t ai_dma_next_interrupt_ticks_{};
    std::uint64_t ai_dma_interrupt_pending_since_ticks_{};
    std::uint64_t ai_dma_last_duration_ticks_{};
    std::uint64_t ai_dma_resync_events_{};
    std::uint64_t ai_dma_resync_missed_buffers_{};
    std::uint64_t ai_dma_notified_deadline_ticks_{};
    AiDmaDeadlineCallback ai_dma_deadline_callback_{};
    void* ai_dma_deadline_callback_user_{};
    AiDmaMmioServiceDecisionProvider
        ai_dma_mmio_service_decision_provider_{};
    void* ai_dma_mmio_service_decision_user_{};
    std::uint64_t ai_audio_submit_count_{};
    bool ai_audio_seen_nonzero_pcm_{};
    galaxy::audio::AiPcmIdentityTracker ai_pcm_identity_tracker_;
    bool ai_pcm_identity_tracking_enabled_{};
    std::unique_ptr<NativeAudioSink> native_audio_;
    std::unique_ptr<NativeIosAnomalyLedger> native_ios_anomalies_;
    NativeAudioFailureCallback native_audio_failure_callback_{};
    void* native_audio_failure_callback_user_{};
    std::atomic_bool native_audio_output_enabled_{true};
    bool dsp_task_booted_{};
    bool dsp_expect_value_mail_{};
    std::uint16_t dsp_mail_to_high_{};
    std::uint32_t dsp_last_command_{};
    // Boot-task upload descriptor reconstructed from the 0x80F3xxxx (command,
    // value) mailbox pairs (Dolphin ROM.cpp ROMUCode::HandleMail): A001=IRAM
    // main-RAM source, A002=IRAM length, B002=DRAM length, C002=IRAM DSP dest,
    // D001=start vector.  Captured so a single boot can persist the real jdsp
    // IRAM image for the static recompiler (maybe_dump_dsp_ucode).
    std::uint32_t dsp_boot_iram_src_{};
    std::uint32_t dsp_boot_iram_len_{};
    std::uint16_t dsp_boot_iram_dest_{};
    std::uint16_t dsp_boot_dram_len_{};
    bool dsp_native_task_vectors_valid_{};
    std::uint32_t dsp_native_task_descriptor_{};
    std::uint16_t dsp_native_task_start_vector_{};
    std::uint16_t dsp_native_task_resume_vector_{};
    std::uint64_t dsp_native_to_mail_count_{};
    std::uint64_t dsp_native_to_mail_after_mram_count_{};
    // Post-boot JAudio command-group state (DSPSendCommands2 framing: one
    // COUNT mail, then COUNT command words; finish mail acknowledges the
    // group).  See on_dsp_mail.
    std::uint32_t dsp_cmd_words_remaining_{};
    std::uint32_t dsp_cmd_first_word_{};
    std::uint32_t dsp_cmd_word_count_{};
    std::array<std::uint32_t, 64> dsp_cmd_words_{};
    bool dsp_cmd_have_first_{};
    struct PendingDspSyncFrame {
        bool active{};
        std::uint64_t sync_frame_index{};
        std::uint32_t command{};
        std::uint32_t left_buffer{};
        std::uint32_t right_buffer{};
        std::uint32_t aux_a_buffer{};
        std::uint32_t aux_b_buffer{};
        std::uint32_t total_subframes{};
        std::uint32_t next_subframe{};
    };
    PendingDspSyncFrame pending_dsp_sync_frame_{};
    bool pending_dsp_sync_release_deferred_{};
    std::uint32_t dsp_channel_table_addr_{};
    std::uint32_t dsp_resampler_filter_addr_{};
    std::uint32_t dsp_adpcm_filter_addr_{};
    std::uint64_t dsp_sync_frame_count_{};
    std::unique_ptr<std::array<NativeDuskDspChannelAux, 64>>
        dsp_dusk_channel_aux_{};
    bool dsp_adpcm_first_trace_logged_{};
    bool dsp_audio_event_signature_valid_{};
    bool dsp_audio_event_seen_adpcm_{};
    std::uint32_t dsp_audio_event_last_active_{};
    std::uint32_t dsp_audio_event_last_rendered_{};
    std::uint32_t dsp_audio_event_last_adpcm_{};
    std::uint32_t dsp_audio_event_log_count_{};
    std::uint32_t dsp_fullscale_trace_log_count_{};
    std::uint32_t dsp_audio_stem_trace_log_count_{};
    DspAudioStats dsp_audio_stats_{};
    struct DspAdpcmPredictorCheckpoint {
        std::uint32_t sample{};
        std::int16_t history_1{};
        std::int16_t history_2{};
    };
    using DspAdpcmPredictorCache =
        std::vector<DspAdpcmPredictorCheckpoint>;
    struct DspChannelRenderState {
        bool was_active{};
        std::uint32_t source{};
        std::uint32_t current_source{};
        std::uint32_t source_origin{};
        std::uint32_t position_bias{};
        std::uint16_t block_samples{};
        std::uint16_t block_bytes{};
        bool looped{};
        std::uint32_t loop_start{};
        std::uint32_t loop_end{};
        std::uint32_t sample_count{};
        std::uint32_t integer_position{};
        std::uint32_t fractional_position_q12{};
        bool adpcm_cache_valid{};
        std::uint32_t adpcm_cache_block{};
        std::uint32_t adpcm_decode_start_sample{};
        bool adpcm_decode_from_loop{};
        std::int16_t adpcm_base_history_1{};
        std::int16_t adpcm_base_history_2{};
        std::int16_t adpcm_loop_history_1{};
        std::int16_t adpcm_loop_history_2{};
        std::int16_t adpcm_history_1{};
        std::int16_t adpcm_history_2{};
        bool adpcm_checkpoint_valid{};
        std::uint32_t adpcm_checkpoint_sample{};
        std::int16_t adpcm_checkpoint_history_1{};
        std::int16_t adpcm_checkpoint_history_2{};
        std::uint64_t adpcm_predictor_cache_key{};
        std::shared_ptr<DspAdpcmPredictorCache> adpcm_predictor_cache{};
        std::array<std::int16_t, 16> adpcm_samples{};
        std::array<std::int8_t, 16> adpcm_sample_clamp_signs{};
        std::array<std::int16_t, 8> fir_history{};
        std::int16_t iir_xn1{};
        std::int16_t iir_xn2{};
        std::int16_t iir_yn1{};
        std::int16_t iir_yn2{};
    };
    std::array<DspChannelRenderState, 64> dsp_render_channels_{};
    std::unordered_map<
        std::uint64_t,
        std::shared_ptr<DspAdpcmPredictorCache>>
        dsp_adpcm_predictor_caches_;
    // From-DSP mails wait here while the (single-slot) mailbox is full; the
    // next one is delivered when the guest consumes the current mail (FromLow
    // read).  Needed because the JAudio boot handshake is TWO mails: DSP_INIT
    // (read by __DSPHandler, which then calls the task's init callback) and
    // the ucode's 0xF3551111 (read by DspHandShake, Petari dsptask.cpp).
    std::deque<std::uint32_t> dsp_from_mail_queue_;
    // --- Native DSP coprocessor ---
    // The real coprocessor runs the lowered RMGE01 ucode in place of the retired
    // high-level mixer. The interrupt flag is written from the DSP worker thread.
    bool dsp_native_enabled_{};
    bool dsp_dusk_hle_enabled_{};
    bool dsp_native_running_{};
    // Declare the rendezvous before both coprocessor owners so reverse member
    // destruction cannot remove it first. The destructor additionally calls
    // dsp_native_shutdown(), which cancels this slot and joins the worker
    // before any member teardown begins.
    galaxy::DspAramMirrorBoundary dsp_native_aram_boundary_{};
    galaxy::DspMramTransactionBoundary dsp_native_mram_transactions_{};
    galaxy::DspMramTransactionBoundary
        dsp_native_aram_commit_transactions_{};
    // Process-lifetime owner for the exact selected-channel identity boundary.
    // Production audio-bus routing uses its validated channel sequence; the
    // larger diagnostic publication payload remains opt-in. It deliberately
    // precedes the coprocessor and worker so DSPCR teardown can join/destroy
    // them without discarding prior publication/DMA evidence.
    std::unique_ptr<galaxy::DspChannelSelectionDmaProbeRecorder>
        dsp_channel_selection_dma_probe_{};
    galaxy::audio::NativeAudioBusControls dsp_audio_bus_controls_{};
    std::unique_ptr<galaxy::NativeDspCoprocessor> dsp_native_{};
    std::unique_ptr<galaxy::NativeDspWorker> dsp_native_worker_{};
    std::atomic<bool> dsp_native_int_pending_{};
    // A Broadway high-half write is only half of the CPU->DSP mailbox
    // transaction. Keep it staged on the CPU-visible mirror until the low-half
    // write completes the single hardware slot.
    bool dsp_native_to_mail_high_staged_{};
    // True while a native-worker DSP->CPU mail has been mirrored into the
    // Broadway-visible MMIO mailbox. The worker-side low half is consumed only
    // when translated guest code reads FromLow, matching the hardware clear.
    bool dsp_native_from_mail_staged_{};
    // The generated DSP raises DIRQ separately from writing the from-DSP
    // mailbox. Keep an observed DIRQ edge until its DCD1 mail is visible, then
    // dispatch exactly one Broadway DSP interrupt for that unread mail.
    bool dsp_native_dirq_pending_{};
    // Prevents dispatching duplicate Broadway DSP interrupts for the same
    // unread DCD1 mail while the hardware mailbox remains held.
    bool dsp_native_from_mail_irq_latched_{};
    // Diagnostic/native-task ownership guard: when suppressing task-done mails,
    // drop the paired DIRQ edge even if the worker posts it just after the mail.
    bool dsp_native_suppressing_task_done_dirq_{};
    std::uint64_t dsp_native_suppressed_task_done_count_{};
    // Diagnostic pacing for native DSP sync-frame completions. The production
    // path lets the native ucode finish ahead of AI DMA; pacing can be
    // re-enabled for mailbox timing traces.
    bool dsp_native_sync_pacing_active_{};
    std::uint64_t dsp_native_next_sync_completion_ticks_{};
    std::uint64_t dsp_native_paced_sync_completion_count_{};
    std::uint64_t dsp_native_held_sync_completion_count_{};
    // In native mode this should remain empty: the worker mailbox itself is the
    // single hardware slot. Retained so diagnostics can assert that no hidden
    // DSP->CPU queue is being used.
    std::deque<std::uint32_t> dsp_native_from_mail_queue_;
    std::uint64_t dsp_native_mram_miss_count_{};
    std::uint64_t dsp_native_aram_miss_count_{};
    // Captured and validated from DsetVARAM on the CPU thread before that mail
    // is published to the native DSP worker. The ARAM backing is immutable for
    // the service lifetime so a guest-global write cannot redirect the two
    // bytes of one accelerator sample to different mappings.
    std::atomic<std::uint32_t> dsp_native_aram_backing_base_{};
    std::byte* dsp_native_aram_backing_host_{};
    static constexpr std::uint32_t kDspNativeAramDirtyWordCount =
        galaxy::DspAramMirrorBoundary::kDirtyPageCount / 64u;
    std::unique_ptr<std::uint64_t[]>
        dsp_native_aram_cpu_dirty_words_;
    std::vector<galaxy::DspAramMirrorBoundary::DirtyPageUpdate>
        dsp_native_aram_dirty_updates_;
    std::atomic<bool> dsp_native_aram_active_{};
    std::atomic<std::uint64_t> dsp_native_aram_outbound_generation_{};
    std::uint32_t dsp_native_command_declared_count_{};
    bool dsp_native_command_is_dset_{};
    bool dsp_native_dset_seen_{};
    // Recent bytes written through WGPIPE: a BP command is a 1-byte 0x61
    // followed by a 4-byte value whose FIRST byte selects the register.
    std::array<std::uint8_t, 5> wgpipe_recent_bytes_{};
    std::uint32_t wgpipe_recent_count_{0};
    std::array<std::uint8_t, 5> wgpipe_pe_hint_recent_bytes_{};
    std::uint32_t wgpipe_pe_hint_recent_count_{0};
    bool gx_pe_scan_hint_pending_{false};
    // Deterministic tick source for AISCNT (see set_tick_source).
    TickSource tick_source_{};
    void* tick_source_user_{};
    // game.pak memory-mapped file handles and view pointer.
    // All three are nullptr until open_game_pak() succeeds.
    void* pak_file_{};
    void* pak_mapping_{};
    const std::byte* pak_view_{};
    std::uint64_t pak_size_{};
    std::vector<DiscOverride> disc_overrides_;
    GuestMemoryV1 memory_{};

    // See install_fst_scene_table()/file_select_scene_active().
    struct FstSceneEntry {
        std::uint64_t disc_offset{};
        std::uint64_t size{};
        bool is_file_select{};
    };
    std::vector<FstSceneEntry> fst_scene_entries_;
    void note_disc_read_for_scene_hint(
        std::uint64_t byte_offset, std::uint64_t byte_count) noexcept;
};

void load_boot_image(
    GuestAddressSpace& address_space,
    const BootImage& image);
void initialize_wii_memory_values(GuestAddressSpace& address_space);
void initialize_boot_memory(
    GuestAddressSpace& address_space,
    const BootImage& image,
    const std::filesystem::path& content_root);

}  // namespace galaxy::host
