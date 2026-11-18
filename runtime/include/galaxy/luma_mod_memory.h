// A mod's memory as a host service reads and writes it (M-029): only through the kernel's copy_in, copy_out, copy_text
// and mod_code (luma_service_provider.h), so a pointer the mod got wrong (unmapped, freed, too short, protected, or a
// function in no code of the mod's) is an answer, LUMA_E_ARGUMENT, and never the end of the game. The provider reports
// it as a wrong argument, as for any it cannot use.
#pragma once

#include <luma_service_provider.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace galaxy::mod_memory {

// A text the mod gave, without its 0: LUMA_OK; LUMA_E_ARGUMENT: null, or unreadable before its 0; LUMA_E_TOO_LARGE:
// more than `most` bytes (out holds the first `most`). Read in pieces, so a text of any length up to `most` is whole.
inline std::int32_t text(const LumaServiceKernel* kernel, const LumaMod* mod, const char* p, std::size_t most,
                         std::string* out) {
    out->clear();
    if (p == nullptr) {
        return LUMA_E_ARGUMENT;
    }
    char piece[1024];
    for (;;) {
        std::uint64_t length = 0;
        const std::int32_t status = kernel->copy_text(mod, p + out->size(), piece, sizeof(piece), &length);
        if (status != LUMA_OK && status != LUMA_E_TOO_LARGE) {
            out->clear();
            return LUMA_E_ARGUMENT;
        }
        out->append(piece, static_cast<std::size_t>(length));
        if (out->size() > most) {
            out->resize(most);
            return LUMA_E_TOO_LARGE;
        }
        if (status == LUMA_OK) {
            return LUMA_OK;
        }
    }
}

// An optional text: null stays null (*given false), else as text().
inline std::int32_t text_or_null(const LumaServiceKernel* kernel, const LumaMod* mod, const char* p, std::size_t most,
                                 std::string* out, bool* given) {
    *given = p != nullptr;
    if (p == nullptr) {
        out->clear();
        return LUMA_OK;
    }
    return text(kernel, mod, p, most, out);
}

// `size` bytes the mod gave. LUMA_E_ARGUMENT: null with a size, or not all readable. LUMA_E_MEMORY: no room.
inline std::int32_t bytes(const LumaServiceKernel* kernel, const LumaMod* mod, const void* p, std::uint64_t size,
                          std::vector<std::uint8_t>* out) {
    out->clear();
    if (size == 0) {
        return LUMA_OK;
    }
    if (p == nullptr || size > SIZE_MAX) {
        return LUMA_E_ARGUMENT;
    }
    try {
        out->resize(static_cast<std::size_t>(size));
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    if (kernel->copy_in(mod, out->data(), p, size) != LUMA_OK) {
        out->clear();
        return LUMA_E_ARGUMENT;
    }
    return LUMA_OK;
}

// A struct the mod gave that starts with its size (uint32_t): *out the bytes it gave (its size, at most sizeof(T)),
// the rest zero, out->size the mod's. LUMA_E_ARGUMENT: null or unreadable. The caller checks the size against the least
// it takes, as before.
template <typename T>
inline std::int32_t sized(const LumaServiceKernel* kernel, const LumaMod* mod, const T* p, T* out) {
    std::uint32_t size = 0;
    if (luma_struct_size(kernel, mod, p, &size) != LUMA_OK) {
        *out = T{};
        return LUMA_E_ARGUMENT;
    }
    return luma_struct_in(kernel, mod, p, size, out);
}

// A value of fixed size (no size field) the mod gave. LUMA_E_ARGUMENT: null or unreadable.
template <typename T>
inline std::int32_t value(const LumaServiceKernel* kernel, const LumaMod* mod, const T* p, T* out) {
    if (p == nullptr) {
        return LUMA_E_ARGUMENT;
    }
    return kernel->copy_in(mod, out, p, sizeof(T));
}

// Whether fn lies in the mod's code (or a mod's it uses): a function it may give a service to call back.
inline bool code(const LumaServiceKernel* kernel, const LumaMod* mod, const void* fn) {
    return fn != nullptr && kernel->mod_code(mod, fn) == 1;
}

}  // namespace galaxy::mod_memory
