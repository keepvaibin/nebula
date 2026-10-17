#pragma once

#include <chrono>
#include <cstdint>

namespace galaxy::output {

// When standard output or error is a pipe or a file (a diagnostics recording's launcher pipe, a redirected
// log), a write blocks its thread for as long as the reader or the disk is slow, and every other thread that
// logs then waits on that one: a launcher reader paused for a second froze the game for a second. The relay
// gives each such stream a private pipe that one thread drains into memory as soon as bytes arrive and a
// second thread forwards to the original destination, so no game thread ever waits on the consumer. Streams
// on a console or with no destination are left alone. Call once, before other threads start.
void start_relay() noexcept;

// Waits until everything written so far has reached the original destinations, at most `limit`. Exit and
// crash paths call it so the last lines are not lost.
void drain_relay(std::chrono::milliseconds limit) noexcept;

// Bytes the relay dropped because more than its memory cap was waiting on a stalled consumer.
[[nodiscard]] std::uint64_t relay_dropped_bytes() noexcept;

}  // namespace galaxy::output
