// SPDX-License-Identifier: GPL-3.0-only
// The display's vsync as the game's frame clock on Apple's systems (sdl_host.h; Android's is display_clock.cpp): the
// display link's vsync times and the interval between them, on galaxy::qpc_now()'s clock (Mach absolute time in
// nanoseconds). The frame slots are predicted on that grid, as on Android, so a late callback delays nothing.
// Where the system has the Metal display link (CAMetalDisplayLink: macOS 14, iOS 17) and the Metal renderer hands over
// its layer (metal_link_layer), that link replaces the older one: its updates give the refreshes and the drawables,
// each with the time it will be shown, and the render thread takes its drawables from it (metal_link_drawable).
#include "sdl_host.h"

#include "galaxy/launch_args.h"
#include "galaxy/render/present_clock.h"
#include "galaxy/vsync_grid.h"

#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>
#else
#import <UIKit/UIKit.h>
#endif

#include <mach/mach_time.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>

namespace {

std::mutex g_mutex;
galaxy::VsyncGrid g_grid;  // the display link's vsyncs
std::atomic<std::int64_t> g_display_period_ns{0};  // the display's stated refresh period
std::atomic<std::int64_t> g_frame_slot_ns{0};
std::atomic<std::int64_t> g_frame_period_ns{0};
std::atomic<std::uint64_t> g_vsyncs{0};  // every vsync a link gave (the start check counts them, P-034)
// The window's screen's shortest and longest refresh intervals (NSScreen minimumRefreshInterval and
// maximumRefreshInterval, macOS 12; seconds, 0: not read): they differ on a display with a range (G2.3.5).
std::atomic<double> g_screen_shortest_s{0.0};
std::atomic<double> g_screen_longest_s{0.0};
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
    g_vsyncs.fetch_add(1u, std::memory_order_relaxed);
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_grid.note(vsync, stated);
}

// Held (set_display_clock_held): the link gives no vsyncs. Read on the main queue, where the link starts and is held.
std::atomic<bool> g_held{false};

// The rate a link asks (P-034): the display's fastest only while in-between pictures fill it (set_display_fills),
// else 60 with a range up to the fastest, so a ProMotion display may run at 60 while the game makes 60 pictures.
// The fastest is read on the main queue (the window's screen); 0 until then.
std::atomic<bool> g_fills{false};
std::atomic<int> g_fastest_fps{0};

CAFrameRateRange asked_range() API_AVAILABLE(macos(12.0), ios(15.0)) {
    const float fastest = static_cast<float>(std::max(g_fastest_fps.load(std::memory_order_relaxed), 60));
    if (g_fills.load(std::memory_order_relaxed) || fastest <= 60.0f) {
        return CAFrameRateRangeMake(fastest, fastest, fastest);
    }
    return CAFrameRateRangeMake(60.0f, fastest, 60.0f);
}

// The Metal display link's state. It lives on a thread of its own with its own run loop (Apple's advice for this
// link: its updates are not held up by the main thread's work); every change to it is a block on that run loop.
// Its drawable for the render thread: one at a time, a newer update's taking the place of one not taken (the older
// goes back to the layer unused), with the time it will be shown (CACurrentMediaTime's clock, seconds). While the link
// is on a layer, the layer's own nextDrawable never returns (seen on GitHub's macOS 15 runner, 11-24): a link that
// gives no drawable in the render thread's wait is let go of, and waited for, before the layer's own are taken.
std::mutex g_link_mutex;
std::condition_variable g_link_ready;
id<CAMetalDrawable> g_link_drawable = nil;  // under g_link_mutex
double g_link_shown_at = 0.0;               // under g_link_mutex
bool g_link_runs = false;                   // under g_link_mutex: the link gives the drawables
std::atomic<bool> g_metal_link_runs{false}; // the same, for the older link's start and stop
CFRunLoopRef g_link_loop = nullptr;         // the link thread's run loop, set once
std::once_flag g_link_thread;
id g_metal_link = nil;  // a CAMetalDisplayLink, on the link's thread only

// The refreshes the link's updates said they would show, noted as vsyncs once they have come (the grid's vsyncs are
// past ones, as the older link's are). On the link's thread only.
std::array<std::int64_t, 4> g_planned{};
std::size_t g_planned_count = 0;

void metal_link_update(id<CAMetalDrawable> drawable, double shown_at) {
    const std::int64_t target = static_cast<std::int64_t>(std::llround(shown_at * 1e9));
    const std::int64_t now = ticks_to_ns(mach_absolute_time());
    std::size_t kept = 0;
    for (std::size_t i = 0; i < g_planned_count; ++i) {
        if (g_planned[i] <= now) {
            note_vsync(g_planned[i], 0);
        } else {
            g_planned[kept++] = g_planned[i];
        }
    }
    g_planned_count = kept;
    if (g_planned_count < g_planned.size() && (g_planned_count == 0 || target > g_planned[g_planned_count - 1])) {
        g_planned[g_planned_count++] = target;
    }
    {
        const std::lock_guard<std::mutex> lock(g_link_mutex);
        if (!g_link_runs) {
            return;
        }
        g_link_drawable = drawable;
        g_link_shown_at = shown_at;
    }
    g_link_ready.notify_one();
}

// A block run on the link's thread (its run loop started once, on first use).
void on_link_thread(void (^block)(void)) {
    std::call_once(g_link_thread, [] {
        dispatch_semaphore_t const started = dispatch_semaphore_create(0);
        NSThread* const thread = [[NSThread alloc] initWithBlock:^{
            g_link_loop = CFRunLoopGetCurrent();
            dispatch_semaphore_signal(started);
            // A port keeps the run loop running with no link on it.
            [NSRunLoop.currentRunLoop addPort:[NSMachPort port] forMode:NSDefaultRunLoopMode];
            for (;;) {
                [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:NSDate.distantFuture];
            }
        }];
        thread.name = @"Nebula display link";
        thread.qualityOfService = NSQualityOfServiceUserInteractive;
        [thread start];
        dispatch_semaphore_wait(started, DISPATCH_TIME_FOREVER);
    });
    CFRunLoopPerformBlock(g_link_loop, kCFRunLoopDefaultMode, block);
    CFRunLoopWakeUp(g_link_loop);
}

// The older link runs only while the Metal display link does not and the clock is not held, and asks asked_range
// where it takes a range (main queue).
void follow_older_link();
void ask_older_range();

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
    if (@available(macOS 12.0, *)) {
        if (window.screen != nil) {
            g_fastest_fps.store(static_cast<int>(window.screen.maximumFramesPerSecond), std::memory_order_relaxed);
            g_screen_shortest_s.store(static_cast<double>(window.screen.minimumRefreshInterval), std::memory_order_relaxed);
            g_screen_longest_s.store(static_cast<double>(window.screen.maximumRefreshInterval), std::memory_order_relaxed);
        }
    }
    // Another screen: the renderer asks again whether the display is variable (G2.3.5 point 5 (c)).
    galaxy::render::g_display_recheck.store(true, std::memory_order_release);
}

void follow_older_link() {
    if (g_link == nullptr) {
        return;
    }
    const bool run = !g_held.load(std::memory_order_relaxed) && !g_metal_link_runs.load(std::memory_order_relaxed);
    if (!run && CVDisplayLinkIsRunning(g_link)) {
        CVDisplayLinkStop(g_link);
    } else if (run && !CVDisplayLinkIsRunning(g_link)) {
        CVDisplayLinkStart(g_link);
    }
}

// The older link's range (a CVDisplayLink asks none).
void ask_older_range() {}

void start_clock() {
    if (CVDisplayLinkCreateWithActiveCGDisplays(&g_link) != kCVReturnSuccess || g_link == nullptr) {
        g_link = nullptr;
        return;
    }
    CVDisplayLinkSetOutputCallback(g_link, on_vsync, nullptr);
    // The window's own display, now and whenever the window moves to another (the main thread's to ask).
    dispatch_async(dispatch_get_main_queue(), ^{
        follow_older_link();
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
        // The display's fastest rate (120 on a ProMotion display; Info.plist's CADisableMinimumFrameDurationOnPhone
        // lets an iPhone give it), asked only while in-between pictures fill it (asked_range, P-034).
        g_fastest_fps.store(static_cast<int>(UIScreen.mainScreen.maximumFramesPerSecond), std::memory_order_relaxed);
        g_ios_link = link;
        ask_older_range();
        follow_older_link();
        [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    });
}

void follow_older_link() {
    if (g_ios_link != nil) {
        g_ios_link.paused =
            g_held.load(std::memory_order_relaxed) || g_metal_link_runs.load(std::memory_order_relaxed);
    }
}

void ask_older_range() {
    if (@available(iOS 15.0, *)) {
        if (g_ios_link != nil) {
            g_ios_link.preferredFrameRateRange = asked_range();
        }
    }
}

#endif

}  // namespace

// The Metal display link's delegate: each update on the link's thread.
API_AVAILABLE(macos(14.0), ios(17.0))
@interface NebulaMetalDisplayLink : NSObject <CAMetalDisplayLinkDelegate>
@end

@implementation NebulaMetalDisplayLink
- (void)metalDisplayLink:(CAMetalDisplayLink*)link needsUpdate:(CAMetalDisplayLinkUpdate*)update {
    metal_link_update(update.drawable, update.targetPresentationTimestamp);
}
@end

namespace {

// On the link's thread: the link on `layer` (nil: none), at the asked range, paused while the clock is held.
void place_metal_link(CAMetalLayer* layer) API_AVAILABLE(macos(14.0), ios(17.0)) {
    static NebulaMetalDisplayLink* const delegate = [NebulaMetalDisplayLink new];
    if (g_metal_link != nil) {
        CAMetalDisplayLink* const old = g_metal_link;
        [old invalidate];
        g_metal_link = nil;
    }
    g_planned_count = 0;
    if (layer != nil) {
        CAMetalDisplayLink* const link = [[CAMetalDisplayLink alloc] initWithMetalLayer:layer];
        link.delegate = delegate;
        link.preferredFrameRateRange = asked_range();
        link.paused = g_held.load(std::memory_order_relaxed);
        [link addToRunLoop:NSRunLoop.currentRunLoop forMode:NSDefaultRunLoopMode];
        g_metal_link = link;
    }
    {
        const std::lock_guard<std::mutex> lock(g_link_mutex);
        g_link_runs = layer != nil;
        g_link_drawable = nil;
    }
    g_link_ready.notify_all();
    g_metal_link_runs.store(layer != nil, std::memory_order_relaxed);
    dispatch_async(dispatch_get_main_queue(), ^{
        follow_older_link();
    });
}

// On the link's thread: the held state and the asked range, as they are now.
void follow_metal_link() API_AVAILABLE(macos(14.0), ios(17.0)) {
    if (g_metal_link != nil) {
        CAMetalDisplayLink* const link = g_metal_link;
        link.paused = g_held.load(std::memory_order_relaxed);
        link.preferredFrameRateRange = asked_range();
    }
}

// From any thread but the link's: the link let go of on its thread, waited for (a second at most), so that the
// layer's own drawables may be taken once this returns.
void let_go_of_metal_link() API_AVAILABLE(macos(14.0), ios(17.0)) {
    on_link_thread(^{
        place_metal_link(nil);
    });
    std::unique_lock<std::mutex> lock(g_link_mutex);
    g_link_ready.wait_for(lock, std::chrono::seconds(1), [] { return !g_link_runs; });
}

// The start check waits so: on the main thread its run loop runs (the links start and are held on the main queue).
void pause_for(double seconds) {
    const std::int64_t end = now_ns() + static_cast<std::int64_t>(seconds * 1e9);
    while (now_ns() < end) {
        if (NSThread.isMainThread) {
            const double left = static_cast<double>(end - now_ns()) / 1e9;
            if (CFRunLoopRunInMode(kCFRunLoopDefaultMode, std::max(left, 0.0), false) != kCFRunLoopRunFinished) {
                continue;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}  // namespace

namespace galaxy::sdl {

void metal_link_layer(void* layer) noexcept {
    if (@available(macOS 14.0, iOS 17.0, *)) {
        std::call_once(g_started, [] { start_clock(); });
        if (layer == nullptr) {
            let_go_of_metal_link();  // the caller lets the layer go, or takes its own drawables, once this returns
            return;
        }
        CAMetalLayer* const metal_layer = (__bridge CAMetalLayer*)layer;
        on_link_thread(^{
            place_metal_link(metal_layer);
        });
    }
}

bool metal_link_drawable(void*& drawable, double& shown_at, std::int64_t wait_ns) noexcept {
    drawable = nullptr;
    shown_at = 0.0;
    std::unique_lock<std::mutex> lock(g_link_mutex);
    if (!g_link_runs) {
        return false;
    }
    g_link_ready.wait_for(lock, std::chrono::nanoseconds(wait_ns), [] { return g_link_drawable != nil || !g_link_runs; });
    if (g_link_drawable == nil) {
        if (!g_link_runs) {
            return false;
        }
        // No update in the wait (held, or a display that gives the link none): the link is let go of, so that the
        // layer's own drawables can be taken; the older link gives the vsyncs from now on.
        lock.unlock();
        std::fprintf(stderr, "[display] the Metal display link gave no drawable in %lld ms: it lets the layer go, whose "
                             "own drawables are taken from now on\n",
                     static_cast<long long>(wait_ns / 1'000'000));
        if (@available(macOS 14.0, iOS 17.0, *)) {
            let_go_of_metal_link();
        }
        return false;
    }
    drawable = (__bridge_retained void*)g_link_drawable;
    shown_at = g_link_shown_at;
    g_link_drawable = nil;
    return true;
}

void set_display_fills(bool fills) noexcept {
    if (g_fills.exchange(fills, std::memory_order_relaxed) == fills) {
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        ask_older_range();
    });
    if (@available(macOS 14.0, iOS 17.0, *)) {
        on_link_thread(^{
            follow_metal_link();
        });
    }
}

int check_display_clock_hold() noexcept {
    std::call_once(g_started, [] { start_clock(); });
    pause_for(0.5);
    const std::uint64_t running_from = g_vsyncs.load(std::memory_order_relaxed);
    pause_for(0.5);
    const std::uint64_t running = g_vsyncs.load(std::memory_order_relaxed) - running_from;
    if (running == 0u) {
        std::printf("start check: display clock: no vsyncs in 0.5 s on this machine, its hold is not checked\n");
        return 0;
    }
    set_display_clock_held(true);
    pause_for(0.2);  // the hold reaches the link (the main queue, the link's thread)
    const std::uint64_t held_from = g_vsyncs.load(std::memory_order_relaxed);
    pause_for(1.0);
    const std::uint64_t held = g_vsyncs.load(std::memory_order_relaxed) - held_from;
    set_display_clock_held(false);
    pause_for(0.3);
    const std::uint64_t again_from = g_vsyncs.load(std::memory_order_relaxed);
    pause_for(0.5);
    const std::uint64_t again = g_vsyncs.load(std::memory_order_relaxed) - again_from;
    std::printf("start check: display clock: %llu vsyncs in 0.5 s, %llu while held for 1 s, %llu in 0.5 s after\n",
                static_cast<unsigned long long>(running), static_cast<unsigned long long>(held),
                static_cast<unsigned long long>(again));
    return held == 0u && again != 0u ? 0 : 6;
}

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
    std::int64_t dropped = 0;
    if (!grid.next_slot(period_ns, last_ns, now_ns(), in_between, galaxy::render::g_variable_refresh.load(std::memory_order_relaxed), target, frame_period, &dropped)) {
        g_frame_slot_ns.store(0, std::memory_order_relaxed);
        return false;
    }
    if (dropped > 0) {
        std::fprintf(stderr,
                     "[pacing] the game's steps were %.0f ms behind the display's vsync: those steps are dropped\n",
                     static_cast<double>(dropped) / 1e6);
    }
    // A slot already past (the clock catching up, P-274) returns at once.
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

// No slot is named for the presents here. Where the Metal display link runs, each drawable comes with the time the
// link will show it and is presented at that time (renderer_mtl.mm); else a Metal layer shows a frame at the first
// vsync after it is ready (a present held for a time of the app's own waited longer: measured through MoltenVK on a
// 60 Hz MacBook Pro, presents 50 to 70 ms ahead). The game's frames keep the vsync grid (wait_display_frame).
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

galaxy::render::RefreshKind apple_refresh_kind(double* shortest_s) noexcept {
#if TARGET_OS_OSX
    const double shortest = g_screen_shortest_s.load(std::memory_order_relaxed);
    const galaxy::render::RefreshKind kind =
        galaxy::render::mac_refresh_kind(shortest, g_screen_longest_s.load(std::memory_order_relaxed));
    if (shortest_s != nullptr) {
        *shortest_s = kind == galaxy::render::RefreshKind::kVariable ? shortest : 0.0;
    }
    return kind;
#else
    // No iOS page read gives a call that says it: fixed until one is read (G2.3.5).
    if (shortest_s != nullptr) {
        *shortest_s = 0.0;
    }
    return galaxy::render::RefreshKind::kFixed;
#endif
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
    // On the main queue, after the link's start (also queued there), and on the Metal display link's thread: the last
    // word is the latest call's.
    dispatch_async(dispatch_get_main_queue(), ^{
        follow_older_link();
    });
    if (@available(macOS 14.0, iOS 17.0, *)) {
        if (g_metal_link_runs.load(std::memory_order_relaxed)) {
            on_link_thread(^{
                follow_metal_link();
            });
        }
    }
}

}  // namespace galaxy::sdl
