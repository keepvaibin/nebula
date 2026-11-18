// SPDX-License-Identifier: GPL-3.0-only
// The display's vsync as the game's frame clock on Apple's systems (sdl_host.h; Android's is display_clock.cpp): the
// display link's vsync times and the interval between them, on galaxy::qpc_now()'s clock (Mach absolute time in
// nanoseconds). The frame slots are predicted on that grid, as on Android, so a late callback delays nothing.
#include "sdl_host.h"

#include "galaxy/launch_args.h"
#include "galaxy/vsync_grid.h"

#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>
#else
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#endif

#include <mach/mach_time.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace {

std::mutex g_mutex;
galaxy::VsyncGrid g_grid;  // the display link's vsyncs
std::atomic<std::int64_t> g_display_period_ns{0};  // the display's stated refresh period
std::atomic<std::int64_t> g_frame_slot_ns{0};
std::atomic<std::int64_t> g_frame_period_ns{0};
std::once_flag g_started;

const mach_timebase_info_data_t& timebase() noexcept {
    static const mach_timebase_info_data_t info = [] {
        mach_timebase_info_data_t value{};
        mach_timebase_info(&value);
        return value;
    }();
    return info;
}

// Mach ticks to nanoseconds and back (one to one on Intel, 125 to 3 on Apple silicon).
std::int64_t ticks_to_ns(std::uint64_t ticks) noexcept {
    const mach_timebase_info_data_t& info = timebase();
    return static_cast<std::int64_t>(static_cast<__uint128_t>(ticks) * info.numer / info.denom);
}

std::uint64_t ns_to_ticks(std::int64_t ns) noexcept {
    const mach_timebase_info_data_t& info = timebase();
    return static_cast<std::uint64_t>(static_cast<__uint128_t>(static_cast<std::uint64_t>(ns)) * info.denom / info.numer);
}

std::int64_t now_ns() noexcept {
    return ticks_to_ns(mach_absolute_time());
}

// A vsync at `vsync` (nanoseconds); `stated` the display's own refresh period when it names one.
void note_vsync(std::int64_t vsync, std::int64_t stated) noexcept {
    if (stated > 0) {
        g_display_period_ns.store(stated, std::memory_order_relaxed);
    }
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_grid.note(vsync, stated);
}

// Held (set_display_clock_held): the link gives no vsyncs. Read on the main queue, where the link starts and is held.
std::atomic<bool> g_held{false};

#if TARGET_OS_OSX

CVDisplayLinkRef g_link = nullptr;

CVReturn on_vsync(CVDisplayLinkRef, const CVTimeStamp* now, const CVTimeStamp*, CVOptionFlags, CVOptionFlags*, void*) {
    std::int64_t stated = 0;
    if (now->videoTimeScale > 0 && now->videoRefreshPeriod > 0) {
        stated = static_cast<std::int64_t>(1e9 * static_cast<double>(now->videoRefreshPeriod) /
                                           static_cast<double>(now->videoTimeScale));
    }
    note_vsync(ticks_to_ns(now->hostTime), stated);
    return kCVReturnSuccess;
}

// The display the game's window is on.
void follow_window(NSWindow* window) {
    NSNumber* const number = window.screen.deviceDescription[@"NSScreenNumber"];
    if (g_link != nullptr && number != nil) {
        CVDisplayLinkSetCurrentCGDisplay(g_link, static_cast<CGDirectDisplayID>(number.unsignedIntValue));
    }
}

void start_clock() {
    if (CVDisplayLinkCreateWithActiveCGDisplays(&g_link) != kCVReturnSuccess || g_link == nullptr) {
        g_link = nullptr;
        return;
    }
    CVDisplayLinkSetOutputCallback(g_link, on_vsync, nullptr);
    // The window's own display, now and whenever the window moves to another (the main thread's to ask).
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!g_held.load(std::memory_order_relaxed)) {
            CVDisplayLinkStart(g_link);
        }
        for (NSWindow* window in NSApp.windows) {
            if (window.isVisible) {
                follow_window(window);
                break;
            }
        }
        [NSNotificationCenter.defaultCenter addObserverForName:NSWindowDidChangeScreenNotification
                                                        object:nil
                                                         queue:NSOperationQueue.mainQueue
                                                    usingBlock:^(NSNotification* note) {
                                                        follow_window(static_cast<NSWindow*>(note.object));
                                                    }];
    });
}

#else

}  // namespace

// The display link's target: its callbacks come on the main run loop, which SDL's UIKit loop runs.
@interface NebulaDisplayClock : NSObject
- (void)vsync:(CADisplayLink*)link;
@end

@implementation NebulaDisplayClock
- (void)vsync:(CADisplayLink*)link {
    // CACurrentMediaTime's clock is Mach absolute time in seconds.
    const std::int64_t stated = link.duration > 0.0 ? static_cast<std::int64_t>(link.duration * 1e9) : 0;
    note_vsync(static_cast<std::int64_t>(link.timestamp * 1e9), stated);
}
@end

namespace {

CADisplayLink* g_ios_link = nil;  // main queue only

void start_clock() {
    dispatch_async(dispatch_get_main_queue(), ^{
        static NebulaDisplayClock* target = [NebulaDisplayClock new];
        CADisplayLink* const link = [CADisplayLink displayLinkWithTarget:target selector:@selector(vsync:)];
        if (@available(iOS 15.0, *)) {
            // Every vsync the display has (120 on a ProMotion display; Info.plist's
            // CADisableMinimumFrameDurationOnPhone lets an iPhone give them): the grid is the display's own.
            const float fastest = static_cast<float>(UIScreen.mainScreen.maximumFramesPerSecond);
            link.preferredFrameRateRange = CAFrameRateRangeMake(fastest, fastest, fastest);
        }
        link.paused = g_held.load(std::memory_order_relaxed);
        [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
        g_ios_link = link;
    });
}

#endif

}  // namespace

namespace galaxy::sdl {

bool wait_display_frame(std::int64_t period_ns, std::int64_t& last_ns, bool in_between) noexcept {
    std::call_once(g_started, [] { start_clock(); });
    galaxy::VsyncGrid grid;
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        grid = g_grid;
    }
    // The next frame slot on the vsync grid (galaxy/vsync_grid.h), or the host's clock when there is no grid that
    // keeps the game at its speed.
    std::int64_t target = 0;
    std::int64_t frame_period = 0;
    if (!grid.next_slot(period_ns, last_ns, now_ns(), in_between, target, frame_period)) {
        g_frame_slot_ns.store(0, std::memory_order_relaxed);
        return false;
    }
    mach_wait_until(ns_to_ticks(target));
    if (::galaxy::diagnosing(::galaxy::kDiagnosePresentStats)) {
        // How long after the vsync it asked for the frame's thread ran again, every ten seconds.
        static std::int64_t late_sum = 0;
        static std::int64_t late_max = 0;
        static unsigned late_over_1ms = 0;
        static unsigned waits = 0;
        static std::int64_t window_began = 0;
        const std::int64_t late = now_ns() - target;
        if (window_began == 0) {
            window_began = target;
        }
        late_sum += late;
        late_max = std::max(late_max, late);
        late_over_1ms += late > 1000000 ? 1u : 0u;
        ++waits;
        if (target - window_began >= 10'000'000'000) {
            std::fprintf(stderr, "[pacing] frame wake-ups after their vsync: %.3f ms average, %.3f ms longest, %u of %u over 1 ms\n",
                         static_cast<double>(late_sum) / waits / 1e6, static_cast<double>(late_max) / 1e6, late_over_1ms, waits);
            late_sum = late_max = 0;
            late_over_1ms = waits = 0u;
            window_began = target;
        }
    }
    last_ns = target;
    g_frame_period_ns.store(frame_period, std::memory_order_relaxed);
    g_frame_slot_ns.store(target, std::memory_order_release);
    return true;
}

// No slot is named for the presents. On Android each frame asks for the vsync after its game frame, a slot the
// compositor latches (VK_GOOGLE_display_timing); a Metal layer shows a frame at the first vsync after it is ready, and
// a present held for a named time waits longer than that: measured through MoltenVK on a 60 Hz MacBook Pro, presents
// 50 to 70 ms ahead and stretches shown at 35 to 43 frames a second. The game's frames keep the vsync grid
// (wait_display_frame); their pictures go to the screen as soon as they are drawn.
bool display_grid(std::int64_t& slot_ns, std::int64_t& period_ns, std::int64_t& vsync_ns,
                  std::int64_t& now) noexcept {
    slot_ns = g_frame_slot_ns.load(std::memory_order_acquire);
    period_ns = g_frame_period_ns.load(std::memory_order_relaxed);
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        vsync_ns = g_grid.interval_ns;
    }
    now = now_ns();
    return false;
}

bool vsync_grid(std::int64_t& vsync_ns, std::int64_t& interval_ns) noexcept {
    const std::int64_t stated = g_display_period_ns.load(std::memory_order_relaxed);
    const std::lock_guard<std::mutex> lock(g_mutex);
    vsync_ns = g_grid.vsync_ns;
    interval_ns = stated > 0 ? stated : g_grid.interval_ns;
    return vsync_ns != 0 && interval_ns != 0;
}

double display_refresh_hz() noexcept {
    if (const std::int64_t period = g_display_period_ns.load(std::memory_order_relaxed); period > 0) {
        return 1e9 / static_cast<double>(period);
    }
    const std::lock_guard<std::mutex> lock(g_mutex);
    return g_grid.interval_ns != 0 ? 1e9 / static_cast<double>(g_grid.interval_ns) : 0.0;
}

void set_display_clock_held(bool held) noexcept {
    g_held.store(held, std::memory_order_relaxed);
    // On the main queue, after the link's start (also queued there): the last word is the latest call's.
    dispatch_async(dispatch_get_main_queue(), ^{
        const bool now_held = g_held.load(std::memory_order_relaxed);
#if TARGET_OS_OSX
        if (g_link != nullptr) {
            if (now_held) {
                CVDisplayLinkStop(g_link);
            } else if (!CVDisplayLinkIsRunning(g_link)) {
                CVDisplayLinkStart(g_link);
            }
        }
#else
        if (g_ios_link != nil) {
            g_ios_link.paused = now_held;
        }
#endif
    });
}

}  // namespace galaxy::sdl
