#pragma once

// A real Wii Remote (RVL-CNT-01 and RVL-CNT-01-TR, with or without a Nunchuk) paired with Windows. Its own input
// reports, EEPROM calibration and Nunchuk calibration go to the game's WPAD (wpad_native.c) as the remote sends them,
// as the console's Bluetooth stack hands them over; the game's report format, IR camera and motor are mirrored to the
// remote. Protocol per WiiBrew's Wiimote pages; this file is an independent implementation.

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace galaxy::input::wiimote {

inline constexpr std::uint16_t kVendorId = 0x057e;
inline constexpr std::uint16_t kProductId = 0x0306;    // RVL-CNT-01
inline constexpr std::uint16_t kProductIdTr = 0x0330;  // RVL-CNT-01-TR (Wii MotionPlus inside)

// Input reports (from the report id; Windows drops Bluetooth's 0xA1 header).
inline constexpr std::uint8_t kStatus = 0x20;
inline constexpr std::uint8_t kReadReply = 0x21;
inline constexpr std::uint8_t kAck = 0x22;
// Output reports. Every one's second byte carries the motor in its lowest bit.
inline constexpr std::uint8_t kOutRumble = 0x10;
inline constexpr std::uint8_t kOutLeds = 0x11;
inline constexpr std::uint8_t kOutReportMode = 0x12;
inline constexpr std::uint8_t kOutIrClock = 0x13;
inline constexpr std::uint8_t kOutStatusRequest = 0x15;
inline constexpr std::uint8_t kOutWrite = 0x16;
inline constexpr std::uint8_t kOutRead = 0x17;
inline constexpr std::uint8_t kOutIrLogic = 0x1a;
inline constexpr std::size_t kOutputBytes = 22;
// Address spaces of reads and writes.
inline constexpr std::uint8_t kEeprom = 0x00;
inline constexpr std::uint8_t kRegisters = 0x04;
// The remote's calibration (IR, then the accelerometer's 0 g and 1 g, each twice with checksums), as the game reads it.
inline constexpr std::uint32_t kCalibrationAddress = 0x0000;
inline constexpr std::uint16_t kCalibrationBytes = 0x2a;
// The extension: unencrypted setup, its id and its calibration (two 16-byte blocks).
inline constexpr std::uint32_t kExtensionSetup1 = 0xa400f0;
inline constexpr std::uint32_t kExtensionSetup2 = 0xa400fb;
inline constexpr std::uint32_t kExtensionId = 0xa400fa;
inline constexpr std::uint32_t kExtensionCalibration = 0xa40020;
inline constexpr std::uint16_t kExtensionCalibrationBytes = 32;

// Output reports, report id first; each returns the bytes used (0 when `out` is too small). The rest of `out` is
// zeroed, so the whole buffer can be sent at the device's output report length.
std::size_t build_rumble(bool rumble, std::span<std::uint8_t> out) noexcept;
std::size_t build_leds(std::uint8_t leds, bool rumble, std::span<std::uint8_t> out) noexcept;  // bit 0: LED 1
std::size_t build_report_mode(std::uint8_t report, bool continuous, bool rumble, std::span<std::uint8_t> out) noexcept;
// kOutIrClock or kOutIrLogic.
std::size_t build_enable(std::uint8_t output, bool on, bool rumble, std::span<std::uint8_t> out) noexcept;
std::size_t build_status_request(bool rumble, std::span<std::uint8_t> out) noexcept;
std::size_t build_write(std::uint8_t space, std::uint32_t address, std::span<const std::uint8_t> data, bool rumble,
                        std::span<std::uint8_t> out) noexcept;  // at most 16 bytes
std::size_t build_read(std::uint8_t space, std::uint32_t address, std::uint16_t size, bool rumble,
                       std::span<std::uint8_t> out) noexcept;

// The IR camera's data for a report: 1 basic (0x36, 0x37), 3 extended (0x33), 5 full (0x3e, 0x3f), 0 none.
[[nodiscard]] std::uint8_t ir_mode_for_report(std::uint8_t report) noexcept;
// The camera's register writes after its two enables, in order: WiiBrew's sequence with the Wii's sensitivity level 3.
struct RegisterWrite {
    std::uint32_t address = 0;
    std::array<std::uint8_t, 9> data{};
    std::uint8_t size = 0;
};
[[nodiscard]] std::array<RegisterWrite, 5> ir_setup(std::uint8_t mode) noexcept;

[[nodiscard]] constexpr bool is_data_report(std::uint8_t id) noexcept {
    return id >= 0x30 && id <= 0x3f;
}
struct Status {
    bool valid = false;
    bool extension = false;
    bool ir = false;
    std::uint8_t battery = 0;
};
[[nodiscard]] Status parse_status(std::span<const std::uint8_t> report) noexcept;
struct ReadReply {
    bool valid = false;
    std::uint8_t error = 0;     // 0 read; 7 or 8 nothing there
    std::uint16_t address = 0;  // the low 16 bits of the first byte's address
    std::span<const std::uint8_t> data;
};
[[nodiscard]] ReadReply parse_read_reply(std::span<const std::uint8_t> report) noexcept;
struct Ack {
    bool valid = false;
    std::uint8_t report = 0;  // the output report acknowledged
    std::uint8_t error = 0;
};
[[nodiscard]] Ack parse_ack(std::span<const std::uint8_t> report) noexcept;
// A Nunchuk's 6 id bytes end a4 20 00 00.
[[nodiscard]] bool is_nunchuk(std::span<const std::uint8_t> id) noexcept;
// The status report's battery byte (0xc8 full) as WPAD's level, 0 (empty) to 4, by the SDK's thresholds.
[[nodiscard]] std::uint8_t battery_level(std::uint8_t battery) noexcept;

}  // namespace galaxy::input::wiimote

namespace galaxy::host {

// What the game's WPAD takes at a remote's connection (NebulaWiimoteConnection).
struct RealWiimoteConnection {
    std::array<std::uint8_t, input::wiimote::kCalibrationBytes> eeprom{};
    std::array<std::uint8_t, input::wiimote::kExtensionCalibrationBytes> extension_calibration{};
    bool nunchuk = false;
    std::uint8_t battery = 0;  // WPAD's level (battery_level)
};

enum class RealWiimoteEvent : std::uint8_t { None, Report, Connected, Disconnected, Battery };

// The remote's state and data reports from the worker to the host's WPAD thread. `serial` counts state changes; the
// host is told the newest state it has not seen (a quick connect and disconnect can never leave it on a remote that
// is gone). Data reports pass only while connected, and only those of the current connection, oldest first; a host
// a whole ring behind loses the oldest.
class RealWiimoteHandoff {
public:
    void connected(const RealWiimoteConnection& connection);
    void disconnected();
    void battery(std::uint8_t level);
    void report(std::span<const std::uint8_t> bytes);
    [[nodiscard]] RealWiimoteEvent wait(std::chrono::milliseconds timeout, std::span<std::uint8_t, 22> report,
                                        std::size_t& size, RealWiimoteConnection& connection);
    // The host plays with the remote (its reports reach the game), or with the keyboard, mouse and controllers.
    void set_active(bool active) noexcept {
        active_.store(active, std::memory_order_release);
    }
    [[nodiscard]] bool in_use() const noexcept {
        return connected_flag_.load(std::memory_order_acquire) && active_.load(std::memory_order_acquire);
    }

private:
    struct Report {
        std::array<std::uint8_t, 22> bytes{};
        std::size_t size = 0;
        std::uint64_t serial = 0;
    };
    bool pending();  // under lock_
    std::mutex lock_;
    std::condition_variable wake_;
    std::array<Report, 32> ring_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::uint64_t serial_ = 0;
    std::uint64_t seen_serial_ = 0;
    std::uint64_t battery_serial_ = 0;
    std::uint64_t seen_battery_serial_ = 0;
    bool connected_ = false;
    RealWiimoteConnection connection_{};
    std::atomic<bool> connected_flag_{false};
    std::atomic<bool> active_{false};
};

// Starts the worker on first use. It reads a remote while the input mode is Auto or Wii Remote, never in hidden test
// runs (a test never takes the player's remote).
void ensure_real_wiimote_started() noexcept;
// The host's WPAD thread plays with the remote: in Auto mode from its first button press until another input device
// is used; in Wii Remote mode while it is connected.
void real_wiimote_set_active(bool active) noexcept;
// What the game asks of the remote: its data report (0x30, 0x31, 0x33, 0x37), the IR camera and the motor.
void real_wiimote_set_report(std::uint8_t report) noexcept;
void real_wiimote_set_ir(bool on) noexcept;
void real_wiimote_set_rumble(bool on) noexcept;
// The next event, waiting up to `timeout_ms`: the remote's state when it changed since the last call (connected, with
// `connection` filled; disconnected; or its battery level in `connection`), else its next data report from its id (at
// most `report`'s size, `size` its length), in the order the remote sent them, only while it is connected.
[[nodiscard]] RealWiimoteEvent real_wiimote_wait(std::uint32_t timeout_ms, std::span<std::uint8_t, 22> report,
                                                 std::size_t& size, RealWiimoteConnection& connection) noexcept;
// A remote is connected and in use (its reports reach the game).
[[nodiscard]] bool real_wiimote_in_use() noexcept;
// A data report's core buttons are pressed (from its id).
[[nodiscard]] bool real_wiimote_buttons_held(std::span<const std::uint8_t> report) noexcept;

}  // namespace galaxy::host
