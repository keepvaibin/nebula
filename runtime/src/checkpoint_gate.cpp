#include "galaxy/checkpoint_gate.h"

#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace galaxy::checkpoint {
namespace {

void validate_requested_mode(RequestedMode mode) {
    if (mode != RequestedMode::Full && mode != RequestedMode::Inline) {
        throw std::invalid_argument("invalid checkpoint-gate requested mode");
    }
}

std::uint64_t checked_increment(
    std::uint64_t value,
    const char* failure_message) {
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(failure_message);
    }
    return value + 1u;
}

}  // namespace

GateController::GateController(RequestedMode requested_mode)
    : requested_mode_(requested_mode) {
    validate_requested_mode(requested_mode_);
}

GateController::~GateController() noexcept {
    // Destroying the owner ahead of a live scope would turn the scope's later
    // unwind into a use-after-free. Treat that lifetime violation as fatal.
    if (full_scope_depth_ != 0u || inline_scope_depth_ != 0u) {
        std::terminate();
    }
}

GateController::FullScope::~FullScope() noexcept {
    if (owner_ != nullptr) {
        owner_->leave_full_scope();
    }
}

GateController::FullScope::FullScope(FullScope&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)) {}

GateController::InlineScope::~InlineScope() noexcept {
    if (owner_ != nullptr) {
        owner_->leave_inline_scope(saved_full_depth_, token_);
    }
}

GateController::InlineScope::InlineScope(InlineScope&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      saved_full_depth_(std::exchange(other.saved_full_depth_, 0u)),
      token_(std::exchange(other.token_, 0u)) {}

void GateController::set_requested_mode(RequestedMode mode) {
    validate_requested_mode(mode);
    if (activated_ && mode != requested_mode_) {
        throw std::logic_error(
            "cannot change checkpoint-gate mode after activation");
    }
    requested_mode_ = mode;
}

void GateController::bind(
    PointerView pointers,
    std::uint64_t* checkpoint_counter,
    const std::uint64_t* checkpoint_next_slow) {
    if (bound_) {
        throw std::logic_error("checkpoint gate is already bound");
    }
    if (pointers.checkpoint_counter == nullptr ||
        pointers.checkpoint_next_slow == nullptr ||
        checkpoint_counter == nullptr || checkpoint_next_slow == nullptr) {
        throw std::invalid_argument("invalid checkpoint-gate binding");
    }
    if (*pointers.checkpoint_counter != nullptr ||
        *pointers.checkpoint_next_slow != nullptr) {
        throw std::logic_error(
            "checkpoint-gate pointer slots are already owned");
    }

    const bool publish_inline =
        activated_ && requested_mode_ == RequestedMode::Inline &&
        full_scope_depth_ == 0u;
    if (publish_inline &&
        telemetry_.transitions_to_inline ==
            std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "checkpoint-gate inline-transition telemetry overflow");
    }

    pointers_ = pointers;
    checkpoint_counter_ = checkpoint_counter;
    checkpoint_next_slow_ = checkpoint_next_slow;
    bound_ = true;
    inline_published_ = false;
    if (publish_inline) {
        publish_inline_unchecked();
    }
}

void GateController::unbind() {
    if (!bound_) {
        throw std::logic_error("checkpoint gate is not bound");
    }
    if (full_scope_depth_ != 0u || inline_scope_depth_ != 0u) {
        throw std::logic_error(
            "cannot unbind checkpoint gate inside a live scope");
    }
    validate_published_state();
    if (inline_published_) {
        if (telemetry_.transitions_to_full ==
            std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error(
                "checkpoint-gate full-transition telemetry overflow");
        }
        publish_full_unchecked();
    }
    pointers_ = {};
    checkpoint_counter_ = nullptr;
    checkpoint_next_slow_ = nullptr;
    bound_ = false;
}

void GateController::activate() {
    if (requested_mode_ != RequestedMode::Inline || activated_) {
        return;
    }
    validate_published_state();
    const std::uint64_t next_activations = checked_increment(
        telemetry_.activations,
        "checkpoint-gate activation telemetry overflow");
    const bool publish_inline = bound_ && full_scope_depth_ == 0u;
    if (publish_inline &&
        telemetry_.transitions_to_inline ==
            std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "checkpoint-gate inline-transition telemetry overflow");
    }

    activated_ = true;
    telemetry_.activations = next_activations;
    if (publish_inline) {
        publish_inline_unchecked();
    }
}

GateController::FullScope GateController::full_scope() {
    enter_full_scope();
    return FullScope(this);
}

GateController::InlineScope GateController::inline_scope() {
    std::uint64_t saved_full_depth = 0u;
    std::uint64_t token = 0u;
    const bool active = enter_inline_scope(saved_full_depth, token);
    return InlineScope(
        active ? this : nullptr, saved_full_depth, token);
}

void GateController::enter_full_scope() {
    validate_published_state();
    const std::uint64_t next_depth = checked_increment(
        full_scope_depth_, "checkpoint-gate full-scope depth overflow");
    const std::uint64_t next_entries = checked_increment(
        telemetry_.full_scope_entries,
        "checkpoint-gate full-scope telemetry overflow");
    if (inline_published_ &&
        telemetry_.transitions_to_full ==
            std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "checkpoint-gate full-transition telemetry overflow");
    }

    if (inline_published_) {
        publish_full_unchecked();
    }
    full_scope_depth_ = next_depth;
    telemetry_.full_scope_entries = next_entries;
    if (next_depth > telemetry_.maximum_full_scope_depth) {
        telemetry_.maximum_full_scope_depth = next_depth;
    }
}

void GateController::leave_full_scope() noexcept {
    // A scope destructor cannot report a recoverable error: underflow, slot
    // takeover, or counter wrap means the translated-code ABI is no longer
    // trustworthy, so fail closed.
    if (full_scope_depth_ == 0u || !published_state_is_valid()) {
        std::terminate();
    }

    const bool restore_inline =
        full_scope_depth_ == 1u && bound_ && activated_ &&
        requested_mode_ == RequestedMode::Inline;
    if (restore_inline &&
        telemetry_.transitions_to_inline ==
            std::numeric_limits<std::uint64_t>::max()) {
        std::terminate();
    }

    --full_scope_depth_;
    if (restore_inline) {
        publish_inline_unchecked();
    }
}

bool GateController::enter_inline_scope(
    std::uint64_t& saved_full_depth,
    std::uint64_t& token) {
    validate_published_state();
    if (!bound_ || !activated_ ||
        requested_mode_ != RequestedMode::Inline) {
        return false;
    }
    const std::uint64_t next_depth = checked_increment(
        inline_scope_depth_, "checkpoint-gate inline-scope depth overflow");
    const std::uint64_t next_entries = checked_increment(
        telemetry_.inline_scope_entries,
        "checkpoint-gate inline-scope telemetry overflow");
    if (!inline_published_ &&
        telemetry_.transitions_to_inline ==
            std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "checkpoint-gate inline-transition telemetry overflow");
    }

    saved_full_depth = full_scope_depth_;
    token = next_depth;
    inline_scope_depth_ = next_depth;
    full_scope_depth_ = 0u;
    telemetry_.inline_scope_entries = next_entries;
    if (inline_scope_depth_ > telemetry_.maximum_inline_scope_depth) {
        telemetry_.maximum_inline_scope_depth = inline_scope_depth_;
    }
    if (!inline_published_) {
        publish_inline_unchecked();
    }
    return true;
}

void GateController::leave_inline_scope(
    std::uint64_t saved_full_depth,
    std::uint64_t token) noexcept {
    // FullScopes created inside this translated-execution region must unwind
    // first. Otherwise restoring an enclosing depth would lose ownership and
    // could expose the fast gate during host callback work.
    if (inline_scope_depth_ == 0u || token != inline_scope_depth_ ||
        full_scope_depth_ != 0u ||
        !published_state_is_valid()) {
        std::terminate();
    }

    if (saved_full_depth != 0u &&
        telemetry_.transitions_to_full ==
            std::numeric_limits<std::uint64_t>::max()) {
        std::terminate();
    }

    if (saved_full_depth != 0u) {
        publish_full_unchecked();
    }
    --inline_scope_depth_;
    full_scope_depth_ = saved_full_depth;
}

void GateController::validate_published_state() const {
    if (!published_state_is_valid()) {
        throw std::logic_error(
            "checkpoint-gate pointer slots changed outside controller");
    }
}

bool GateController::published_state_is_valid() const noexcept {
    if ((inline_published_ && full_scope_depth_ != 0u) ||
        (inline_scope_depth_ != 0u &&
         (!bound_ || !activated_ ||
          requested_mode_ != RequestedMode::Inline)) ||
        (bound_ && activated_ &&
         requested_mode_ == RequestedMode::Inline &&
         full_scope_depth_ == 0u && !inline_published_)) {
        return false;
    }
    if (!bound_) {
        return !inline_published_ &&
            pointers_.checkpoint_counter == nullptr &&
            pointers_.checkpoint_next_slow == nullptr &&
            checkpoint_counter_ == nullptr && checkpoint_next_slow_ == nullptr;
    }
    if (pointers_.checkpoint_counter == nullptr ||
        pointers_.checkpoint_next_slow == nullptr ||
        checkpoint_counter_ == nullptr || checkpoint_next_slow_ == nullptr) {
        return false;
    }
    if (inline_published_) {
        return *pointers_.checkpoint_counter == checkpoint_counter_ &&
            *pointers_.checkpoint_next_slow == checkpoint_next_slow_;
    }
    return *pointers_.checkpoint_counter == nullptr &&
        *pointers_.checkpoint_next_slow == nullptr;
}

void GateController::publish_full_unchecked() noexcept {
    // The counter pointer is the generated-code enable bit. Clear it first so
    // a partially observed transition can only take the conservative path.
    *pointers_.checkpoint_counter = nullptr;
    *pointers_.checkpoint_next_slow = nullptr;
    inline_published_ = false;
    ++telemetry_.transitions_to_full;
}

void GateController::publish_inline_unchecked() noexcept {
    // Publish the threshold first and the counter pointer last. Generated code
    // therefore cannot observe an enabled gate without a valid threshold.
    *pointers_.checkpoint_next_slow = checkpoint_next_slow_;
    *pointers_.checkpoint_counter = checkpoint_counter_;
    inline_published_ = true;
    ++telemetry_.transitions_to_inline;
}

}  // namespace galaxy::checkpoint
