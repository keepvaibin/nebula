// WindowOwner: the window is created and destroyed on its own thread, also when stop() ends a modal loop; a window made with
// no_focus (--no-focus 1, a test's visible run) is shown without being activated.
#include "galaxy/render/window_owner.h"
#include "galaxy/test_wait.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
// The owner thread's events are waited for themselves; the deadline only reports that one never came (N-1073).
constexpr DWORD kEventWaitMs = 60000u;
constexpr UINT kBlock = WM_APP + 1;
constexpr UINT kNested = WM_APP + 2;
HANDLE entered{};
HANDLE release_owner{};
std::atomic<DWORD> created_on{};
std::atomic<DWORD> destroyed_on{};
std::atomic<unsigned> destroyed_count{};
std::atomic<unsigned> activations{};  // WM_ACTIVATE to active, WM_ACTIVATEAPP to active, WM_SETFOCUS: any of them is a window that asked for the focus

LRESULT CALLBACK test_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CREATE) created_on.store(GetCurrentThreadId());
    if (message == kBlock) {
        SetEvent(entered);
        (void)WaitForSingleObject(release_owner, kEventWaitMs);
        return 0;
    }
    if (message == kNested) {
        SetEvent(entered);
        MSG next{};
        while (GetMessageW(&next, nullptr, 0, 0) > 0) {
            TranslateMessage(&next);
            DispatchMessageW(&next);
        }
        // A native modal loop propagates WM_QUIT to its enclosing owner loop.
        PostQuitMessage(static_cast<int>(next.wParam));
        return 0;
    }
    if ((message == WM_ACTIVATE && LOWORD(wp) != WA_INACTIVE) || (message == WM_ACTIVATEAPP && wp != FALSE) ||
        message == WM_SETFOCUS) {
        activations.fetch_add(1);
    }
    if (message == WM_DESTROY) {
        destroyed_on.store(GetCurrentThreadId());
        destroyed_count.fetch_add(1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

bool expect(bool condition, const char* description) {
    if (!condition) std::cerr << "FAIL: " << description << '\n';
    return condition;
}
}

int main() {
    entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    release_owner = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!entered || !release_owner) return 1;
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = test_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"GalaxyWindowOwnerRegression";
    if (!RegisterClassW(&window_class)) return 1;
    galaxy::render::WindowOwnerConfig config{
        window_class.lpszClassName, L"Hidden ownership test", WS_OVERLAPPEDWINDOW,
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, 0, 0, 160, 120, false};
    bool okay = true;
    const DWORD consumer_thread = GetCurrentThreadId();
    {
        galaxy::render::WindowOwner owner;
        HWND window = owner.start(config);
        okay &= expect(window != nullptr, "hidden window created");
        okay &= expect(created_on.load() != consumer_thread,
                       "HWND belongs to an independent thread");
        bool duplicate_rejected = false;
        try { (void)owner.start(config); }
        catch (const std::logic_error&) { duplicate_rejected = true; }
        okay &= expect(duplicate_rejected, "duplicate start rejected");

        okay &= expect(PostMessageW(window, kBlock, 0, 0) != FALSE,
                       "blocking UI work submitted");
        okay &= expect(WaitForSingleObject(entered, kEventWaitMs) == WAIT_OBJECT_0,
                       "owner entered blocked handler");
        // This thread does work while the owner's handler is blocked: a count of steps, not a time.
        volatile unsigned consumer_steps = 0;
        while (consumer_steps < 1'000'000u) ++consumer_steps;
        okay &= expect(consumer_steps > 100 && destroyed_count.load() == 0,
                       "consumer progresses while UI owner is blocked");
        SetEvent(release_owner);
        owner.stop();
        okay &= expect(!IsWindow(window) && destroyed_count.load() == 1 &&
                           destroyed_on.load() == created_on.load(),
                       "stop destroys HWND on its creating thread");
        owner.stop();

        ResetEvent(entered);
        window = owner.start(config);
        okay &= expect(window != nullptr, "owner can restart after stop");
        okay &= expect(PostMessageW(window, kNested, 0, 0) != FALSE &&
                           WaitForSingleObject(entered, kEventWaitMs) == WAIT_OBJECT_0,
                       "owner entered nested message loop");
        owner.stop();
        okay &= expect(!IsWindow(window) && destroyed_count.load() == 2 &&
                           destroyed_on.load() == created_on.load(),
                       "stop escapes nested loop and destroys on owner");

        config.class_name = L"GalaxyWindowOwnerMissingClass";
        okay &= expect(owner.start(config) == nullptr,
                       "creation failure returns without retaining a thread");
        config.class_name = window_class.lpszClassName;
        window = owner.start(config);
        okay &= expect(window != nullptr, "restart after creation failure");
    }
    okay &= expect(destroyed_count.load() == 3,
                   "destructor joins and destroys remaining HWND");
    // A visible window made with no_focus: shown, never the foreground window, with the no-activate style, and no message of
    // an activation or of the focus ever reaches it (the three calls and SW_SHOWNORMAL are not made).
    {
        galaxy::render::WindowOwnerConfig visible{
            window_class.lpszClassName, L"No focus test", WS_OVERLAPPEDWINDOW, WS_EX_APPWINDOW | WS_EX_NOACTIVATE, 0, 0, 160, 120,
            true, false, true};
        activations.store(0);
        galaxy::render::WindowOwner owner;
        HWND window = owner.start(visible);
        okay &= expect(window != nullptr, "the no-focus window is created");
        if (window != nullptr) {
            // A round trip through the owner's queue: what the owner did before it is all delivered.
            (void)SendMessageW(window, WM_NULL, 0, 0);
            okay &= expect(galaxy::test_wait([&] { return IsWindowVisible(window) != FALSE; }), "the no-focus window is visible");
            okay &= expect(GetForegroundWindow() != window, "the no-focus window is not the foreground window");
            okay &= expect((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0, "it has the no-activate style");
            okay &= expect(activations.load() == 0, "it received no activation and no focus message");
        }
        owner.stop();
    }
    UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
    CloseHandle(entered);
    CloseHandle(release_owner);
    if (okay) std::cout << "Window owner lifecycle and consumer progress PASS\n";
    return okay ? 0 : 1;
}
