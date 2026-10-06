#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>

namespace galaxy::host {

enum class NativeIosDevice : std::uint8_t {
    Unknown, Nand, Fs, Es, Di, Bluetooth, StmImmediate, StmEventHook, Other,
};

struct NativeIosRequestIdentity {
    std::uint32_t request{};
    std::uint32_t command{};
    std::uint32_t handle{};
    // Original +0x0C..+0x1C words, before a reply can reuse request memory.
    // Read/write: buffer,length; ioctl: opcode,in,inLength,out,outLength;
    // ioctlv: opcode,inCount,outCount,vectors. Never read MMIO to capture these.
    std::array<std::uint32_t, 5> arguments{};
    NativeIosDevice device{};
    bool arguments_valid{};
};

struct NativeIosClockSample {
    // Clock/counter reads are bracketed; neither endpoint implies simultaneous
    // wall/CPU/cycle sampling. Zero CPU delta can reflect timer quantization.
    std::uint64_t before_ns{};
    std::uint64_t after_ns{};
    std::uint64_t cpu_100ns{};
    std::uint64_t cycles{};
    bool cpu_valid{};
    bool cycles_valid{};
};

struct NativeIosAnomalyRecord {
    std::uint64_t sequence{};
    NativeIosRequestIdentity identity{};
    NativeIosClockSample entry{};
    NativeIosClockSample exit{};
    bool unwound{};

    bool wall_valid() const noexcept {
        return entry.before_ns != 0u && entry.after_ns >= entry.before_ns &&
            exit.before_ns >= entry.after_ns && exit.after_ns >= exit.before_ns;
    }
    std::uint64_t minimum_elapsed_ns() const noexcept {
        return wall_valid() ? exit.before_ns - entry.after_ns : 0u;
    }
    std::uint64_t maximum_elapsed_ns() const noexcept {
        return wall_valid() ? exit.after_ns - entry.before_ns : 0u;
    }
};

// Owned solely by the GuestAddressSpace simulation thread. Keep first slow
// requests even after ordinary traffic or overflow; no allocation or I/O.
class NativeIosAnomalyLedger {
public:
    static constexpr std::size_t kCapacity = 32u;
    static constexpr std::uint64_t kThresholdNs = 5'000'000u;

    std::uint64_t begin() noexcept { return ++started_; }
    void finish(const NativeIosAnomalyRecord& record) noexcept {
        ++finished_;
        unwound_ += record.unwound ? 1u : 0u;
        if (!record.wall_valid()) {
            ++invalid_wall_;
        } else if (record.maximum_elapsed_ns() < kThresholdNs) {
            return;
        } else {
            ++slow_;
        }
        if (size_ < records_.size()) {
            records_[size_++] = record;
        } else {
            ++overflow_;
        }
    }
    std::span<const NativeIosAnomalyRecord> records() const noexcept {
        return {records_.data(), size_};
    }
    std::uint64_t started() const noexcept { return started_; }
    std::uint64_t finished() const noexcept { return finished_; }
    std::uint64_t slow() const noexcept { return slow_; }
    std::uint64_t overflow() const noexcept { return overflow_; }
    std::uint64_t invalid_wall() const noexcept { return invalid_wall_; }
    std::uint64_t unwound() const noexcept { return unwound_; }

private:
    std::array<NativeIosAnomalyRecord, kCapacity> records_{};
    std::size_t size_{};
    std::uint64_t started_{};
    std::uint64_t finished_{};
    std::uint64_t slow_{};
    std::uint64_t overflow_{};
    std::uint64_t invalid_wall_{};
    std::uint64_t unwound_{};
};

// Also closes on an existing exception; diagnostics cannot replace it. The
// caller copies request identity before executing any command or reply.
class NativeIosRequestTrace {
public:
    using Clock = NativeIosClockSample (*)() noexcept;
    NativeIosRequestTrace(
        NativeIosAnomalyLedger& ledger, NativeIosRequestIdentity identity,
        Clock clock) noexcept
        : ledger_(ledger), clock_(clock), exceptions_(std::uncaught_exceptions()) {
        record_.sequence = ledger_.begin();
        record_.identity = identity;
        record_.entry = clock_();
    }
    ~NativeIosRequestTrace() noexcept {
        record_.exit = clock_();
        record_.unwound = std::uncaught_exceptions() > exceptions_;
        ledger_.finish(record_);
    }
    NativeIosRequestTrace(const NativeIosRequestTrace&) = delete;
    NativeIosRequestTrace& operator=(const NativeIosRequestTrace&) = delete;

private:
    NativeIosAnomalyLedger& ledger_;
    Clock clock_;
    int exceptions_{};
    NativeIosAnomalyRecord record_{};
};

}  // namespace galaxy::host
