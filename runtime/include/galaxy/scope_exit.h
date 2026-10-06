#pragma once

#include <type_traits>
#include <utility>

namespace galaxy {

// For cleanup that is required on both normal return and native unwinding.
// The callback cannot throw and the guard cannot move, so a live scope owns
// exactly one cleanup. This does not intercept or replace an exception.
template <typename Cleanup>
class ScopeExit final {
    static_assert(std::is_nothrow_invocable_v<Cleanup&>);

public:
    explicit ScopeExit(Cleanup cleanup)
        noexcept(std::is_nothrow_move_constructible_v<Cleanup>)
        : cleanup_(std::move(cleanup)) {}

    ~ScopeExit() noexcept { cleanup_(); }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit(ScopeExit&&) = delete;
    ScopeExit& operator=(ScopeExit&&) = delete;

private:
    Cleanup cleanup_;
};

// Own noexcept rollback only until the whole callback has returned normally.
// A later caller failure must not roll back this already completed boundary.
// The original native exception propagates through ordinary C++ unwinding.
template <typename Callback, typename Cleanup>
void run_with_exception_cleanup(Callback&& callback, Cleanup cleanup) {
    static_assert(std::is_nothrow_invocable_v<Cleanup&>);
    bool completed = false;
    const ScopeExit scope([&]() noexcept {
        if (!completed) cleanup();
    });
    std::forward<Callback>(callback)();
    completed = true;
}

// Only omit the handler when its owner has proved every failure postlude is
// empty. Ordinary C++ unwinding and all callback/local destructors still run.
// A required handler retains catch/rethrow semantics, including a fallible
// diagnostic handler replacing an exception exactly as before.
template <typename Callback, typename Handler>
void run_with_optional_exception_handler(
    bool required, Callback&& callback, Handler&& handler) {
    if (!required) {
        std::forward<Callback>(callback)();
        return;
    }
    try {
        std::forward<Callback>(callback)();
    } catch (...) {
        std::forward<Handler>(handler)();
        throw;
    }
}

}  // namespace galaxy
