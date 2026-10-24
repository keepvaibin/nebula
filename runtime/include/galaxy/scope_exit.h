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

}  // namespace galaxy
