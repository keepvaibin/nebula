#pragma once

#include "galaxy/content_viewport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::input {

inline constexpr std::uint8_t kWiimoteHidInput = 0xA1;
inline constexpr std::uint8_t kWiimoteHidOutput = 0xA2;
inline constexpr std::size_t kMaxWiimoteInputReportBytes = 23;
inline constexpr std::uint64_t kNativeHidTimelineTicksPerSecond =
    60'750'000ull;
inline constexpr std::uint32_t kProvisionalVirtualHidReportRateHz = 100u;
inline constexpr std::uint64_t kNativeHidReportPeriodTicks =
    kNativeHidTimelineTicksPerSecond /
    kProvisionalVirtualHidReportRateHz;
inline constexpr std::uint16_t kWiimoteRestAccelX = 0x01fc;
inline constexpr std::uint16_t kWiimoteRestAccelY = 0x0204;
inline constexpr std::uint16_t kWiimoteRestAccelZ = 0x0264;

// Parse the decimal grammar accepted by native input-script integer fields.
// The grammar is intentionally narrower than the CRT conversion routines:
// ASCII digits only, no sign or whitespace, and no modulo wrap on overflow.
// `value` is changed only on success.
[[nodiscard]] bool parse_native_input_script_u64(
    std::string_view text,
    std::uint64_t& value) noexcept;

// Resolve a marker-relative script offset without permitting either the text
// conversion or anchor addition to wrap. `start_vi` is changed only on
// success.
[[nodiscard]] bool resolve_native_input_script_start_vi(
    std::uint64_t anchor_vi,
    std::string_view offset_text,
    std::uint64_t& start_vi) noexcept;

struct WiimoteButtons {
    bool left = false;
    bool right = false;
    bool down = false;
    bool up = false;
    bool plus = false;
    bool two = false;
    bool one = false;
    bool b = false;
    bool a = false;
    bool minus = false;
    bool home = false;
};

struct Axis10 {
    std::uint16_t x = 512;
    std::uint16_t y = 512;
    std::uint16_t z = 512;
};

struct IrDot {
    bool visible = false;
    std::uint16_t x = 0x03ff;
    std::uint16_t y = 0x03ff;
    std::uint8_t size = 0;
};

struct NunchukState {
    std::uint8_t stick_x = 0x7b;
    std::uint8_t stick_y = 0x81;
    Axis10 accel{};
    bool c = false;
    bool z = false;
};

struct WiimoteInputSnapshot {
    WiimoteButtons buttons{};
    Axis10 accel{kWiimoteRestAccelX, kWiimoteRestAccelY, kWiimoteRestAccelZ};
    std::array<IrDot, 4> ir{};
    NunchukState nunchuk{};
};

struct WiimoteDeviceStatus {
    WiimoteButtons buttons{};
    std::uint8_t led_mask = 0x10;
    bool battery_low = false;
    bool extension_connected = true;
    bool speaker_enabled = false;
    bool ir_enabled = false;
    std::uint8_t battery_level = 0xff;
};

// One device-produced HID sample. `due_slots` is greater than one only when
// the host services the native hardware timeline late; in that case this is
// the newest sample and the older elapsed slots are explicitly coalesced.
struct NativeHidCadenceSample {
    std::uint64_t epoch{};
    std::uint64_t sequence{};
    std::uint64_t deadline_ticks{};
    std::uint64_t due_slots{};
};

// Exact periodic identities drained atomically from the native deadline
// broker. Device code consumes only this immutable batch; a later wall-clock
// read may measure production lateness but cannot invent another elapsed edge.
struct NativeHidPublishedBatch {
    std::uint64_t count{};
    std::uint64_t first_sequence{};
    std::uint64_t last_sequence{};
    std::uint64_t first_deadline_ticks{};
    std::uint64_t last_deadline_ticks{};
};

struct NativeHidTimingProfile {
    std::string_view id;
    std::uint32_t nominal_rate_hz{};
    std::uint64_t period_ticks{};
    std::uint64_t maximum_production_lateness_ticks{};
    bool strict_proof_qualified{};
};

// This is an empirical host-acquisition profile for the virtual keyboard,
// mouse, and XInput adapter. Local physical-Wiimote captures cluster around
// 100 reports/s, but do not establish the remote's oscillator. The profile is
// therefore usable for normal input latency and explicitly ineligible for a
// strict hardware-cadence attestation.
inline constexpr NativeHidTimingProfile
    kProvisionalVirtualHidTimingProfile{
        "virtual-host-acquisition-100hz-provisional",
        kProvisionalVirtualHidReportRateHz,
        kNativeHidReportPeriodTicks,
        kNativeHidReportPeriodTicks - 1u,
        false};

// Project-owned native keyboard/mouse/XInput endpoint contract. This does not
// claim that a physical Wii Remote has a crystal-locked 100 Hz oscillator.
// Instead, the PC adapter itself is defined to sample once every 607,500
// Broadway timeline ticks and to service each deadline before the next one.
// It emits byte-exact raw HID/ACL reports into the translated WUD/WPAD/KPAD
// stack. Explicit keyboard/mouse/controller mode also has an authorized
// synthetic KPAD adapter; that separate route is not hardware-cadence proof.
// Because the raw endpoint and its clock
// are implemented and specified here, strict proof can qualify this contract
// without misrepresenting the separately completion-driven physical-HID path.
inline constexpr NativeHidTimingProfile kNativeVirtualHidTimingProfile{
    "native-virtual-contract-v1-100hz",
    kProvisionalVirtualHidReportRateHz,
    kNativeHidReportPeriodTicks,
    kNativeHidReportPeriodTicks - 1u,
    true};

// Deterministic unit tests may exercise strict conservation with a qualified
// synthetic clock. Production code must never select this profile.
inline constexpr NativeHidTimingProfile kQualifiedTestHidTimingProfile{
    "qualified-test-only-100hz",
    kProvisionalVirtualHidReportRateHz,
    kNativeHidReportPeriodTicks,
    0u,
    true};

struct NativeHidCadenceStats {
    bool armed{};
    bool strict_proof{};
    bool profile_strict_proof_qualified{};
    bool exact_cadence_valid{true};
    std::string_view profile_id{};
    std::uint32_t nominal_rate_hz{};
    std::uint64_t period_ticks{};
    std::uint64_t maximum_production_lateness_ticks{};
    // The virtual endpoint contract requires the exact ACL handoff to occur
    // before the following report deadline. This limit is therefore one tick
    // less than period_ticks and is measured in the same 60.75 MHz timeline.
    std::uint64_t delivery_age_limit_ticks{};
    std::uint64_t epochs_started{};
    std::uint64_t disconnect_resets{};
    std::uint64_t scheduled_samples{};
    std::uint64_t produced_samples{};
    std::uint64_t skipped_due_slots{};
    std::uint64_t queue_replacements{};
    std::uint64_t mode_invalidations{};
    std::uint64_t reset_queue_discards{};
    std::uint64_t delivered_samples{};
    std::uint64_t delivery_gaps{};
    std::uint64_t delivery_bursts{};
    std::uint64_t out_of_order_deliveries{};
    // Every delivered sample contributes exactly one age observation. Every
    // delivery after the first in a logical epoch also contributes exactly
    // one interval observation. The two count identities let shutdown
    // analysis hard-fail missing or double-counted timing telemetry.
    std::uint64_t delivery_age_samples{};
    std::uint64_t delivery_age_total_ticks{};
    std::uint64_t min_delivery_age_ticks{};
    std::uint64_t max_delivery_age_ticks{};
    std::uint64_t last_delivery_age_ticks{};
    std::uint64_t delivery_timing_streams_started{};
    std::uint64_t delivery_interval_samples{};
    std::uint64_t delivery_interval_total_ticks{};
    std::uint64_t min_delivery_interval_ticks{};
    std::uint64_t max_delivery_interval_ticks{};
    std::uint64_t max_delivery_interval_jitter_ticks{};
    std::uint64_t last_produced_epoch{};
    std::uint64_t last_produced_sequence{};
    std::uint64_t last_produced_deadline_ticks{};
    std::uint64_t last_production_ticks{};
    std::uint64_t max_production_lateness_ticks{};
    std::uint64_t last_delivered_epoch{};
    std::uint64_t last_delivered_sequence{};
    std::uint64_t last_delivered_deadline_ticks{};
    std::uint64_t last_delivery_ticks{};

    [[nodiscard]] bool delivery_timing_conservation_valid() const noexcept {
        if (delivery_age_samples != delivered_samples ||
            delivery_timing_streams_started > delivered_samples) {
            return false;
        }
        return delivery_interval_samples ==
            delivered_samples - delivery_timing_streams_started;
    }
};

// A transport-agnostic one-packet FIFO for completion-driven HID sources.
// Producers never wait for guest reads: a new completion atomically replaces
// an older unconsumed packet, and every completed report remains conserved as
// exactly one delivery, replacement, reset discard, or occupied packet.
struct NativeNewestOnlyHidPacket {
    std::vector<std::byte> packet;
    std::uint64_t sequence{};
    std::uint64_t completion_ticks{};
};

struct NativeNewestOnlyHidFifoStats {
    std::uint64_t completed_reports{};
    std::uint64_t fifo_replacements{};
    std::uint64_t reset_discards{};
    std::uint64_t delivered_reports{};
    std::uint64_t last_delivered_sequence{};
    std::uint64_t max_delivery_age_ticks{};
};

class NativeNewestOnlyHidFifo {
public:
    [[nodiscard]] std::uint64_t push(
        std::vector<std::byte> packet,
        std::uint64_t completion_ticks);
    void note_delivery(std::uint64_t delivery_ticks);
    void discard_on_reset();

    [[nodiscard]] const NativeNewestOnlyHidPacket* front() const noexcept {
        return queued_.has_value() ? &*queued_ : nullptr;
    }
    [[nodiscard]] bool occupied() const noexcept {
        return queued_.has_value();
    }
    [[nodiscard]] const NativeNewestOnlyHidFifoStats& stats() const noexcept {
        return stats_;
    }
    [[nodiscard]] bool conservation_valid() const noexcept;

private:
    void require_conservation() const;

    std::optional<NativeNewestOnlyHidPacket> queued_;
    NativeNewestOnlyHidFifoStats stats_{};
};

// Physical HID control replies are protocol messages, not replaceable state.
// Keep them bounded and ordered alongside one coalesced continuous sample.
// The caller serializes every operation (the worker uses its mailbox mutex).
class NativeHidInputMailbox {
public:
    explicit NativeHidInputMailbox(std::size_t maximum_pending_replies = 256u);
    [[nodiscard]] std::uint64_t push(std::vector<std::byte> packet,
                                     std::uint64_t completion_ticks);
    [[nodiscard]] const NativeNewestOnlyHidPacket* front() const noexcept;
    void note_delivery(std::uint64_t delivery_ticks);
    void discard_on_reset();
    [[nodiscard]] bool occupied() const noexcept { return front() != nullptr; }
    [[nodiscard]] const NativeNewestOnlyHidFifoStats& stats() const noexcept { return stats_; }
    [[nodiscard]] bool conservation_valid() const noexcept;

private:
    void require_conservation() const;
    const std::size_t maximum_pending_replies_;
    std::deque<NativeNewestOnlyHidPacket> replies_;
    std::optional<NativeNewestOnlyHidPacket> continuous_;
    NativeNewestOnlyHidFifoStats stats_{};
};

// Optional deterministic clock dependency. A supplied callback must be
// thread-safe, monotonic, use one epoch, and must not reenter the worker.
// An empty clock selects the worker's steady-clock origin.
struct NativeHidIoClock {
    std::uint64_t (*read_ticks)(void* user){};
    void* user{};
};

// Blocking platform HID work is isolated behind this interface. Production
// uses the Windows HID backend; deterministic tests inject a fake backend.
// `poll_input` must be nonblocking once `open` returns. Read and write may run
// concurrently, and `request_stop` must make an in-flight operation finish in
// bounded time so shutdown never depends on another guest command.
class NativeHidIoBackend {
public:
    virtual ~NativeHidIoBackend() = default;
    virtual void open() = 0;
    [[nodiscard]] virtual bool poll_input(
        std::vector<std::byte>& report) = 0;
    // Returns false only when request_stop cancelled an in-flight write.
    // Any non-cancellation write failure must throw.
    [[nodiscard]] virtual bool write_output(
        std::span<const std::byte> report) = 0;
    virtual void request_stop() noexcept = 0;
    virtual void close() noexcept = 0;
};

struct NativeHidIoWorkerStats {
    bool started{};
    bool opened{};
    bool failed{};
    bool stopped{};
    bool input_occupied{};
    bool input_conservation_valid{};
    bool output_in_flight{};
    std::size_t pending_output_reports{};
    std::size_t maximum_pending_output_reports{};
    std::uint64_t output_reports_enqueued{};
    std::uint64_t output_reports_completed{};
    NativeNewestOnlyHidFifoStats input{};
};

enum class NativeHidIoTakeStatus {
    Empty,
    TooLarge,
    Ok,
};

struct NativeHidIoTakeResult {
    NativeHidIoTakeStatus status = NativeHidIoTakeStatus::Empty;
    NativeNewestOnlyHidPacket report{};
};

class NativeHidIoWorker {
public:
    using FailureNotifier = void (*)(void* user) noexcept;

    explicit NativeHidIoWorker(
        std::unique_ptr<NativeHidIoBackend> backend,
        std::size_t maximum_pending_output_reports = 64u,
        std::uint32_t input_poll_interval_ms = 1u,
        std::size_t maximum_pending_input_replies = 256u,
        NativeHidIoClock clock = {});
    ~NativeHidIoWorker();
    NativeHidIoWorker(const NativeHidIoWorker&) = delete;
    NativeHidIoWorker& operator=(const NativeHidIoWorker&) = delete;
    NativeHidIoWorker(NativeHidIoWorker&&) = delete;
    NativeHidIoWorker& operator=(NativeHidIoWorker&&) = delete;

    // Starts the coordinator thread and returns without waiting for platform
    // discovery/open. All potentially blocking backend calls stay off-caller.
    // The owner must serialize lifecycle calls and not call stop from notifier.
    void start();
    void stop() noexcept;
    void set_failure_notifier(
        FailureNotifier callback,
        void* user) noexcept;
    void enqueue_output(std::vector<std::byte> report);
    // Returns the next retained control reply or newest continuous sample in
    // completion order. TooLarge leaves that packet queued for a larger read.
    [[nodiscard]] NativeHidIoTakeResult take_latest_input(
        std::size_t maximum_report_bytes);
    void discard_input_on_reset();
    [[nodiscard]] bool input_occupied() const noexcept;
    [[nodiscard]] NativeHidIoWorkerStats stats() const noexcept;
    void throw_if_failed() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Emits one stable, machine-readable audit record containing every cadence
// statistic. Runtime shutdown calls this before ExitProcess so strict proof
// runs cannot lose their terminal device-cadence evidence to skipped C++
// destructors.
void dump_native_hid_cadence_audit(
    std::ostream& output,
    std::string_view tag,
    const NativeHidCadenceStats& stats,
    bool fifo_occupied);

// Deterministic profiled report clock in the Wii 60.75 MHz time-base domain.
// This class deliberately has no USB/read concept: callers advance hardware
// time, enqueue at most the returned newest sample, and account delivery only
// when a transport consumer drains that sample.
class NativeHidReportCadence {
public:
    explicit NativeHidReportCadence(
        bool strict_proof = false,
        NativeHidTimingProfile profile =
            kProvisionalVirtualHidTimingProfile) noexcept;

    void arm(std::uint64_t reporting_origin_ticks);
    // Starts a new report-semantics epoch without moving the hardware sample
    // deadline. Used when an IR gate/register transition invalidates bytes
    // already sampled under the old meaning.
    void begin_logical_epoch();
    void disarm(bool disconnected) noexcept;

    [[nodiscard]] std::optional<NativeHidCadenceSample> collect_latest_due(
        std::uint64_t now_ticks);
    [[nodiscard]] NativeHidCadenceSample collect_published_due(
        const NativeHidPublishedBatch& batch,
        std::uint64_t production_ticks);

    void note_queue_replacement();
    void note_mode_invalidation() noexcept;
    void note_reset_queue_discard();
    void note_delivery(
        const NativeHidCadenceSample& sample,
        std::uint64_t delivery_ticks);

    [[nodiscard]] const NativeHidCadenceStats& stats() const noexcept {
        return stats_;
    }

    // Exact first device slot that has not yet been collected. The deadline
    // broker is rephased from this value, never from a guessed origin/period
    // relationship, so an origin sample collected inline cannot be published
    // a second time by the broker.
    [[nodiscard]] std::optional<std::uint64_t>
    next_unconsumed_deadline_ticks() const noexcept {
        return stats_.armed
            ? std::optional<std::uint64_t>{next_deadline_ticks_}
            : std::nullopt;
    }
    [[nodiscard]] std::optional<std::uint64_t>
    next_unconsumed_sequence() const noexcept {
        return stats_.armed
            ? std::optional<std::uint64_t>{next_sequence_}
            : std::nullopt;
    }

private:
    [[noreturn]] void fail_strict(const char* message);

    NativeHidCadenceStats stats_{};
    NativeHidTimingProfile profile_{};
    std::uint64_t epoch_{};
    std::uint64_t epoch_origin_ticks_{};
    std::uint64_t epoch_first_sequence_{};
    std::uint64_t next_sequence_{1u};
    std::uint64_t next_deadline_ticks_{};
    std::uint64_t last_delivery_slot_{};
    bool delivered_in_epoch_{};
};

[[nodiscard]] float normalize_xinput_axis(
    std::int16_t raw,
    std::int16_t deadzone) noexcept;

[[nodiscard]] std::uint8_t native_stick_axis_byte(
    float axis,
    std::uint8_t negative,
    std::uint8_t center,
    std::uint8_t positive) noexcept;

[[nodiscard]] std::uint8_t native_stick_axis_byte(
    float axis,
    std::uint8_t negative,
    std::uint8_t positive) noexcept;

enum class NativePointerAxisSource {
    None,
    KeyboardMouse,
    Controller,
};

struct NativePointerAxes {
    float x = 0.0f;
    float y = 0.0f;
    bool moving = false;
    bool hold_visible = false;
    NativePointerAxisSource source = NativePointerAxisSource::None;
};

// The Windows client and the Wii presentation viewport are deliberately
// separate. A resizable native window may contain letterbox/pillarbox space;
// those pixels are not valid sensor-bar coordinates.
using NativePointerViewport = galaxy::ContentViewport;

struct NativeMousePointerSample {
    // Diagnostic/button-ownership metadata only. Pointer projection is based
    // on the global cursor's geometry inside the game content viewport and
    // intentionally continues while the window is unfocused.
    bool window_focused = false;
    bool absolute_valid = false;
    bool inside_client = false;
    int client_x = 0;
    int client_y = 0;
    int client_width = 0;
    int client_height = 0;
    NativePointerViewport viewport{};
    float x_scale = 1.0f;
    float y_scale = 1.0f;
};

struct NativeMousePointerProjection {
    float x = 0.0f;
    float y = 0.0f;
    bool visible = false;
};

inline constexpr double kNativeWiiPointerContentAspect =
    galaxy::kWiiContentAspect;

// Fit the logical Wii presentation into the current client area. This is a
// pure seam so recorded/replayed host coordinates and production Win32 input
// use byte-for-byte identical viewport rules.
[[nodiscard]] inline NativePointerViewport fit_native_pointer_viewport(
    int client_width,
    int client_height,
    double content_aspect = kNativeWiiPointerContentAspect) noexcept {
    return galaxy::fit_content_viewport(
        client_width, client_height, content_aspect);
}

// Project one coherent client-area acquisition to native [-1,+1] pointer
// coordinates. Known outside-client or outside-viewport samples are invalid;
// transient acquisition-loss holding remains a caller-owned timeline policy.
//
// This generalizes the direct client-dimension normalization used by the CC0
// Dusklight menu pointer (src/dusk/menu_pointer.cpp) with explicit viewport,
// validity, and edge-gain contracts for Nebula's raw Wii HID producer.
[[nodiscard]] NativeMousePointerProjection project_native_mouse_pointer(
    const NativeMousePointerSample& sample) noexcept;

// Take one coherent level snapshot of a fixed virtual-key set. Disabling the
// keyboard acquisition gate must not touch the host provider; hidden or
// unfocused native routes can therefore keep scripted/replay key input
// deterministic. Pointer geometry is acquired separately and may track while
// unfocused.
template <std::size_t KeyCount, typename KeyDown>
[[nodiscard]] std::array<bool, KeyCount> collect_native_key_levels(
    bool collect,
    const std::array<int, KeyCount>& virtual_keys,
    KeyDown&& key_down) {
    std::array<bool, KeyCount> levels{};
    if (!collect) {
        return levels;
    }
    for (std::size_t index = 0; index < virtual_keys.size(); ++index) {
        levels[index] = key_down(virtual_keys[index]);
    }
    return levels;
}

template <std::size_t KeyCount>
[[nodiscard]] constexpr bool native_key_level(
    const std::array<int, KeyCount>& virtual_keys,
    const std::array<bool, KeyCount>& levels,
    int virtual_key) noexcept {
    for (std::size_t index = 0; index < virtual_keys.size(); ++index) {
        if (virtual_keys[index] == virtual_key) {
            return levels[index];
        }
    }
    return false;
}

// Build the diagnostic-only raw-key mask without touching the provider when
// no trace record will be emitted. Bits 2..17 retain their established wire
// order; the provider is queried exactly once per key, in array order.
template <typename KeyDown>
[[nodiscard]] std::uint32_t collect_native_raw_key_trace_mask(
    bool collect,
    const std::array<int, 16>& virtual_keys,
    KeyDown&& key_down) {
    if (!collect) {
        return 0u;
    }

    std::uint32_t raw_key_mask = 0u;
    for (std::size_t index = 0; index < virtual_keys.size(); ++index) {
        if (key_down(virtual_keys[index])) {
            raw_key_mask |= 1u << (index + 2u);
        }
    }
    return raw_key_mask;
}

[[nodiscard]] NativePointerAxes merge_native_pointer_axes(
    float keyboard_x,
    float keyboard_y,
    float controller_x,
    float controller_y,
    bool controller_connected,
    bool controller_mode) noexcept;

void publish_wiimote_ir_dots(
    WiimoteInputSnapshot& snapshot,
    float pointer_x,
    float pointer_y,
    bool active) noexcept;

[[nodiscard]] bool advance_wiimote_shake_accel(
    WiimoteInputSnapshot& snapshot,
    bool pressed,
    bool& previous_pressed,
    std::uint8_t& remaining_frames) noexcept;

enum class WiimoteReportBuildStatus {
    Ok,
    UnsupportedReportMode,
    OutputTooSmall,
};

struct WiimoteReportBuildResult {
    WiimoteReportBuildStatus status = WiimoteReportBuildStatus::Ok;
    std::size_t size = 0;
};

[[nodiscard]] std::size_t wiimote_input_report_size(
    std::uint8_t report_mode) noexcept;

[[nodiscard]] WiimoteReportBuildResult build_wiimote_input_report(
    std::uint8_t report_mode,
    const WiimoteInputSnapshot& snapshot,
    std::span<std::uint8_t> output) noexcept;

// Applies RMGE01's active Nunchuk extension-wire cipher to a complete Wii
// Remote input report. The caller supplies the two eight-byte tables read
// from the current guest WPAD channel; retail derives those tables during each
// extension handshake, so they must never be replaced by a process-global
// default. The core, accelerometer, and IR portions remain untouched. Returns
// false unless `report` is a complete supported report mode carrying Nunchuk
// extension bytes.
[[nodiscard]] bool encrypt_rmge01_nunchuk_extension_payload(
    std::span<std::uint8_t> report,
    std::span<const std::uint8_t, 8> additive_key,
    std::span<const std::uint8_t, 8> xor_key) noexcept;

[[nodiscard]] WiimoteReportBuildResult build_wiimote_status_report(
    const WiimoteDeviceStatus& status,
    std::span<std::uint8_t> output) noexcept;

[[nodiscard]] WiimoteReportBuildResult build_wiimote_ack_report(
    const WiimoteButtons& buttons,
    std::uint8_t report_id,
    std::uint8_t error,
    std::span<std::uint8_t> output) noexcept;

[[nodiscard]] WiimoteReportBuildResult build_wiimote_read_memory_report(
    const WiimoteButtons& buttons,
    std::uint16_t offset,
    std::uint8_t error,
    std::span<const std::uint8_t> data,
    std::span<std::uint8_t> output) noexcept;

[[nodiscard]] WiimoteReportBuildResult normalize_wiimote_hid_input_report(
    std::span<const std::uint8_t> raw,
    std::span<std::uint8_t> output) noexcept;

[[nodiscard]] WiimoteReportBuildResult build_wiimote_hid_output_report(
    std::span<const std::uint8_t> bluetooth_output_report,
    std::span<std::uint8_t> output) noexcept;

}  // namespace galaxy::input
