#include "galaxy/host_pointer_events.h"
#include "galaxy/content_viewport.h"
#include "galaxy/native_mouse_pointer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>

namespace {

using galaxy::host::HostPointerButton;
using galaxy::host::HostPointerCaptureCleanupDecision;
using galaxy::host::HostPointerCoordinates;
using galaxy::host::HostPointerCursorInsidePublicationResult;
using galaxy::host::HostPointerCursorOutsidePublicationResult;
using galaxy::host::HostPointerCursorPollMode;
using galaxy::host::HostPointerCursorPollSelection;
using galaxy::host::HostPointerCursorPollSelectionInput;
using galaxy::host::HostPointerEventRing;
using galaxy::host::HostPointerFallbackInsidePublicationResult;
using galaxy::host::HostPointerSnapshot;
using galaxy::host::HostPointerSnapshotCache;
using galaxy::host::HostPointerTransition;
using galaxy::host::HostPointerTransitionKind;
using galaxy::host::HostPointerTransitionReadResult;

bool expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

constexpr DWORD kExpectedSequenceExhaustionExitCode = 90u;
constexpr DWORD kUnexpectedSequenceExhaustionReturnExitCode = 91u;
HostPointerSnapshot* s_exhaustion_snapshot = nullptr;
std::uint64_t s_expected_exhaustion_event_sequence = 0;
std::uint64_t s_expected_exhaustion_absolute_sequence = 0;

[[noreturn]] void exit_expected_sequence_exhaustion() noexcept {
    const bool no_partial_advance =
        s_exhaustion_snapshot != nullptr &&
        s_exhaustion_snapshot->event_sequence ==
            s_expected_exhaustion_event_sequence &&
        s_exhaustion_snapshot->absolute_sequence ==
            s_expected_exhaustion_absolute_sequence;
    ExitProcess(no_partial_advance ? kExpectedSequenceExhaustionExitCode : 94u);
}

int run_sequence_exhaustion_child(std::string_view category) {
    SetErrorMode(
        SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
        SEM_NOOPENFILEERRORBOX);
    std::set_terminate(&exit_expected_sequence_exhaustion);
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    HostPointerSnapshot snapshot{};
    snapshot.coordinates = {true, true, 320, 200, 854, 480};
    const auto arm_exhaustion_observation = [&snapshot]() noexcept {
        s_exhaustion_snapshot = &snapshot;
        s_expected_exhaustion_event_sequence = snapshot.event_sequence;
        s_expected_exhaustion_absolute_sequence = snapshot.absolute_sequence;
    };

    if (category == "message-event") {
        snapshot.event_sequence = maximum;
        arm_exhaustion_observation();
        galaxy::host::publish_host_pointer_client_coordinate(
            snapshot, {321, 201, 854, 480, 10'000u});
    } else if (category == "message-absolute") {
        snapshot.absolute_sequence = maximum;
        arm_exhaustion_observation();
        galaxy::host::publish_host_pointer_client_coordinate(
            snapshot, {321, 201, 854, 480, 10'000u});
    } else if (category == "seed-absolute") {
        snapshot.absolute_sequence = maximum;
        arm_exhaustion_observation();
        galaxy::host::publish_host_pointer_seed(
            snapshot, {1280, 720, true, 10'000u});
    } else if (category == "resize-absolute") {
        snapshot.absolute_sequence = maximum;
        arm_exhaustion_observation();
        galaxy::host::publish_host_pointer_seed(
            snapshot, {640, 480, false, 10'000u});
    } else if (category == "move-absolute") {
        snapshot.absolute_sequence = maximum;
        arm_exhaustion_observation();
        galaxy::host::invalidate_host_pointer_geometry(snapshot);
    } else if (category == "inside-event") {
        snapshot.event_sequence = maximum;
        arm_exhaustion_observation();
        (void)galaxy::host::apply_host_pointer_cursor_inside_publication(
            snapshot, {true, 321, 201, 10'000u, maximum, 854, 480, snapshot.absolute_sequence});
    } else if (category == "inside-absolute") {
        snapshot.event_sequence = 7u;
        snapshot.absolute_sequence = maximum;
        arm_exhaustion_observation();
        (void)galaxy::host::apply_host_pointer_cursor_inside_publication(
            snapshot, {true, 321, 201, 10'000u, 7u, 854, 480, snapshot.absolute_sequence});
    } else if (category == "outside-event") {
        snapshot.event_sequence = maximum;
        arm_exhaustion_observation();
        (void)galaxy::host::apply_host_pointer_cursor_outside_publication(
            snapshot, {true, false, true, maximum, 854, 480, snapshot.absolute_sequence});
    } else if (category == "window-loss-event") {
        snapshot.event_sequence = maximum;
        arm_exhaustion_observation();
        galaxy::host::publish_host_pointer_window_visibility_loss(snapshot);
    } else if (category == "fallback-event") {
        snapshot.event_sequence = maximum;
        arm_exhaustion_observation();
        (void)galaxy::host::apply_host_pointer_fallback_inside_publication(
            snapshot, {321, 201, 854, 480, 10'000u, maximum, snapshot.absolute_sequence});
    } else if (category == "fallback-absolute") {
        snapshot.event_sequence = 7u;
        snapshot.absolute_sequence = maximum;
        arm_exhaustion_observation();
        (void)galaxy::host::apply_host_pointer_fallback_inside_publication(
            snapshot, {321, 201, 854, 480, 10'000u, 7u, snapshot.absolute_sequence});
    } else {
        return 92;
    }
    return static_cast<int>(kUnexpectedSequenceExhaustionReturnExitCode);
}

bool sequence_exhaustion_case_hard_fails(std::string_view category) {
    wchar_t executable[MAX_PATH]{};
    const DWORD executable_length = GetModuleFileNameW(
        nullptr, executable, static_cast<DWORD>(MAX_PATH));
    if (executable_length == 0u ||
        executable_length >= static_cast<DWORD>(MAX_PATH)) {
        return expect(false, "test executable path is available for exhaustion child");
    }

    std::wstring wide_category(category.begin(), category.end());
    std::wstring command_line = L"\"";
    command_line.append(executable, executable_length);
    command_line += L"\" --sequence-exhaustion ";
    command_line += wide_category;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        executable,
        command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process);
    if (created == FALSE) {
        return expect(false, "sequence exhaustion child process launches");
    }

    const DWORD wait_result = WaitForSingleObject(process.hProcess, 5'000u);
    DWORD exit_code = STILL_ACTIVE;
    if (wait_result == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &exit_code);
    } else {
        TerminateProcess(process.hProcess, 93u);
        WaitForSingleObject(process.hProcess, 1'000u);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    const std::string message =
        "sequence exhaustion hard-fails without wrapping: " +
        std::string(category);
    return expect(
        wait_result == WAIT_OBJECT_0 &&
            exit_code == kExpectedSequenceExhaustionExitCode,
        message);
}

HostPointerCoordinates coordinates(
    std::int32_t x,
    std::int32_t y,
    std::int32_t width = 854,
    std::int32_t height = 480,
    bool absolute_valid = true,
    bool inside_client = true) {
    return {
        absolute_valid,
        inside_client,
        x,
        y,
        width,
        height,
    };
}

constexpr std::uint64_t transition_acquired_ms(
    std::uint64_t sequence) noexcept {
    return 50'000u + sequence * 17u;
}

bool same_coordinates(
    const HostPointerCoordinates& actual,
    const HostPointerCoordinates& expected) {
    return actual.absolute_valid == expected.absolute_valid &&
        actual.inside_client == expected.inside_client &&
        actual.client_x == expected.client_x &&
        actual.client_y == expected.client_y &&
        actual.client_width == expected.client_width &&
        actual.client_height == expected.client_height;
}

struct AcceptedCursorPollBaseline {
    bool valid = false;
    std::int32_t x = 0;
    std::int32_t y = 0;
};

HostPointerCursorPollSelection select_cursor_poll(
    const AcceptedCursorPollBaseline& baseline,
    std::int32_t candidate_x,
    std::int32_t candidate_y,
    bool reject_as_stale_center,
    bool recovers_visibility,
    bool authoritative) noexcept {
    return galaxy::host::select_host_pointer_cursor_poll({
        baseline.valid,
        baseline.x,
        baseline.y,
        candidate_x,
        candidate_y,
        reject_as_stale_center,
        recovers_visibility,
        authoritative,
        baseline.valid, baseline.x, baseline.y,
    });
}

void commit_published_cursor_poll(
    AcceptedCursorPollBaseline& baseline,
    const HostPointerCursorPollSelection& selection,
    std::int32_t candidate_x,
    std::int32_t candidate_y,
    bool publication_succeeded = true) noexcept {
    if (!selection.should_publish || !publication_succeeded) {
        return;
    }
    baseline = {true, candidate_x, candidate_y};
}

bool host_pointer_poll_cache_expires_on_precise_fake_clock() {
    // The old GetTickCount64 identity stays constant while precise time and
    // cursor coordinates advance. Exercise whole-snapshot reuse, not sleeps.
    constexpr std::uint64_t kStartNs = 5'000'000'000u;
    constexpr std::uint64_t kUnchangedCoarseIdentityMs = 1'000u;
    HostPointerSnapshot cached{};
    std::uint64_t cached_ns = 0u;
    std::uint64_t acquisitions = 0u;
    const auto acquire = [&cached, &cached_ns, &acquisitions](
                             std::uint64_t now_ns,
                             std::int32_t x,
                             std::uint64_t configured_ms) {
        if (galaxy::host::host_pointer_poll_cache_fresh(
                now_ns, cached_ns, configured_ms)) {
            return cached;
        }
        cached.coordinates = coordinates(x, 200);
        cached.absolute_sequence = ++acquisitions;
        cached.absolute_acquired_ms = kUnchangedCoarseIdentityMs;
        cached_ns = now_ns;
        return cached;
    };

    const auto first = acquire(kStartNs, 100, 1u);
    const auto before_expiry = acquire(kStartNs + 999'999u, 200, 1u);
    const auto at_expiry = acquire(kStartNs + 1'000'000u, 200, 1u);
    const auto coarse_clock_still_unchanged =
        acquire(kStartNs + 15'600'000u, 300, 1u);
    bool passed = expect(
        first.coordinates.client_x == 100 && first.absolute_sequence == 1u &&
            before_expiry.coordinates.client_x == 100 &&
            before_expiry.absolute_sequence == 1u,
        "precise pointer cache reuses a coherent snapshot only before 1 ms");
    passed &= expect(
        at_expiry.coordinates.client_x == 200 &&
            at_expiry.absolute_sequence == 2u &&
            at_expiry.absolute_acquired_ms == first.absolute_acquired_ms,
        "pointer coordinates refresh at exactly 1 ms without changing the coarse identity clock");
    passed &= expect(
        coarse_clock_still_unchanged.coordinates.client_x == 300 &&
            coarse_clock_still_unchanged.absolute_sequence == 3u &&
            coarse_clock_still_unchanged.absolute_acquired_ms ==
                first.absolute_acquired_ms,
        "unchanged 15.6 ms coarse clock cannot retain a stale 1 ms snapshot");

    const auto disabled_first = acquire(kStartNs + 15'600'000u, 400, 0u);
    const auto disabled_second = acquire(kStartNs + 15'600'000u, 500, 0u);
    passed &= expect(
        disabled_first.coordinates.client_x == 400 &&
            disabled_second.coordinates.client_x == 500 && acquisitions == 5u,
        "zero pointer-cache duration reacquires even at identical clock values");
    return passed;
}

bool host_pointer_poll_cache_rejects_invalid_clock_and_caps_duration() {
    using galaxy::host::host_pointer_poll_cache_fresh;
    constexpr std::uint64_t kAnchorNs = 5'000'000'000u;
    constexpr std::uint64_t kMaximum =
        std::numeric_limits<std::uint64_t>::max();
    bool passed = expect(
        !host_pointer_poll_cache_fresh(kAnchorNs, 0u, 1u) &&
            !host_pointer_poll_cache_fresh(0u, kAnchorNs, 1u) &&
            !host_pointer_poll_cache_fresh(kAnchorNs - 1u, kAnchorNs, 1u),
        "missing, future, and backwards cache timestamps force acquisition");
    passed &= expect(
        host_pointer_poll_cache_fresh(
            kAnchorNs + 15'999'999u, kAnchorNs, 17u) &&
            !host_pointer_poll_cache_fresh(
                kAnchorNs + 16'000'000u, kAnchorNs, 17u) &&
            host_pointer_poll_cache_fresh(
                kAnchorNs + 15'999'999u, kAnchorNs, kMaximum) &&
            !host_pointer_poll_cache_fresh(
                kAnchorNs + 16'000'000u, kAnchorNs, kMaximum),
        "oversized pointer-cache durations retain the 16 ms cap without multiplication overflow");
    passed &= expect(
        host_pointer_poll_cache_fresh(
            kMaximum, kMaximum - 999'999u, 1u) &&
            !host_pointer_poll_cache_fresh(
                kMaximum, kMaximum - 1'000'000u, 1u) &&
            !host_pointer_poll_cache_fresh(1u, kMaximum, 1u),
        "precise cache expiry is safe near the unsigned clock limit");
    return passed;
}

bool rejected_poll_is_reconsidered_after_authority_timeout() {
    // A was previously accepted and is also the coordinate in the fresh WM
    // snapshot. B arrives through GetCursorPos before that message ages out.
    AcceptedCursorPollBaseline baseline{true, 100, 100};
    constexpr std::int32_t kCandidateX = 420;
    constexpr std::int32_t kCandidateY = 260;

    const HostPointerCursorPollSelection while_wm_is_authoritative =
        select_cursor_poll(
            baseline,
            kCandidateX,
            kCandidateY,
            false,
            false,
            false);
    bool passed = expect(
        while_wm_is_authoritative.candidate_changed,
        "new cursor poll is detected while the WM coordinate is fresh");
    passed &= expect(
        !while_wm_is_authoritative.should_publish,
        "fresh WM coordinate rejects a conflicting cursor poll");
    commit_published_cursor_poll(
        baseline,
        while_wm_is_authoritative,
        kCandidateX,
        kCandidateY);
    passed &= expect(
        baseline.x == 100 && baseline.y == 100,
        "rejected cursor poll does not consume the accepted baseline");

    const HostPointerCursorPollSelection after_authority_timeout =
        select_cursor_poll(
            baseline,
            kCandidateX,
            kCandidateY,
            false,
            false,
            true);
    passed &= expect(
        after_authority_timeout.candidate_changed,
        "unchanged rejected poll remains changed after authority timeout");
    passed &= expect(
        after_authority_timeout.should_publish,
        "same cursor poll is accepted after authority timeout");
    commit_published_cursor_poll(
        baseline,
        after_authority_timeout,
        kCandidateX,
        kCandidateY);
    passed &= expect(
        baseline.x == kCandidateX && baseline.y == kCandidateY,
        "successful cursor publication commits its accepted baseline");

    const HostPointerCursorPollSelection unchanged_after_publication =
        select_cursor_poll(
            baseline,
            kCandidateX,
            kCandidateY,
            false,
            false,
            true);
    passed &= expect(
        !unchanged_after_publication.candidate_changed &&
            !unchanged_after_publication.should_publish,
        "accepted unchanged poll does not republish without recovery");
    return passed;
}

bool stale_center_poll_is_reconsidered_after_rejection_timeout() {
    AcceptedCursorPollBaseline baseline{true, 700, 100};
    constexpr std::int32_t kCenterX = 426;
    constexpr std::int32_t kCenterY = 239;

    const HostPointerCursorPollSelection during_stale_center_window =
        select_cursor_poll(
            baseline,
            kCenterX,
            kCenterY,
            true,
            false,
            true);
    bool passed = expect(
        during_stale_center_window.candidate_changed &&
            !during_stale_center_window.should_publish,
        "stale-center guard rejects an otherwise authoritative changed poll");
    commit_published_cursor_poll(
        baseline,
        during_stale_center_window,
        kCenterX,
        kCenterY);
    passed &= expect(
        baseline.x == 700 && baseline.y == 100,
        "stale-center rejection preserves the accepted baseline");

    const HostPointerCursorPollSelection after_stale_center_timeout =
        select_cursor_poll(
            baseline,
            kCenterX,
            kCenterY,
            false,
            false,
            true);
    passed &= expect(
        after_stale_center_timeout.candidate_changed &&
            after_stale_center_timeout.should_publish,
        "same centered poll is accepted after stale-center timeout");
    return passed;
}

bool non_authoritative_visibility_recovery_cannot_override_wndproc() {
    const AcceptedCursorPollBaseline baseline{true, 320, 240};
    const HostPointerCursorPollSelection selection = select_cursor_poll(
        baseline,
        320,
        240,
        false,
        true,
        false);
    return expect(
        !selection.candidate_changed && !selection.should_publish,
        "visibility recovery cannot bypass recent WndProc authority");
}

bool stalled_client_messages_do_not_throttle_changed_cursor_polls() {
    constexpr std::uint64_t kLastClientMessageMs = 1'000u;
    constexpr std::uint64_t kMessageRecentMs = 120u;
    constexpr std::uint64_t kStaleCenterGuardMs = 5'000u;
    AcceptedCursorPollBaseline baseline{true, 100, 100};

    const auto first_timing =
        galaxy::host::classify_host_pointer_client_message_timing(
            1'121u,
            kLastClientMessageMs,
            kMessageRecentMs,
            kStaleCenterGuardMs);
    const HostPointerCursorPollSelection first = select_cursor_poll(
        baseline,
        200,
        200,
        false,
        false,
        !first_timing.recent);
    bool passed = expect(
        first_timing.age_valid && !first_timing.recent,
        "a client-message stall longer than 120 ms makes cursor polling authoritative");
    passed &= expect(
        first_timing.stale_center_guard_active,
        "the longer stale-center guard remains active after authority changes");
    passed &= expect(
        first.should_publish,
        "first changed cursor coordinate publishes after the message stall");
    commit_published_cursor_poll(baseline, first, 200, 200);

    // The first cursor publication has an acquisition time of 1121 ms, but it
    // must not alter kLastClientMessageMs. A changed sample one millisecond
    // later therefore remains authoritative instead of waiting another 120 ms.
    const auto second_timing =
        galaxy::host::classify_host_pointer_client_message_timing(
            1'122u,
            kLastClientMessageMs,
            kMessageRecentMs,
            kStaleCenterGuardMs);
    const HostPointerCursorPollSelection second = select_cursor_poll(
        baseline,
        201,
        201,
        false,
        false,
        !second_timing.recent);
    passed &= expect(
        !second_timing.recent && second.should_publish,
        "successive changed cursor coordinates publish at normal poll cadence");

    const HostPointerCursorPollSelection stale_center = select_cursor_poll(
        baseline,
        426,
        239,
        second_timing.stale_center_guard_active,
        false,
        !second_timing.recent);
    passed &= expect(
        !stale_center.should_publish,
        "normal cursor-poll cadence does not bypass stale-center rejection");

    const auto expired_guard =
        galaxy::host::classify_host_pointer_client_message_timing(
            6'001u,
            kLastClientMessageMs,
            kMessageRecentMs,
            kStaleCenterGuardMs);
    passed &= expect(
        !expired_guard.stale_center_guard_active,
        "stale-center rejection expires only from the client-message clock");
    return passed;
}

bool local_cursor_poll_is_decoupled_from_the_window_message_pump() {
    const auto local_recent =
        galaxy::host::select_host_pointer_cursor_poll_authority(
            HostPointerCursorPollMode::Adaptive,
            false,
            true);
    bool passed = expect(
        local_recent.poll_enabled && local_recent.authoritative &&
            !local_recent.stale_center_guard_allowed,
        "adaptive local cursor polling stays authoritative while a client message is recent");

    // Model a renderer that pumps one fresh WM_MOUSEMOVE and then takes much
    // longer than the native HID period to render. Every changed OS cursor
    // sample must still publish instead of being pinned to that render-thread
    // message for the full recent-message window.
    AcceptedCursorPollBaseline baseline{true, 100, 100};
    for (std::int32_t coordinate = 101; coordinate <= 120; ++coordinate) {
        const HostPointerCursorPollSelection selection = select_cursor_poll(
            baseline,
            coordinate,
            coordinate,
            false,
            false,
            local_recent.authoritative);
        passed &= expect(
            selection.should_publish,
            "each changed local cursor sample publishes at the native HID cadence");
        commit_published_cursor_poll(
            baseline,
            selection,
            coordinate,
            coordinate);
    }
    passed &= expect(
        baseline.x == 120 && baseline.y == 120,
        "local cursor polling reaches the newest sample without waiting for another render message");

    const auto remote_recent =
        galaxy::host::select_host_pointer_cursor_poll_authority(
            HostPointerCursorPollMode::Adaptive,
            true,
            true);
    passed &= expect(
        remote_recent.poll_enabled && !remote_recent.authoritative &&
            remote_recent.stale_center_guard_allowed,
        "adaptive remote polling preserves a recent live client message over a possibly stale centered poll");

    const auto remote_stalled =
        galaxy::host::select_host_pointer_cursor_poll_authority(
            HostPointerCursorPollMode::Adaptive,
            true,
            false);
    passed &= expect(
        remote_stalled.poll_enabled && remote_stalled.authoritative &&
            remote_stalled.stale_center_guard_allowed,
        "adaptive remote polling takes over after client messages stall while retaining its stale-center guard");

    const auto forced_remote =
        galaxy::host::select_host_pointer_cursor_poll_authority(
            HostPointerCursorPollMode::Forced,
            true,
            true);
    passed &= expect(
        forced_remote.poll_enabled && forced_remote.authoritative &&
            !forced_remote.stale_center_guard_allowed,
        "forced cursor polling is authoritative even in a remote session");

    const auto disabled =
        galaxy::host::select_host_pointer_cursor_poll_authority(
            HostPointerCursorPollMode::Disabled,
            false,
            false);
    passed &=
        expect(!disabled.poll_enabled && !disabled.authoritative &&
                   !disabled.stale_center_guard_allowed,
               "disabled cursor polling attempts no cached-event override");
    return passed;
}

bool authoritative_outside_poll_publishes_known_sensor_loss() {
    HostPointerSnapshot snapshot{};
    snapshot.coordinates = coordinates(640, 120);
    snapshot.absolute_sequence = 31u;
    snapshot.event_sequence = 17u;
    snapshot.absolute_acquired_ms = 9'000u;
    snapshot.last_client_message_ms = 8'900u;
    snapshot.leave_sequence = 4u;

    const HostPointerCursorOutsidePublicationResult result =
        galaxy::host::apply_host_pointer_cursor_outside_publication(
            snapshot, {true, false, true, 17u, 854, 480, snapshot.absolute_sequence});
    bool passed = expect(
        result == HostPointerCursorOutsidePublicationResult::PublishedOutside,
        "authoritative successful outside poll publishes sensor-bar loss");
    passed &= expect(snapshot.coordinates.absolute_valid &&
                         !snapshot.coordinates.inside_client,
                     "outside publication preserves a valid coordinate while "
                     "clearing visibility");
    passed &= expect(snapshot.coordinates.client_x == 640 &&
                         snapshot.coordinates.client_y == 120,
                     "outside publication does not invent or clamp a "
                     "replacement coordinate");
    passed &= expect(
        snapshot.event_sequence == 18u && snapshot.leave_sequence == 18u,
        "outside publication advances the event identity and records its "
        "leave epoch");
    passed &= expect(snapshot.absolute_sequence == 31u &&
                         snapshot.absolute_acquired_ms == 9'000u &&
                         snapshot.last_client_message_ms == 8'900u,
                     "visibility-only publication does not forge coordinate or "
                     "WndProc freshness");

    const HostPointerCursorOutsidePublicationResult repeated =
        galaxy::host::apply_host_pointer_cursor_outside_publication(
            snapshot, {true, false, true, 18u, 854, 480, snapshot.absolute_sequence});
    passed &= expect(
        repeated == HostPointerCursorOutsidePublicationResult::AlreadyOutside &&
            snapshot.event_sequence == 18u,
        "unchanged outside polls do not republish or churn sequence state");
    return passed;
}

bool window_visibility_loss_invalidates_an_in_flight_cursor_poll() {
    HostPointerSnapshot snapshot{};
    snapshot.coordinates = coordinates(320, 200);
    snapshot.absolute_sequence = 11u;
    snapshot.event_sequence = 7u;
    snapshot.absolute_acquired_ms = 4'000u;

    galaxy::host::publish_host_pointer_window_visibility_loss(snapshot);
    bool passed = expect(
        !snapshot.coordinates.inside_client &&
            snapshot.event_sequence == 8u && snapshot.leave_sequence == 8u,
        "one authoritative window visibility loss advances its event identity exactly once");

    const HostPointerCursorInsidePublicationResult stale_poll =
        galaxy::host::apply_host_pointer_cursor_inside_publication(
            snapshot,
            {
                true,
                330,
                210,
                4'001u,
                7u,
                854,
                480,
                snapshot.absolute_sequence,
            });
    passed &= expect(
        stale_poll == HostPointerCursorInsidePublicationResult::Rejected &&
            !snapshot.coordinates.inside_client &&
            snapshot.event_sequence == 8u && snapshot.absolute_sequence == 11u,
        "a poll classified before the newer window loss cannot resurrect pointer visibility");

    // Focus loss is a distinct authoritative window event even when a prior
    // leave already made the pointer unavailable. It must invalidate a poll
    // that began in the intervening outside state.
    galaxy::host::publish_host_pointer_window_visibility_loss(snapshot);
    passed &= expect(
        !snapshot.coordinates.inside_client &&
            snapshot.event_sequence == 9u && snapshot.leave_sequence == 9u,
        "a later focus loss advances exactly one new event identity while already outside");
    const HostPointerCursorInsidePublicationResult pre_focus_poll =
        galaxy::host::apply_host_pointer_cursor_inside_publication(
            snapshot,
            {
                true,
                320,
                200,
                4'002u,
                8u,
                854,
                480,
                snapshot.absolute_sequence,
            });
    passed &= expect(
        pre_focus_poll == HostPointerCursorInsidePublicationResult::Rejected &&
            !snapshot.coordinates.inside_client &&
            snapshot.event_sequence == 9u && snapshot.absolute_sequence == 11u,
        "a poll started before focus loss cannot publish after that focus event");
    return passed;
}

bool unchanged_coordinate_reacquisition_advances_absolute_identity() {
    HostPointerSnapshot snapshot{};
    snapshot.coordinates = coordinates(320, 200, 854, 480, true, false);
    snapshot.absolute_sequence = 31u;
    snapshot.event_sequence = 18u;
    snapshot.absolute_acquired_ms = 9'000u;
    snapshot.leave_sequence = 18u;

    const AcceptedCursorPollBaseline baseline{true, 320, 200};
    const HostPointerCursorPollSelection selection = select_cursor_poll(
        baseline, 320, 200, false, true, true);
    bool passed = expect(
        !selection.candidate_changed && selection.should_publish,
        "an authoritative unchanged cursor sample may reacquire visibility");

    const HostPointerCursorInsidePublicationResult result =
        galaxy::host::apply_host_pointer_cursor_inside_publication(
            snapshot,
            {
                selection.should_publish,
                320,
                200,
                9'100u,
                18u,
                854,
                480,
                snapshot.absolute_sequence,
            });
    passed &= expect(
        result == HostPointerCursorInsidePublicationResult::PublishedInside &&
            snapshot.coordinates.inside_client &&
            snapshot.coordinates.client_x == 320 &&
            snapshot.coordinates.client_y == 200,
        "the selected unchanged sample restores visibility without changing coordinates");
    passed &= expect(
        snapshot.event_sequence == 19u &&
            snapshot.absolute_sequence == 32u &&
            snapshot.absolute_acquired_ms == 9'100u,
        "reacquisition advances the event and coherent absolute publication identities");
    return passed;
}

bool snapshot_sequence_boundaries_are_fail_closed_for_every_publisher() {
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    bool passed = true;

    HostPointerSnapshot message{};
    message.event_sequence = maximum - 1u;
    message.absolute_sequence = maximum - 1u;
    galaxy::host::publish_host_pointer_client_coordinate(
        message, {-1, 200, 854, 480, 20'000u});
    passed &= expect(
        message.event_sequence == maximum &&
            message.absolute_sequence == maximum &&
            message.leave_sequence == maximum &&
            !message.coordinates.inside_client,
        "coordinate-bearing client message reaches UINT64_MAX without wrapping either identity");

    HostPointerSnapshot seed{};
    seed.event_sequence = 17u;
    seed.absolute_sequence = maximum - 1u;
    galaxy::host::publish_host_pointer_seed(
        seed, {1280, 720, true, 20'001u});
    passed &= expect(
        seed.event_sequence == 17u && seed.absolute_sequence == maximum &&
            seed.leave_sequence == 17u &&
            seed.coordinates.client_x == 639 &&
            seed.coordinates.client_y == 359 &&
            !seed.coordinates.inside_client,
        "changed seed geometry reaches UINT64_MAX once without inventing an event identity");

    HostPointerSnapshot unchanged_seed{};
    unchanged_seed.coordinates = coordinates(320, 200, 854, 480, true, false);
    unchanged_seed.event_sequence = 19u;
    unchanged_seed.absolute_sequence = maximum;
    galaxy::host::publish_host_pointer_seed(
        unchanged_seed, {854, 480, false, 20'002u});
    passed &= expect(
        unchanged_seed.event_sequence == 19u &&
            unchanged_seed.absolute_sequence == maximum &&
            unchanged_seed.coordinates.client_x == 320 &&
            unchanged_seed.coordinates.client_y == 200,
        "unchanged resize seed performs no sequence advance at UINT64_MAX");

    HostPointerSnapshot resized{};
    resized.coordinates = coordinates(800, 200, 854, 480, true, true);
    resized.event_sequence = 23u;
    resized.absolute_sequence = maximum - 1u;
    galaxy::host::publish_host_pointer_seed(
        resized, {640, 480, false, 20'002u});
    passed &= expect(
        resized.event_sequence == 23u &&
            resized.absolute_sequence == maximum &&
            resized.coordinates.client_x == 639 &&
            resized.coordinates.client_y == 200 &&
            resized.coordinates.inside_client,
        "non-forced resize reaches UINT64_MAX once while preserving acquired visibility");

    HostPointerSnapshot inside{};
    inside.coordinates = coordinates(320, 200, 854, 480, true, false);
    inside.event_sequence = maximum - 1u;
    inside.absolute_sequence = maximum - 1u;
    const HostPointerCursorInsidePublicationResult inside_result =
        galaxy::host::apply_host_pointer_cursor_inside_publication(
            inside,
            {
                true,
                320,
                200,
                20'003u,
                maximum - 1u,
                854,
                480,
                inside.absolute_sequence,
            });
    passed &= expect(
        inside_result ==
                HostPointerCursorInsidePublicationResult::PublishedInside &&
            inside.event_sequence == maximum &&
            inside.absolute_sequence == maximum &&
            inside.coordinates.inside_client,
        "inside cursor publication reaches UINT64_MAX without wrapping either identity");

    HostPointerSnapshot outside{};
    outside.coordinates = coordinates(320, 200);
    outside.event_sequence = maximum - 1u;
    outside.absolute_sequence = 77u;
    const HostPointerCursorOutsidePublicationResult outside_result =
        galaxy::host::apply_host_pointer_cursor_outside_publication(
            outside, {true, false, true, maximum - 1u, 854, 480, outside.absolute_sequence});
    passed &= expect(
        outside_result ==
                HostPointerCursorOutsidePublicationResult::PublishedOutside &&
            outside.event_sequence == maximum &&
            outside.leave_sequence == maximum &&
            outside.absolute_sequence == 77u,
        "outside cursor publication reaches UINT64_MAX only in the event domain");

    HostPointerSnapshot window_loss{};
    window_loss.coordinates = coordinates(320, 200);
    window_loss.event_sequence = maximum - 1u;
    window_loss.absolute_sequence = 79u;
    galaxy::host::publish_host_pointer_window_visibility_loss(window_loss);
    passed &= expect(
        window_loss.event_sequence == maximum &&
            window_loss.leave_sequence == maximum &&
            window_loss.absolute_sequence == 79u &&
            !window_loss.coordinates.inside_client,
        "window visibility loss reaches UINT64_MAX only in the event domain");

    HostPointerSnapshot fallback{};
    fallback.event_sequence = maximum - 1u;
    fallback.absolute_sequence = maximum - 1u;
    const HostPointerFallbackInsidePublicationResult fallback_result =
        galaxy::host::apply_host_pointer_fallback_inside_publication(
            fallback,
            {321, 201, 854, 480, 20'004u, maximum - 1u, fallback.absolute_sequence});
    passed &= expect(
        fallback_result ==
                HostPointerFallbackInsidePublicationResult::PublishedInside &&
            fallback.event_sequence == maximum &&
            fallback.absolute_sequence == maximum &&
            fallback.coordinates.inside_client &&
            fallback.coordinates.client_x == 321,
        "fallback acquisition reaches UINT64_MAX without wrapping either identity");

    HostPointerSnapshot rejected_at_max{};
    rejected_at_max.coordinates =
        coordinates(320, 200, 854, 480, true, false);
    rejected_at_max.event_sequence = maximum;
    rejected_at_max.absolute_sequence = maximum;
    const HostPointerCursorInsidePublicationResult rejected_inside =
        galaxy::host::apply_host_pointer_cursor_inside_publication(
            rejected_at_max,
            {false, 320, 200, 20'005u, maximum, 854, 480, rejected_at_max.absolute_sequence});
    const HostPointerCursorOutsidePublicationResult already_outside =
        galaxy::host::apply_host_pointer_cursor_outside_publication(
            rejected_at_max, {true, false, true, maximum, 854, 480, rejected_at_max.absolute_sequence});
    const HostPointerFallbackInsidePublicationResult rejected_fallback =
        galaxy::host::apply_host_pointer_fallback_inside_publication(
            rejected_at_max,
            {320, 200, 854, 480, 20'005u, maximum - 1u, rejected_at_max.absolute_sequence});
    passed &= expect(
        rejected_inside == HostPointerCursorInsidePublicationResult::Rejected &&
            already_outside ==
                HostPointerCursorOutsidePublicationResult::AlreadyOutside &&
            rejected_fallback ==
                HostPointerFallbackInsidePublicationResult::Rejected &&
            rejected_at_max.event_sequence == maximum &&
            rejected_at_max.absolute_sequence == maximum,
        "rejected or no-op publishers remain legal at UINT64_MAX and do not mutate identities");

    constexpr std::string_view exhaustion_cases[] = {
        "message-event",
        "message-absolute",
        "seed-absolute",
        "resize-absolute",
        "move-absolute",
        "inside-event",
        "inside-absolute",
        "outside-event",
        "window-loss-event",
        "fallback-event",
        "fallback-absolute",
    };
    for (const std::string_view category : exhaustion_cases) {
        passed &= sequence_exhaustion_case_hard_fails(category);
    }
    return passed;
}

bool fresh_wndproc_state_rejects_stale_or_non_authoritative_outside_poll() {
    HostPointerSnapshot snapshot{};
    snapshot.coordinates = coordinates(220, 140);
    snapshot.absolute_sequence = 50u;
    snapshot.event_sequence = 40u;
    snapshot.absolute_acquired_ms = 5'000u;
    snapshot.last_client_message_ms = 5'000u;

    const auto remote_recent =
        galaxy::host::select_host_pointer_cursor_poll_authority(
            HostPointerCursorPollMode::Adaptive, true, true);
    bool passed =
        expect(remote_recent.poll_enabled && !remote_recent.authoritative,
               "recent remote WndProc coordinate remains authoritative over "
               "cursor polling");
    const HostPointerCursorOutsidePublicationResult non_authoritative =
        galaxy::host::apply_host_pointer_cursor_outside_publication(
            snapshot, {
                          true,
                          false,
                          remote_recent.authoritative,
                          40u,
                          854,
                          480,
                          snapshot.absolute_sequence,
                      });
    passed &=
        expect(non_authoritative ==
                       HostPointerCursorOutsidePublicationResult::Rejected &&
                   snapshot.coordinates.inside_client &&
                   snapshot.event_sequence == 40u,
               "non-authoritative outside poll cannot erase a fresh WndProc "
               "coordinate");

    // Model a newer mouse message landing after cursor sampling began.
    snapshot.coordinates = coordinates(300, 160);
    snapshot.absolute_sequence = 51u;
    snapshot.event_sequence = 41u;
    snapshot.absolute_acquired_ms = 5'001u;
    snapshot.last_client_message_ms = 5'001u;
    const HostPointerCursorOutsidePublicationResult event_race =
        galaxy::host::apply_host_pointer_cursor_outside_publication(
            snapshot, {true, false, true, 40u, 854, 480, snapshot.absolute_sequence});
    passed &= expect(
        event_race == HostPointerCursorOutsidePublicationResult::Rejected &&
            snapshot.coordinates.inside_client &&
            snapshot.coordinates.client_x == 300 &&
            snapshot.event_sequence == 41u,
        "event-sequence revalidation preserves a newer WndProc publication");

    const HostPointerCursorOutsidePublicationResult bounds_race =
        galaxy::host::apply_host_pointer_cursor_outside_publication(
            snapshot, {true, false, true, 41u, 1280, 720, snapshot.absolute_sequence});
    passed &= expect(
        bounds_race == HostPointerCursorOutsidePublicationResult::Rejected &&
            snapshot.coordinates.inside_client,
        "client-bounds revalidation rejects an outside sample classified "
        "against "
        "stale geometry");

    const HostPointerCursorOutsidePublicationResult failed_sample =
        galaxy::host::apply_host_pointer_cursor_outside_publication(
            snapshot, {false, false, true, 41u, 854, 480, snapshot.absolute_sequence});
    passed &= expect(
        failed_sample == HostPointerCursorOutsidePublicationResult::Rejected &&
            snapshot.coordinates.inside_client,
        "failed cursor acquisition cannot publish known-outside state");
    return passed;
}

bool startup_seed_never_fabricates_inside_visibility() {
    bool passed = expect(
        !galaxy::host::select_host_pointer_seed_inside(true, true),
        "forced startup center does not inherit stale inside visibility");
    passed &= expect(
        !galaxy::host::select_host_pointer_seed_inside(false, true),
        "forced startup center remains inactive without cursor evidence");
    passed &=
        expect(!galaxy::host::select_host_pointer_seed_inside(false, false),
               "non-forced geometry refresh preserves a proven outside sample");
    passed &= expect(
        galaxy::host::select_host_pointer_seed_inside(true, false),
        "non-forced resize may preserve a previously acquired inside sample");
    return passed;
}

bool focused_stationary_cursor_acquisition_can_follow_inactive_seed() {
    HostPointerSnapshot snapshot{};
    galaxy::host::publish_host_pointer_seed(
        snapshot, {1280, 720, true, 30'000u});
    bool passed = expect(
        snapshot.coordinates.absolute_valid &&
            !snapshot.coordinates.inside_client &&
            snapshot.event_sequence == 0u &&
            snapshot.absolute_sequence == 1u &&
            snapshot.absolute_acquired_ms == 0u,
        "startup geometry seed is valid but remains inactive");

    galaxy::host::publish_host_pointer_seed(
        snapshot, {1280, 720, false, 30'100u});
    passed &= expect(
        snapshot.event_sequence == 0u &&
            snapshot.absolute_sequence == 1u &&
            snapshot.absolute_acquired_ms == 0u,
        "unchanged geometry seed does not masquerade as a fresh cursor sample");

    const HostPointerFallbackInsidePublicationResult result =
        galaxy::host::apply_host_pointer_fallback_inside_publication(
            snapshot,
            {900, 300, 1280, 720, 30'101u, snapshot.event_sequence, snapshot.absolute_sequence});
    passed &= expect(
        result ==
                HostPointerFallbackInsidePublicationResult::PublishedInside &&
            snapshot.coordinates.absolute_valid &&
            snapshot.coordinates.inside_client &&
            snapshot.coordinates.client_x == 900 &&
            snapshot.coordinates.client_y == 300 &&
            snapshot.event_sequence == 1u &&
            snapshot.absolute_sequence == 2u &&
            snapshot.absolute_acquired_ms == 30'101u,
        "focused stationary cursor acquisition activates the pointer after startup");
    return passed;
}

bool geometry_and_known_outside_are_authoritative_for_ir_availability() {
    const auto focused_inside =
        galaxy::host::select_host_pointer_ir_availability(true, true, true);
    bool passed =
        expect(focused_inside.acquired_inside_available &&
                   !focused_inside.invalid_sample_hold_allowed,
               "focused valid inside acquisition makes guest IR available");

    const auto unfocused_inside =
        galaxy::host::select_host_pointer_ir_availability(false, true, true);
    passed &= expect(
        unfocused_inside.acquired_inside_available &&
            !unfocused_inside.invalid_sample_hold_allowed,
        "unfocused inside coordinates remain available to lightweight IR tracking");

    const auto focused_captured_outside =
        galaxy::host::select_host_pointer_ir_availability(true, true, false);
    passed &= expect(
        !focused_captured_outside.acquired_inside_available &&
            !focused_captured_outside.invalid_sample_hold_allowed,
        "known outside captured coordinates cannot become inside when clamped");

    const auto focused_invalid =
        galaxy::host::select_host_pointer_ir_availability(true, false, false);
    passed &= expect(
        !focused_invalid.acquired_inside_available &&
            focused_invalid.invalid_sample_hold_allowed,
        "focused invalid sampling alone may use bounded uncertainty hold");

    const auto unfocused_invalid =
        galaxy::host::select_host_pointer_ir_availability(false, false, false);
    passed &= expect(
        !unfocused_invalid.acquired_inside_available &&
            unfocused_invalid.invalid_sample_hold_allowed,
        "unfocused invalid sampling may use the same bounded uncertainty hold");
    return passed;
}

bool mouse_button_delivery_uses_inside_game_geometry_without_focus() {
    using galaxy::host::select_host_pointer_mouse_button_delivery;
    bool passed = expect(
        select_host_pointer_mouse_button_delivery(
            false, true, true, true, true),
        "unfocused click over the valid game client reaches Wii A/B");
    passed &= expect(
        select_host_pointer_mouse_button_delivery(
            true, true, true, true, true),
        "focused click over the valid game client still reaches Wii A/B");
    passed &= expect(
        !select_host_pointer_mouse_button_delivery(
            false, true, false, false, true),
        "known outside coordinates cannot deliver a mouse click");
    passed &= expect(
        !select_host_pointer_mouse_button_delivery(
            false, false, true, true, true),
        "invalid absolute coordinates cannot deliver a mouse click");
    passed &= expect(
        !select_host_pointer_mouse_button_delivery(
            false, true, true, false, true),
        "content bars cannot deliver a mouse click");
    passed &= expect(
        !select_host_pointer_mouse_button_delivery(
            false, true, true, true, false),
        "a covering app cannot deliver a mouse click to the game");
    return passed;
}

bool normal_capture_release_preserves_pointer_visibility() {
    const HostPointerCaptureCleanupDecision normal_release =
        galaxy::host::select_host_pointer_capture_cleanup(false, false);
    bool passed = expect(
        !normal_release.publish_cancel_all,
        "normal ReleaseCapture does not publish a spurious CancelAll");
    passed &= expect(
        !normal_release.clear_window_state,
        "normal ReleaseCapture preserves the inside-client pointer state");

    const HostPointerCaptureCleanupDecision unexpected_loss =
        galaxy::host::select_host_pointer_capture_cleanup(false, true);
    passed &= expect(
        unexpected_loss.publish_cancel_all &&
            unexpected_loss.clear_window_state,
        "capture loss while pressed cancels buttons and pointer state");

    const HostPointerCaptureCleanupDecision cancel_mode =
        galaxy::host::select_host_pointer_capture_cleanup(true, false);
    passed &= expect(
        !cancel_mode.publish_cancel_all && cancel_mode.clear_window_state,
        "WM_CANCELMODE clears pointer window state without a phantom edge");
    return passed;
}

bool expect_transition(
    const HostPointerTransition& transition,
    std::uint64_t sequence,
    std::uint64_t focus_epoch,
    HostPointerTransitionKind kind,
    const HostPointerCoordinates& expected_coordinates,
    std::string_view message) {
    return expect(transition.sequence == sequence, message) &&
        expect(transition.focus_epoch == focus_epoch, message) &&
        expect(transition.kind == kind, message) &&
        expect(
            transition.absolute_acquired_ms ==
                transition_acquired_ms(sequence),
            message) &&
        expect(
            same_coordinates(transition.coordinates, expected_coordinates),
            message);
}

bool quick_click_is_lossless_and_non_destructive() {
    HostPointerEventRing ring;
    const auto initial = ring.read_after(0);
    bool passed = expect(
        initial.result == HostPointerTransitionReadResult::Empty,
        "new ring is empty");
    passed &= expect(initial.latest_sequence == 0, "new ring sequence starts at zero");
    passed &= expect(
        initial.current_focus_epoch == 1,
        "new ring focus epoch starts at one");

    const HostPointerCoordinates down = coordinates(110, 220);
    const HostPointerCoordinates up = coordinates(111, 221);
    passed &= expect(
        ring.accept_button_transition(
            HostPointerButton::Left,
            true,
            down,
            transition_acquired_ms(1)),
        "quick-click down is accepted");
    passed &= expect(
        ring.accept_button_transition(
            HostPointerButton::Left,
            false,
            up,
            transition_acquired_ms(2)),
        "quick-click up is accepted before any read");

    const auto first = ring.read_after(0);
    passed &= expect(
        first.result == HostPointerTransitionReadResult::Ready,
        "quick-click down remains readable");
    passed &= expect_transition(
        first.transition,
        1,
        1,
        HostPointerTransitionKind::LeftDown,
        down,
        "quick-click down preserves its transition and coordinates");

    const auto first_again = ring.read_after(0);
    passed &= expect_transition(
        first_again.transition,
        1,
        1,
        HostPointerTransitionKind::LeftDown,
        down,
        "read-after is non-destructive");

    const auto second = ring.read_after(1);
    passed &= expect_transition(
        second.transition,
        2,
        1,
        HostPointerTransitionKind::LeftUp,
        up,
        "quick-click release follows on the next sequence");
    passed &= expect(
        ring.read_after(2).result == HostPointerTransitionReadResult::Empty,
        "quick-click cursor reaches the ring head");
    return passed;
}

bool double_click_keeps_all_four_edges() {
    HostPointerEventRing ring;
    constexpr HostPointerTransitionKind expected_kinds[] = {
        HostPointerTransitionKind::LeftDown,
        HostPointerTransitionKind::LeftUp,
        HostPointerTransitionKind::LeftDown,
        HostPointerTransitionKind::LeftUp,
    };

    bool passed = true;
    for (std::uint64_t sequence = 1; sequence <= 4; ++sequence) {
        const bool down = (sequence & 1u) != 0u;
        passed &= expect(
            ring.accept_button_transition(
                HostPointerButton::Left,
                down,
                coordinates(
                    static_cast<std::int32_t>(100 + sequence),
                    static_cast<std::int32_t>(200 + sequence)),
                transition_acquired_ms(sequence)),
            "double-click transition is accepted");
    }

    for (std::uint64_t sequence = 1; sequence <= 4; ++sequence) {
        const auto poll = ring.read_after(sequence - 1u);
        passed &= expect_transition(
            poll.transition,
            sequence,
            1,
            expected_kinds[sequence - 1u],
            coordinates(
                static_cast<std::int32_t>(100 + sequence),
                static_cast<std::int32_t>(200 + sequence)),
            "double-click retains every ordered edge");
    }
    return passed;
}

bool held_levels_are_deduplicated_independently() {
    HostPointerEventRing ring;
    const HostPointerCoordinates left_down = coordinates(10, 20);
    const HostPointerCoordinates duplicate_left = coordinates(300, 301);
    const HostPointerCoordinates right_down = coordinates(30, 40);
    bool passed = expect(
        ring.accept_button_transition(
            HostPointerButton::Left,
            true,
            left_down,
            transition_acquired_ms(1)),
        "initial held left down is accepted");
    passed &= expect(
        !ring.accept_button_transition(
            HostPointerButton::Left,
            true,
            duplicate_left,
            90'001u),
        "repeated held left down is deduplicated");
    passed &= expect(
        ring.accept_button_transition(
            HostPointerButton::Right,
            true,
            right_down,
            transition_acquired_ms(2)),
        "right button changes independently while left is held");
    passed &= expect(
        !ring.accept_button_transition(
            HostPointerButton::Right,
            true,
            duplicate_left,
            90'002u),
        "repeated held right down is deduplicated");

    const auto left = ring.read_after(0);
    const auto right = ring.read_after(1);
    passed &= expect_transition(
        left.transition,
        1,
        1,
        HostPointerTransitionKind::LeftDown,
        left_down,
        "deduplication does not replace the original left coordinate");
    passed &= expect_transition(
        right.transition,
        2,
        1,
        HostPointerTransitionKind::RightDown,
        right_down,
        "independent right held edge is retained");
    passed &= expect(
        right.latest_sequence == 2,
        "duplicate held messages do not consume sequence numbers");

    passed &= expect(
        ring.accept_button_transition(
            HostPointerButton::Left,
            false,
            coordinates(50, 60),
            transition_acquired_ms(3)),
        "held left release is accepted");
    passed &= expect(
        !ring.accept_button_transition(
            HostPointerButton::Left,
            false,
            coordinates(70, 80),
            90'003u),
        "repeated left release is deduplicated");
    return passed;
}

bool drag_release_preserves_release_coordinate() {
    HostPointerEventRing ring;
    const HostPointerCoordinates down = coordinates(25, 30, 640, 480);
    const HostPointerCoordinates up = coordinates(
        -12,
        501,
        1280,
        720,
        true,
        false);
    bool passed = expect(
        ring.accept_button_transition(
            HostPointerButton::Left,
            true,
            down,
            transition_acquired_ms(1)),
        "drag starts with a captured down point");
    passed &= expect(
        ring.accept_button_transition(
            HostPointerButton::Left,
            false,
            up,
            transition_acquired_ms(2)),
        "drag release outside the client is accepted");
    const auto release = ring.read_after(1);
    passed &= expect_transition(
        release.transition,
        2,
        1,
        HostPointerTransitionKind::LeftUp,
        up,
        "drag release preserves exact point and resized client dimensions");
    return passed;
}

bool focus_loss_releases_buttons_without_erasing_pointer_geometry() {
    HostPointerEventRing ring;
    const HostPointerCoordinates cancelled = coordinates(
        400,
        200,
        854,
        480,
        true,
        true);
    bool passed = expect(
        ring.accept_button_transition(
            HostPointerButton::Left,
            true,
            coordinates(100, 100),
            transition_acquired_ms(1)),
        "focus-loss test accepts left down");
    passed &= expect(
        ring.accept_button_transition(
            HostPointerButton::Right,
            true,
            coordinates(110, 110),
            transition_acquired_ms(2)),
        "focus-loss test accepts right down");
    ring.invalidate_focus(cancelled, transition_acquired_ms(3));

    const auto cancel = ring.read_after(2);
    passed &= expect(
        cancel.current_focus_epoch == 2,
        "focus invalidation advances the current epoch");
    passed &= expect_transition(
        cancel.transition,
        3,
        2,
        HostPointerTransitionKind::CancelAll,
        cancelled,
        "focus invalidation records one CancelAll while preserving pointer geometry");
    passed &= expect(
        !ring.accept_button_transition(
            HostPointerButton::Left,
            false,
            coordinates(120, 120),
            90'004u),
        "focus invalidation already released left");
    passed &= expect(
        !ring.accept_button_transition(
            HostPointerButton::Right,
            false,
            coordinates(130, 130),
            90'005u),
        "focus invalidation already released right");
    passed &= expect(
        ring.accept_button_transition(
            HostPointerButton::Left,
            true,
            coordinates(140, 140),
            transition_acquired_ms(4)),
        "new-epoch left down is accepted after focus returns");
    const auto next_epoch = ring.read_after(3);
    passed &= expect_transition(
        next_epoch.transition,
        4,
        2,
        HostPointerTransitionKind::LeftDown,
        coordinates(140, 140),
        "post-focus transition remains in the current epoch");
    return passed;
}

bool unexpected_capture_loss_is_an_explicit_cancel() {
    HostPointerEventRing ring;
    const HostPointerCoordinates cancelled = coordinates(
        222,
        333,
        854,
        480,
        true,
        false);
    bool passed = expect(
        ring.accept_button_transition(
            HostPointerButton::Right,
            true,
            coordinates(200, 300),
            transition_acquired_ms(1)),
        "capture-loss test accepts right down");
    ring.cancel_unexpected_capture(cancelled, transition_acquired_ms(2));
    const auto cancel = ring.read_after(1);
    passed &= expect_transition(
        cancel.transition,
        2,
        2,
        HostPointerTransitionKind::CancelAll,
        cancelled,
        "unexpected capture loss emits an explicit CancelAll");
    passed &= expect(
        cancel.current_focus_epoch == 2,
        "unexpected capture loss forms a new input epoch");
    passed &= expect(
        !ring.accept_button_transition(
            HostPointerButton::Right,
            false,
            coordinates(223, 334),
            90'006u),
        "capture cancellation already released right");
    return passed;
}

HostPointerCoordinates sequence_coordinates(std::uint64_t sequence) {
    return coordinates(
        static_cast<std::int32_t>(sequence),
        -static_cast<std::int32_t>(sequence),
        1024,
        768);
}

bool wrap_reports_overflow_without_resynchronizing() {
    static_assert(galaxy::host::kHostPointerTransitionCapacity == 1024u);
    HostPointerEventRing ring;
    constexpr std::uint64_t kPublished =
        galaxy::host::kHostPointerTransitionCapacity + 1u;
    bool passed = true;
    for (std::uint64_t sequence = 1; sequence <= kPublished; ++sequence) {
        passed &= expect(
            ring.accept_button_transition(
                HostPointerButton::Left,
                (sequence & 1u) != 0u,
                sequence_coordinates(sequence),
                transition_acquired_ms(sequence)),
            "wrap test publishes every alternating edge");
    }

    const auto lost = ring.read_after(0);
    passed &= expect(
        lost.result == HostPointerTransitionReadResult::Overflow,
        "reader behind overwritten sequence receives Overflow");
    passed &= expect(
        lost.latest_sequence == kPublished,
        "overflow reports the actual ring head");

    const auto oldest = ring.read_after(1);
    passed &= expect_transition(
        oldest.transition,
        2,
        1,
        HostPointerTransitionKind::LeftUp,
        sequence_coordinates(2),
        "oldest retained event remains readable after wrap");
    const auto newest = ring.read_after(kPublished - 1u);
    passed &= expect_transition(
        newest.transition,
        kPublished,
        1,
        HostPointerTransitionKind::LeftDown,
        sequence_coordinates(kPublished),
        "newest wrapped event retains its exact sequence and coordinate");
    passed &= expect(
        ring.read_after(kPublished).result ==
            HostPointerTransitionReadResult::Empty,
        "reader at wrapped head receives Empty");
    return passed;
}

bool concurrent_readers_observe_one_ordered_journal() {
    HostPointerEventRing ring;
    constexpr std::uint64_t kPublished = 1000;
    std::atomic<bool> start{false};
    std::atomic<bool> reader_one_ok{true};
    std::atomic<bool> reader_two_ok{true};

    const auto reader = [&](std::atomic<bool>& result) {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::uint64_t cursor = 0;
        while (cursor < kPublished) {
            const auto poll = ring.read_after(cursor);
            if (poll.result == HostPointerTransitionReadResult::Empty) {
                std::this_thread::yield();
                continue;
            }
            if (poll.result == HostPointerTransitionReadResult::Overflow ||
                poll.transition.sequence != cursor + 1u ||
                poll.transition.focus_epoch != 1u ||
                poll.transition.absolute_acquired_ms !=
                    transition_acquired_ms(cursor + 1u) ||
                !same_coordinates(
                    poll.transition.coordinates,
                    sequence_coordinates(cursor + 1u))) {
                result.store(false, std::memory_order_release);
                return;
            }
            cursor = poll.transition.sequence;
        }
    };

    std::thread first(reader, std::ref(reader_one_ok));
    std::thread second(reader, std::ref(reader_two_ok));
    start.store(true, std::memory_order_release);
    bool producer_ok = true;
    for (std::uint64_t sequence = 1; sequence <= kPublished; ++sequence) {
        producer_ok =
            ring.accept_button_transition(
                HostPointerButton::Left,
                (sequence & 1u) != 0u,
                sequence_coordinates(sequence),
                transition_acquired_ms(sequence)) &&
            producer_ok;
    }
    first.join();
    second.join();

    return expect(producer_ok, "concurrent producer retains every edge") &&
        expect(
            reader_one_ok.load(std::memory_order_acquire),
            "first concurrent reader sees all events in order") &&
        expect(
            reader_two_ok.load(std::memory_order_acquire),
            "second concurrent reader independently sees all events in order");
}

bool snapshot_matches_publication(const HostPointerSnapshot& snapshot) {
    if (snapshot.event_sequence == 0u) {
        return !snapshot.coordinates.absolute_valid &&
            !snapshot.coordinates.inside_client &&
            snapshot.coordinates.client_x == 0 &&
            snapshot.coordinates.client_y == 0 &&
            snapshot.coordinates.client_width == 0 &&
            snapshot.coordinates.client_height == 0 &&
            snapshot.absolute_sequence == 0u &&
            snapshot.absolute_acquired_ms == 0u &&
            snapshot.last_client_message_ms == 0u &&
            snapshot.leave_sequence == 0u;
    }

    const std::uint64_t publication = snapshot.event_sequence;
    return snapshot.coordinates.absolute_valid &&
        snapshot.coordinates.inside_client == ((publication & 1u) != 0u) &&
        snapshot.coordinates.client_x ==
            static_cast<std::int32_t>(publication) &&
        snapshot.coordinates.client_y ==
            -static_cast<std::int32_t>(publication) &&
        snapshot.coordinates.client_width ==
            static_cast<std::int32_t>(1000u + publication) &&
        snapshot.coordinates.client_height ==
            static_cast<std::int32_t>(2000u + publication) &&
        snapshot.absolute_sequence == publication * 3u &&
        snapshot.absolute_acquired_ms == publication * 5u &&
        snapshot.last_client_message_ms == publication * 7u &&
        snapshot.leave_sequence == publication * 11u;
}

bool concurrent_snapshot_publications_never_tear_or_lose_updates() {
    HostPointerSnapshotCache cache;
    constexpr std::uint64_t kUpdatesPerWriter = 25000u;
    constexpr std::uint64_t kWriterCount = 2u;
    constexpr std::uint64_t kExpectedUpdates =
        kUpdatesPerWriter * kWriterCount;
    std::atomic<bool> start{false};
    std::atomic<unsigned> writers_remaining{
        static_cast<unsigned>(kWriterCount)};
    std::atomic<bool> first_reader_ok{true};
    std::atomic<bool> second_reader_ok{true};

    const auto writer = [&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::uint64_t i = 0; i < kUpdatesPerWriter; ++i) {
            (void)cache.update([](HostPointerSnapshot& snapshot) noexcept {
                const std::uint64_t publication =
                    snapshot.event_sequence + 1u;
                snapshot.coordinates = coordinates(
                    static_cast<std::int32_t>(publication),
                    -static_cast<std::int32_t>(publication),
                    static_cast<std::int32_t>(1000u + publication),
                    static_cast<std::int32_t>(2000u + publication),
                    true,
                    (publication & 1u) != 0u);
                snapshot.absolute_sequence = publication * 3u;
                snapshot.event_sequence = publication;
                snapshot.absolute_acquired_ms = publication * 5u;
                snapshot.last_client_message_ms = publication * 7u;
                snapshot.leave_sequence = publication * 11u;
            });
        }
        writers_remaining.fetch_sub(1u, std::memory_order_release);
    };

    const auto reader = [&](std::atomic<bool>& result) {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        do {
            if (!snapshot_matches_publication(cache.snapshot())) {
                result.store(false, std::memory_order_release);
                return;
            }
        } while (writers_remaining.load(std::memory_order_acquire) != 0u);
        if (!snapshot_matches_publication(cache.snapshot())) {
            result.store(false, std::memory_order_release);
        }
    };

    std::thread first_writer(writer);
    std::thread second_writer(writer);
    std::thread first_reader(reader, std::ref(first_reader_ok));
    std::thread second_reader(reader, std::ref(second_reader_ok));
    start.store(true, std::memory_order_release);
    first_writer.join();
    second_writer.join();
    first_reader.join();
    second_reader.join();

    const HostPointerSnapshot final_snapshot = cache.snapshot();
    return expect(
               first_reader_ok.load(std::memory_order_acquire),
               "first concurrent snapshot reader never observes a torn tuple") &&
        expect(
            second_reader_ok.load(std::memory_order_acquire),
            "second concurrent snapshot reader never observes a torn tuple") &&
        expect(
            snapshot_matches_publication(final_snapshot),
            "final concurrent snapshot is internally coherent") &&
        expect(
            final_snapshot.event_sequence == kExpectedUpdates,
            "serialized multi-writer updates lose no sequence increments");
}

bool authoritative_poll_repairs_an_intervening_message() {
    HostPointerSnapshot snapshot{};
    galaxy::host::publish_host_pointer_client_coordinate(
        snapshot, {100, 100, 854, 480, 1000u});
    galaxy::host::publish_host_pointer_client_coordinate(
        snapshot, {420, 260, 854, 480, 1001u});
    const auto select = [&snapshot](bool authoritative, bool stale_center) {
        return galaxy::host::select_host_pointer_cursor_poll({
            true, 100, 100, 100, 100, stale_center, false, authoritative,
            snapshot.coordinates.absolute_valid,
            snapshot.coordinates.client_x, snapshot.coordinates.client_y});
    };
    bool passed = expect(!select(false, false).should_publish &&
                         !select(true, true).should_publish,
                         "current divergence cannot bypass authority or stale-center veto");
    const auto selection = select(true, false);
    passed &= expect(!selection.candidate_changed && selection.should_publish,
                     "unchanged accepted A repairs a later B message");
    const auto result = galaxy::host::apply_host_pointer_cursor_inside_publication(
        snapshot, {selection.should_publish, 100, 100, 1002u,
                   snapshot.event_sequence, 854, 480, snapshot.absolute_sequence});
    passed &= expect(result == HostPointerCursorInsidePublicationResult::PublishedInside &&
                         snapshot.coordinates.client_x == 100 &&
                         snapshot.coordinates.client_y == 100 &&
                         snapshot.absolute_acquired_ms == 1002u &&
                         !select(true, false).should_publish,
                     "repaired current A suppresses stationary acquisition churn");
    return passed;
}

bool seed_and_geometry_aba_reject_in_flight_publications() {
    HostPointerSnapshot snapshot{};
    galaxy::host::publish_host_pointer_client_coordinate(
        snapshot, {320, 200, 854, 480, 1000u});
    const auto observed = snapshot;
    galaxy::host::publish_host_pointer_seed(snapshot, {640, 480, false, 2000u});
    galaxy::host::publish_host_pointer_seed(snapshot, {854, 480, false, 2001u});
    const auto reject_all = [&snapshot, &observed]() {
        return galaxy::host::apply_host_pointer_cursor_inside_publication(
                   snapshot, {true, 321, 201, 2002u, observed.event_sequence,
                              854, 480, observed.absolute_sequence}) ==
                   HostPointerCursorInsidePublicationResult::Rejected &&
               galaxy::host::apply_host_pointer_cursor_outside_publication(
                   snapshot, {true, false, true, observed.event_sequence,
                              854, 480, observed.absolute_sequence}) ==
                   HostPointerCursorOutsidePublicationResult::Rejected &&
               galaxy::host::apply_host_pointer_fallback_inside_publication(
                   snapshot, {321, 201, 854, 480, 2002u, observed.event_sequence,
                              observed.absolute_sequence}) ==
                   HostPointerFallbackInsidePublicationResult::Rejected;
    };
    bool passed = expect(snapshot.event_sequence == observed.event_sequence &&
                         snapshot.absolute_sequence == observed.absolute_sequence + 2u &&
                         snapshot.absolute_acquired_ms == 1000u && reject_all(),
                         "geometry ABA rejects all old acquisitions without forging sample time");
    snapshot = observed;
    galaxy::host::invalidate_host_pointer_geometry(snapshot);
    passed &= expect(same_coordinates(snapshot.coordinates, observed.coordinates) &&
                     snapshot.absolute_acquired_ms == observed.absolute_acquired_ms &&
                     snapshot.event_sequence == observed.event_sequence && reject_all(),
                     "same-size origin invalidation preserves point and age but rejects old polls");
    snapshot = observed;
    galaxy::host::publish_host_pointer_seed(snapshot, {854, 480, true, 2003u});
    passed &= expect(snapshot.absolute_acquired_ms == 0u &&
                     !snapshot.coordinates.inside_client && reject_all(),
                     "same-size forced seed rejects old acquisitions and has unknown sample time");
    return passed;
}

bool invalid_geometry_and_samples_are_rejected_safely() {
    bool passed = true;
    for (const int extent : {0, 1, -1, std::numeric_limits<int>::min()}) {
        HostPointerSnapshot snapshot{};
        galaxy::host::publish_host_pointer_seed(snapshot, {extent, extent, true, 1u});
        passed &= expect(!snapshot.coordinates.absolute_valid &&
                         snapshot.absolute_acquired_ms == 0u,
                         "invalid seed extent never creates a cursor sample");
        galaxy::host::publish_host_pointer_client_coordinate(
            snapshot, {0, 0, extent, extent, 2u});
        passed &= expect(!snapshot.coordinates.absolute_valid &&
                         !snapshot.coordinates.inside_client,
                         "invalid client extent is unavailable without clamp arithmetic");
        passed &= expect(galaxy::host::apply_host_pointer_fallback_inside_publication(
                            snapshot, {0, 0, extent, extent, 3u,
                                       snapshot.event_sequence, snapshot.absolute_sequence}) ==
                            HostPointerFallbackInsidePublicationResult::Rejected,
                         "invalid fallback extent cannot publish inside");
    }
    HostPointerSnapshot valid{};
    galaxy::host::publish_host_pointer_client_coordinate(valid, {100, 100, 854, 480, 1u});
    passed &= expect(galaxy::host::apply_host_pointer_cursor_inside_publication(
                         valid, {true, 854, -1, 2u, valid.event_sequence, 854, 480,
                                 valid.absolute_sequence}) ==
                         HostPointerCursorInsidePublicationResult::Rejected,
                     "selected out-of-range sample cannot claim inside validity");
    valid.coordinates.absolute_valid = false;
    passed &= expect(galaxy::host::apply_host_pointer_cursor_inside_publication(
                         valid, {true, 100, 100, 3u, valid.event_sequence, 854, 480,
                                 valid.absolute_sequence}) ==
                         HostPointerCursorInsidePublicationResult::PublishedInside &&
                     valid.coordinates.absolute_valid && valid.coordinates.inside_client,
                     "a real in-range acquisition restores coherent absolute validity");
    return passed;
}

bool snapshot_mutation_invocation_matches_its_nothrow_contract() {
    struct RefQualifiedMutation {
        void operator()(HostPointerSnapshot& snapshot) & noexcept {
            snapshot.absolute_sequence = 42u;
        }
        void operator()(HostPointerSnapshot&) && { throw 1; }
    };
    galaxy::host::HostPointerSnapshotCache cache;
    return expect(cache.update(RefQualifiedMutation{}).absolute_sequence == 42u,
                  "temporary mutation invokes the checked lvalue overload");
}

bool published_content_is_coherent_and_bounded() {
    if (!expect(galaxy::presentation_content_aspect(21.0/9.0,true)==16.0/9.0 &&
        galaxy::presentation_content_aspect(16.0/10.0,true)==16.0/9.0 &&
        galaxy::presentation_content_aspect(4.0/3.0,true)==4.0/3.0 &&
        galaxy::presentation_content_aspect(21.0/9.0,false)==21.0/9.0,
        "owned movie composition preserves native paths while real-time content keeps its camera aspect")) return false;
    galaxy::PresentationContentPublication publication;
    bool passed=expect(!publication.read(),"unpublished presentation has no invented input rectangle");
    passed &= expect(publication.publish(3840,2160,{480,0,2880,2160}),"4:3 physical pillarbox publishes");
    const auto first=publication.read();
    passed &= expect(first && first->surface_width==3840 && first->viewport.left==480 &&
        first->viewport.width==2880 && first->generation==1u,"input receives the exact final-blit rectangle");
    passed &= expect(!publication.publish(10,10,{9,0,5,5}) && publication.read()->generation==1u,
        "invalid bounds cannot overwrite a valid presentation");
    std::atomic<bool> done{false},bad{false};
    std::thread writer([&]{for(unsigned i=0;i<10000u;++i){
        if(i&1u) publication.publish(854,480,{0,0,853,480});
        else publication.publish(3840,2160,{480,0,2880,2160});
    }done.store(true);});
    while(!done.load()) if(const auto value=publication.read()) {
        const bool small=value->surface_width==854 && value->surface_height==480 &&
            value->viewport.left==0 && value->viewport.width==853 && value->viewport.height==480;
        const bool large=value->surface_width==3840 && value->surface_height==2160 &&
            value->viewport.left==480 && value->viewport.width==2880 && value->viewport.height==2160;
        if(!small && !large) bad.store(true);
    }
    writer.join();
    return passed && expect(!bad.load(),"concurrent resize publication never mixes rectangles and surface dimensions");
}

bool mouse_query_uses_matching_pixel_and_rejects_invalid_edges() {
    bool passed=true;
    for (const unsigned width : {608u,748u,832u,1092u,1664u}) {
        const auto center=galaxy::input::mouse_pointer_query(0.0f,0.0f,width,480u,640u);
        passed &= expect(center && center->screen_x==width*0.5f &&
            center->screen_y==240.0f && center->efb_x==320u && center->efb_y==240u,
            "aspect changes keep normalized mouse/depth coordinates consistent");
        const auto origin=galaxy::input::mouse_pointer_query(-1.0f,-1.0f,width,480u,640u);
        passed &= expect(origin && origin->efb_x==0u && origin->efb_y==0u,
            "first visible content pixel maps to original GX query");
        const auto menu_edge=galaxy::input::mouse_pointer_position(1.0f,1.0f,width,480u);
        passed &= expect(menu_edge && menu_edge->x==width && menu_edge->y==480u,
            "screen-only menu edge stays current without a borrowed depth sample");
        const auto depth_edge=galaxy::input::mouse_pointer_query(1.0f,1.0f,width,480u,640u);
        passed &= expect(depth_edge && depth_edge->efb_x==639u && depth_edge->efb_y==479u &&
            depth_edge->screen_x<width && depth_edge->screen_y<480.0f,
            "inclusive physical edge uses an interior coordinate and its matching GX pixel");
    }
    passed &= expect(!galaxy::input::mouse_pointer_query(
        std::numeric_limits<float>::quiet_NaN(),0,832,480,640) &&
        !galaxy::input::mouse_pointer_query(0,0,832,529,640) &&
        !galaxy::input::mouse_pointer_query(0,0,832,480,641) &&
        !galaxy::input::mouse_pointer_query(-1.01f,0,832,480,640),
        "invalid input/unsupported geometry never reuse a different pixel depth");
    return passed;
}

}  // namespace

int main(int argc, char** argv) {
    if (!published_content_is_coherent_and_bounded()) return 1;
    if (argc == 3 && std::string_view(argv[1]) == "--sequence-exhaustion") {
        return run_sequence_exhaustion_child(argv[2]);
    }
    bool passed = true;
    passed &= mouse_query_uses_matching_pixel_and_rejects_invalid_edges();
    passed &= authoritative_poll_repairs_an_intervening_message();
    passed &= seed_and_geometry_aba_reject_in_flight_publications();
    passed &= invalid_geometry_and_samples_are_rejected_safely();
    passed &= snapshot_mutation_invocation_matches_its_nothrow_contract();
    passed &= host_pointer_poll_cache_expires_on_precise_fake_clock();
    passed &= host_pointer_poll_cache_rejects_invalid_clock_and_caps_duration();
    passed &= rejected_poll_is_reconsidered_after_authority_timeout();
    passed &= stale_center_poll_is_reconsidered_after_rejection_timeout();
    passed &=
        non_authoritative_visibility_recovery_cannot_override_wndproc();
    passed &= stalled_client_messages_do_not_throttle_changed_cursor_polls();
    passed &= local_cursor_poll_is_decoupled_from_the_window_message_pump();
    passed &= authoritative_outside_poll_publishes_known_sensor_loss();
    passed &=
        window_visibility_loss_invalidates_an_in_flight_cursor_poll();
    passed &=
        unchanged_coordinate_reacquisition_advances_absolute_identity();
    passed &=
        snapshot_sequence_boundaries_are_fail_closed_for_every_publisher();
    passed &=
        fresh_wndproc_state_rejects_stale_or_non_authoritative_outside_poll();
    passed &= startup_seed_never_fabricates_inside_visibility();
    passed &=
        focused_stationary_cursor_acquisition_can_follow_inactive_seed();
    passed &=
        geometry_and_known_outside_are_authoritative_for_ir_availability();
    passed &= mouse_button_delivery_uses_inside_game_geometry_without_focus();
    passed &= normal_capture_release_preserves_pointer_visibility();
    passed &= quick_click_is_lossless_and_non_destructive();
    passed &= double_click_keeps_all_four_edges();
    passed &= held_levels_are_deduplicated_independently();
    passed &= drag_release_preserves_release_coordinate();
    passed &=
        focus_loss_releases_buttons_without_erasing_pointer_geometry();
    passed &= unexpected_capture_loss_is_an_explicit_cancel();
    passed &= wrap_reports_overflow_without_resynchronizing();
    passed &= concurrent_readers_observe_one_ordered_journal();
    passed &= concurrent_snapshot_publications_never_tear_or_lose_updates();
    return passed ? 0 : 1;
}
