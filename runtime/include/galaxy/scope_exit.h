#pragma once

#include <type_traits>
#include <utility>

namespace galaxy {

// Runs `cleanup` when the scope ends, by return or by an exception. The cleanup cannot throw and the guard
// cannot move, so each scope runs its cleanup exactly once.
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
