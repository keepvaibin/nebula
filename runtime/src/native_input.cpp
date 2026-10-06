#include "galaxy/native_input.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace galaxy::input {

bool parse_native_input_script_u64(
    std::string_view text,
    std::uint64_t& value) noexcept {
    if (text.empty()) {
        return false;
    }

    std::uint64_t parsed = 0u;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const std::uint64_t digit =
            static_cast<std::uint64_t>(character - '0');
        if (parsed >
            (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) {
            return false;
        }
        parsed = parsed * 10u + digit;
    }

    value = parsed;
    return true;
}

bool resolve_native_input_script_start_vi(
    std::uint64_t anchor_vi,
    std::string_view offset_text,
    std::uint64_t& start_vi) noexcept {
    std::uint64_t offset = 0u;
    if (!parse_native_input_script_u64(offset_text, offset) ||
        offset > std::numeric_limits<std::uint64_t>::max() - anchor_vi) {
        return false;
    }
    start_vi = anchor_vi + offset;
    return true;
}

namespace {

void checked_add(
    std::uint64_t& destination,
    std::uint64_t increment,
    const char* message) {
    if (increment >
        std::numeric_limits<std::uint64_t>::max() - destination) {
        throw std::overflow_error(message);
    }
    destination += increment;
}

std::uint64_t checked_offset(
    std::uint64_t base,
    std::uint64_t count,
    std::uint64_t stride,
    const char* message) {
    if (count != 0u &&
        stride > std::numeric_limits<std::uint64_t>::max() / count) {
        throw std::overflow_error(message);
    }
    const std::uint64_t offset = count * stride;
    if (offset > std::numeric_limits<std::uint64_t>::max() - base) {
        throw std::overflow_error(message);
    }
    return base + offset;
}

std::uint16_t clamp_axis10(std::uint16_t value) noexcept {
    return std::min<std::uint16_t>(
        value, static_cast<std::uint16_t>(0x03ffu));
}

std::uint8_t high8(std::uint16_t value) noexcept {
    return static_cast<std::uint8_t>(clamp_axis10(value) >> 2);
}

void write_core_buttons(
    const WiimoteInputSnapshot& snapshot,
    bool include_accel_lsb,
    std::span<std::uint8_t> output,
    std::size_t& cursor) noexcept {
    std::uint8_t byte0 = 0;
    std::uint8_t byte1 = 0;
    const WiimoteButtons& buttons = snapshot.buttons;
    if (buttons.left) byte0 |= 0x01u;
    if (buttons.right) byte0 |= 0x02u;
    if (buttons.down) byte0 |= 0x04u;
    if (buttons.up) byte0 |= 0x08u;
    if (buttons.plus) byte0 |= 0x10u;
    if (buttons.two) byte1 |= 0x01u;
    if (buttons.one) byte1 |= 0x02u;
    if (buttons.b) byte1 |= 0x04u;
    if (buttons.a) byte1 |= 0x08u;
    if (buttons.minus) byte1 |= 0x10u;
    if (buttons.home) byte1 |= 0x80u;
    if (include_accel_lsb) {
        // A real Wii Remote carries the accelerometer's available low bits in
        // otherwise-unused core-button bits. RMGE01's raw-HID decoder masks
        // these bits out of the button word before extracting them again.
        // Y0 and Z0 are not present in the normal report formats.
        byte0 |= static_cast<std::uint8_t>(
            (clamp_axis10(snapshot.accel.x) & 0x03u) << 5u);
        byte1 |= static_cast<std::uint8_t>(
            (clamp_axis10(snapshot.accel.y) & 0x02u) << 4u);
        byte1 |= static_cast<std::uint8_t>(
            (clamp_axis10(snapshot.accel.z) & 0x02u) << 5u);
    }
    output[cursor++] = byte0;
    output[cursor++] = byte1;
}

void write_accel(
    const Axis10& accel,
    std::span<std::uint8_t> output,
    std::size_t& cursor) noexcept {
    output[cursor++] = high8(accel.x);
    output[cursor++] = high8(accel.y);
    output[cursor++] = high8(accel.z);
}

void write_ir_basic_pair(
    const IrDot& first,
    const IrDot& second,
    std::span<std::uint8_t> output,
    std::size_t& cursor) noexcept {
    std::uint8_t high = 0xffu;
    if (!first.visible) {
        output[cursor++] = 0xffu;
        output[cursor++] = 0xffu;
    } else {
        const std::uint16_t x = clamp_axis10(first.x);
        const std::uint16_t y = clamp_axis10(first.y);
        output[cursor++] = static_cast<std::uint8_t>(x);
        output[cursor++] = static_cast<std::uint8_t>(y);
    }
    if (first.visible || second.visible) {
        const std::uint16_t x1 =
            first.visible ? clamp_axis10(first.x) : 0x03ffu;
        const std::uint16_t y1 =
            first.visible ? clamp_axis10(first.y) : 0x03ffu;
        const std::uint16_t x2 =
            second.visible ? clamp_axis10(second.x) : 0x03ffu;
        const std::uint16_t y2 =
            second.visible ? clamp_axis10(second.y) : 0x03ffu;
        high = static_cast<std::uint8_t>(
            ((y1 >> 8) & 0x03u) << 6 |
            ((x1 >> 8) & 0x03u) << 4 |
            ((y2 >> 8) & 0x03u) << 2 |
            ((x2 >> 8) & 0x03u));
    }
    output[cursor++] = high;
    if (!second.visible) {
        output[cursor++] = 0xffu;
        output[cursor++] = 0xffu;
    } else {
        const std::uint16_t x = clamp_axis10(second.x);
        const std::uint16_t y = clamp_axis10(second.y);
        output[cursor++] = static_cast<std::uint8_t>(x);
        output[cursor++] = static_cast<std::uint8_t>(y);
    }
}

void write_ir_basic_10(
    const WiimoteInputSnapshot& snapshot,
    std::span<std::uint8_t> output,
    std::size_t& cursor) noexcept {
    write_ir_basic_pair(snapshot.ir[0], snapshot.ir[1], output, cursor);
    write_ir_basic_pair(snapshot.ir[2], snapshot.ir[3], output, cursor);
}

void write_ir_extended_dot(
    const IrDot& dot,
    std::span<std::uint8_t> output,
    std::size_t& cursor) noexcept {
    if (!dot.visible) {
        output[cursor++] = 0xffu;
        output[cursor++] = 0xffu;
        output[cursor++] = 0xffu;
        return;
    }
    const std::uint16_t x = clamp_axis10(dot.x);
    const std::uint16_t y = clamp_axis10(dot.y);
    output[cursor++] = static_cast<std::uint8_t>(x);
    output[cursor++] = static_cast<std::uint8_t>(y);
    output[cursor++] = static_cast<std::uint8_t>(
        ((y >> 8) & 0x03u) << 6 |
        ((x >> 8) & 0x03u) << 4 |
        (dot.size & 0x0fu));
}

void write_ir_extended_12(
    const WiimoteInputSnapshot& snapshot,
    std::span<std::uint8_t> output,
    std::size_t& cursor) noexcept {
    for (const IrDot& dot : snapshot.ir) {
        write_ir_extended_dot(dot, output, cursor);
    }
}

void write_nunchuk_extension(
    const NunchukState& nunchuk,
    std::span<std::uint8_t> output,
    std::size_t& cursor,
    std::size_t byte_count) noexcept {
    const std::uint16_t x = clamp_axis10(nunchuk.accel.x);
    const std::uint16_t y = clamp_axis10(nunchuk.accel.y);
    const std::uint16_t z = clamp_axis10(nunchuk.accel.z);
    const std::array<std::uint8_t, 6> packed{
        nunchuk.stick_x,
        nunchuk.stick_y,
        high8(x),
        high8(y),
        high8(z),
        static_cast<std::uint8_t>(
            ((z & 0x03u) << 6) |
            ((y & 0x03u) << 4) |
            ((x & 0x03u) << 2) |
            (nunchuk.c ? 0x00u : 0x02u) |
            (nunchuk.z ? 0x00u : 0x01u)),
    };
    const std::size_t to_copy = std::min(byte_count, packed.size());
    for (std::size_t i = 0; i < to_copy; ++i) {
        output[cursor++] = packed[i];
    }
    for (std::size_t i = to_copy; i < byte_count; ++i) {
        output[cursor++] = 0;
    }
}

struct NunchukExtensionPayloadLayout {
    std::size_t offset;
    std::size_t size;
};

std::optional<NunchukExtensionPayloadLayout> nunchuk_extension_payload_layout(
    std::span<const std::uint8_t> report) noexcept {
    if (report.size() < 2u || report[0] != kWiimoteHidInput) {
        return std::nullopt;
    }
    switch (report[1]) {
    case 0x32u:
        return NunchukExtensionPayloadLayout{4u, 8u};
    case 0x34u:
        return NunchukExtensionPayloadLayout{4u, 19u};
    case 0x35u:
        return NunchukExtensionPayloadLayout{7u, 16u};
    case 0x36u:
        return NunchukExtensionPayloadLayout{14u, 9u};
    case 0x37u:
        return NunchukExtensionPayloadLayout{17u, 6u};
    default:
        return std::nullopt;
    }
}

}  // namespace

bool NativeNewestOnlyHidFifo::conservation_valid() const noexcept {
    std::uint64_t unaccounted = stats_.completed_reports;
    const auto subtract = [&unaccounted](std::uint64_t count) noexcept {
        if (count > unaccounted) {
            return false;
        }
        unaccounted -= count;
        return true;
    };
    return subtract(stats_.fifo_replacements) &&
        subtract(stats_.reset_discards) &&
        subtract(stats_.delivered_reports) &&
        unaccounted == (queued_.has_value() ? 1u : 0u);
}

void NativeNewestOnlyHidFifo::require_conservation() const {
    if (!conservation_valid()) {
        throw std::logic_error(
            "newest-only native HID FIFO violated report conservation");
    }
}

std::uint64_t NativeNewestOnlyHidFifo::push(
    std::vector<std::byte> packet,
    std::uint64_t completion_ticks) {
    require_conservation();
    if (packet.empty()) {
        throw std::invalid_argument(
            "newest-only native HID FIFO rejected an empty packet");
    }
    if (stats_.completed_reports ==
        std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "newest-only native HID completion sequence overflowed");
    }
    if (queued_.has_value() &&
        stats_.fifo_replacements ==
            std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "newest-only native HID FIFO replacement counter overflowed");
    }

    const std::uint64_t sequence = stats_.completed_reports + 1u;
    NativeNewestOnlyHidPacket newest{
        std::move(packet), sequence, completion_ticks};
    ++stats_.completed_reports;
    if (queued_.has_value()) {
        ++stats_.fifo_replacements;
    }
    queued_ = std::move(newest);
    require_conservation();
    return sequence;
}

void NativeNewestOnlyHidFifo::note_delivery(std::uint64_t delivery_ticks) {
    require_conservation();
    if (!queued_.has_value()) {
        throw std::logic_error(
            "newest-only native HID FIFO delivered without a packet");
    }
    if (delivery_ticks < queued_->completion_ticks) {
        throw std::invalid_argument(
            "newest-only native HID FIFO delivery predates completion");
    }
    if (stats_.delivered_reports ==
        std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "newest-only native HID delivery counter overflowed");
    }

    ++stats_.delivered_reports;
    stats_.last_delivered_sequence = queued_->sequence;
    stats_.max_delivery_age_ticks = std::max(
        stats_.max_delivery_age_ticks,
        delivery_ticks - queued_->completion_ticks);
    queued_.reset();
    require_conservation();
}

void NativeNewestOnlyHidFifo::discard_on_reset() {
    require_conservation();
    if (!queued_.has_value()) {
        return;
    }
    if (stats_.reset_discards ==
        std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "newest-only native HID reset-discard counter overflowed");
    }
    ++stats_.reset_discards;
    queued_.reset();
    require_conservation();
}

NativeHidInputMailbox::NativeHidInputMailbox(std::size_t maximum_pending_replies)
    : maximum_pending_replies_(maximum_pending_replies) {
    if (maximum_pending_replies == 0u) {
        throw std::invalid_argument("physical HID reply mailbox cannot be empty");
    }
}

bool NativeHidInputMailbox::conservation_valid() const noexcept {
    std::uint64_t remaining = stats_.completed_reports;
    for (const auto count : {stats_.fifo_replacements, stats_.reset_discards,
                             stats_.delivered_reports}) {
        if (count > remaining) return false;
        remaining -= count;
    }
    return remaining == replies_.size() + (continuous_.has_value() ? 1u : 0u);
}

void NativeHidInputMailbox::require_conservation() const {
    if (!conservation_valid()) {
        throw std::logic_error("physical HID mailbox violated report conservation");
    }
}

std::uint64_t NativeHidInputMailbox::push(std::vector<std::byte> packet,
                                        std::uint64_t completion_ticks) {
    require_conservation();
    if (packet.empty()) throw std::invalid_argument("physical HID mailbox rejected empty packet");
    const bool replaceable = packet.size() >= 2u &&
        packet[0] == std::byte{kWiimoteHidInput} &&
        packet[1] >= std::byte{0x30u} && packet[1] <= std::byte{0x37u};
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (stats_.completed_reports == maximum ||
        (replaceable && continuous_.has_value() && stats_.fifo_replacements == maximum)) {
        throw std::overflow_error("physical HID mailbox counter overflowed");
    }
    if (!replaceable && replies_.size() >= maximum_pending_replies_) {
        throw std::overflow_error("physical HID control reply mailbox overflowed");
    }
    const auto sequence = stats_.completed_reports + 1u;
    NativeNewestOnlyHidPacket accepted{std::move(packet), sequence, completion_ticks};
    if (replaceable) {
        const bool replaced = continuous_.has_value();
        continuous_ = std::move(accepted);
        if (replaced) ++stats_.fifo_replacements;
    } else {
        // Commit counters only after the allocating insertion succeeds.
        replies_.push_back(std::move(accepted));
    }
    ++stats_.completed_reports;
    require_conservation();
    return sequence;
}

const NativeNewestOnlyHidPacket* NativeHidInputMailbox::front() const noexcept {
    if (replies_.empty()) return continuous_.has_value() ? &*continuous_ : nullptr;
    if (continuous_.has_value() && continuous_->sequence < replies_.front().sequence) {
        return &*continuous_;
    }
    return &replies_.front();
}

void NativeHidInputMailbox::note_delivery(std::uint64_t delivery_ticks) {
    require_conservation();
    const auto* selected = front();
    if (selected == nullptr) throw std::logic_error("physical HID delivery without packet");
    if (delivery_ticks < selected->completion_ticks) {
        throw std::invalid_argument("physical HID delivery predates completion");
    }
    if (stats_.delivered_reports == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("physical HID delivery counter overflowed");
    }
    ++stats_.delivered_reports;
    stats_.last_delivered_sequence = selected->sequence;
    stats_.max_delivery_age_ticks = std::max(stats_.max_delivery_age_ticks,
                                            delivery_ticks - selected->completion_ticks);
    if (selected == (continuous_.has_value() ? &*continuous_ : nullptr)) {
        continuous_.reset();
    } else {
        replies_.pop_front();
    }
    require_conservation();
}

void NativeHidInputMailbox::discard_on_reset() {
    require_conservation();
    const auto discarded = replies_.size() + (continuous_.has_value() ? 1u : 0u);
    if (discarded > std::numeric_limits<std::uint64_t>::max() - stats_.reset_discards) {
        throw std::overflow_error("physical HID reset discard counter overflowed");
    }
    stats_.reset_discards += discarded;
    replies_.clear();
    continuous_.reset();
    require_conservation();
}

struct NativeHidIoWorker::Impl {
    Impl(
        std::unique_ptr<NativeHidIoBackend> backend_value,
        std::size_t maximum_pending_output_reports_value,
        std::uint32_t input_poll_interval_ms_value,
        std::size_t maximum_pending_input_replies,
        NativeHidIoClock clock_value)
        : backend(std::move(backend_value)),
          maximum_pending_output_reports(
              maximum_pending_output_reports_value),
          input_poll_interval(input_poll_interval_ms_value),
          clock_origin(std::chrono::steady_clock::now()),
          input_fifo(maximum_pending_input_replies), clock(clock_value) {
        if (backend == nullptr) {
            throw std::invalid_argument(
                "native HID I/O worker requires a backend");
        }
        if (maximum_pending_output_reports == 0u) {
            throw std::invalid_argument(
                "native HID I/O worker output mailbox cannot be empty");
        }
        if (input_poll_interval_ms_value == 0u) {
            throw std::invalid_argument(
                "native HID I/O worker poll interval cannot be zero");
        }
    }

    [[nodiscard]] std::uint64_t now_ticks() const {
        if (clock.read_ticks != nullptr) return clock.read_ticks(clock.user);
        constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000u;
        const auto elapsed = std::chrono::steady_clock::now() - clock_origin;
        const auto elapsed_ns_signed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed)
                .count();
        if (elapsed_ns_signed < 0) {
            throw std::runtime_error(
                "native HID I/O worker monotonic clock moved backward");
        }
        const std::uint64_t elapsed_ns =
            static_cast<std::uint64_t>(elapsed_ns_signed);
        const std::uint64_t elapsed_seconds =
            elapsed_ns / nanoseconds_per_second;
        if (elapsed_seconds >
            std::numeric_limits<std::uint64_t>::max() /
                kNativeHidTimelineTicksPerSecond) {
            throw std::overflow_error(
                "native HID I/O worker clock overflowed");
        }
        return elapsed_seconds * kNativeHidTimelineTicksPerSecond +
            (elapsed_ns % nanoseconds_per_second) *
                kNativeHidTimelineTicksPerSecond /
                nanoseconds_per_second;
    }

    void record_failure(std::string reason) noexcept {
        FailureNotifier notify = nullptr;
        void* notify_user = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (failed) {
                return;
            }
            failed = true;
            stop_requested = true;
            failure_reason = std::move(reason);
            notify = failure_notifier;
            notify_user = failure_notifier_user;
        }
        backend->request_stop();
        wake.notify_all();
        if (notify != nullptr) {
            notify(notify_user);
        }
    }

    void record_exception(const char* boundary) noexcept {
        try {
            throw;
        } catch (const std::exception& error) {
            try {
                record_failure(
                    std::string(boundary) + ": " + error.what());
            } catch (...) {
                record_failure(boundary);
            }
        } catch (...) {
            record_failure(
                std::string(boundary) + ": unknown exception");
        }
    }

    [[nodiscard]] bool stopping() const noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        return stop_requested;
    }

    void output_loop() noexcept {
        try {
            for (;;) {
                std::vector<std::byte> report;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait(lock, [&] {
                        return stop_requested ||
                            !pending_output_reports.empty();
                    });
                    if (stop_requested) {
                        return;
                    }
                    report = std::move(pending_output_reports.front());
                    pending_output_reports.pop_front();
                    output_in_flight = true;
                }

                const bool delivered = backend->write_output(report);

                bool completion_counter_overflowed = false;
                bool cancelled = false;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    output_in_flight = false;
                    cancelled = !delivered && stop_requested;
                    if (delivered) {
                        completion_counter_overflowed =
                            output_reports_completed ==
                            std::numeric_limits<std::uint64_t>::max();
                        if (!completion_counter_overflowed) {
                            ++output_reports_completed;
                        }
                    }
                }
                if (cancelled) {
                    return;
                }
                if (!delivered) {
                    throw std::runtime_error(
                        "native HID output backend cancelled without a stop request");
                }
                if (completion_counter_overflowed) {
                    throw std::overflow_error(
                        "native HID output completion counter overflowed");
                }
            }
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                output_in_flight = false;
            }
            record_exception("native HID output worker failed");
        }
    }

    void input_loop() {
        constexpr std::uint32_t maximum_reports_per_poll = 256u;
        while (!stopping()) {
            std::uint32_t reports_this_poll = 0u;
            for (;;) {
                std::vector<std::byte> report;
                if (!backend->poll_input(report)) {
                    break;
                }
                if (++reports_this_poll > maximum_reports_per_poll) {
                    throw std::runtime_error(
                        "native HID input completion drain exceeded its hard bound");
                }
                const std::uint64_t completion_ticks = now_ticks();
                std::lock_guard<std::mutex> lock(mutex);
                static_cast<void>(
                    input_fifo.push(std::move(report), completion_ticks));
            }

            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock, input_poll_interval, [&] {
                return stop_requested;
            });
        }
    }

    void coordinator_loop() noexcept {
        std::thread output_thread;
        try {
            backend->open();
            {
                std::lock_guard<std::mutex> lock(mutex);
                opened = true;
            }
            wake.notify_all();

            if (!stopping()) {
                output_thread = std::thread([this] { output_loop(); });
                input_loop();
            }
        } catch (...) {
            record_exception("native HID coordinator failed");
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            stop_requested = true;
        }
        backend->request_stop();
        wake.notify_all();
        if (output_thread.joinable()) {
            output_thread.join();
        }
        backend->close();
        {
            std::lock_guard<std::mutex> lock(mutex);
            opened = false;
            stopped = true;
        }
        wake.notify_all();
    }

    std::unique_ptr<NativeHidIoBackend> backend;
    const std::size_t maximum_pending_output_reports;
    const std::chrono::milliseconds input_poll_interval;
    const std::chrono::steady_clock::time_point clock_origin;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread coordinator_thread;
    std::deque<std::vector<std::byte>> pending_output_reports;
    NativeHidInputMailbox input_fifo;
    const NativeHidIoClock clock;
    FailureNotifier failure_notifier{};
    void* failure_notifier_user{};
    std::string failure_reason;
    std::size_t maximum_observed_pending_output_reports{};
    std::uint64_t output_reports_enqueued{};
    std::uint64_t output_reports_completed{};
    bool started{};
    bool opened{};
    bool failed{};
    bool stopped{};
    bool stop_requested{};
    bool output_in_flight{};
};

NativeHidIoWorker::NativeHidIoWorker(
    std::unique_ptr<NativeHidIoBackend> backend,
    std::size_t maximum_pending_output_reports,
    std::uint32_t input_poll_interval_ms,
    std::size_t maximum_pending_input_replies,
    NativeHidIoClock clock)
    : impl_(std::make_unique<Impl>(
          std::move(backend),
          maximum_pending_output_reports,
          input_poll_interval_ms, maximum_pending_input_replies, clock)) {}

NativeHidIoWorker::~NativeHidIoWorker() {
    stop();
}

void NativeHidIoWorker::start() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->failed) {
            throw std::runtime_error(
                "native HID I/O worker already failed: " +
                impl_->failure_reason);
        }
        if (impl_->started) {
            return;
        }
        if (impl_->stopped || impl_->stop_requested) {
            throw std::logic_error(
                "native HID I/O worker cannot restart after stop");
        }
        impl_->started = true;
    }
    try {
        impl_->coordinator_thread =
            std::thread([implementation = impl_.get()] {
                implementation->coordinator_loop();
            });
    } catch (...) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->started = false;
        throw;
    }
}

void NativeHidIoWorker::stop() noexcept {
    if (impl_ == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stop_requested = true;
        if (!impl_->started) {
            impl_->stopped = true;
        }
    }
    impl_->backend->request_stop();
    impl_->wake.notify_all();
    if (impl_->coordinator_thread.joinable()) {
        impl_->coordinator_thread.join();
    }
}

void NativeHidIoWorker::set_failure_notifier(
    FailureNotifier callback,
    void* user) noexcept {
    bool notify_now = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->failure_notifier = callback;
        impl_->failure_notifier_user = user;
        notify_now = impl_->failed && callback != nullptr;
    }
    if (notify_now) {
        callback(user);
    }
}

void NativeHidIoWorker::enqueue_output(std::vector<std::byte> report) {
    if (report.empty()) {
        throw std::invalid_argument(
            "native HID output mailbox rejected an empty report");
    }

    bool overflowed = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->started) {
            throw std::logic_error(
                "native HID output was queued before worker start");
        }
        if (impl_->failed) {
            throw std::runtime_error(
                "native HID output worker failed: " +
                impl_->failure_reason);
        }
        if (impl_->stop_requested) {
            throw std::runtime_error(
                "native HID output was queued after worker stop");
        }
        if (impl_->pending_output_reports.size() >=
            impl_->maximum_pending_output_reports) {
            overflowed = true;
        } else {
            if (impl_->output_reports_enqueued ==
                std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error(
                    "native HID output enqueue counter overflowed");
            }
            impl_->pending_output_reports.push_back(std::move(report));
            ++impl_->output_reports_enqueued;
            impl_->maximum_observed_pending_output_reports = std::max(
                impl_->maximum_observed_pending_output_reports,
                impl_->pending_output_reports.size());
        }
    }
    if (overflowed) {
        impl_->record_failure("native HID output mailbox overflowed");
        throw std::runtime_error("native HID output mailbox overflowed");
    }
    impl_->wake.notify_all();
}

NativeHidIoTakeResult NativeHidIoWorker::take_latest_input(
    std::size_t maximum_report_bytes) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const NativeNewestOnlyHidPacket* newest = impl_->input_fifo.front();
    if (newest == nullptr) {
        return {};
    }
    if (newest->packet.size() > maximum_report_bytes) {
        return NativeHidIoTakeResult{NativeHidIoTakeStatus::TooLarge, {}};
    }
    NativeHidIoTakeResult result{NativeHidIoTakeStatus::Ok, *newest};
    // Sample after synchronized selection, never before a producer can install
    // a later completion. A clock regression remains an error, not clamped age.
    const std::uint64_t delivery_ticks = impl_->now_ticks();
    impl_->input_fifo.note_delivery(delivery_ticks);
    return result;
}

void NativeHidIoWorker::discard_input_on_reset() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->input_fifo.discard_on_reset();
}

bool NativeHidIoWorker::input_occupied() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->input_fifo.occupied();
}

NativeHidIoWorkerStats NativeHidIoWorker::stats() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return NativeHidIoWorkerStats{
        impl_->started,
        impl_->opened,
        impl_->failed,
        impl_->stopped,
        impl_->input_fifo.occupied(),
        impl_->input_fifo.conservation_valid(),
        impl_->output_in_flight,
        impl_->pending_output_reports.size(),
        impl_->maximum_observed_pending_output_reports,
        impl_->output_reports_enqueued,
        impl_->output_reports_completed,
        impl_->input_fifo.stats()};
}

void NativeHidIoWorker::throw_if_failed() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->failed) {
        throw std::runtime_error(
            "native HID I/O worker failed asynchronously: " +
            impl_->failure_reason);
    }
}

NativeHidReportCadence::NativeHidReportCadence(
    bool strict_proof,
    NativeHidTimingProfile profile) noexcept
    : profile_(profile) {
    stats_.strict_proof = strict_proof;
    stats_.profile_strict_proof_qualified =
        profile_.strict_proof_qualified;
    stats_.profile_id = profile_.id;
    stats_.nominal_rate_hz = profile_.nominal_rate_hz;
    stats_.period_ticks = profile_.period_ticks;
    stats_.maximum_production_lateness_ticks =
        profile_.maximum_production_lateness_ticks;
    stats_.delivery_age_limit_ticks = profile_.period_ticks == 0u
        ? 0u
        : profile_.period_ticks - 1u;
}

void dump_native_hid_cadence_audit(
    std::ostream& output,
    std::string_view tag,
    const NativeHidCadenceStats& stats,
    bool fifo_occupied) {
    output << "[input-hid-cadence-audit] tag=" << tag
           << " timeline-hz=" << kNativeHidTimelineTicksPerSecond
           << " profile-id=" << stats.profile_id
           << " profile-qualified="
           << (stats.profile_strict_proof_qualified ? 1 : 0)
           << " nominal-rate-hz=" << stats.nominal_rate_hz
           << " period-ticks=" << stats.period_ticks
           << " max-production-lateness-limit-ticks="
           << stats.maximum_production_lateness_ticks
           << " max-delivery-age-limit-ticks="
           << stats.delivery_age_limit_ticks
           << " strict=" << (stats.strict_proof ? 1 : 0)
           << " armed=" << (stats.armed ? 1 : 0)
           << " exact-cadence-valid="
           << (stats.exact_cadence_valid ? 1 : 0)
           << " epochs-started=" << stats.epochs_started
           << " disconnect-resets=" << stats.disconnect_resets
           << " scheduled-samples=" << stats.scheduled_samples
           << " produced-samples=" << stats.produced_samples
           << " skipped-due-slots=" << stats.skipped_due_slots
           << " queue-replacements=" << stats.queue_replacements
           << " mode-invalidations=" << stats.mode_invalidations
           << " reset-queue-discards=" << stats.reset_queue_discards
           << " fifo-occupied=" << (fifo_occupied ? 1 : 0)
           << " delivered-samples=" << stats.delivered_samples
           << " delivery-gaps=" << stats.delivery_gaps
           << " delivery-bursts=" << stats.delivery_bursts
           << " out-of-order-deliveries="
           << stats.out_of_order_deliveries
           << " delivery-age-samples=" << stats.delivery_age_samples
           << " delivery-age-total-ticks="
           << stats.delivery_age_total_ticks
           << " min-delivery-age-ticks="
           << stats.min_delivery_age_ticks
           << " max-delivery-age-ticks="
           << stats.max_delivery_age_ticks
           << " last-delivery-age-ticks="
           << stats.last_delivery_age_ticks
           << " delivery-timing-streams-started="
           << stats.delivery_timing_streams_started
           << " delivery-interval-samples="
           << stats.delivery_interval_samples
           << " delivery-interval-total-ticks="
           << stats.delivery_interval_total_ticks
           << " min-delivery-interval-ticks="
           << stats.min_delivery_interval_ticks
           << " max-delivery-interval-ticks="
           << stats.max_delivery_interval_ticks
           << " max-delivery-interval-jitter-ticks="
           << stats.max_delivery_interval_jitter_ticks
           << " delivery-timing-conserved="
           << (stats.delivery_timing_conservation_valid() ? 1 : 0)
           << " last-produced-epoch=" << stats.last_produced_epoch
           << " last-produced-sequence=" << stats.last_produced_sequence
           << " last-produced-deadline-ticks="
           << stats.last_produced_deadline_ticks
           << " last-production-ticks=" << stats.last_production_ticks
           << " max-production-lateness-ticks="
           << stats.max_production_lateness_ticks
           << " last-delivered-epoch=" << stats.last_delivered_epoch
           << " last-delivered-sequence=" << stats.last_delivered_sequence
           << " last-delivered-deadline-ticks="
           << stats.last_delivered_deadline_ticks
           << " last-delivery-ticks=" << stats.last_delivery_ticks << '\n';
}

void NativeHidReportCadence::arm(std::uint64_t reporting_origin_ticks) {
    if (profile_.id.empty() || profile_.nominal_rate_hz == 0u ||
        profile_.period_ticks == 0u) {
        stats_.exact_cadence_valid = false;
        throw std::invalid_argument(
            "native HID timing profile is incomplete");
    }
    if ((kNativeHidTimelineTicksPerSecond % profile_.nominal_rate_hz) != 0u ||
        (kNativeHidTimelineTicksPerSecond / profile_.nominal_rate_hz) !=
            profile_.period_ticks) {
        stats_.exact_cadence_valid = false;
        throw std::invalid_argument(
            "native HID timing profile rate/period is inconsistent with the timeline");
    }
    if (profile_.maximum_production_lateness_ticks >=
        profile_.period_ticks) {
        stats_.exact_cadence_valid = false;
        throw std::invalid_argument(
            "native HID timing profile lateness bound must be smaller than its period");
    }
    if (stats_.strict_proof && !profile_.strict_proof_qualified) {
        stats_.exact_cadence_valid = false;
        fail_strict(
            "strict native HID cadence requires a qualified timing profile");
    }
    if (epoch_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("native HID cadence epoch overflow");
    }
    ++epoch_;
    checked_add(
        stats_.epochs_started, 1u,
        "native HID cadence epoch counter overflow");
    stats_.armed = true;
    epoch_origin_ticks_ = reporting_origin_ticks;
    epoch_first_sequence_ = next_sequence_;
    next_deadline_ticks_ = reporting_origin_ticks;
    delivered_in_epoch_ = false;
    last_delivery_slot_ = 0u;
}

void NativeHidReportCadence::begin_logical_epoch() {
    if (!stats_.armed) {
        throw std::logic_error(
            "native HID logical epoch cannot begin while cadence is disarmed");
    }
    if (epoch_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("native HID cadence epoch overflow");
    }
    ++epoch_;
    checked_add(
        stats_.epochs_started, 1u,
        "native HID cadence epoch counter overflow");
    // Preserve next_deadline_ticks_: a register write changes the meaning of
    // future bytes, not the independently running profiled report clock.
    epoch_origin_ticks_ = next_deadline_ticks_;
    epoch_first_sequence_ = next_sequence_;
    delivered_in_epoch_ = false;
    last_delivery_slot_ = 0u;
}

void NativeHidReportCadence::disarm(bool disconnected) noexcept {
    stats_.armed = false;
    next_deadline_ticks_ = 0u;
    delivered_in_epoch_ = false;
    last_delivery_slot_ = 0u;
    if (disconnected &&
        stats_.disconnect_resets !=
            std::numeric_limits<std::uint64_t>::max()) {
        ++stats_.disconnect_resets;
    }
}

std::optional<NativeHidCadenceSample>
NativeHidReportCadence::collect_latest_due(std::uint64_t now_ticks) {
    if (!stats_.armed || now_ticks < next_deadline_ticks_) {
        return std::nullopt;
    }

    const std::uint64_t elapsed = now_ticks - next_deadline_ticks_;
    const std::uint64_t elapsed_periods =
        elapsed / profile_.period_ticks;
    if (elapsed_periods == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("native HID due-slot count overflow");
    }
    const std::uint64_t due_slots = elapsed_periods + 1u;
    const std::uint64_t newest_sequence = checked_offset(
        next_sequence_, due_slots - 1u, 1u,
        "native HID sample sequence overflow");
    const std::uint64_t newest_deadline = checked_offset(
        next_deadline_ticks_, due_slots - 1u,
        profile_.period_ticks,
        "native HID sample deadline overflow");
    return collect_published_due(
        NativeHidPublishedBatch{
            due_slots,
            next_sequence_,
            newest_sequence,
            next_deadline_ticks_,
            newest_deadline},
        now_ticks);
}

NativeHidCadenceSample NativeHidReportCadence::collect_published_due(
    const NativeHidPublishedBatch& batch,
    std::uint64_t production_ticks) {
    if (!stats_.armed) {
        throw std::logic_error(
            "native HID published edge reached a disarmed cadence");
    }
    if (batch.count == 0u || batch.first_sequence == 0u ||
        batch.last_sequence == 0u) {
        throw std::invalid_argument(
            "native HID published batch has incomplete identity");
    }
    const std::uint64_t expected_last_sequence = checked_offset(
        batch.first_sequence, batch.count - 1u, 1u,
        "native HID published sequence range overflow");
    const std::uint64_t expected_last_deadline = checked_offset(
        batch.first_deadline_ticks, batch.count - 1u,
        profile_.period_ticks,
        "native HID published deadline range overflow");
    if (batch.first_sequence != next_sequence_ ||
        batch.first_deadline_ticks != next_deadline_ticks_ ||
        batch.last_sequence != expected_last_sequence ||
        batch.last_deadline_ticks != expected_last_deadline) {
        throw std::runtime_error(
            "native HID broker batch does not match the next device edge");
    }
    if (production_ticks < batch.last_deadline_ticks) {
        throw std::runtime_error(
            "native HID broker batch was consumed before its last deadline"
            " production-ticks=" + std::to_string(production_ticks) +
            " first-deadline=" +
            std::to_string(batch.first_deadline_ticks) +
            " last-deadline=" +
            std::to_string(batch.last_deadline_ticks) +
            " count=" + std::to_string(batch.count) +
            " first-sequence=" + std::to_string(batch.first_sequence) +
            " last-sequence=" + std::to_string(batch.last_sequence));
    }

    const std::uint64_t following_sequence = checked_offset(
        batch.last_sequence, 1u, 1u,
        "native HID next sample sequence overflow");
    const std::uint64_t following_deadline = checked_offset(
        batch.last_deadline_ticks, 1u, profile_.period_ticks,
        "native HID next sample deadline overflow");
    checked_add(
        stats_.scheduled_samples, batch.count,
        "native HID scheduled-sample counter overflow");
    checked_add(
        stats_.produced_samples, 1u,
        "native HID produced-sample counter overflow");
    checked_add(
        stats_.skipped_due_slots, batch.count - 1u,
        "native HID skipped-slot counter overflow");
    stats_.last_produced_epoch = epoch_;
    stats_.last_produced_sequence = batch.last_sequence;
    stats_.last_produced_deadline_ticks = batch.last_deadline_ticks;
    stats_.last_production_ticks = production_ticks;
    const std::uint64_t production_lateness =
        production_ticks - batch.last_deadline_ticks;
    stats_.max_production_lateness_ticks = std::max(
        stats_.max_production_lateness_ticks,
        production_lateness);
    next_sequence_ = following_sequence;
    next_deadline_ticks_ = following_deadline;

    if (batch.count > 1u) {
        stats_.exact_cadence_valid = false;
        if (stats_.strict_proof) {
            fail_strict("native HID cadence skipped one or more profiled slots");
        }
    }
    if (production_lateness >
        profile_.maximum_production_lateness_ticks) {
        stats_.exact_cadence_valid = false;
        if (stats_.strict_proof) {
            fail_strict(
                "native HID sample exceeded its qualified production-lateness bound");
        }
    }
    return NativeHidCadenceSample{
        epoch_,
        batch.last_sequence,
        batch.last_deadline_ticks,
        batch.count};
}

void NativeHidReportCadence::note_queue_replacement() {
    checked_add(
        stats_.queue_replacements, 1u,
        "native HID queue-replacement counter overflow");
    stats_.exact_cadence_valid = false;
    if (stats_.strict_proof) {
        fail_strict("native HID device FIFO replaced an unconsumed sample");
    }
}

void NativeHidReportCadence::note_mode_invalidation() noexcept {
    if (stats_.mode_invalidations !=
        std::numeric_limits<std::uint64_t>::max()) {
        ++stats_.mode_invalidations;
    } else {
        stats_.exact_cadence_valid = false;
    }
}

void NativeHidReportCadence::note_reset_queue_discard() {
    checked_add(
        stats_.reset_queue_discards, 1u,
        "native HID reset-discard counter overflow");
    stats_.exact_cadence_valid = false;
    if (stats_.strict_proof) {
        fail_strict("native HID reset discarded an unconsumed device sample");
    }
}

void NativeHidReportCadence::note_delivery(
    const NativeHidCadenceSample& sample,
    std::uint64_t delivery_ticks) {
    if (!stats_.armed || sample.epoch != epoch_ ||
        sample.sequence < epoch_first_sequence_ ||
        sample.sequence > stats_.last_produced_sequence ||
        sample.deadline_ticks < epoch_origin_ticks_) {
        checked_add(
            stats_.out_of_order_deliveries, 1u,
            "native HID out-of-order counter overflow");
        stats_.exact_cadence_valid = false;
        throw std::runtime_error(
            "native HID delivered a stale or unknown cadence sample");
    }

    const std::uint64_t epoch_sequence_offset =
        sample.sequence - epoch_first_sequence_;
    const std::uint64_t expected_deadline = checked_offset(
        epoch_origin_ticks_, epoch_sequence_offset,
        profile_.period_ticks,
        "native HID delivery deadline overflow");
    if (sample.deadline_ticks != expected_deadline ||
        delivery_ticks < sample.deadline_ticks) {
        checked_add(
            stats_.out_of_order_deliveries, 1u,
            "native HID out-of-order counter overflow");
        stats_.exact_cadence_valid = false;
        throw std::runtime_error(
            "native HID delivered a sample outside its hardware deadline");
    }

    const std::uint64_t expected_sequence = delivered_in_epoch_
        ? checked_offset(
              stats_.last_delivered_sequence,
              1u,
              1u,
              "native HID expected delivery sequence overflow")
        : epoch_first_sequence_;
    if (sample.sequence < expected_sequence ||
        (delivered_in_epoch_ &&
         (sample.deadline_ticks <= stats_.last_delivered_deadline_ticks ||
          delivery_ticks <= stats_.last_delivery_ticks))) {
        checked_add(
            stats_.out_of_order_deliveries, 1u,
            "native HID out-of-order counter overflow");
        stats_.exact_cadence_valid = false;
        throw std::runtime_error(
            "native HID delivery sequence moved backward");
    }

    bool cadence_defect = false;
    if (sample.sequence > expected_sequence) {
        checked_add(
            stats_.delivery_gaps, sample.sequence - expected_sequence,
            "native HID delivery-gap counter overflow");
        cadence_defect = true;
    }
    const std::uint64_t delivery_slot =
        (delivery_ticks - epoch_origin_ticks_) /
        profile_.period_ticks;
    if (delivered_in_epoch_ && delivery_slot == last_delivery_slot_) {
        checked_add(
            stats_.delivery_bursts, 1u,
            "native HID delivery-burst counter overflow");
        cadence_defect = true;
    }

    const std::uint64_t delivery_age =
        delivery_ticks - sample.deadline_ticks;
    checked_add(
        stats_.delivery_age_samples,
        1u,
        "native HID delivery-age sample counter overflow");
    checked_add(
        stats_.delivery_age_total_ticks,
        delivery_age,
        "native HID delivery-age total overflow");
    if (stats_.delivery_age_samples == 1u) {
        stats_.min_delivery_age_ticks = delivery_age;
    } else {
        stats_.min_delivery_age_ticks = std::min(
            stats_.min_delivery_age_ticks, delivery_age);
    }
    stats_.max_delivery_age_ticks = std::max(
        stats_.max_delivery_age_ticks, delivery_age);
    stats_.last_delivery_age_ticks = delivery_age;

    if (delivered_in_epoch_) {
        const std::uint64_t delivery_interval =
            delivery_ticks - stats_.last_delivery_ticks;
        const std::uint64_t deadline_interval =
            sample.deadline_ticks - stats_.last_delivered_deadline_ticks;
        const std::uint64_t interval_jitter =
            delivery_interval >= deadline_interval
            ? delivery_interval - deadline_interval
            : deadline_interval - delivery_interval;
        checked_add(
            stats_.delivery_interval_samples,
            1u,
            "native HID delivery-interval sample counter overflow");
        checked_add(
            stats_.delivery_interval_total_ticks,
            delivery_interval,
            "native HID delivery-interval total overflow");
        if (stats_.delivery_interval_samples == 1u) {
            stats_.min_delivery_interval_ticks = delivery_interval;
        } else {
            stats_.min_delivery_interval_ticks = std::min(
                stats_.min_delivery_interval_ticks, delivery_interval);
        }
        stats_.max_delivery_interval_ticks = std::max(
            stats_.max_delivery_interval_ticks, delivery_interval);
        stats_.max_delivery_interval_jitter_ticks = std::max(
            stats_.max_delivery_interval_jitter_ticks, interval_jitter);
    } else {
        checked_add(
            stats_.delivery_timing_streams_started,
            1u,
            "native HID delivery timing-stream counter overflow");
    }

    checked_add(
        stats_.delivered_samples, 1u,
        "native HID delivered-sample counter overflow");
    stats_.last_delivered_epoch = sample.epoch;
    stats_.last_delivered_sequence = sample.sequence;
    stats_.last_delivered_deadline_ticks = sample.deadline_ticks;
    stats_.last_delivery_ticks = delivery_ticks;
    last_delivery_slot_ = delivery_slot;
    delivered_in_epoch_ = true;

    const bool delivery_timing_defect =
        delivery_age > stats_.delivery_age_limit_ticks ||
        stats_.max_delivery_interval_jitter_ticks >
            stats_.delivery_age_limit_ticks ||
        !stats_.delivery_timing_conservation_valid();

    if (cadence_defect || delivery_timing_defect) {
        stats_.exact_cadence_valid = false;
        if (stats_.strict_proof) {
            if (delivery_age > stats_.delivery_age_limit_ticks) {
                fail_strict(
                    "native HID delivery exceeded one profiled report period");
            }
            if (!stats_.delivery_timing_conservation_valid()) {
                fail_strict(
                    "native HID delivery timing telemetry was not conserved");
            }
            if (stats_.max_delivery_interval_jitter_ticks >
                stats_.delivery_age_limit_ticks) {
                fail_strict(
                    "native HID delivery interval jitter exceeded one profiled report period");
            }
            fail_strict(
                "native HID delivery had a sequence gap or same-slot burst");
        }
    }
}

[[noreturn]] void NativeHidReportCadence::fail_strict(const char* message) {
    throw std::runtime_error(message);
}

float normalize_xinput_axis(std::int16_t raw, std::int16_t deadzone) noexcept {
    const int value = static_cast<int>(raw);
    const int dz = static_cast<int>(deadzone);
    const int magnitude = value < 0 ? -value : value;
    if (magnitude <= dz) {
        return 0.0f;
    }
    const int range = value < 0 ? 32768 - dz : 32767 - dz;
    const float scaled =
        static_cast<float>(magnitude - dz) / static_cast<float>(range);
    return std::clamp(value < 0 ? -scaled : scaled, -1.0f, 1.0f);
}

std::uint8_t native_stick_axis_byte(
    float axis,
    std::uint8_t negative,
    std::uint8_t center,
    std::uint8_t positive) noexcept {
    axis = std::clamp(axis, -1.0f, 1.0f);
    if (axis < 0.0f) {
        return static_cast<std::uint8_t>(
            std::clamp<int>(
                static_cast<int>(
                    std::lround(
                        static_cast<float>(center) +
                        axis * static_cast<float>(center - negative))),
                0,
                255));
    }
    return static_cast<std::uint8_t>(
        std::clamp<int>(
            static_cast<int>(
                std::lround(
                    static_cast<float>(center) +
                    axis * static_cast<float>(positive - center))),
            0,
            255));
}

std::uint8_t native_stick_axis_byte(
    float axis,
    std::uint8_t negative,
    std::uint8_t positive) noexcept {
    return native_stick_axis_byte(axis, negative, 128, positive);
}

NativePointerAxes merge_native_pointer_axes(
    float keyboard_x,
    float keyboard_y,
    float controller_x,
    float controller_y,
    bool controller_connected,
    bool controller_mode) noexcept {
    constexpr float kPointerMoveEpsilon = 0.001f;
    const auto moved = [](float x, float y) {
        return std::fabs(x) > kPointerMoveEpsilon ||
               std::fabs(y) > kPointerMoveEpsilon;
    };

    NativePointerAxes axes{};
    if (controller_mode) {
        axes.x = controller_connected ? controller_x : 0.0f;
        axes.y = controller_connected ? controller_y : 0.0f;
        axes.moving = controller_connected && moved(axes.x, axes.y);
        axes.hold_visible = controller_connected;
        axes.source = axes.moving ? NativePointerAxisSource::Controller
                                  : NativePointerAxisSource::None;
        return axes;
    }

    // Input modes are exclusive: in keyboard/mouse mode a connected controller
    // cannot move, recenter, or keep the IR pointer visible.
    axes.x = std::clamp(keyboard_x, -1.0f, 1.0f);
    axes.y = std::clamp(keyboard_y, -1.0f, 1.0f);
    axes.moving = moved(axes.x, axes.y);
    axes.hold_visible = false;
    if (axes.moving) {
        axes.source = NativePointerAxisSource::KeyboardMouse;
    }
    return axes;
}

NativeMousePointerProjection project_native_mouse_pointer(
    const NativeMousePointerSample& sample) noexcept {
    NativeMousePointerProjection projection{};
    if (!sample.absolute_valid || !sample.inside_client ||
        sample.client_width <= 1 ||
        sample.client_height <= 1 || sample.viewport.width <= 1 ||
        sample.viewport.height <= 1 || sample.viewport.left < 0 ||
        sample.viewport.top < 0 ||
        sample.viewport.width > sample.client_width - sample.viewport.left ||
        sample.viewport.height > sample.client_height - sample.viewport.top ||
        sample.client_x < 0 || sample.client_y < 0 ||
        sample.client_x >= sample.client_width ||
        sample.client_y >= sample.client_height ||
        sample.client_x < sample.viewport.left ||
        sample.client_y < sample.viewport.top ||
        sample.client_x >= sample.viewport.left + sample.viewport.width ||
        sample.client_y >= sample.viewport.top + sample.viewport.height) {
        return projection;
    }

    const auto project_axis = [](int coordinate, int origin, int extent,
                                 float requested_scale) noexcept {
        const float normalized =
            (static_cast<float>(coordinate - origin) /
             static_cast<float>(extent - 1)) *
                2.0f -
            1.0f;
        const float clamped_normalized =
            std::clamp(normalized, -1.0f, 1.0f);
        const float scale = std::clamp(
            std::isfinite(requested_scale) ? std::fabs(requested_scale) : 1.0f,
            0.05f,
            1.0f);
        if (scale >= 0.999f) {
            return clamped_normalized;
        }

        // Low-gain center band for the game's save-select cursor, then full
        // camera reach at the edges.
        constexpr float kCenterBand = 0.50f;
        const float magnitude = std::fabs(clamped_normalized);
        const float sign = std::signbit(clamped_normalized) ? -1.0f : 1.0f;
        if (magnitude <= kCenterBand) {
            return sign * magnitude * scale;
        }
        const float center_edge = kCenterBand * scale;
        const float edge_scale =
            (1.0f - center_edge) / (1.0f - kCenterBand);
        return sign * std::clamp(
                          center_edge +
                              (magnitude - kCenterBand) * edge_scale,
                          0.0f,
                          1.0f);
    };

    projection.x = project_axis(
        sample.client_x, sample.viewport.left, sample.viewport.width,
        sample.x_scale);
    projection.y = project_axis(
        sample.client_y, sample.viewport.top, sample.viewport.height,
        sample.y_scale);
    projection.visible = true;
    return projection;
}

void publish_wiimote_ir_dots(
    WiimoteInputSnapshot& snapshot,
    float pointer_x,
    float pointer_y,
    bool active) noexcept {
    snapshot.ir = {};
    if (!active) {
        return;
    }
    constexpr int kKpadFrameMinRaw = 26;
    constexpr int kKpadFrameMaxXRaw = 997;
    constexpr int kKpadFrameMaxYRaw = 725;
    constexpr int kPrimaryHalfSeparation = 80;
    // The raw IR camera raster is not the KPAD pointing space: RMGE01's
    // WPAD/KPAD stack applies EEPROM DPD calibration, the below-screen
    // sensor-bar offset and its own scale, giving 226 raw camera units per
    // KPAD unit with neutral at (510, 487). Compensating here keeps the guest
    // decoding real Wiimote reports with no KPAD/WPAD host hook.
    constexpr float kKpadPointerRawUnitsPerHostUnit = 226.0f;
    constexpr float kKpadPointerNeutralXRaw = 510.0f;
    constexpr float kKpadPointerNeutralYRaw = 487.0f;
    const int center_x = std::clamp<int>(
        static_cast<int>(
            std::lround(
                kKpadPointerNeutralXRaw -
                std::clamp(pointer_x, -1.0f, 1.0f) *
                    kKpadPointerRawUnitsPerHostUnit)),
        kKpadFrameMinRaw + kPrimaryHalfSeparation,
        kKpadFrameMaxXRaw - kPrimaryHalfSeparation);
    const int center_y = std::clamp<int>(
        static_cast<int>(
            std::lround(
                kKpadPointerNeutralYRaw +
                std::clamp(pointer_y, -1.0f, 1.0f) *
                    kKpadPointerRawUnitsPerHostUnit)),
        kKpadFrameMinRaw,
        kKpadFrameMaxYRaw);
    snapshot.ir[0] = {
        true,
        static_cast<std::uint16_t>(center_x - kPrimaryHalfSeparation),
        static_cast<std::uint16_t>(center_y),
        8};
    snapshot.ir[1] = {
        true,
        static_cast<std::uint16_t>(center_x + kPrimaryHalfSeparation),
        static_cast<std::uint16_t>(center_y),
        8};
}

bool advance_wiimote_shake_accel(
    WiimoteInputSnapshot& snapshot,
    bool pressed,
    bool& previous_pressed,
    std::uint8_t& remaining_frames) noexcept {
    if (pressed && !previous_pressed) {
        remaining_frames = 12;
    }
    previous_pressed = pressed;
    if (remaining_frames == 0) {
        return false;
    }
    const bool phase = (remaining_frames & 1u) != 0u;
    snapshot.accel.x = phase ? 0x02f0u : 0x0110u;
    snapshot.accel.y = 0x0200u;
    snapshot.accel.z = phase ? 0x0130u : 0x02d0u;
    --remaining_frames;
    return true;
}

std::size_t wiimote_input_report_size(std::uint8_t report_mode) noexcept {
    switch (report_mode) {
    case 0x30:
        return 4;
    case 0x31:
        return 7;
    case 0x32:
        return 12;
    case 0x33:
        return 19;
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
        return 23;
    default:
        return 0;
    }
}

namespace {

std::size_t wiimote_any_input_report_size(std::uint8_t report_id) noexcept {
    switch (report_id) {
    case 0x20:
        return 8;
    case 0x21:
        return 23;
    case 0x22:
        return 6;
    default:
        return wiimote_input_report_size(report_id);
    }
}

}  // namespace

WiimoteReportBuildResult normalize_wiimote_hid_input_report(
    std::span<const std::uint8_t> raw,
    std::span<std::uint8_t> output) noexcept {
    if (raw.empty()) {
        return {WiimoteReportBuildStatus::UnsupportedReportMode, 0};
    }

    if (raw[0] == kWiimoteHidInput && raw.size() >= 2u) {
        const std::size_t expected = wiimote_any_input_report_size(raw[1]);
        if (expected == 0u) {
            return {WiimoteReportBuildStatus::UnsupportedReportMode, 0};
        }
        if (output.size() < expected || raw.size() < expected) {
            return {WiimoteReportBuildStatus::OutputTooSmall, expected};
        }
        std::copy_n(raw.begin(), expected, output.begin());
        return {WiimoteReportBuildStatus::Ok, expected};
    }

    std::size_t offset = 0;
    if (raw[0] == 0u && raw.size() >= 2u &&
        wiimote_any_input_report_size(raw[1]) != 0u) {
        offset = 1u;
    }
    const std::size_t source_size = raw.size() - offset;
    if (source_size == 0u) {
        return {WiimoteReportBuildStatus::UnsupportedReportMode, 0};
    }
    const std::uint8_t report_id = raw[offset];
    const std::size_t expected = wiimote_any_input_report_size(report_id);
    if (expected == 0u) {
        return {WiimoteReportBuildStatus::UnsupportedReportMode, 0};
    }
    if (output.size() < expected || source_size < expected - 1u) {
        return {WiimoteReportBuildStatus::OutputTooSmall, expected};
    }

    output[0] = kWiimoteHidInput;
    std::copy_n(raw.begin() + static_cast<std::ptrdiff_t>(offset),
                expected - 1u,
                output.begin() + 1);
    return {WiimoteReportBuildStatus::Ok, expected};
}

WiimoteReportBuildResult build_wiimote_hid_output_report(
    std::span<const std::uint8_t> bluetooth_output_report,
    std::span<std::uint8_t> output) noexcept {
    if (bluetooth_output_report.size() < 2u ||
        bluetooth_output_report[0] != kWiimoteHidOutput) {
        return {WiimoteReportBuildStatus::UnsupportedReportMode, 0};
    }

    const std::size_t required = bluetooth_output_report.size() - 1u;
    if (output.size() < required) {
        return {WiimoteReportBuildStatus::OutputTooSmall, required};
    }

    std::fill(output.begin(), output.end(), std::uint8_t{0});
    output[0] = bluetooth_output_report[1];
    if (bluetooth_output_report.size() > 2u) {
        std::copy(
            bluetooth_output_report.begin() + 2,
            bluetooth_output_report.end(),
            output.begin() + 1);
    }
    return {WiimoteReportBuildStatus::Ok, output.size()};
}

WiimoteReportBuildResult build_wiimote_input_report(
    std::uint8_t report_mode,
    const WiimoteInputSnapshot& snapshot,
    std::span<std::uint8_t> output) noexcept {
    const std::size_t report_size = wiimote_input_report_size(report_mode);
    if (report_size == 0) {
        return {WiimoteReportBuildStatus::UnsupportedReportMode, 0};
    }
    if (output.size() < report_size) {
        return {WiimoteReportBuildStatus::OutputTooSmall, report_size};
    }

    std::size_t cursor = 0;
    output[cursor++] = kWiimoteHidInput;
    output[cursor++] = report_mode;

    switch (report_mode) {
    case 0x30:
        write_core_buttons(snapshot, false, output, cursor);
        break;
    case 0x31:
        write_core_buttons(snapshot, true, output, cursor);
        write_accel(snapshot.accel, output, cursor);
        break;
    case 0x32:
        write_core_buttons(snapshot, false, output, cursor);
        write_nunchuk_extension(snapshot.nunchuk, output, cursor, 8);
        break;
    case 0x33:
        write_core_buttons(snapshot, true, output, cursor);
        write_accel(snapshot.accel, output, cursor);
        write_ir_extended_12(snapshot, output, cursor);
        break;
    case 0x34:
        write_core_buttons(snapshot, false, output, cursor);
        write_nunchuk_extension(snapshot.nunchuk, output, cursor, 19);
        break;
    case 0x35:
        write_core_buttons(snapshot, true, output, cursor);
        write_accel(snapshot.accel, output, cursor);
        write_nunchuk_extension(snapshot.nunchuk, output, cursor, 16);
        break;
    case 0x36:
        write_core_buttons(snapshot, false, output, cursor);
        write_ir_basic_10(snapshot, output, cursor);
        write_nunchuk_extension(snapshot.nunchuk, output, cursor, 9);
        break;
    case 0x37:
        write_core_buttons(snapshot, true, output, cursor);
        write_accel(snapshot.accel, output, cursor);
        write_ir_basic_10(snapshot, output, cursor);
        write_nunchuk_extension(snapshot.nunchuk, output, cursor, 6);
        break;
    default:
        return {WiimoteReportBuildStatus::UnsupportedReportMode, 0};
    }

    return {WiimoteReportBuildStatus::Ok, cursor};
}

bool encrypt_rmge01_nunchuk_extension_payload(
    std::span<std::uint8_t> report,
    std::span<const std::uint8_t, 8> additive_key,
    std::span<const std::uint8_t, 8> xor_key) noexcept {
    const auto layout = nunchuk_extension_payload_layout(report);
    if (!layout.has_value() || layout->offset > report.size() ||
        layout->size > report.size() - layout->offset) {
        return false;
    }
    for (std::size_t i = 0; i < layout->size; ++i) {
        const std::uint8_t plaintext = report[layout->offset + i];
        const std::uint8_t subtractive = static_cast<std::uint8_t>(
            plaintext - additive_key[i % additive_key.size()]);
        report[layout->offset + i] = static_cast<std::uint8_t>(
            subtractive ^ xor_key[i % xor_key.size()]);
    }
    return true;
}

WiimoteReportBuildResult build_wiimote_status_report(
    const WiimoteDeviceStatus& status,
    std::span<std::uint8_t> output) noexcept {
    constexpr std::size_t kStatusReportSize = 8;
    if (output.size() < kStatusReportSize) {
        return {WiimoteReportBuildStatus::OutputTooSmall, kStatusReportSize};
    }
    WiimoteInputSnapshot snapshot{};
    snapshot.buttons = status.buttons;
    std::size_t cursor = 0;
    output[cursor++] = kWiimoteHidInput;
    output[cursor++] = 0x20;
    write_core_buttons(snapshot, false, output, cursor);
    std::uint8_t flags = static_cast<std::uint8_t>(status.led_mask & 0xf0u);
    if (status.battery_low) flags |= 0x01u;
    if (status.extension_connected) flags |= 0x02u;
    if (status.speaker_enabled) flags |= 0x04u;
    if (status.ir_enabled) flags |= 0x08u;
    output[cursor++] = flags;
    output[cursor++] = 0;
    output[cursor++] = 0;
    output[cursor++] = status.battery_level;
    return {WiimoteReportBuildStatus::Ok, cursor};
}

WiimoteReportBuildResult build_wiimote_ack_report(
    const WiimoteButtons& buttons,
    std::uint8_t report_id,
    std::uint8_t error,
    std::span<std::uint8_t> output) noexcept {
    constexpr std::size_t kAckReportSize = 6;
    if (output.size() < kAckReportSize) {
        return {WiimoteReportBuildStatus::OutputTooSmall, kAckReportSize};
    }
    WiimoteInputSnapshot snapshot{};
    snapshot.buttons = buttons;
    std::size_t cursor = 0;
    output[cursor++] = kWiimoteHidInput;
    output[cursor++] = 0x22;
    write_core_buttons(snapshot, false, output, cursor);
    output[cursor++] = report_id;
    output[cursor++] = error;
    return {WiimoteReportBuildStatus::Ok, cursor};
}

WiimoteReportBuildResult build_wiimote_read_memory_report(
    const WiimoteButtons& buttons,
    std::uint16_t offset,
    std::uint8_t error,
    std::span<const std::uint8_t> data,
    std::span<std::uint8_t> output) noexcept {
    constexpr std::size_t kReadMemoryReportSize = 23;
    if (output.size() < kReadMemoryReportSize) {
        return {
            WiimoteReportBuildStatus::OutputTooSmall,
            kReadMemoryReportSize};
    }
    const std::size_t data_size =
        error == 0u ? std::min<std::size_t>(data.size(), 16u) : 0u;
    WiimoteInputSnapshot snapshot{};
    snapshot.buttons = buttons;
    std::size_t cursor = 0;
    output[cursor++] = kWiimoteHidInput;
    output[cursor++] = 0x21;
    write_core_buttons(snapshot, false, output, cursor);
    const std::uint8_t size_nibble =
        error != 0u
            ? 0xF0u
            : (data_size == 0u
                   ? 0u
                   : static_cast<std::uint8_t>((data_size - 1u) << 4u));
    output[cursor++] =
        static_cast<std::uint8_t>(size_nibble | (error & 0x0fu));
    output[cursor++] = static_cast<std::uint8_t>(offset >> 8);
    output[cursor++] = static_cast<std::uint8_t>(offset);
    for (std::size_t i = 0; i < data_size; ++i) {
        output[cursor++] = data[i];
    }
    for (std::size_t i = data_size; i < 16u; ++i) {
        output[cursor++] = 0;
    }
    return {WiimoteReportBuildStatus::Ok, cursor};
}

}  // namespace galaxy::input
