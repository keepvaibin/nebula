#pragma once

#include <cstdint>
#include <array>

namespace galaxy::checkpoint {

// Full mode sends every translated taken branch through the host callback.
// Inline mode permits generated code to use checkpoint_counter and
// checkpoint_next_slow between deterministic host callbacks.
enum class RequestedMode : std::uint8_t {
    Full = 0,
    Inline = 1,
};

// Hardware PE levels stay latched until the guest acknowledges them. A level
// cannot be dispatched with MSR[EE] clear; restoring EE is a separate, forced
// architectural checkpoint. FIFO scanning still needs service while masked
// because it creates the hardware level rather than dispatching it.
[[nodiscard]] constexpr bool pe_service_due(
    bool gate_masked_levels,
    bool external_interrupts_enabled,
    bool latched_level,
    bool fifo_scan_due) noexcept {
    return fifo_scan_due ||
        (latched_level &&
         (!gate_masked_levels || external_interrupts_enabled));
}

// A broker publication can lag its exact device edge. Testing only the event
// mask is therefore insufficient at mtmsr: every active absolute deadline
// must still be in the future, including the independently latched DEC.
[[nodiscard]] constexpr bool quiet_architectural_checkpoint(
    std::uint64_t now_ticks,
    const std::array<std::uint64_t, 4>& device_deadlines,
    std::uint32_t pending_mask,
    bool hardware_irq_pending,
    std::uint64_t checkpoint,
    std::uint64_t next_slow,
    bool eligible) noexcept {
    if (!eligible || pending_mask != 0u || hardware_irq_pending ||
        checkpoint >= next_slow) return false;
    for (const auto deadline : device_deadlines) {
        if (deadline != 0u && deadline <= now_ticks) return false;
    }
    return true;
}

// A generic view of the two NativeServicesV1 checkpoint pointer fields.  The
// controller deliberately does not own either the slots or their pointees.
// They must outlive the binding and every scope made from it.
struct PointerView {
    std::uint64_t** checkpoint_counter{};
    const std::uint64_t** checkpoint_next_slow{};
};

struct Telemetry {
    // Number of effective activate() calls. Repeated activate() calls are
    // idempotent and do not inflate this value.
    std::uint64_t activations{};
    std::uint64_t full_scope_entries{};
    std::uint64_t maximum_full_scope_depth{};
    std::uint64_t inline_scope_entries{};
    std::uint64_t maximum_inline_scope_depth{};
    std::uint64_t transitions_to_full{};
    std::uint64_t transitions_to_inline{};
};

// Single-guest-execution-thread policy controller for the translated branch
// checkpoint fast gate. It has no clock, timeout, or guest-state dependency.
// Pending-event bypass remains in generated code via NativeServicesV1's
// pending_event_mask and is intentionally outside this controller.
class GateController {
public:
    class FullScope {
    public:
        ~FullScope() noexcept;

        FullScope(const FullScope&) = delete;
        FullScope& operator=(const FullScope&) = delete;
        FullScope(FullScope&& other) noexcept;
        FullScope& operator=(FullScope&&) = delete;

    private:
        friend class GateController;
        explicit FullScope(GateController* owner) noexcept : owner_(owner) {}

        GateController* owner_{};
    };

    // A narrowly scoped override for translated guest execution entered from
    // inside a host callback tree. It temporarily republishes the inline gate
    // while preserving every enclosing FullScope. Any nested host callback
    // still creates its own FullScope and therefore runs conservatively.
    class InlineScope {
    public:
        ~InlineScope() noexcept;

        InlineScope(const InlineScope&) = delete;
        InlineScope& operator=(const InlineScope&) = delete;
        InlineScope(InlineScope&& other) noexcept;
        InlineScope& operator=(InlineScope&&) = delete;

        [[nodiscard]] bool active() const noexcept {
            return owner_ != nullptr;
        }

    private:
        friend class GateController;
        InlineScope(
            GateController* owner,
            std::uint64_t saved_full_depth,
            std::uint64_t token) noexcept
            : owner_(owner),
              saved_full_depth_(saved_full_depth),
              token_(token) {}

        GateController* owner_{};
        std::uint64_t saved_full_depth_{};
        std::uint64_t token_{};
    };

    explicit GateController(
        RequestedMode requested_mode = RequestedMode::Full);
    ~GateController() noexcept;

    GateController(const GateController&) = delete;
    GateController& operator=(const GateController&) = delete;
    GateController(GateController&&) = delete;
    GateController& operator=(GateController&&) = delete;

    // Requested mode may be configured until Inline mode is activated. It is
    // immutable after activation so callback-tree behavior cannot change
    // implicitly in the middle of guest execution.
    void set_requested_mode(RequestedMode mode);

    // Binding requires two empty slots and non-null pointees. Binding twice,
    // slot takeover, or unbinding while any scope is live hard-fails with a
    // logic error rather than silently changing checkpoint ownership.
    void bind(
        PointerView pointers,
        std::uint64_t* checkpoint_counter,
        const std::uint64_t* checkpoint_next_slow);
    void unbind();

    // Activates Inline mode only when it was requested. Activation inside a
    // FullScope is deferred until the outermost scope exits.
    void activate();

    // Every dynamic host callback tree should own one of these scopes. Nested
    // scopes reference-count the full-checkpoint state; only the outermost
    // entry/exit changes the service pointers.
    [[nodiscard]] FullScope full_scope();

    // Temporarily permits translated guest execution to use the inline gate
    // even when its caller owns one or more FullScopes. This is an inactive
    // no-op unless Inline mode is bound and activated. Scopes must remain
    // strictly lexical; destroying one while a nested FullScope is live is a
    // fatal ownership violation.
    [[nodiscard]] InlineScope inline_scope();

    [[nodiscard]] RequestedMode requested_mode() const noexcept {
        return requested_mode_;
    }
    [[nodiscard]] bool bound() const noexcept { return bound_; }
    [[nodiscard]] bool activated() const noexcept { return activated_; }
    [[nodiscard]] bool inline_published() const noexcept {
        return inline_published_;
    }
    [[nodiscard]] std::uint64_t full_scope_depth() const noexcept {
        return full_scope_depth_;
    }
    [[nodiscard]] std::uint64_t inline_scope_depth() const noexcept {
        return inline_scope_depth_;
    }
    [[nodiscard]] const Telemetry& telemetry() const noexcept {
        return telemetry_;
    }

private:
    void enter_full_scope();
    void leave_full_scope() noexcept;
    [[nodiscard]] bool enter_inline_scope(
        std::uint64_t& saved_full_depth,
        std::uint64_t& token);
    void leave_inline_scope(
        std::uint64_t saved_full_depth,
        std::uint64_t token) noexcept;
    void validate_published_state() const;
    [[nodiscard]] bool published_state_is_valid() const noexcept;
    void publish_full_unchecked() noexcept;
    void publish_inline_unchecked() noexcept;

    RequestedMode requested_mode_{RequestedMode::Full};
    PointerView pointers_{};
    std::uint64_t* checkpoint_counter_{};
    const std::uint64_t* checkpoint_next_slow_{};
    std::uint64_t full_scope_depth_{};
    std::uint64_t inline_scope_depth_{};
    Telemetry telemetry_{};
    bool bound_{};
    bool activated_{};
    bool inline_published_{};
};

}  // namespace galaxy::checkpoint
