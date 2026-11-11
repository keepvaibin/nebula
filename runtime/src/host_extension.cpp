// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/host_extension.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace galaxy::extensions {
namespace {

constexpr std::size_t kMostExtensions = 8u;

// Written by attach before any other thread starts, then only read. Each entry is the extension's structure as far as it
// gave one; the rest zero (absent).
std::array<NebulaHostExtension, kMostExtensions> g_extensions{};
std::size_t g_count = 0u;
std::uint32_t g_flags = 0u;
bool g_controls = false;
bool g_pictures = false;
bool g_sound = false;
std::uint32_t g_vram_budget_mb = 0u;
std::uint32_t g_disc_read_delay_us = 0u;

void* load_library(const std::filesystem::path& file, std::string& error) {
#if defined(_WIN32)
    // Its own folder first for what it loads, as a mod plugin's.
    HMODULE library = LoadLibraryExW(file.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (library == nullptr) {
        error = "it did not load (error " + std::to_string(GetLastError()) + ")";
    }
    return library;
#else
    void* library = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        const char* why = dlerror();
        error = why != nullptr ? why : "it did not load";
    }
    return library;
#endif
}

NebulaExtensionAttach find_attach(void* library) {
#if defined(_WIN32)
    return reinterpret_cast<NebulaExtensionAttach>(
        reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), "nebula_extension_attach")));
#else
    return reinterpret_cast<NebulaExtensionAttach>(dlsym(library, "nebula_extension_attach"));
#endif
}

}  // namespace

bool attach(std::span<const std::filesystem::path> files, const NebulaHostForExtension& host, std::string& error) {
    for (const std::filesystem::path& file : files) {
        const std::string name = file.filename().string();
        if (g_count == kMostExtensions) {
            error = name + ": more than " + std::to_string(kMostExtensions) + " extensions";
            return false;
        }
        std::string why;
        void* const library = load_library(file, why);
        if (library == nullptr) {
            error = name + ": " + why;
            return false;
        }
        const NebulaExtensionAttach attach_one = find_attach(library);
        if (attach_one == nullptr) {
            error = name + ": no nebula_extension_attach";
            return false;
        }
        const NebulaHostExtension* const given = attach_one(&host);
        if (given == nullptr) {
            error = name + ": it declined";
            return false;
        }
        if (given->version == 0u || given->version > NEBULA_HOST_EXTENSION_VERSION ||
            given->size < offsetof(NebulaHostExtension, flags) + sizeof(given->flags)) {
            error = name + ": an extension version this host does not know";
            return false;
        }
        NebulaHostExtension& kept = g_extensions[g_count++];
        std::memcpy(&kept, given, std::min<std::size_t>(given->size, sizeof(kept)));
        g_flags |= kept.flags;
        g_controls = g_controls || kept.controls != nullptr;
        g_pictures = g_pictures || kept.picture_path != nullptr;
        g_sound = g_sound || kept.sound != nullptr;
        if (kept.vram_budget_mb != 0u && (g_vram_budget_mb == 0u || kept.vram_budget_mb < g_vram_budget_mb)) {
            g_vram_budget_mb = kept.vram_budget_mb;
        }
        g_disc_read_delay_us = std::max(g_disc_read_delay_us, kept.disc_read_delay_us);
    }
    return true;
}

std::uint64_t vram_budget_bytes() noexcept {
    return std::uint64_t{g_vram_budget_mb} << 20u;
}

std::uint32_t disc_read_delay_us() noexcept {
    return g_disc_read_delay_us;
}

void log_line(const char* tag, const char* text) noexcept {
    bool any = false;
    for (std::size_t i = 0; i < g_count; ++i) {
        any = any || g_extensions[i].log_line != nullptr;
    }
    if (!any) {
        return;
    }
    // Given as it is: a line of any length reaches the extension whole.
    for (std::size_t i = 0; i < g_count; ++i) {
        if (g_extensions[i].log_line != nullptr) {
            g_extensions[i].log_line(g_extensions[i].user, tag != nullptr ? tag : "", text != nullptr ? text : "");
        }
    }
}

const void* service(const char* name, std::uint32_t major, std::uint32_t minor) noexcept {
    if (name == nullptr) {
        return nullptr;
    }
    for (std::size_t i = 0; i < g_count; ++i) {
        if (g_extensions[i].service != nullptr) {
            if (const void* const table = g_extensions[i].service(g_extensions[i].user, name, major, minor)) {
                return table;
            }
        }
    }
    return nullptr;
}

std::uint32_t flags() noexcept {
    return g_flags;
}

void frame(const NebulaExtensionFrame& frame) noexcept {
    for (std::size_t i = 0; i < g_count; ++i) {
        if (g_extensions[i].frame != nullptr) {
            g_extensions[i].frame(g_extensions[i].user, &frame);
        }
    }
}

bool controls(const NebulaExtensionFrame& frame, NebulaExtensionControls& controls) noexcept {
    controls = {};
    if (!g_controls) {
        return false;
    }
    for (std::size_t i = 0; i < g_count; ++i) {
        if (g_extensions[i].controls == nullptr) {
            continue;
        }
        NebulaExtensionControls one{};
        g_extensions[i].controls(g_extensions[i].user, &frame, &one);
        controls.buttons |= one.buttons;
        controls.shake = static_cast<std::uint8_t>(controls.shake | (one.shake != 0u ? 1u : 0u));
        if (one.has_stick != 0u) {
            controls.has_stick = 1u;
            controls.stick_x = one.stick_x;
            controls.stick_y = one.stick_y;
        }
        if (one.has_pointer != 0u) {
            controls.has_pointer = 1u;
            controls.pointer_x = one.pointer_x;
            controls.pointer_y = one.pointer_y;
        }
    }
    return true;
}

bool keeps_pictures() noexcept {
    return g_pictures;
}

const char* picture_path(std::uint64_t game_frame, std::uint32_t kind) noexcept {
    const char* path = nullptr;
    for (std::size_t i = 0; i < g_count; ++i) {
        if (g_extensions[i].picture_path == nullptr) {
            continue;
        }
        // Each is asked (each counts the presents); the first that names a file has it.
        const char* const named = g_extensions[i].picture_path(g_extensions[i].user, game_frame, kind);
        if (path == nullptr) {
            path = named;
        }
    }
    return path;
}

bool hears_sound() noexcept {
    return g_sound;
}

void sound(const float* samples, std::uint32_t frames, std::uint32_t channels, std::uint32_t rate) noexcept {
    for (std::size_t i = 0; i < g_count; ++i) {
        if (g_extensions[i].sound != nullptr) {
            g_extensions[i].sound(g_extensions[i].user, samples, frames, channels, rate);
        }
    }
}

bool mods_changed() noexcept {
    bool changed = false;
    for (std::size_t i = 0; i < g_count; ++i) {
        if (g_extensions[i].mods_changed != nullptr && g_extensions[i].mods_changed(g_extensions[i].user) != 0u) {
            changed = true;
        }
    }
    return changed;
}

}  // namespace galaxy::extensions
