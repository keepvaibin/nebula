#include "galaxy/gx/window_owner.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
constexpr UINT kBlock = WM_APP + 1;
constexpr UINT kNested = WM_APP + 2;
HANDLE entered{};
HANDLE release_owner{};
std::atomic<DWORD> created_on{};
std::atomic<DWORD> destroyed_on{};
std::atomic<unsigned> destroyed_count{};

LRESULT CALLBACK test_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CREATE) created_on.store(GetCurrentThreadId());
    if (message == kBlock) {
        SetEvent(entered);
        (void)WaitForSingleObject(release_owner, 5000);
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
    galaxy::gx::WindowOwnerConfig config{
        window_class.lpszClassName, L"Hidden ownership test", WS_OVERLAPPEDWINDOW,
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, 0, 0, 160, 120, false};
    bool okay = true;
    const DWORD consumer_thread = GetCurrentThreadId();
    {
        galaxy::gx::WindowOwner owner;
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
        okay &= expect(WaitForSingleObject(entered, 2000) == WAIT_OBJECT_0,
                       "owner entered blocked handler");
        unsigned consumer_steps = 0;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(100);
        while (std::chrono::steady_clock::now() < deadline) ++consumer_steps;
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
                           WaitForSingleObject(entered, 2000) == WAIT_OBJECT_0,
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
    UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
    CloseHandle(entered);
    CloseHandle(release_owner);
    if (okay) std::cout << "Window owner lifecycle and consumer progress PASS\n";
    return okay ? 0 : 1;
}
