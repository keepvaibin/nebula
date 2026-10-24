#include "galaxy/wiimote_hid.h"

#include <algorithm>
#include <initializer_list>

namespace galaxy::input::wiimote {

namespace {

std::size_t put(std::span<std::uint8_t> out, std::initializer_list<std::uint8_t> bytes) noexcept {
    if (out.size() < bytes.size()) return 0;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    std::copy(bytes.begin(), bytes.end(), out.begin());
    return bytes.size();
}

std::uint8_t motor(bool rumble) noexcept {
    return rumble ? 0x01u : 0x00u;
}

}  // namespace

std::size_t build_rumble(bool rumble, std::span<std::uint8_t> out) noexcept {
    return put(out, {kOutRumble, motor(rumble)});
}

std::size_t build_leds(std::uint8_t leds, bool rumble, std::span<std::uint8_t> out) noexcept {
    return put(out, {kOutLeds, static_cast<std::uint8_t>(((leds & 0x0fu) << 4) | motor(rumble))});
}

std::size_t build_report_mode(std::uint8_t report, bool continuous, bool rumble, std::span<std::uint8_t> out) noexcept {
    return put(out, {kOutReportMode, static_cast<std::uint8_t>((continuous ? 0x04u : 0x00u) | motor(rumble)), report});
}

std::size_t build_enable(std::uint8_t output, bool on, bool rumble, std::span<std::uint8_t> out) noexcept {
    return put(out, {output, static_cast<std::uint8_t>((on ? 0x04u : 0x00u) | motor(rumble))});
}

std::size_t build_status_request(bool rumble, std::span<std::uint8_t> out) noexcept {
    return put(out, {kOutStatusRequest, motor(rumble)});
}

std::size_t build_write(std::uint8_t space, std::uint32_t address, std::span<const std::uint8_t> data, bool rumble,
                        std::span<std::uint8_t> out) noexcept {
    if (data.empty() || data.size() > 16u || out.size() < kOutputBytes) return 0;
    const std::size_t used = put(out, {kOutWrite, static_cast<std::uint8_t>(space | motor(rumble)),
                                       static_cast<std::uint8_t>(address >> 16), static_cast<std::uint8_t>(address >> 8),
                                       static_cast<std::uint8_t>(address), static_cast<std::uint8_t>(data.size())});
    std::copy(data.begin(), data.end(), out.begin() + static_cast<std::ptrdiff_t>(used));
    return kOutputBytes;
}

std::size_t build_read(std::uint8_t space, std::uint32_t address, std::uint16_t size, bool rumble,
                       std::span<std::uint8_t> out) noexcept {
    return put(out, {kOutRead, static_cast<std::uint8_t>(space | motor(rumble)), static_cast<std::uint8_t>(address >> 16),
                     static_cast<std::uint8_t>(address >> 8), static_cast<std::uint8_t>(address),
                     static_cast<std::uint8_t>(size >> 8), static_cast<std::uint8_t>(size)});
}

std::uint8_t ir_mode_for_report(std::uint8_t report) noexcept {
    switch (report) {
    case 0x33: return 3;
    case 0x36:
    case 0x37: return 1;
    case 0x3e:
    case 0x3f: return 5;
    default: return 0;
    }
}

std::array<RegisterWrite, 5> ir_setup(std::uint8_t mode) noexcept {
    return {{{0xb00030, {0x08}, 1},
             {0xb00000, {0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xaa, 0x00, 0x64}, 9},
             {0xb0001a, {0x63, 0x03}, 2},
             {0xb00033, {mode}, 1},
             {0xb00030, {0x08}, 1}}};
}

Status parse_status(std::span<const std::uint8_t> report) noexcept {
    Status status;
    if (report.size() < 7u || report[0] != kStatus) return status;
    status.valid = true;
    status.extension = (report[3] & 0x02u) != 0u;
    status.ir = (report[3] & 0x08u) != 0u;
    status.battery = report[6];
    return status;
}

ReadReply parse_read_reply(std::span<const std::uint8_t> report) noexcept {
    ReadReply reply;
    if (report.size() < 22u || report[0] != kReadReply) return reply;
    reply.valid = true;
    reply.error = report[3] & 0x0fu;
    reply.address = static_cast<std::uint16_t>((report[4] << 8) | report[5]);
    reply.data = report.subspan(6, static_cast<std::size_t>((report[3] >> 4) + 1u));
    return reply;
}

Ack parse_ack(std::span<const std::uint8_t> report) noexcept {
    Ack ack;
    if (report.size() < 5u || report[0] != kAck) return ack;
    ack.valid = true;
    ack.report = report[3];
    ack.error = report[4];
    return ack;
}

std::uint8_t battery_level(std::uint8_t battery) noexcept {
    return battery > 0x54u ? 4u : battery > 0x43u ? 3u : battery > 0x32u ? 2u : battery > 0x02u ? 1u : 0u;
}

bool is_nunchuk(std::span<const std::uint8_t> id) noexcept {
    return id.size() >= 6u && id[2] == 0xa4u && id[3] == 0x20u && id[4] == 0x00u && id[5] == 0x00u;
}

}  // namespace galaxy::input::wiimote

namespace galaxy::host {

void RealWiimoteHandoff::connected(const RealWiimoteConnection& connection) {
    {
        const std::lock_guard<std::mutex> lock(lock_);
        connection_ = connection;
        connected_ = true;
        ++serial_;
        connected_flag_.store(true, std::memory_order_release);
    }
    wake_.notify_one();
}

void RealWiimoteHandoff::disconnected() {
    {
        const std::lock_guard<std::mutex> lock(lock_);
        connected_ = false;
        ++serial_;
        connected_flag_.store(false, std::memory_order_release);
    }
    wake_.notify_one();
}

void RealWiimoteHandoff::battery(std::uint8_t level) {
    {
        const std::lock_guard<std::mutex> lock(lock_);
        connection_.battery = level;
        ++battery_serial_;
    }
    wake_.notify_one();
}

void RealWiimoteHandoff::report(std::span<const std::uint8_t> bytes) {
    {
        const std::lock_guard<std::mutex> lock(lock_);
        if (!connected_) return;
        if (head_ - tail_ == ring_.size()) ++tail_;
        Report& r = ring_[head_++ % ring_.size()];
        r.size = std::min(bytes.size(), r.bytes.size());
        std::copy_n(bytes.begin(), r.size, r.bytes.begin());
        r.serial = serial_;
    }
    wake_.notify_one();
}

bool RealWiimoteHandoff::pending() {
    while (head_ != tail_ && ring_[tail_ % ring_.size()].serial != serial_) ++tail_;  // an earlier connection's
    return serial_ != seen_serial_ || battery_serial_ != seen_battery_serial_ || (connected_ && head_ != tail_);
}

RealWiimoteEvent RealWiimoteHandoff::wait(std::chrono::milliseconds timeout, std::span<std::uint8_t, 22> report,
                                          std::size_t& size, RealWiimoteConnection& connection) {
    std::unique_lock<std::mutex> lock(lock_);
    if (!pending() && timeout.count() > 0) wake_.wait_for(lock, timeout, [this] { return pending(); });
    if (serial_ != seen_serial_) {
        seen_serial_ = serial_;
        seen_battery_serial_ = battery_serial_;
        connection = connection_;
        return connected_ ? RealWiimoteEvent::Connected : RealWiimoteEvent::Disconnected;
    }
    if (battery_serial_ != seen_battery_serial_) {
        seen_battery_serial_ = battery_serial_;
        connection = connection_;
        return RealWiimoteEvent::Battery;
    }
    if (!connected_ || head_ == tail_) return RealWiimoteEvent::None;
    const Report& r = ring_[tail_++ % ring_.size()];
    size = r.size;
    std::copy_n(r.bytes.begin(), r.size, report.begin());
    return RealWiimoteEvent::Report;
}

bool real_wiimote_buttons_held(std::span<const std::uint8_t> report) noexcept {
    return report.size() >= 3u && input::wiimote::is_data_report(report[0]) && ((report[1] & 0x1fu) != 0u ||
                                                                                  (report[2] & 0x9fu) != 0u);
}

}  // namespace galaxy::host
