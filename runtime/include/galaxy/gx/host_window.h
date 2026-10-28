// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The game window as the renderers see it, on every platform: its size and state, opening and closing it, showing
// it, its display's refresh rate, the settings overlay and HOME Menu drawing, and (Vulkan) its surface. Implemented by
// host_window_win32.cpp (Windows) and the SDL host's window (platform/sdl).

#include "galaxy/gx/ui_canvas.h"
#include "galaxy/gx/window_owner.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#if defined(NEBULA_GPU_VULKAN)
#include <vulkan/vulkan_core.h>
#endif

namespace galaxy::gx::host_window {

#if defined(_WIN32)
using NativeWindow = HWND;
#else
using NativeWindow = void*;  // SDL_Window*
#endif

// The swap chain's size (the client area), set by the renderer at start and on resize.
extern unsigned backbuffer_width;
extern unsigned backbuffer_height;
// Swap-chain presents per second (any source), for the FRAME RATE row.
extern std::atomic<double> measured_present_hz;
// The mouse wheel's turn the game has not taken yet (pointer_wheel in nebula_game_api.h), in 120ths of a notch,
// positive away from the player; the settings menu takes the wheel itself while it shows.
extern std::atomic<std::int32_t> pointer_wheel;
// The game's files (set_native_settings_content_root in gx_d3d12.h).
extern std::string native_settings_content_root;
extern std::atomic<bool> borderless_active;
extern std::atomic<bool> exclusive_fullscreen_active;
// The last internal resolution refused for the video-memory budget (0: none).
extern std::atomic<unsigned> efb_scale_rejected;
// The window is being moved or sized.
extern std::atomic<bool> move_size_active;
// The window was closed.
extern std::atomic<bool> quit_request;
// The latest client size (width << 32 | height), recorded by the window. The render thread resizes the swap chain at
// the top of begin_frame, between frames with no command list recording, so every GPU object changes on the thread
// that owns it.
extern std::atomic<std::uint64_t> pending_resize_extent;
// Set by the overlay when Match Output is switched on, so the render thread derives the scale for the current window.
extern std::atomic<bool> follow_window_rearm;
// The window's drawing surface is withdrawn (Android: the activity out of sight, or a fold moving it to the other
// display). The platform sets surface_withdrawn and then takes surface_use, which the renderer holds from acquiring a
// swap-chain image until it is presented, so once the platform holds it no present uses the surface. The renderer
// drops its swap chain and surface at its next frame, presents nothing until surface_withdrawn is cleared, then
// recreates both. Windows never withdraws its window's surface.
extern std::atomic<bool> surface_withdrawn;
extern std::timed_mutex surface_use;

// The internal resolution that fills the display.
[[nodiscard]] unsigned display_efb_scale_cap();
// Saves changed settings; false (kept dirty for a retry) when a file could not be written.
bool flush_native_settings_if_dirty();
// Takes a config.ini that Nebula Settings changed while the game runs.
void poll_render_config_file();
// GALAXY_HIDE_WINDOW: a hidden test window.
[[nodiscard]] bool hidden_window_mode_enabled();
// GALAXY_TEST_SHOWN_WINDOW with it: 0 hidden, 1 one pixel on the main monitor, 2 whole in its corner (captures).
[[nodiscard]] unsigned shown_test_window_mode();

// The game window at the renderer's start, owned by `owner`. Sets backbuffer_width/height; a null window (logged)
// when it cannot be created.
struct OpenedWindow {
    NativeWindow window = nullptr;
    bool shown = false;
};
[[nodiscard]] OpenedWindow open_game_window(WindowOwner& owner, int window_width, int window_height);
// Ends the window (the renderer's shutdown).
void close_game_window(WindowOwner& owner) noexcept;
// Shows a window opened hidden, after its first present.
void show_window(NativeWindow window) noexcept;
// The refresh rate of the display the window is on, or 0 when unknown.
[[nodiscard]] double display_refresh_hz(NativeWindow window) noexcept;
// The display's vblanks: the latest and the interval between them, on galaxy::qpc_now()'s clock (Windows: timed on
// the window's monitor; Android: Choreographer's); false while not known.
[[nodiscard]] bool vblank_grid(NativeWindow window, std::int64_t& vblank, std::int64_t& period) noexcept;
// Where the game's retrace follows the display's vsync (Android): the latest retrace, the retrace period, the
// display's vsync interval and now (CLOCK_MONOTONIC nanoseconds), for presents timed to it; else false.
[[nodiscard]] bool present_grid(std::int64_t& retrace_ns, std::int64_t& period_ns, std::int64_t& vsync_ns,
                                std::int64_t& now_ns) noexcept;

void draw_native_settings_overlay(UiCanvas& renderer);
void draw_home_menu(UiCanvas& renderer);

#if defined(NEBULA_GPU_VULKAN)
// The instance extensions the window's surface needs, and the surface (VK_NULL_HANDLE when it cannot be made).
[[nodiscard]] std::vector<const char*> vulkan_instance_extensions();
[[nodiscard]] VkSurfaceKHR create_vulkan_surface(VkInstance instance, NativeWindow window);
// What the graphics driver wrote to the platform's log from this process (Android: its log lines other than the
// game's own; elsewhere nothing), for a report when the driver refuses something.
[[nodiscard]] std::string driver_log();
#endif

}  // namespace galaxy::gx::host_window
