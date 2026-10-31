// SPDX-License-Identifier: GPL-3.0-only
// The cooker library (nebula_import.dll / libnebula_import.so, built from crates/nebula-import) loaded from beside
// the executable on first use.
#include "galaxy/cooker_bridge.h"

#include <cctype>
#include <filesystem>
#include <mutex>

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

namespace galaxy::cook {
namespace {

using CookFile = int (*)(const char* name, const std::uint8_t* data, std::size_t size, std::uint8_t** out,
                         std::size_t* out_size);
using CookFree = void (*)(std::uint8_t* data, std::size_t size);

struct Library {
    CookFile cook = nullptr;
    CookFree release = nullptr;
};

const Library& library() {
    static Library loaded;
    static std::once_flag once;
    std::call_once(once, [] {
#if defined(_WIN32)
        wchar_t exe[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (n == 0 || n == MAX_PATH) {
            return;
        }
        const std::filesystem::path dll = std::filesystem::path(exe).parent_path() / L"nebula_import.dll";
        HMODULE module = LoadLibraryW(dll.c_str());
        if (module == nullptr) {
            return;
        }
        loaded.cook = reinterpret_cast<CookFile>(GetProcAddress(module, "nebula_cook_file"));
        loaded.release = reinterpret_cast<CookFree>(GetProcAddress(module, "nebula_cook_free"));
#else
        void* module = dlopen("libnebula_import.so", RTLD_NOW);
        if (module == nullptr) {
            return;
        }
        loaded.cook = reinterpret_cast<CookFile>(dlsym(module, "nebula_cook_file"));
        loaded.release = reinterpret_cast<CookFree>(dlsym(module, "nebula_cook_free"));
#endif
        if (loaded.cook == nullptr || loaded.release == nullptr) {
            loaded = Library{};
        }
    });
    return loaded;
}

}  // namespace

bool cooks(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos) {
        return false;
    }
    std::string extension(name.substr(dot + 1));
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension == "arc" || extension == "szs" || extension == "bti" || extension == "bmd" || extension == "bdl" ||
           extension == "tpl" || extension == "brfnt" || extension == "jpc";
}

std::optional<std::vector<std::uint8_t>> cook_file(std::string_view name, std::span<const std::uint8_t> bytes,
                                                   std::string* error) {
    const Library& lib = library();
    if (lib.cook == nullptr) {
        if (error != nullptr) {
            *error = "the cooker (nebula_import) is not beside the game";
        }
        return std::nullopt;
    }
    const std::string path(name);
    std::uint8_t* out = nullptr;
    std::size_t out_size = 0;
    const int result = lib.cook(path.c_str(), bytes.data(), bytes.size(), &out, &out_size);
    if (result < 0) {
        if (error != nullptr) {
            *error = "the cooker cannot read " + path;
        }
        return std::nullopt;
    }
    if (result == 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> cooked(out, out + out_size);
    lib.release(out, out_size);
    return cooked;
}

}  // namespace galaxy::cook
