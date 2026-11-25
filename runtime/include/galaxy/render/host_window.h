// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The game window as the renderers see it, on every platform: its size and state, opening and closing it, showing
// it, its display's refresh rate, the settings menu's per-present step, and (Vulkan) its surface. Implemented by
// host_window_win32.cpp (Windows) and the SDL host's window (platform/sdl).

#include "galaxy/render/render_config.h"
#include "galaxy/render/variable_refresh.h"
#include "galaxy/render/window_owner.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#if defined(NEBULA_GPU_VULKAN)
#include <vulkan/vulkan_core.h>
#endif

namespace galaxy::render::host_window {

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
extern std::atomic<bool> borderless_active;
// The last internal resolution refused for the video-memory budget (0: none).
// The window is being moved or sized.
extern std::atomic<bool> move_size_active;
// The window was closed.
extern std::atomic<bool> quit_request;
// The latest client size (width << 32 | height), recorded by the window. The render thread resizes the swap chain at
// the top of begin_frame, between frames with no command list recording, so every GPU object changes on the thread
// that owns it.
extern std::atomic<std::uint64_t> pending_resize_extent;
// Set by the overlay when Match Output is switched on, so the render thread derives the scale for the current window.
// The window's drawing surface is withdrawn (Android: the activity out of sight, or a fold moving it to the other
// display; never on Windows, where closing hides the window and close_game_window destroys it after the renderer let its
// swap chain and surface go). The platform sets surface_withdrawn and then takes surface_use, which the renderer holds from acquiring a
// swap-chain image until it is presented, so once the platform holds it no present uses the surface. The renderer
// drops its swap chain and surface at its next frame, presents nothing until surface_withdrawn is cleared, then
// recreates both.
extern std::atomic<bool> surface_withdrawn;
extern std::timed_mutex surface_use;
// The platform stopped waiting for surface_use (Android: 2 s after surface_withdrawn) and let the surface go while the
// renderer held it (P-281). The renderer presents nothing more into it and runs the device-lost path; it clears this when
// it lets its own surface go, so a surface made after that never sees it.
extern std::atomic<bool> surface_overdue;

// Saves changed settings; false (kept dirty for a retry) when a file could not be written.
bool flush_native_settings_if_dirty();
// Starts the watch for a config.ini that Nebula Settings changes while the game runs (on the settings' own thread;
// the caller only starts it, once).
void watch_render_config_file();
// A hidden start (launch_args.h started_hidden): the window is never shown.
[[nodiscard]] bool hidden_window_mode_enabled();

// The game window at the renderer's start, owned by `owner`. Sets backbuffer_width/height; a null window (logged)
// when it cannot be created. `shown`: the player sees it already (Android, iOS); else the renderer calls show_window
// after its first present, unless the start is hidden (hidden_window_mode_enabled).
struct OpenedWindow {
    NativeWindow window = nullptr;
    bool shown = false;
};
[[nodiscard]] OpenedWindow open_game_window(WindowOwner& owner, int window_width, int window_height);
// Ends the window (the renderer's shutdown).
void close_game_window(WindowOwner& owner) noexcept;
// The window's first picture is presented: the player sees the window from now (open_game_window). Any thread.
void show_window(NativeWindow window) noexcept;
// A shown window the player cannot see now: hidden, minimised or cloaked (Windows), hidden or minimised (SDL), not
// visible on any screen (macOS). The Vulkan and Metal renderers skip acquiring and presenting while it holds, as
// Direct3D does on DXGI_STATUS_OCCLUDED; they ask at most every 100 ms. Any thread.
[[nodiscard]] bool window_unseen(NativeWindow window) noexcept;
// The refresh rate of the display the window is on, or 0 when unknown.
[[nodiscard]] double display_refresh_hz(NativeWindow window) noexcept;
// The display mode's own rate, never its vblanks' (Windows: the mode's whole hertz; a display whose refreshes follow the
// presents times them at the presents' rate); elsewhere the rate above. 0 when unknown.
[[nodiscard]] double display_mode_hz(NativeWindow window) noexcept;
// What the system says of that display (render/variable_refresh.h, G2.3.5): Android's Display.hasArrSupport (API 36),
// a Mac screen's refresh-interval range (macOS 12), iOS fixed; kUnknown where no call says it (Windows, Linux), where
// the presents prove it. `shortest_s` (may be null): the display's shortest interval where the system gives one
// (seconds; 0 otherwise). Render thread.
[[nodiscard]] RefreshKind system_refresh_kind(NativeWindow window, double* shortest_s = nullptr) noexcept;
// The display's vblanks: the latest and the interval between them, on galaxy::qpc_now()'s clock (Windows: timed on
// the window's monitor; Android: Choreographer's); false while not known.
// An event set when the window is minimised or restored and when it closes (the main thread sleeps on it).
void set_state_wake(void* event) noexcept;
// The grid as last timed, without starting the timing.
[[nodiscard]] bool vblank_grid_known(std::int64_t& vblank, std::int64_t& period) noexcept;
[[nodiscard]] bool vblank_grid(NativeWindow window, std::int64_t& vblank, std::int64_t& period) noexcept;
// A present the renderer saw reach the display at `stamp` (galaxy::qpc_now()'s clock; `exact`: present timing's own
// time, else the moment present wait returned after waiting): Linux's display clock is fitted from these
// (platform/sdl/display_clock.cpp, I-005); every other platform has its own vblanks and takes nothing. Render thread.
void note_present_shown(std::int64_t stamp, bool exact) noexcept;
// Where the game's frames follow the display's vsync (Android): the latest frame slot, the frame period, the
// display's vsync interval and now (CLOCK_MONOTONIC nanoseconds), for presents timed to it; else false.
[[nodiscard]] bool present_grid(std::int64_t& slot_ns, std::int64_t& period_ns, std::int64_t& vsync_ns,
                                std::int64_t& now_ns) noexcept;

// Each present: the vendor upscaler download's progress.
void native_settings_present();
// Opens the settings menu on that page (a tab's number), its first row chosen.
void open_native_settings(unsigned page);
// The settings menu on MODS, whose question then asks about the first mod that is on with code the player hasn't
// answered for (one imported outside the game: Android's launcher, the mods folder). False, and nothing opens, when
// none waits.
bool open_native_settings_for_waiting_code();
// The settings menu on SYSTEM with the benchmark's results window for `result` (a result file, or the results folder
// for its newest): the start after a benchmark.
void open_native_settings_results(const std::filesystem::path& result);

// What the settings menu (runtime/src/ui/native_settings.cpp) asks of its platform.
struct SettingsMonitor {
    std::string id, name;  // id: the value of RenderConfig::monitor
    unsigned width = 0, height = 0, refresh_hz = 0;
    long max_client_width = 0, max_client_height = 0;  // the work area less the window's frame
    bool primary = false;
    std::vector<std::pair<unsigned, unsigned>> resolutions;
};
[[nodiscard]] std::vector<SettingsMonitor> settings_monitors();
// A window menu is open or the window is being moved or sized: the game takes no input.
[[nodiscard]] bool window_modal_active() noexcept;
// The window's inside to that size in pixels, from any thread (an extension's resize_window).
void resize_window_client(unsigned width, unsigned height) noexcept;
enum class FilePick { Miis, ModArchives, ModFolders, Saves };
// The platform's file picker; `done` runs off the window's thread with what was chosen. Android picks documents
// (no folders: ModFolders is not offered there) and hands over copies the game can read by path. Saves picks one file.
void pick_files(FilePick kind, std::function<void(std::vector<std::filesystem::path>)> done);
// The platform's save dialog for a file the game made (an export): the player chooses where it goes, under `file`'s
// name, and it is copied there. `done` runs off the window's thread.
enum class SaveCopy { Saved, Cancelled, Failed };
void save_copy(const std::filesystem::path& file, std::function<void(SaveCopy)> done);
// Shows a folder of the game's in the platform's file manager; folders_open is false where none can (Android: the
// app's own storage is private to it).
void open_folder(const std::filesystem::path& folder);
[[nodiscard]] bool folders_open() noexcept;
struct UpscalerCaps {
    bool dlss = false, xess = false, frame_generation = false, frame_generation_dynamic = false, fsr_frame_generation = false;
    unsigned frame_generation_max = 0, frames_presented = 1;
};
[[nodiscard]] UpscalerCaps upscaler_caps() noexcept;
[[nodiscard]] unsigned dlss_min_width(unsigned quality, unsigned width, unsigned height) noexcept;
[[nodiscard]] bool dlss_files_present();
[[nodiscard]] bool xess_files_present();
[[nodiscard]] bool frame_generation_files_present();
// DLSS (upscaler 3) or XeSS (upscaler 4) from its maker's release, on a thread of its own.
[[nodiscard]] unsigned vendor_megabytes(unsigned upscaler);
[[nodiscard]] bool start_vendor_install(unsigned upscaler);
void cancel_vendor_install();
enum class VendorInstall { Running, Done, Canceled, Failed };
// `percent`: of what it has to read.
[[nodiscard]] VendorInstall poll_vendor_install(unsigned& percent);
// The maker's license, read from the release.
void fetch_vendor_license(unsigned upscaler);
enum class VendorLicense { Loading, Ready, Failed };
[[nodiscard]] VendorLicense vendor_license(std::string& text);
// What a start-again starts: the game as usual, or a diagnostics run (--diagnostics 1).
enum class Restart : int { Start = 1, Diagnostics = 2 };
// Ends the game as Quit does and starts it again once it has closed; false where this host cannot (can_restart).
[[nodiscard]] bool restart_game(Restart kind);
// The start-again restart_game asked for, read by the host as the game ends: 0 none, else a Restart's value.
[[nodiscard]] int restart_wanted() noexcept;
// This host can start the game again (not iOS, where an app cannot start itself).
[[nodiscard]] bool can_restart() noexcept;
#if defined(__ANDROID__)
// The activity starts the game again in a new process (GameActivity.restartGame) and ends this one: the host's last
// step (host_main finish_and_exit), once the saves are written.
void start_again(Restart kind);
#endif
// Ends the game as the window's close does (the settings' Update on Windows, once Nebula Setup has started).
void quit_game() noexcept;

extern std::atomic<bool> rtx_adapter;          // the session's GPU is an NVIDIA RTX (DLSS)
[[nodiscard]] float upscaler_render_fraction(const RenderConfig& render, unsigned output_width, unsigned output_height);
void save_native_settings_now();

#if defined(NEBULA_GPU_METAL)
// The window's Metal layer (a CAMetalLayer*, the window's to keep), which the Metal renderer draws to; null when the
// window has none. Its drawable size is the renderer's to set.
[[nodiscard]] void* metal_layer(NativeWindow window);
// The Metal display link (CAMetalDisplayLink, macOS 14 and iOS 17), which times the display clock and gives the
// layer's drawables, each with the time it will be shown: the layer it runs on (null: none, the older link and the
// layer's own drawables as before). Nothing where the system has no such link.
void metal_link_layer(void* layer) noexcept;
// The link's next drawable (a retained CAMetalDrawable*, the caller's to release) and the time it will be shown
// (CACurrentMediaTime's seconds), waiting up to `wait_ns`. False where no link gives drawables or none came in time
// (a held link, a display that gives it no updates): the caller takes the layer's own.
[[nodiscard]] bool metal_link_drawable(void*& drawable, double& shown_at, std::int64_t wait_ns) noexcept;
// In-between pictures fill the display's fastest rate (the display link asks that rate; else 60 with a range up to it).
void set_display_fills(bool fills) noexcept;
#endif

#if defined(NEBULA_GPU_VULKAN)
// The instance extensions the window's surface needs, and the surface (VK_NULL_HANDLE when it cannot be made).
[[nodiscard]] std::vector<const char*> vulkan_instance_extensions();
[[nodiscard]] VkSurfaceKHR create_vulkan_surface(VkInstance instance, NativeWindow window);
// What the graphics driver wrote to the platform's log from this process (Android: its log lines other than the
// game's own; elsewhere nothing), for a report when the driver refuses something.
[[nodiscard]] std::string driver_log();
#endif

}  // namespace galaxy::render::host_window
