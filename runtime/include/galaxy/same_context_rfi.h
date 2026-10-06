#pragma once

#include "galaxy/native_api.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace galaxy::interrupt {

[[nodiscard]] constexpr bool validated_checkpoint_rfi_source(
    std::uint32_t loaded_context, std::uint32_t load_call_pc,
    std::uint32_t expected_context) noexcept {
    return expected_context != 0u && loaded_context == expected_context &&
           load_call_pc == 0x804A381Cu;
}

// Retired stateful HLE bodies contained explicit callbacks with retained RAM
// locals. Keep those exact RMGE01 ranges on the flat path even if re-enabled.
[[nodiscard]] constexpr bool checkpoint_native_helper_range(std::uint32_t pc) noexcept {
    return (pc >= 0x8000C53Cu && pc < 0x8000C5C0u) ||
           (pc >= 0x802617B4u && pc < 0x80261854u) ||
           (pc >= 0x80443904u && pc < 0x80443980u) ||
           (pc >= 0x80448710u && pc < 0x80448790u) ||
           (pc >= 0x80496A60u && pc < 0x80496A74u);
}

// Only a statically emitted checkpoint may retain its native caller. Capture
// architectural operands immediately before the actual translated exception,
// not on every checkpoint. No guest state is restored by this proof.
class CheckpointRfiProof final {
public:
    enum class Mismatch : std::size_t {
        None, Uncaptured, Disqualified, Ownership, Identity, Pc, Context, Depth,
        Gpr, Fpr, Ps1, Gqr, Segments, Control, Fpscr, Msr, Reservation, Count
    };
    CheckpointRfiProof(const PpcContext* native_context,
                       std::uint32_t guest_context, std::uint32_t resume_pc,
                       std::size_t live_depth, std::uint32_t guest_thread = 0u) noexcept
        : native_context_(native_context), guest_context_(guest_context),
          resume_pc_(resume_pc), live_depth_(live_depth), guest_thread_(guest_thread) {}

    void capture(const PpcContext& context, std::uint32_t exception,
                 std::uint32_t guest_context, std::uint32_t saved_resume_pc,
                 std::uint32_t guest_thread = 0u) noexcept {
        if (exception != 4u && exception != 8u) return;
        if (captured_ || &context != native_context_ ||
            guest_context != guest_context_ || saved_resume_pc != resume_pc_ ||
            guest_thread != guest_thread_) {
            disqualified_ = true;
            return;
        }
        std::copy(std::begin(context.gpr), std::end(context.gpr), saved_.gpr);
        std::copy(std::begin(context.fpr_bits), std::end(context.fpr_bits), saved_.fpr);
        std::copy(std::begin(context.ps1_bits), std::end(context.ps1_bits), saved_.ps1);
        std::copy(std::begin(context.gqr), std::end(context.gqr), saved_.gqr);
        std::copy(std::begin(context.segment_registers), std::end(context.segment_registers), saved_.segments);
        saved_.cr=context.cr; saved_.lr=context.lr; saved_.ctr=context.ctr;
        saved_.xer=context.xer; saved_.fpscr=context.fpscr;
        saved_.hid2=context.hid2; saved_.msr=context.msr;
        saved_.reserved_address=context.reserved_address;
        saved_.reserved_value=context.reserved_value;
        exception_=exception;
        captured_=true;
    }

    [[nodiscard]] bool matches(const PpcContext& context,
                              std::uint32_t transfer_pc, std::uint32_t exception,
                              std::uint32_t current_context, std::uint32_t physical_context,
                              std::size_t live_depth, bool ownership_clear,
                              std::uint32_t guest_thread = 0u) const noexcept {
        return mismatch(context,transfer_pc,exception,current_context,physical_context,
                        live_depth,ownership_clear,guest_thread)==Mismatch::None;
    }

    [[nodiscard]] Mismatch mismatch(const PpcContext& context,
        std::uint32_t transfer_pc, std::uint32_t exception,
        std::uint32_t current_context, std::uint32_t physical_context,
        std::size_t live_depth, bool ownership_clear, std::uint32_t guest_thread = 0u) const noexcept {
        if (!captured_) return Mismatch::Uncaptured;
        if (disqualified_) return Mismatch::Disqualified;
        if (!ownership_clear) return Mismatch::Ownership;
        if (&context!=native_context_ || exception!=exception_) return Mismatch::Identity;
        if (transfer_pc!=resume_pc_ || context.pc!=resume_pc_) return Mismatch::Pc;
        if (current_context!=guest_context_ || physical_context!=(guest_context_ & 0x3FFFFFFFu) ||
            guest_thread!=guest_thread_) return Mismatch::Context;
        if (live_depth!=live_depth_) return Mismatch::Depth;
        if (!std::equal(std::begin(context.gpr),std::end(context.gpr),saved_.gpr)) return Mismatch::Gpr;
        if (!std::equal(std::begin(context.fpr_bits),std::end(context.fpr_bits),saved_.fpr)) return Mismatch::Fpr;
        if (!std::equal(std::begin(context.ps1_bits),std::end(context.ps1_bits),saved_.ps1)) return Mismatch::Ps1;
        if (!std::equal(std::begin(context.gqr),std::end(context.gqr),saved_.gqr)) return Mismatch::Gqr;
        if (!std::equal(std::begin(context.segment_registers),std::end(context.segment_registers),saved_.segments)) return Mismatch::Segments;
        if (context.cr!=saved_.cr || context.lr!=saved_.lr || context.ctr!=saved_.ctr ||
            context.xer!=saved_.xer || context.hid2!=saved_.hid2) return Mismatch::Control;
        if (context.fpscr!=saved_.fpscr) return Mismatch::Fpscr;
        if (context.msr!=saved_.msr) return Mismatch::Msr;
        if (context.reserved_address!=saved_.reserved_address || context.reserved_value!=saved_.reserved_value) return Mismatch::Reservation;
        return Mismatch::None;
    }

private:
    struct Registers {
        std::uint32_t gpr[32];
        std::uint64_t fpr[32],ps1[32];
        std::uint32_t gqr[8],segments[16];
        std::uint32_t cr,lr,ctr,xer,fpscr,hid2,msr,reserved_address,reserved_value;
    } saved_; // Read only after capture; avoid clearing a snapshot on every call.
    const PpcContext* native_context_;
    std::uint32_t guest_context_,resume_pc_;
    std::size_t live_depth_;
    std::uint32_t guest_thread_;
    std::uint32_t exception_{};
    bool captured_{},disqualified_{};
};

} // namespace galaxy::interrupt
