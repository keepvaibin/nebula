#include "galaxy/flat_guest_memory.h"

// GPLv3 source adaptation from WiiCompiled's
// runtime/src/guest_flat_memory.cpp::{EnsureReservation, MapGuestView,
// Initialize, HostPointer} at commit
// 83463764b8acda394e058b0c689a10b8561fc380:
// https://github.com/patchzyy/Wiicompiled/blob/83463764b8acda394e058b0c689a10b8561fc380/runtime/src/guest_flat_memory.cpp
// Galaxy-specific changes: fixed RMGE01 MEM1/MEM2 extents, no Mario Kart MMIO
// assumptions, no executable/deferred-read hooks yet, and strict unmapped
// failure. No handler commits a previously unmapped guest page.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace galaxy::host::flat_guest_memory {
namespace {

constexpr std::size_t kAllocationGranularity = 0x10000u;
constexpr DWORD kMemReplacePlaceholder = 0x00004000u;
constexpr DWORD kMemReservePlaceholder = 0x00040000u;
constexpr DWORD kMemPreservePlaceholder = 0x00000002u;

using VirtualAlloc2Fn = PVOID(WINAPI*)(
    HANDLE, PVOID, SIZE_T, ULONG, ULONG, void*, ULONG);
using MapViewOfFile3Fn = PVOID(WINAPI*)(
    HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, void*, ULONG);

struct SharedSection {
    HANDLE mapping = nullptr;
    std::byte* host_view = nullptr;
};

std::mutex g_init_mutex;
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_owner_occupied{false};
bool g_initialization_failed = false;
std::byte* g_guest_base = nullptr;
SharedSection g_mem1;
SharedSection g_mem2;

[[noreturn]] void fail_win32(const char* operation) {
    std::ostringstream message;
    message << operation << " failed (GetLastError=" << GetLastError() << ')';
    throw std::runtime_error(message.str());
}

template <typename Function>
Function require_kernelbase_symbol(HMODULE kernelbase, const char* name) {
    auto* symbol = GetProcAddress(kernelbase, name);
    if (symbol == nullptr) {
        throw std::runtime_error(
            std::string("flat guest memory requires Windows VirtualAlloc2/MapViewOfFile3; missing ") +
            name);
    }
    return reinterpret_cast<Function>(symbol);
}

SharedSection create_section(std::uint32_t size) {
    SharedSection section{};
    section.mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0u, size, nullptr);
    if (section.mapping == nullptr) {
        fail_win32("CreateFileMappingW for guest RAM");
    }
    section.host_view = static_cast<std::byte*>(
        MapViewOfFile(section.mapping, FILE_MAP_ALL_ACCESS, 0u, 0u, size));
    if (section.host_view == nullptr) {
        const DWORD error = GetLastError();
        CloseHandle(section.mapping);
        SetLastError(error);
        fail_win32("MapViewOfFile for host guest-RAM alias");
    }
    return section;
}

void map_guest_alias(
    MapViewOfFile3Fn map_view_of_file3,
    const SharedSection& section,
    std::uint32_t guest_address,
    std::uint32_t size) {
    auto* target = g_guest_base + guest_address;
    // Mirrors WiiCompiled's placeholder split and exact-address view
    // replacement; every span is 64-KiB aligned and disjoint. A failed split
    // is not fatal: VirtualFree returns ERROR_INVALID_PARAMETER when the range
    // is already an exact-size placeholder. MapViewOfFile3 is the real check.
    VirtualFree(target, size, MEM_RELEASE | kMemPreservePlaceholder);
    void* const view = map_view_of_file3(
        section.mapping, GetCurrentProcess(), target, 0u, size,
        kMemReplacePlaceholder, PAGE_READWRITE, nullptr, 0u);
    if (view == nullptr) {
        fail_win32("MapViewOfFile3 exact guest alias");
    }
    if (view != target) {
        throw std::runtime_error(
            "MapViewOfFile3 placed a guest alias away from its required address");
    }
}

std::byte* mapped_host_pointer(
    std::uint32_t guest_address,
    std::uint32_t size) noexcept {
    if (size == 0u ||
        static_cast<std::uint64_t>(guest_address) + size > kGuestSpaceSize) {
        return nullptr;
    }
    const std::uint32_t alias = guest_address & 0xF0000000u;
    const std::uint32_t offset = guest_address & 0x0FFFFFFFu;
    if ((alias == 0x00000000u || alias == 0x80000000u ||
         alias == 0xC0000000u) &&
        offset <= kMem1Size && size <= kMem1Size - offset) {
        return g_mem1.host_view + offset;
    }
    if ((alias == 0x10000000u || alias == 0x90000000u ||
         alias == 0xD0000000u) &&
        offset <= kMem2Size && size <= kMem2Size - offset) {
        return g_mem2.host_view + offset;
    }
    return nullptr;
}

}  // namespace

void initialize() {
    const std::lock_guard<std::mutex> lock(g_init_mutex);
    if (g_initialized.load(std::memory_order_acquire)) {
        return;
    }
    if (g_initialization_failed) {
        throw std::runtime_error(
            "flat guest memory initialization previously failed; restart the process");
    }
    g_initialization_failed = true;

    HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
    if (kernelbase == nullptr) {
        kernelbase = LoadLibraryW(L"kernelbase.dll");
    }
    if (kernelbase == nullptr) {
        fail_win32("LoadLibraryW(kernelbase.dll)");
    }
    const auto virtual_alloc2 =
        require_kernelbase_symbol<VirtualAlloc2Fn>(kernelbase, "VirtualAlloc2");
    const auto map_view_of_file3 =
        require_kernelbase_symbol<MapViewOfFile3Fn>(kernelbase, "MapViewOfFile3");

    void* const requested = reinterpret_cast<void*>(kGuestBase);
    void* const reservation = virtual_alloc2(
        GetCurrentProcess(), requested,
        static_cast<SIZE_T>(kGuestSpaceSize + kAllocationGranularity),
        MEM_RESERVE | kMemReservePlaceholder, PAGE_NOACCESS, nullptr, 0u);
    if (reservation == nullptr) {
        fail_win32("VirtualAlloc2 exact 4-GiB guest reservation");
    }
    if (reservation != requested) {
        VirtualFree(reservation, 0u, MEM_RELEASE);
        throw std::runtime_error(
            "VirtualAlloc2 placed the guest reservation away from its required fixed base");
    }
    g_guest_base = static_cast<std::byte*>(reservation);

    // Both alias groups map the *same* section pages. Host consumers use the
    // ordinary section view, so memory is not copied at a frame boundary.
    g_mem1 = create_section(kMem1Size);
    g_mem2 = create_section(kMem2Size);
    map_guest_alias(map_view_of_file3, g_mem1, 0x00000000u, kMem1Size);
    map_guest_alias(map_view_of_file3, g_mem1, 0x80000000u, kMem1Size);
    map_guest_alias(map_view_of_file3, g_mem1, 0xC0000000u, kMem1Size);
    map_guest_alias(map_view_of_file3, g_mem2, 0x10000000u, kMem2Size);
    map_guest_alias(map_view_of_file3, g_mem2, 0x90000000u, kMem2Size);
    map_guest_alias(map_view_of_file3, g_mem2, 0xD0000000u, kMem2Size);

    g_initialized.store(true, std::memory_order_release);
}

bool is_initialized() noexcept {
    return g_initialized.load(std::memory_order_acquire);
}

std::byte* mem1_host_view() noexcept {
    return is_initialized() ? g_mem1.host_view : nullptr;
}

std::byte* mem2_host_view() noexcept {
    return is_initialized() ? g_mem2.host_view : nullptr;
}

std::byte* host_pointer(
    std::uint32_t guest_address,
    std::uint32_t size) noexcept {
    return is_initialized() ? mapped_host_pointer(guest_address, size) : nullptr;
}

bool is_mapped_ram_span(
    std::uint32_t guest_address,
    std::uint32_t size) noexcept {
    return host_pointer(guest_address, size) != nullptr;
}

OwnerLease::~OwnerLease() {
    if (active_) {
        g_owner_occupied.store(false, std::memory_order_release);
    }
}

bool OwnerLease::acquire() {
    if (active_) {
        return true;
    }
    bool expected = false;
    if (!g_owner_occupied.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return false;
    }
    try {
        initialize();
        // Views persist across sequential owners; fresh guests start with
        // the same zero-filled RAM image as the vector-backed path.
        std::memset(mem1_host_view(), 0, kMem1Size);
        std::memset(mem2_host_view(), 0, kMem2Size);
    } catch (...) {
        g_owner_occupied.store(false, std::memory_order_release);
        throw;
    }
    active_ = true;
    return true;
}

}  // namespace galaxy::host::flat_guest_memory
