#pragma once

// Nintendo Switch Pro Controller (HID, USB + Bluetooth) and Switch 2 Pro
// Controller (USB) report decoding, calibration parsing, motion helpers.
// Protocol facts follow dekuNukem/Nintendo_Switch_Reverse_Engineering and
// SDL's zlib-licensed SDL_hidapi_switch.c / SDL_hidapi_switch2.c (see
// docs/KEYBOARD_CONTROLS.md); this file is an independent implementation.

#include "galaxy/input_bindings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace galaxy::input::switch_hid {

inline constexpr std::uint16_t kNintendoVendorId = 0x057e;
inline constexpr std::uint16_t kProControllerProductId = 0x2009;
inline constexpr std::uint16_t kPro2ControllerProductId = 0x2069;

// Input report ids (Switch Pro Controller).
inline constexpr std::uint8_t kReportSubcommandReply = 0x21;
inline constexpr std::uint8_t kReportFullState = 0x30;
inline constexpr std::uint8_t kReportSimpleState = 0x3f;
inline constexpr std::uint8_t kReportUsbAck = 0x81;
// Output report ids.
inline constexpr std::uint8_t kOutputSubcommand = 0x01;
inline constexpr std::uint8_t kOutputUsbCommand = 0x80;
// Subcommands.
inline constexpr std::uint8_t kSubcommandSetReportMode = 0x03;
inline constexpr std::uint8_t kSubcommandSpiRead = 0x10;
inline constexpr std::uint8_t kSubcommandPlayerLights = 0x30;
inline constexpr std::uint8_t kSubcommandEnableImu = 0x40;
inline constexpr std::uint8_t kSubcommandEnableVibration = 0x48;
// USB (0x80) commands.
inline constexpr std::uint8_t kUsbHandshake = 0x02;
inline constexpr std::uint8_t kUsbHighSpeed = 0x03;
inline constexpr std::uint8_t kUsbForceHid = 0x04;
// SPI flash addresses.
inline constexpr std::uint32_t kSpiFactoryStickCal = 0x603d;  // 18 bytes: left 9, right 9
inline constexpr std::uint32_t kSpiUserStickCal = 0x8010;     // 22 bytes: magic+9 per stick
inline constexpr std::uint32_t kSpiFactoryImuCal = 0x6020;    // 24 bytes
inline constexpr std::uint32_t kSpiUserImuCal = 0x8026;       // 26 bytes: magic + 24

struct StickAxisCal {
    int center = 2048;
    int below = 1400;  // raw counts from center to full negative deflection
    int above = 1400;  // raw counts from center to full positive deflection
};
struct StickCal {
    StickAxisCal x{};
    StickAxisCal y{};
};

// Switch Pro factory/user 9-byte blocks. The two sticks pack their fields in
// different orders.
[[nodiscard]] StickCal parse_left_stick_cal(std::span<const std::uint8_t, 9> data) noexcept;
[[nodiscard]] StickCal parse_right_stick_cal(std::span<const std::uint8_t, 9> data) noexcept;
// Switch 2 flash blocks: neutral, max, min (12-bit packed pairs).
[[nodiscard]] StickCal parse_switch2_stick_cal(std::span<const std::uint8_t, 9> data) noexcept;
// -1..1, up/right positive for raw values that grow up/right.
[[nodiscard]] float normalize_stick(int raw, const StickAxisCal& cal) noexcept;

// IMU sample in the SDL sensor frame: +X right, +Y up, +Z toward the player.
// Acceleration in g, rotation rate in rad/s (pitch = gyro[0], yaw = gyro[1]).
struct ImuSample {
    std::array<float, 3> accel{};
    std::array<float, 3> gyro{};
};

struct ImuCal {
    std::array<float, 3> accel_scale{1.0f / 4096.0f, 1.0f / 4096.0f, 1.0f / 4096.0f};  // g per count
    std::array<float, 3> gyro_scale{};   // rad/s per count
    std::array<std::int16_t, 3> gyro_offset{};
    ImuCal() noexcept;
};
// 24 factory bytes (accel origin, accel sensitivity, gyro origin, gyro
// sensitivity; little-endian int16 x/y/z each). Degenerate values keep the
// defaults (+-8 g, +-2000 dps).
[[nodiscard]] ImuCal parse_imu_cal(std::span<const std::uint8_t, 24> data) noexcept;

struct DecodedState {
    std::uint32_t buttons = 0;  // unified pad bits (input_bindings.h)
    std::array<int, 4> stick_raw{};  // LX, LY, RX, RY raw 12-bit (simple: 16-bit)
    bool simple = false;             // 0x3F report: 16-bit sticks, Y grows down
    bool left_trigger = false;
    bool right_trigger = false;
    // PlayStation and Steam decoders: `axes` (PadAxis order, sticks up positive,
    // triggers 0..1, no dead zone) replace stick_raw and the trigger flags.
    bool normalized = false;
    std::array<float, kPadAxisCount> axes{};
    std::size_t imu_count = 0;
    std::array<ImuSample, 3> imu{};
    // Seconds from the previous IMU sample to imu[0] by the controller's own
    // clock (0: unknown, the host clock applies). One sample per report.
    float imu_dt = 0.0f;
};

// Switch Pro Controller input report (byte 0 = report id). Full state (0x30)
// and subcommand replies (0x21) carry buttons and sticks; 0x30 also three IMU
// samples. 0x3F is the Bluetooth default before the mode is set. Returns
// false for other ids or short reports.
[[nodiscard]] bool decode_pro_report(std::span<const std::uint8_t> report, const ImuCal& imu_cal,
                                     DecodedState& out) noexcept;

// Converts decoded raw sticks into a PadSample with calibration and the
// scheme deadzones (per axis, like XInput).
[[nodiscard]] PadSample pad_sample_from(const DecodedState& state, ControlScheme scheme,
                                        const StickCal& left, const StickCal& right,
                                        float stick_deadzone, float pointer_deadzone) noexcept;

// Subcommand output report (0x01): packet number, neutral rumble, subcommand
// id and data. Returns the number of meaningful bytes (pad to the device's
// output report length).
std::size_t build_subcommand(std::uint8_t packet_number, std::uint8_t subcommand,
                             std::span<const std::uint8_t> data, std::span<std::uint8_t> out) noexcept;
std::size_t build_spi_read(std::uint8_t packet_number, std::uint32_t address, std::uint8_t length,
                           std::span<std::uint8_t> out) noexcept;
std::size_t build_usb_command(std::uint8_t command, std::span<std::uint8_t> out) noexcept;

// A 0x21 reply to `subcommand` (ack bit set). For SPI reads also checks the
// echoed address and returns the data bytes.
struct SubcommandReply {
    bool matched = false;
    std::span<const std::uint8_t> data{};
};
[[nodiscard]] SubcommandReply match_subcommand_reply(std::span<const std::uint8_t> report,
                                                     std::uint8_t subcommand,
                                                     std::uint32_t spi_address = 0) noexcept;

// Switch 2 Pro Controller (USB)
// The HID interface only streams input reports after the controller receives the init
// sequence on its vendor bulk interface (interface 1).
inline constexpr std::uint8_t kSwitch2InputReport = 0x05;
inline constexpr std::size_t kSwitch2ReportBytes = 64;

// Commands are an 8-byte header {command, 0x91, arg1 LE16, length LE16, 0, 0}
// followed by `length` payload bytes.
struct Switch2Command {
    std::array<std::uint8_t, 32> bytes{};
    std::size_t size = 0;
};
// The init sequence SDL sends (enable features incl. IMU, report format 5,
// start output), followed by the player-1 LED command.
[[nodiscard]] std::span<const Switch2Command> switch2_init_sequence() noexcept;
[[nodiscard]] Switch2Command switch2_flash_read(std::uint32_t address) noexcept;
[[nodiscard]] Switch2Command switch2_player_led(std::uint8_t pattern) noexcept;

struct Switch2ImuState {
    std::array<float, 3> gyro_bias{};  // rad/s (flash 0x13040)
    std::uint32_t first_timestamp = 0;
    int sample_count = 0;
    bool ready = false;
    float gyro_full_scale = 34.8f;  // rad/s at int16 max; SDL saw 40.0 on some units
};

// Decodes a report-id-5 packet. IMU fills `out.imu[0]` once the timestamp
// rate has been identified (100 samples).
[[nodiscard]] bool decode_switch2_report(std::span<const std::uint8_t> report, Switch2ImuState& imu,
                                         DecodedState& out) noexcept;

// Motion

// One controller's motion state, fed one IMU sample at a time (Pro: three
// samples 5 ms apart per report; Pro Controller 2: one per report).
//
// Gravity: the accelerometer direction, carried through rotations by the gyro
// and pulled toward the measured direction while |a| is near 1 g
// (complementary filter, 0.5 s).
//
// Gyro bias: starts at the controller's stored calibration origin (already
// removed by the decoder) and is refined from every 0.5 s window in which the
// controller rests (gyro standard deviation under kRestGyroSd and |a| steady).
//
// Pointer: rotation about the world vertical (yaw) moves it sideways and
// rotation about the horizontal right axis (pitch) moves it up and down, so the
// controller points the same way held flat or upright (JoyShockMapper's
// "world space"). A picture width is 40 degrees of yaw at sensitivity 1;
// vertical gain keeps equal pixels per degree. Rates under kGyroNoiseGate are
// scaled down toward zero so resting noise does not drift the pointer.
//
// Shake: the linear acceleration |a - gravity| averaged over two samples
// crossing the sensitivity threshold, or a wrist flick faster than the gyro
// threshold, starts a shake; it is held kShakeHoldUs and another cannot start
// for kShakeCooldownUs.
struct MotionSettings {
    bool shake = true;
    std::uint8_t shake_sensitivity = 3;  // 1 (hard) .. 5 (light)
    bool gyro_pointer = true;
    float gyro_sensitivity = 1.0f;       // multiplier on 40 degrees per picture width
    float picture_aspect = 16.0f / 9.0f;
};

class SwitchMotion {
public:
    static constexpr std::uint64_t kShakeHoldUs = 60'000;
    static constexpr std::uint64_t kShakeCooldownUs = 280'000;
    static constexpr float kDegreesPerPictureWidth = 40.0f;
    static constexpr float kRestGyroSd = 0.0175f;    // rad/s (1 deg/s)
    static constexpr float kGyroNoiseGate = 0.026f;  // rad/s (1.5 deg/s)
    static constexpr float kRestWindowSeconds = 0.5f;

    struct Output {
        float pointer_x = 0.0f;
        float pointer_y = 0.0f;  // up positive
        bool shake_started = false;
    };
    Output feed(const ImuSample& sample, float dt_seconds, std::uint64_t time_us,
                const MotionSettings& settings) noexcept;
    [[nodiscard]] bool shake_pressed(std::uint64_t now_us) const noexcept {
        return shake_active_ && now_us < shake_hold_until_us_;
    }
    [[nodiscard]] const std::array<float, 3>& gravity() const noexcept { return gravity_; }
    [[nodiscard]] bool gravity_ready() const noexcept { return gravity_ready_; }
    [[nodiscard]] const std::array<float, 3>& gyro_bias() const noexcept { return bias_; }
    [[nodiscard]] unsigned rest_windows() const noexcept { return rest_windows_; }
    [[nodiscard]] static float shake_accel_threshold_g(std::uint8_t sensitivity) noexcept;
    [[nodiscard]] static float shake_gyro_threshold(std::uint8_t sensitivity) noexcept;  // rad/s

private:
    std::array<float, 3> gravity_{0.0f, 1.0f, 0.0f};
    bool gravity_ready_ = false;
    std::array<float, 3> bias_{};
    // Rest window accumulators.
    std::array<double, 3> sum_{};
    std::array<double, 3> sum_sq_{};
    float accel_min_ = 1e9f;
    float accel_max_ = -1e9f;
    float window_time_ = 0.0f;
    unsigned window_samples_ = 0;
    unsigned rest_windows_ = 0;
    // Shake.
    float previous_linear_ = 0.0f;
    float previous_rate_ = 0.0f;
    std::uint64_t shake_hold_until_us_ = 0;
    std::uint64_t shake_cooldown_until_us_ = 0;
    bool shake_active_ = false;
};

}  // namespace galaxy::input::switch_hid
