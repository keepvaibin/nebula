// SPDX-License-Identifier: GPL-3.0-only
// A function's first byte, from a mod library's own unwind tables (luma_guard.cpp function_at: a cut names the export
// it was in). The tables are the mod's file: every read is bounded by the bytes the caller says are there, so a
// malformed or hostile table gives 0 (no function named), never a read outside them. Pure: tests and the fuzz target
// (tools/fuzz/luma_unwind_fuzz.cpp) give it any bytes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace galaxy::luma_guard {

// ELF .eh_frame_hdr (the PT_GNU_EH_FRAME segment): version 1, then the encodings of eh_frame_ptr, fde_count and the
// table, then a sorted table of (function start, FDE) pairs, each a datarel sdata4 (0x3B), counted from the header's
// own address. `hdr` is its first byte with `size` bytes readable from there; `address` is where it is loaded (hdr's
// address in the process; a test passes its own). The largest function start at or below pc, or 0.
inline std::uintptr_t eh_frame_hdr_function(const std::uint8_t* hdr, std::size_t size, std::uintptr_t address,
                                            std::uintptr_t pc) noexcept {
    if (hdr == nullptr || size < 4u || hdr[0] != 1u || hdr[3] != 0x3Bu) return 0;
    std::size_t at = 4;
    // A 4-byte value in `encoding` (udata4 or sdata4; absolute, pcrel or datarel).
    const auto read = [&](std::uint8_t encoding, bool absolute_only, std::uintptr_t* out) noexcept -> bool {
        const unsigned form = encoding & 0x0Fu;
        const unsigned apply = encoding & 0x70u;
        if ((form != 0x03u && form != 0x0Bu) || size - at < 4u) return false;
        if (absolute_only && apply != 0x00u) return false;
        std::uint32_t raw = 0;
        std::memcpy(&raw, hdr + at, 4u);
        const std::uintptr_t value = form == 0x0Bu ? static_cast<std::uintptr_t>(static_cast<std::intptr_t>(static_cast<std::int32_t>(raw)))
                                                   : static_cast<std::uintptr_t>(raw);
        const std::uintptr_t here = address + at;
        at += 4u;
        switch (apply) {
        case 0x00u: *out = value; return true;
        case 0x10u: *out = here + value; return true;     // pcrel
        case 0x30u: *out = address + value; return true;  // datarel
        default: return false;
        }
    };
    std::uintptr_t eh_frame = 0, count = 0;
    if (!read(hdr[1], false, &eh_frame) || !read(hdr[2], true, &count)) return 0;
    // The table must lie wholly in the bytes given.
    if (count == 0u || count > (size - at) / 8u) return 0;
    const std::uint8_t* const table = hdr + at;
    const auto start = [&](std::size_t i) noexcept {
        std::int32_t v = 0;
        std::memcpy(&v, table + i * 8u, 4u);
        return address + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(v));
    };
    std::size_t low = 0, high = static_cast<std::size_t>(count);  // the first entry whose start is above pc
    while (low < high) {
        const std::size_t mid = low + (high - low) / 2u;
        if (start(mid) <= pc) {
            low = mid + 1u;
        } else {
            high = mid;
        }
    }
    return low != 0u ? start(low - 1u) : 0u;
}

// Windows x64: a RUNTIME_FUNCTION (begin, end, unwind info: offsets in the image) and the UNWIND_INFO it names. A
// function's later parts chain (UNW_FLAG_CHAININFO) to its first entry, whose begin is the function's. `image` is the
// image's first byte with `size` bytes in it (SizeOfImage); `entry` is the offset of the RUNTIME_FUNCTION the
// system's lookup gave. Each step of the chain is given to `visit(begin, info)` (info: the UNWIND_INFO's first 4 bytes,
// in the image) until it returns true or the chain ends (its last entry is the function's first). False when a read
// would leave the image or the chain is longer than 32.
template <typename Visit>
bool pe_unwind_chain(const std::uint8_t* image, std::size_t size, std::size_t entry, Visit&& visit) noexcept {
    constexpr unsigned kChainInfo = 0x4u;  // UNW_FLAG_CHAININFO
    for (unsigned depth = 0; depth < 32u; ++depth) {
        if (image == nullptr || entry > size || size - entry < 12u) return false;
        std::uint32_t begin = 0, unwind = 0;
        std::memcpy(&begin, image + entry, 4u);
        std::memcpy(&unwind, image + entry + 8u, 4u);
        if (unwind > size || size - unwind < 4u) return false;
        const std::uint8_t* const info = image + unwind;
        if (visit(begin, info)) return true;
        if (((info[0] >> 3) & kChainInfo) == 0u) return true;
        // The chained RUNTIME_FUNCTION follows the unwind codes (2 bytes each, an even count).
        entry = static_cast<std::size_t>(unwind) + 4u + ((info[2] + 1u) & ~1u) * 2u;
    }
    return false;
}

// The function's first byte as an offset in the image (the begin of the chain's last entry), or 0.
inline std::uint32_t pe_function_begin(const std::uint8_t* image, std::size_t size, std::size_t entry) noexcept {
    std::uint32_t first = 0;
    const bool whole = pe_unwind_chain(image, size, entry, [&](std::uint32_t begin, const std::uint8_t*) noexcept {
        first = begin;
        return false;
    });
    return whole ? first : 0u;
}

// The function's frame register (UNWIND_INFO's FrameRegister, the first one along the chain): the unwind reads the
// frame's saved words from that register's value. 0: none; -1: a read would leave the image.
inline int pe_frame_register(const std::uint8_t* image, std::size_t size, std::size_t entry) noexcept {
    int reg = 0;
    const bool whole = pe_unwind_chain(image, size, entry, [&](std::uint32_t, const std::uint8_t* info) noexcept {
        reg = info[3] & 0x0F;
        return reg != 0;
    });
    return whole ? reg : -1;
}

}  // namespace galaxy::luma_guard
