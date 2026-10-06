#include "galaxy/flat_guest_memory.h"
#include "galaxy/flat_guest_read.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <string_view>
#include <utility>

namespace {

namespace flat = galaxy::host::flat_guest_memory;
namespace flat_read = galaxy::host::flat_guest_read;

bool expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

volatile std::uint8_t* guest_byte(std::uint32_t address) {
    return reinterpret_cast<volatile std::uint8_t*>(
        flat::kGuestBase + address);
}

bool check_alias_group(
    const std::array<std::uint32_t, 3>& bases,
    std::uint32_t size,
    std::byte* host_view,
    std::uint8_t value) {
    constexpr std::uint32_t kOffset = 0x1234u;
    bool passed = true;
    passed &= expect(host_view != nullptr, "host section view exists");
    if (host_view == nullptr) {
        return false;
    }

    // A store through a guest view must reach the host view and every alias.
    *guest_byte(bases[0] + kOffset) = value;
    passed &= expect(
        static_cast<std::uint8_t>(host_view[kOffset]) == value,
        "guest store is visible in the host section view");
    for (std::uint32_t base : bases) {
        passed &= expect(
            *guest_byte(base + kOffset) == value,
            "all guest aliases share the same backing section");
        passed &= expect(
            flat::host_pointer(base + kOffset, 1u) == host_view + kOffset,
            "host_pointer resolves the alias to its shared host view");
        passed &= expect(
            flat::is_mapped_ram_span(base + size - 1u, 1u),
            "last mapped byte is accepted");
        passed &= expect(
            !flat::is_mapped_ram_span(base + size - 1u, 2u),
            "span crossing a RAM boundary is rejected");
        passed &= expect(
            flat::host_pointer(base + size, 1u) == nullptr,
            "first byte beyond the RAM region is rejected");
    }

    host_view[kOffset] = static_cast<std::byte>(value ^ 0x5Au);
    for (std::uint32_t base : bases) {
        passed &= expect(
            *guest_byte(base + kOffset) == (value ^ 0x5Au),
            "host section store is visible in every guest alias");
    }
    return passed;
}

bool check_unmapped_page(std::uintptr_t address, std::string_view label) {
    MEMORY_BASIC_INFORMATION before{};
    bool passed = expect(
        VirtualQuery(reinterpret_cast<const void*>(address), &before,
                     sizeof(before)) == sizeof(before),
        label);
    if (!passed) {
        return false;
    }
    passed &= expect(before.State != MEM_COMMIT,
                     "unmapped guest page is not committed");

    std::uint8_t byte = 0u;
    SIZE_T bytes_read = 0u;
    const BOOL read_succeeded = ReadProcessMemory(
        GetCurrentProcess(), reinterpret_cast<const void*>(address),
        &byte, sizeof(byte), &bytes_read);
    passed &= expect(!read_succeeded && bytes_read == 0u,
                     "unmapped guest read fails without a recovery handler");

    MEMORY_BASIC_INFORMATION after{};
    passed &= expect(
        VirtualQuery(reinterpret_cast<const void*>(address), &after,
                     sizeof(after)) == sizeof(after),
        "unmapped page can be queried after the failed read");
    passed &= expect(after.State != MEM_COMMIT,
                     "failed read does not auto-commit unmapped guest memory");
    return passed;
}

struct DeviceReadProbe {
    std::uint32_t calls{};
    std::uint32_t last_address{};
};

bool read_device_probe(
    void* user,
    std::uint32_t address,
    std::uint32_t size,
    std::byte* output) {
    auto& probe = *static_cast<DeviceReadProbe*>(user);
    ++probe.calls;
    probe.last_address = address;
    for (std::uint32_t index = 0u; index < size; ++index) {
        output[index] = static_cast<std::byte>(0x10u + index);
    }
    return true;
}

bool check_flat_reads() {
    galaxy::GuestMemoryV1 memory{};
    DeviceReadProbe probe{};
    memory.user = &probe;
    memory.read_device = &read_device_probe;
    for (std::uint32_t base :
         {0x00000000u, 0x80000000u, 0xC0000000u}) {
        memory.fast_regions[base >> 28] = {
            flat::kMem1Size, flat::mem1_host_view()};
    }
    for (std::uint32_t base :
         {0x10000000u, 0x90000000u, 0xD0000000u}) {
        memory.fast_regions[base >> 28] = {
            flat::kMem2Size, flat::mem2_host_view()};
    }

    constexpr std::uint32_t kOffset = 0x2040u;
    const std::array<std::byte, 8> bytes{
        std::byte{0x12}, std::byte{0x34}, std::byte{0x56},
        std::byte{0x78}, std::byte{0x9A}, std::byte{0xBC},
        std::byte{0xDE}, std::byte{0xF0}};
    bool passed = true;
    for (const auto [base, view] : {
             std::pair{0x00000000u, flat::mem1_host_view()},
             std::pair{0x10000000u, flat::mem2_host_view()}}) {
        std::memcpy(view + kOffset, bytes.data(), bytes.size());
        for (std::uint32_t alias :
             {base, base | 0x80000000u, base | 0xC0000000u}) {
            const std::uint32_t address = alias + kOffset;
            passed &= expect(flat_read::eligible(&memory, address, 8u),
                             "shared RAM span is eligible for a flat read");
            passed &= expect(
                flat_read::read_u8(&memory, address, nullptr, 0u) == 0x12u,
                "flat u8 read matches guest bytes");
            passed &= expect(
                flat_read::read_u16(&memory, address, nullptr, 0u) ==
                    0x1234u,
                "flat u16 read preserves guest big-endian order");
            passed &= expect(
                flat_read::read_u32(&memory, address, nullptr, 0u) ==
                    0x12345678u,
                "flat u32 read preserves guest big-endian order");
            passed &= expect(
                flat_read::read_u64(&memory, address, nullptr, 0u) ==
                    0x123456789ABCDEF0ull,
                "flat u64 read preserves guest big-endian order");
        }
    }
    passed &= expect(probe.calls == 0u,
                     "mapped RAM reads bypass the checked device callback");

    // The generated DLL reads this per-instance ABI capability, not the
    // process-global mapping. Exercise its direct, denied, and checked paths.
    memory.flat_guest_read_base = reinterpret_cast<const std::byte*>(
        flat::kGuestBase);
    const std::byte* const captured_base = galaxy::guest_flat_read_base(&memory);
    passed &= expect(
        galaxy::guest_load_flat_or_checked_u32(
            captured_base, &memory, 0x80002040u, nullptr, 0u) ==
            0x12345678u,
        "ABI flat read preserves aliased RAM bytes and big-endian order");
    passed &= expect(
        galaxy::guest_load_flat_or_checked_u64(
            captured_base, &memory, 0x90002040u, nullptr, 0u) ==
            0x123456789ABCDEF0ull,
        "ABI flat read resolves the MEM2 alias without a callback");
    passed &= expect(probe.calls == 0u,
                     "ABI flat RAM path does not consult the device callback");
    passed &= expect(
        galaxy::guest_load_flat_or_checked_u16(
            captured_base, &memory, flat::kMem1Size - 1u, nullptr, 0u) ==
            0x1011u,
        "ABI boundary-crossing read retains the checked callback");
    passed &= expect(
        galaxy::guest_load_flat_or_checked_u8(
            nullptr, &memory, 0xCC002000u, nullptr, 0u) == 0x10u,
        "absent ABI capability retains the checked device path");

    for (std::uint32_t address :
         {0xCC002000u, 0xE0000000u, 0x02000000u}) {
        passed &= expect(!flat_read::eligible(&memory, address, 8u),
                         "device or unmapped page is not eligible");
        passed &= expect(
            flat_read::read_u64(&memory, address, nullptr, 0u) ==
                0x1011121314151617ull &&
                probe.last_address == address,
            "device or unmapped read uses Galaxy's checked callback");
    }
    passed &= expect(
        !flat_read::eligible(&memory, flat::kMem1Size - 1u, 2u),
        "flat read rejects a span crossing the MEM1 boundary");
    passed &= expect(
        flat_read::read_u16(
            &memory, flat::kMem1Size - 1u, nullptr, 0u) == 0x1011u,
        "boundary-crossing read uses Galaxy's checked callback");
    passed &= expect(
        !flat_read::eligible(&memory, 0xFFFFFFFFu, 8u),
        "flat read rejects a wrapped guest address");
    passed &= expect(
        flat_read::read_u8(&memory, 0xFFFFFFFFu, nullptr, 0u) == 0x10u,
        "unmapped final guest byte uses Galaxy's checked callback");

    // A foreign GuestMemoryV1 may have its own valid RAM; the fixed view must
    // never override its backing even when the guest address is mapped here.
    std::array<std::byte, 8> foreign_bytes{};
    foreign_bytes[0] = std::byte{0x7B};
    galaxy::GuestMemoryV1 foreign{};
    foreign.fast_regions[8] = {8u, foreign_bytes.data()};
    passed &= expect(!flat_read::eligible(&foreign, 0x80000000u, 1u),
                     "foreign RAM backing is not eligible");
    passed &= expect(
        flat_read::read_u8(&foreign, 0x80000000u, nullptr, 0u) == 0x7Bu,
        "foreign RAM read retains its own checked backing");
    passed &= expect(
        galaxy::guest_flat_read_base(&foreign) == nullptr &&
            galaxy::guest_load_flat_or_checked_u8(
                galaxy::guest_flat_read_base(&foreign), &foreign,
                0x80000000u, nullptr, 0u) == 0x7Bu,
        "foreign ABI instance cannot borrow another owner's guest view");
    return passed;
}

}  // namespace

int main() {
    try {
        flat::initialize();
        flat::initialize();  // Process-lifetime initialization is idempotent.
    } catch (const std::exception& error) {
        std::cerr << "FAILED: flat guest initialization: " << error.what()
                  << '\n';
        return 1;
    }

    bool passed = expect(flat::is_initialized(), "flat memory initialized");
    {
        flat::OwnerLease first;
        flat::OwnerLease concurrent;
        passed &= expect(first.acquire(), "first live owner acquires flat RAM");
        passed &= expect(!concurrent.acquire(),
                         "second live owner retains independent RAM backing");
        flat::mem1_host_view()[0x1234u] = std::byte{0xA5};
    }
    {
        flat::OwnerLease sequential;
        passed &= expect(sequential.acquire(),
                         "sequential owner acquires the retained mapping");
        passed &= expect(flat::mem1_host_view()[0x1234u] == std::byte{0},
                         "sequential owner begins with zero-filled RAM");
    }
    passed &= check_alias_group(
        {0x00000000u, 0x80000000u, 0xC0000000u}, flat::kMem1Size,
        flat::mem1_host_view(), 0xA5u);
    passed &= check_alias_group(
        {0x10000000u, 0x90000000u, 0xD0000000u}, flat::kMem2Size,
        flat::mem2_host_view(), 0x6Cu);
    passed &= expect(flat::host_pointer(0x00000000u, 0u) == nullptr,
                     "zero-sized span is rejected");
    passed &= expect(flat::host_pointer(0xFFFFFFFFu, 2u) == nullptr,
                     "address overflow beyond the 32-bit guest space is rejected");
    passed &= expect(flat::host_pointer(0xE0000000u, 1u) == nullptr,
                     "locked cache is not silently treated as shared RAM");
    passed &= check_unmapped_page(flat::kGuestBase + 0x02000000u,
                                  "unmapped MEM1 gap can be queried");
    passed &= check_unmapped_page(flat::kGuestBase + flat::kGuestSpaceSize,
                                  "reserved 64-KiB overrun guard can be queried");
    passed &= check_flat_reads();
    return passed ? 0 : 1;
}
