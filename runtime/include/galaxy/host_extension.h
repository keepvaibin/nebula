// SPDX-License-Identifier: GPL-3.0-only
// The host's extensions (nebula_host_extension.h, --extension): attached at start, before any other thread, and asked at
// fixed points. Without one each question is a load and a branch.
#pragma once

#include <luma_service_provider.h>
#include <nebula_host_extension.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace galaxy::extensions {

// Loads and attaches each file in order. False, with the reason in `error`, when one cannot load or declines.
[[nodiscard]] bool attach(std::span<const std::filesystem::path> files, const NebulaHostForExtension& host, std::string& error);

// Every extension's NEBULA_EXT_* flags together.
[[nodiscard]] std::uint32_t flags() noexcept;

// Game thread, once a game frame.
void frame(const NebulaExtensionFrame& frame) noexcept;

// Game thread, at each sample: the controls the extensions hold, together. False when none gives controls.
[[nodiscard]] bool controls(const NebulaExtensionFrame& frame, NebulaExtensionControls& controls) noexcept;
// Game thread, at each sample: player 1's motor strength now, given to the extensions that take it when it changes.
void motor(std::uint8_t strength) noexcept;

// Render thread: whether any extension keeps pictures, and the file the first one names for this present (null: none).
[[nodiscard]] bool keeps_pictures() noexcept;
[[nodiscard]] const char* picture_path(std::uint64_t game_frame, std::uint32_t kind) noexcept;

// Sound thread: whether any extension takes the mix, and the mix.
[[nodiscard]] bool hears_sound() noexcept;
void sound(const float* samples, std::uint32_t frames, std::uint32_t channels, std::uint32_t rate) noexcept;

// Game thread: an extension takes the mods as changed.
[[nodiscard]] bool mods_changed() noexcept;

// The process ends now (`again`: the game starts again after it): each extension's `ending`, once.
void ending(bool again) noexcept;

// The extensions' graphics memory budget in bytes (0: the card's own) and wait before each file read in microseconds.
[[nodiscard]] std::uint64_t vram_budget_bytes() noexcept;
[[nodiscard]] std::uint32_t disc_read_delay_us() noexcept;
// Each line the host logs, to every extension that listens ("<tag> <text>").
void log_line(const char* tag, const char* text) noexcept;
// A service an extension offers to mods by name, or null.
[[nodiscard]] const void* service(const char* name, std::uint32_t major, std::uint32_t minor) noexcept;
// The extensions' LumaCore service providers ("test." services, checked at attach), in attach order: `give` is called
// once for each.
void luma_providers(void (*give)(const LumaServiceProvider* provider, void* user), void* user) noexcept;

}  // namespace galaxy::extensions
