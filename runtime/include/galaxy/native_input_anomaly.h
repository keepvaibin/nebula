#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace galaxy::input {

// Simulation-thread diagnostics only. Samples are the last already-observed
// time-base values, not fresh observations of entry/exit. Zero stays unknown.
struct NativeInputDeferredSample {
    std::uint64_t ticks{};
    std::uint64_t qpc{};
    std::uint64_t cpu_100ns{};
    std::uint32_t pc{};
};

struct NativeInputDeferralEpisode {
    std::uint64_t attempts{};
    NativeInputDeferredSample first{};
    NativeInputDeferredSample last{};

    void observe(NativeInputDeferredSample sample) noexcept {
        if (attempts == 0u) {
            first = sample;
        }
        last = sample;
        if (attempts != std::numeric_limits<std::uint64_t>::max()) {
            ++attempts;
        }
    }

    NativeInputDeferralEpisode take() noexcept {
        const NativeInputDeferralEpisode result = *this;
        *this = {};
        return result;
    }
};

// First anomalies survive ordinary traffic and later overflow. No allocation,
// clock, atomics or I/O: the runtime simulation thread also owns final dumping.
template <typename Record, std::size_t Capacity>
class NativeInputAnomalyLedger {
public:
    static_assert(Capacity > 0u);

    void append(const Record& record) noexcept {
        if (size_ < Capacity) {
            records_[size_++] = record;
        } else if (overflow_ != std::numeric_limits<std::uint64_t>::max()) {
            ++overflow_;
        }
    }

    std::span<const Record> records() const noexcept {
        return {records_.data(), size_};
    }
    std::uint64_t overflow() const noexcept { return overflow_; }
    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    std::array<Record, Capacity> records_{};
    std::size_t size_{};
    std::uint64_t overflow_{};
};

}  // namespace galaxy::input
