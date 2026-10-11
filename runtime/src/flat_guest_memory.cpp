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
#include <cstdio>
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

namespace {

FaultAccessCallbacks g_fault_callbacks{};
std::atomic<bool> g_fault_handler_installed{false};
void* g_fault_handler_cookie = nullptr;
std::atomic<std::uint64_t> g_fault_reads{0};
std::atomic<std::uint64_t> g_fault_writes{0};
std::atomic<std::uint64_t> g_fault_locked_cache{0};
std::atomic<std::uint64_t> g_fault_mmio{0};
std::atomic<std::uint64_t> g_fault_unhandled{0};
char g_last_unhandled[192]{};
// Fault histogram by 4 KiB guest page; the simulation thread is the only
// writer, so plain memory suffices.
constexpr std::size_t kFaultPageSlots = 64u;
std::uint32_t g_fault_pages[kFaultPageSlots]{};
std::uint64_t g_fault_page_counts[kFaultPageSlots]{};
std::size_t g_fault_page_used = 0u;

// Exact addresses (4-byte granularity) carry the register identity; pages
// stay for the summary.
constexpr std::size_t kFaultAddressSlots = 256u;
std::uint32_t g_fault_addresses[kFaultAddressSlots]{};
std::uint64_t g_fault_address_counts[kFaultAddressSlots]{};
std::size_t g_fault_address_used = 0u;

void count_fault_page(std::uint32_t guest_address) noexcept {
    const std::uint32_t page = guest_address >> 12u;
    std::size_t i = 0;
    for (; i < g_fault_page_used; ++i) {
        if (g_fault_pages[i] == page) { ++g_fault_page_counts[i]; break; }
    }
    if (i == g_fault_page_used && g_fault_page_used < kFaultPageSlots) {
        g_fault_pages[g_fault_page_used] = page;
        g_fault_page_counts[g_fault_page_used] = 1u;
        ++g_fault_page_used;
    }
    const std::uint32_t word = guest_address & ~3u;
    for (i = 0; i < g_fault_address_used; ++i) {
        if (g_fault_addresses[i] == word) { ++g_fault_address_counts[i]; return; }
    }
    if (g_fault_address_used < kFaultAddressSlots) {
        g_fault_addresses[g_fault_address_used] = word;
        g_fault_address_counts[g_fault_address_used] = 1u;
        ++g_fault_address_used;
    }
}

// x86-64 memory-operand emulation for fastmem faults. The generated module
// accesses guest memory through plain loads and stores the compiler is free
// to fold into arithmetic, merge, forward or vectorize, so a fault may land on
// any instruction with one memory operand that clang emits for those
// templates: mov/movbe/movzx/movsx/movsxd in both directions, immediate
// stores, add/or/and/sub/xor/cmp/test with the memory operand as source or
// destination, and the SSE/AVX moves (movss/movsd/movups/movaps/movdqu/
// movdqa/movd/movq, pinsr/pextr) in legacy and VEX-128 encodings. A device
// access wider than the device accepts is completed as ascending 4-byte
// pieces (then 2, then 1). Anything else is reported and left to the default
// path. A census of nine compiled shards (800k instructions) found exactly
// these forms with two-register addressing.
enum class AluOp : std::uint8_t { None, Add, Or, And, Sub, Xor, Cmp, Test };

struct DecodedAccess {
    bool valid{};
    bool store{};        // the instruction writes the memory operand
    bool load{};         // the instruction reads the memory operand
    bool movbe{};        // value byte-swapped between register and memory
    bool immediate{};    // the non-memory operand is an immediate
    bool zero_extend{};  // movzx
    bool sign_extend{};  // movsx / movsxd
    bool memory_destination{};  // ALU result goes to memory
    bool memory_first{};        // the memory operand is the first (destination-side) operand
    bool vector{};              // the register operand is an XMM register
    bool vector_zero_upper{};   // scalar/partial load zeroes the rest of the register
    unsigned vector_lane{};     // pinsr/pextr lane (in units of `size`)
    unsigned vector_source{};   // VEX vvvv register whose other lanes pinsr keeps (16 = none)
    AluOp alu{AluOp::None};
    std::uint64_t imm{};
    std::uint32_t size{};       // memory operand width in bytes
    std::uint32_t reg_size{};   // register operand width (differs for movzx/movsx)
    unsigned reg{};             // 0-15
    bool rex{};
    unsigned length{};
};

constexpr std::uint64_t size_mask(std::uint32_t size) noexcept {
    return size >= 8u ? ~std::uint64_t{0} : ((std::uint64_t{1} << (8u * size)) - 1u);
}

constexpr std::uint64_t sign_bit(std::uint32_t size) noexcept {
    return std::uint64_t{1} << (8u * size - 1u);
}

std::uint64_t sign_extend_value(std::uint64_t value, std::uint32_t from, std::uint32_t to) noexcept {
    const std::uint64_t masked = value & size_mask(from);
    if ((masked & sign_bit(from)) == 0u) return masked;
    return (masked | ~size_mask(from)) & size_mask(to);
}

// Decodes the ModRM/SIB/displacement/immediate tail shared by every form.
unsigned decode_operand_tail(
    const std::uint8_t* code, unsigned i, DecodedAccess& access, bool rex_r,
    unsigned immediate_bytes, bool immediate_sign_extended, unsigned digit_select,
    const AluOp* alu_by_digit) noexcept {
    const std::uint8_t modrm = code[i++];
    const unsigned mod = modrm >> 6u;
    const unsigned rm = modrm & 7u;
    const unsigned digit = (modrm >> 3u) & 7u;
    if (mod == 3u) return 0u;
    if (digit_select == 0x10u) {
        // 80/81/83 /digit: 0 add 1 or 2 adc 3 sbb 4 and 5 sub 6 xor 7 cmp
        access.alu = alu_by_digit[digit];
        if (access.alu == AluOp::None) return 0u;
        access.memory_destination = access.alu != AluOp::Cmp;
        access.store = access.memory_destination;
    } else if (digit_select == 0u && digit != 0u) {
        return 0u;
    }
    access.reg = digit | (rex_r ? 8u : 0u);
    if (rm == 4u) ++i;                       // SIB
    if (mod == 1u) i += 1u;
    else if (mod == 2u) i += 4u;
    else if (mod == 0u && rm == 5u) i += 4u;  // RIP-relative (never ours)
    if (immediate_bytes != 0u) {
        std::uint64_t imm = 0u;
        for (unsigned b = 0; b < immediate_bytes; ++b) {
            imm |= static_cast<std::uint64_t>(code[i + b]) << (8u * b);
        }
        if (immediate_sign_extended) {
            imm = sign_extend_value(imm, immediate_bytes, 8u);
        }
        access.imm = imm & size_mask(access.size);
        i += immediate_bytes;
    }
    return i;
}

// SSE/AVX move forms after the 0F escape. `pp` is the mandatory prefix
// (0 none, 1 66, 2 F3, 3 F2), `map` the opcode map (1 0F, 2 0F38, 3 0F3A).
bool decode_vector_form(
    unsigned map, unsigned pp, std::uint8_t op, bool wide, DecodedAccess& access,
    unsigned& immediate_bytes) noexcept {
    access.vector = true;
    access.reg_size = 16u;
    if (map == 1u) {
        switch (op) {
        case 0x10u: case 0x11u:   // movups/movupd/movss/movsd
            access.size = pp == 2u ? 4u : pp == 3u ? 8u : 16u;
            access.store = op == 0x11u;
            access.load = !access.store;
            access.vector_zero_upper = access.load && access.size != 16u;
            return true;
        case 0x28u: case 0x29u:   // movaps/movapd
            if (pp >= 2u) return false;
            access.size = 16u;
            access.store = op == 0x29u;
            access.load = !access.store;
            return true;
        case 0x6Fu: case 0x7Fu:   // movdqa (66) / movdqu (F3)
            if (pp != 1u && pp != 2u) return false;
            access.size = 16u;
            access.store = op == 0x7Fu;
            access.load = !access.store;
            return true;
        case 0x6Eu:               // movd/movq xmm, r/m
            if (pp != 1u) return false;
            access.size = wide ? 8u : 4u;
            access.load = true;
            access.vector_zero_upper = true;
            return true;
        case 0x7Eu:
            if (pp == 1u) {       // movd/movq r/m, xmm
                access.size = wide ? 8u : 4u;
                access.store = true;
                return true;
            }
            if (pp == 2u) {       // movq xmm, m64
                access.size = 8u;
                access.load = true;
                access.vector_zero_upper = true;
                return true;
            }
            return false;
        case 0xD6u:               // movq m64, xmm
            if (pp != 1u) return false;
            access.size = 8u;
            access.store = true;
            return true;
        default:
            return false;
        }
    }
    if (map == 3u && pp == 1u) {
        switch (op) {
        case 0x22u:               // pinsrd/pinsrq xmm, r/m, imm8
            access.size = wide ? 8u : 4u;
            access.load = true;
            immediate_bytes = 1u;
            return true;
        case 0x20u:               // pinsrb
            access.size = 1u;
            access.load = true;
            immediate_bytes = 1u;
            return true;
        case 0x16u:               // pextrd/pextrq r/m, xmm, imm8
            access.size = wide ? 8u : 4u;
            access.store = true;
            immediate_bytes = 1u;
            return true;
        case 0x14u:               // pextrb
            access.size = 1u;
            access.store = true;
            immediate_bytes = 1u;
            return true;
        case 0x15u:               // pextrw
            access.size = 2u;
            access.store = true;
            immediate_bytes = 1u;
            return true;
        default:
            return false;
        }
    }
    if (map == 1u && pp == 1u && op == 0xC4u) {  // pinsrw xmm, r/m16, imm8
        access.size = 2u;
        access.load = true;
        immediate_bytes = 1u;
        return true;
    }
    return false;
}

DecodedAccess decode_access(const std::uint8_t* code) noexcept {
    DecodedAccess access{};
    access.vector_source = 16u;
    static constexpr AluOp kAluByOpcodeRow[8] = {
        AluOp::Add, AluOp::Or, AluOp::None, AluOp::None,  // adc/sbb not emulated
        AluOp::And, AluOp::Sub, AluOp::Xor, AluOp::Cmp};
    unsigned i = 0u;
    bool operand16 = false;
    bool rex_w = false, rex_r = false;
    unsigned pp = 0u;  // mandatory SSE prefix: 1 = 66, 2 = F3, 3 = F2
    for (; i < 5u; ++i) {
        if (code[i] == 0x66u) { operand16 = true; pp = 1u; continue; }
        if (code[i] == 0xF3u) { pp = 2u; continue; }
        if (code[i] == 0xF2u) { pp = 3u; continue; }
        if ((code[i] & 0xF0u) == 0x40u) {
            access.rex = true;
            rex_w = (code[i] & 0x08u) != 0u;
            rex_r = (code[i] & 0x04u) != 0u;
            continue;
        }
        break;
    }
    unsigned immediate_bytes = 0u;
    bool immediate_sign_extended = false;
    unsigned digit_select = 0xFFu;   // ALU /digit forms need ModRM.reg

    // VEX-encoded SSE/AVX forms (128-bit only; a 256-bit form is unhandled).
    if (code[i] == 0xC5u || code[i] == 0xC4u) {
        unsigned map = 1u;
        bool vex_w = false;
        bool vex_l = false;
        unsigned vvvv = 0u;
        if (code[i] == 0xC5u) {
            const std::uint8_t b1 = code[i + 1u];
            rex_r = (b1 & 0x80u) == 0u;
            vvvv = (~b1 >> 3u) & 0xFu;
            vex_l = (b1 & 0x04u) != 0u;
            pp = b1 & 3u;
            i += 2u;
        } else {
            const std::uint8_t b1 = code[i + 1u];
            const std::uint8_t b2 = code[i + 2u];
            rex_r = (b1 & 0x80u) == 0u;
            map = b1 & 0x1Fu;
            vex_w = (b2 & 0x80u) != 0u;
            vvvv = (~b2 >> 3u) & 0xFu;
            vex_l = (b2 & 0x04u) != 0u;
            pp = b2 & 3u;
            i += 3u;
        }
        if (vex_l) return access;
        const std::uint8_t op = code[i++];
        if (!decode_vector_form(map, pp, op, vex_w, access, immediate_bytes)) {
            return access;
        }
        access.rex = true;  // VEX never names ah/ch/dh/bh
        if (access.load && immediate_bytes != 0u) access.vector_source = vvvv;
        const unsigned end = decode_operand_tail(
            code, i, access, rex_r, immediate_bytes, false, 0xFFu, kAluByOpcodeRow);
        if (end == 0u) return access;
        if (immediate_bytes != 0u) access.vector_lane = static_cast<unsigned>(access.imm);
        access.length = end;
        access.valid = true;
        return access;
    }

    const std::uint32_t wide = rex_w ? 8u : (operand16 ? 2u : 4u);
    const std::uint8_t op = code[i];
    if (op == 0x0Fu && code[i + 1u] == 0x38u && (code[i + 2u] == 0xF0u || code[i + 2u] == 0xF1u)) {
        access.store = code[i + 2u] == 0xF1u;
        access.load = !access.store;
        access.movbe = true;
        access.size = wide;
        access.reg_size = wide;
        i += 3u;
    } else if (op == 0x0Fu && (code[i + 1u] == 0xB6u || code[i + 1u] == 0xB7u ||
                               code[i + 1u] == 0xBEu || code[i + 1u] == 0xBFu)) {
        access.load = true;
        access.size = (code[i + 1u] & 1u) != 0u ? 2u : 1u;
        access.reg_size = wide;
        access.zero_extend = code[i + 1u] < 0xBEu;
        access.sign_extend = !access.zero_extend;
        i += 2u;
    } else if (op == 0x0Fu && (code[i + 1u] == 0x3Au || code[i + 1u] == 0x38u)) {
        // Legacy-encoded three-byte SSE4 forms (pinsr/pextr).
        const unsigned map = code[i + 1u] == 0x3Au ? 3u : 2u;
        if (!decode_vector_form(map, pp, code[i + 2u], rex_w, access, immediate_bytes)) {
            return access;
        }
        i += 3u;
    } else if (op == 0x0Fu &&
               (code[i + 1u] == 0x10u || code[i + 1u] == 0x11u || code[i + 1u] == 0x28u ||
                code[i + 1u] == 0x29u || code[i + 1u] == 0x6Eu || code[i + 1u] == 0x6Fu ||
                code[i + 1u] == 0x7Eu || code[i + 1u] == 0x7Fu || code[i + 1u] == 0xD6u ||
                code[i + 1u] == 0xC4u)) {
        // Legacy-encoded SSE moves; the mandatory prefix selects the form.
        if (!decode_vector_form(1u, pp, code[i + 1u], rex_w, access, immediate_bytes)) {
            return access;
        }
        i += 2u;
    } else if (op == 0x63u && rex_w) {  // movsxd r64, r/m32
        access.load = true;
        access.size = 4u;
        access.reg_size = 8u;
        access.sign_extend = true;
        i += 1u;
    } else if (op == 0x8Au || op == 0x8Bu) {
        access.load = true;
        access.size = op == 0x8Au ? 1u : wide;
        access.reg_size = access.size;
        i += 1u;
    } else if (op == 0x88u || op == 0x89u) {
        access.store = true;
        access.size = op == 0x88u ? 1u : wide;
        access.reg_size = access.size;
        i += 1u;
    } else if (op == 0xC6u || op == 0xC7u) {
        access.store = true;
        access.immediate = true;
        access.size = op == 0xC6u ? 1u : wide;
        access.reg_size = access.size;
        immediate_bytes = op == 0xC6u ? 1u : (operand16 ? 2u : 4u);
        immediate_sign_extended = access.size == 8u;
        digit_select = 0u;
        i += 1u;
    } else if ((op & 0xC4u) == 0x00u && (op & 0x07u) <= 3u && (op >> 3u) != 2u && (op >> 3u) != 3u) {
        // 00/01/02/03 add, 08/09/0A/0B or, 20-23 and, 28-2B sub, 30-33 xor, 38-3B cmp
        access.alu = kAluByOpcodeRow[op >> 3u];
        if (access.alu == AluOp::None) return access;
        const bool byte_form = (op & 1u) == 0u;
        const bool memory_dest = (op & 2u) == 0u;  // 00/01: r/m += r ; 02/03: r += r/m
        access.size = byte_form ? 1u : wide;
        access.reg_size = access.size;
        access.load = true;
        access.memory_first = memory_dest;
        access.memory_destination = memory_dest && access.alu != AluOp::Cmp;
        access.store = access.memory_destination;
        i += 1u;
    } else if (op == 0x84u || op == 0x85u) {
        access.alu = AluOp::Test;
        access.size = op == 0x84u ? 1u : wide;
        access.reg_size = access.size;
        access.load = true;
        access.memory_first = true;
        i += 1u;
    } else if (op == 0x80u || op == 0x81u || op == 0x83u) {
        access.size = op == 0x80u ? 1u : wide;
        access.reg_size = access.size;
        access.load = true;
        access.immediate = true;
        access.memory_first = true;
        immediate_bytes = op == 0x81u ? (operand16 ? 2u : 4u) : 1u;
        immediate_sign_extended = true;
        digit_select = 0x10u;  // resolved from ModRM.reg below
        i += 1u;
    } else if (op == 0xF6u || op == 0xF7u) {
        access.alu = AluOp::Test;
        access.size = op == 0xF6u ? 1u : wide;
        access.reg_size = access.size;
        access.load = true;
        access.immediate = true;
        access.memory_first = true;
        immediate_bytes = op == 0xF6u ? 1u : (operand16 ? 2u : 4u);
        immediate_sign_extended = access.size == 8u;
        digit_select = 0u;
        i += 1u;
    } else {
        return access;
    }
    const unsigned end = decode_operand_tail(
        code, i, access, rex_r, immediate_bytes, immediate_sign_extended, digit_select,
        kAluByOpcodeRow);
    if (end == 0u) return access;
    if (access.vector && immediate_bytes != 0u) {
        access.vector_lane = static_cast<unsigned>(access.imm);
        if (access.load) access.vector_source = access.reg;  // legacy pinsr merges into its destination
    }
    access.length = end;
    access.valid = true;
    return access;
}

DWORD64* context_register(CONTEXT& context, unsigned index) noexcept {
    switch (index) {
    case 0: return &context.Rax; case 1: return &context.Rcx;
    case 2: return &context.Rdx; case 3: return &context.Rbx;
    case 4: return &context.Rsp; case 5: return &context.Rbp;
    case 6: return &context.Rsi; case 7: return &context.Rdi;
    case 8: return &context.R8; case 9: return &context.R9;
    case 10: return &context.R10; case 11: return &context.R11;
    case 12: return &context.R12; case 13: return &context.R13;
    case 14: return &context.R14; default: return &context.R15;
    }
}

std::uint8_t* context_xmm(CONTEXT& context, unsigned index) noexcept {
    return reinterpret_cast<std::uint8_t*>(&context.Xmm0) + 16u * (index & 15u);
}

// Legacy byte registers: without REX, indices 4-7 name ah/ch/dh/bh.
bool high_byte_register(const DecodedAccess& access) noexcept {
    return access.reg_size == 1u && !access.rex && access.reg >= 4u && access.reg <= 7u;
}

std::uint64_t read_register(CONTEXT& context, const DecodedAccess& access) noexcept {
    if (high_byte_register(access)) {
        return (*context_register(context, access.reg - 4u) >> 8u) & 0xFFu;
    }
    return *context_register(context, access.reg) & size_mask(access.reg_size);
}

void write_register(CONTEXT& context, const DecodedAccess& access, std::uint64_t value) noexcept {
    if (high_byte_register(access)) {
        DWORD64* const low = context_register(context, access.reg - 4u);
        *low = (*low & ~std::uint64_t{0xFF00u}) | ((value & 0xFFu) << 8u);
        return;
    }
    DWORD64* const reg = context_register(context, access.reg);
    switch (access.reg_size) {
    case 1u: *reg = (*reg & ~std::uint64_t{0xFFu}) | (value & 0xFFu); break;
    case 2u: *reg = (*reg & ~std::uint64_t{0xFFFFu}) | (value & 0xFFFFu); break;
    case 4u: *reg = value & 0xFFFFFFFFu; break;   // zero-extends
    default: *reg = value; break;
    }
}

std::uint64_t byte_swap(std::uint64_t value, std::uint32_t size) noexcept {
    std::uint64_t result = 0u;
    for (std::uint32_t i = 0; i < size; ++i) {
        result = (result << 8u) | ((value >> (8u * i)) & 0xFFu);
    }
    return result;
}

constexpr DWORD kFlagCarry = 0x0001u, kFlagParity = 0x0004u, kFlagAdjust = 0x0010u,
                kFlagZero = 0x0040u, kFlagSign = 0x0080u, kFlagOverflow = 0x0800u;

std::uint64_t alu(AluOp op, std::uint64_t a, std::uint64_t b, std::uint32_t size, DWORD& eflags) noexcept {
    const std::uint64_t mask = size_mask(size);
    const std::uint64_t sign = sign_bit(size);
    a &= mask;
    b &= mask;
    std::uint64_t result = 0u;
    DWORD flags = eflags & ~(kFlagCarry | kFlagParity | kFlagAdjust | kFlagZero | kFlagSign | kFlagOverflow);
    switch (op) {
    case AluOp::Add:
        result = (a + b) & mask;
        if (result < a) flags |= kFlagCarry;
        if (((a ^ result) & (b ^ result) & sign) != 0u) flags |= kFlagOverflow;
        if (((a ^ b ^ result) & 0x10u) != 0u) flags |= kFlagAdjust;
        break;
    case AluOp::Sub:
    case AluOp::Cmp:
        result = (a - b) & mask;
        if (a < b) flags |= kFlagCarry;
        if (((a ^ b) & (a ^ result) & sign) != 0u) flags |= kFlagOverflow;
        if (((a ^ b ^ result) & 0x10u) != 0u) flags |= kFlagAdjust;
        break;
    case AluOp::And:
    case AluOp::Test:
        result = a & b;
        break;
    case AluOp::Or:
        result = a | b;
        break;
    case AluOp::Xor:
        result = a ^ b;
        break;
    default:
        break;
    }
    if (result == 0u) flags |= kFlagZero;
    if ((result & sign) != 0u) flags |= kFlagSign;
    unsigned ones = 0u;
    for (unsigned bit = 0; bit < 8u; ++bit) ones += static_cast<unsigned>((result >> bit) & 1u);
    if ((ones & 1u) == 0u) flags |= kFlagParity;
    eflags = flags;
    return result;
}

void note_unhandled(std::uint32_t guest_address, const std::uint8_t* code, const char* reason) noexcept {
    g_fault_unhandled.fetch_add(1u, std::memory_order_relaxed);
    std::snprintf(
        g_last_unhandled, sizeof(g_last_unhandled),
        "fastmem fault at guest 0x%08X not handled (%s): %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
        static_cast<unsigned>(guest_address), reason,
        code[0], code[1], code[2], code[3], code[4], code[5], code[6], code[7], code[8], code[9]);
}

// Completes one access through the device path. A width the device does not
// accept (a merged 8-byte store of two registers, a 16-byte vector move) is
// completed as ascending pieces, which is the order the guest's separate
// accesses had.
bool fault_transfer(std::uint32_t guest_address, std::uint32_t size, bool write, std::byte* image) noexcept {
    const bool accepted = write
        ? (g_fault_callbacks.write != nullptr &&
           g_fault_callbacks.write(g_fault_callbacks.user, guest_address, size, image))
        : (g_fault_callbacks.read != nullptr &&
           g_fault_callbacks.read(g_fault_callbacks.user, guest_address, size, image));
    if (accepted) return true;
    if (size <= 1u) return false;
    const std::uint32_t piece = size > 4u ? 4u : size / 2u;
    for (std::uint32_t offset = 0u; offset < size; offset += piece) {
        if (!fault_transfer(guest_address + offset, piece, write, image + offset)) return false;
    }
    return true;
}

LONG WINAPI fastmem_vectored_handler(EXCEPTION_POINTERS* pointers) noexcept {
    EXCEPTION_RECORD* const record = pointers != nullptr ? pointers->ExceptionRecord : nullptr;
    CONTEXT* const context = pointers != nullptr ? pointers->ContextRecord : nullptr;
    if (record == nullptr || context == nullptr ||
        record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        record->NumberParameters < 2u) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const auto fault = static_cast<std::uintptr_t>(record->ExceptionInformation[1]);
    if (g_guest_base == nullptr || fault < reinterpret_cast<std::uintptr_t>(g_guest_base) ||
        fault >= reinterpret_cast<std::uintptr_t>(g_guest_base) + kGuestSpaceSize) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (GetCurrentThreadId() != g_fault_callbacks.thread_id) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const auto guest_address = static_cast<std::uint32_t>(fault - reinterpret_cast<std::uintptr_t>(g_guest_base));
    const auto* code = reinterpret_cast<const std::uint8_t*>(context->Rip);
    const DecodedAccess access = decode_access(code);
    if (!access.valid) {
        note_unhandled(guest_address, code, "unknown instruction form");
        return EXCEPTION_CONTINUE_SEARCH;
    }
    count_fault_page(guest_address);
    if ((guest_address & 0xFFFF0000u) == 0xE0000000u) {
        g_fault_locked_cache.fetch_add(1u, std::memory_order_relaxed);
    } else if ((guest_address & 0xFF000000u) == 0xCC000000u || (guest_address & 0xFF000000u) == 0x0C000000u) {
        g_fault_mmio.fetch_add(1u, std::memory_order_relaxed);
    }
    alignas(16) std::byte image[16]{};
    if (access.load) {
        if (!fault_transfer(guest_address, access.size, false, image)) {
            note_unhandled(guest_address, code, "load not accepted");
            return EXCEPTION_CONTINUE_SEARCH;
        }
        g_fault_reads.fetch_add(1u, std::memory_order_relaxed);
    }
    if (access.vector) {
        std::uint8_t* const xmm = context_xmm(*context, access.reg);
        if (access.store) {
            const std::uint8_t* const source =
                xmm + (access.vector_source == 16u ? access.vector_lane * access.size : 0u);
            std::memcpy(image, source, access.size);
            if (!fault_transfer(guest_address, access.size, true, image)) {
                note_unhandled(guest_address, code, "vector store not accepted");
                return EXCEPTION_CONTINUE_SEARCH;
            }
            g_fault_writes.fetch_add(1u, std::memory_order_relaxed);
        } else {
            std::uint8_t result[16]{};
            if (access.vector_source != 16u) {
                // pinsr: every other lane comes from the VEX source (or, in
                // the legacy encoding, the destination itself).
                std::memcpy(result, context_xmm(*context, access.vector_source), 16u);
                const unsigned offset = access.vector_lane * access.size;
                if (offset + access.size <= 16u) std::memcpy(result + offset, image, access.size);
            } else if (access.vector_zero_upper || access.size == 16u) {
                std::memcpy(result, image, access.size);
            } else {
                std::memcpy(result, xmm, 16u);
                std::memcpy(result, image, access.size);
            }
            std::memcpy(xmm, result, 16u);
        }
        context->Rip += access.length;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    std::uint64_t memory_value = 0u;
    if (access.load) {
        for (std::uint32_t b = 0; b < access.size && b < 8u; ++b) {
            memory_value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(image[b])) << (8u * b);
        }
    }
    std::uint64_t store_value = 0u;
    bool do_store = false;
    if (access.alu != AluOp::None) {
        DWORD eflags = context->EFlags;
        const std::uint64_t other = access.immediate ? access.imm : read_register(*context, access);
        const std::uint64_t first = access.memory_first ? memory_value : other;
        const std::uint64_t second = access.memory_first ? other : memory_value;
        const std::uint64_t result = alu(access.alu, first, second, access.size, eflags);
        if (access.memory_destination) {
            store_value = result;
            do_store = true;
        } else if (access.alu != AluOp::Cmp && access.alu != AluOp::Test) {
            write_register(*context, access, result);
        }
        context->EFlags = eflags;
    } else if (access.store) {
        store_value = access.immediate ? access.imm : read_register(*context, access);
        if (access.movbe) store_value = byte_swap(store_value, access.size);
        do_store = true;
    } else {
        std::uint64_t value = memory_value;
        if (access.movbe) value = byte_swap(value, access.size);
        if (access.sign_extend) value = sign_extend_value(value, access.size, access.reg_size);
        else if (access.zero_extend) value &= size_mask(access.size);
        write_register(*context, access, value);
    }
    if (do_store) {
        for (std::uint32_t b = 0; b < access.size && b < 8u; ++b) {
            image[b] = static_cast<std::byte>((store_value >> (8u * b)) & 0xFFu);
        }
        if (!fault_transfer(guest_address, access.size, true, image)) {
            note_unhandled(guest_address, code, "store not accepted");
            return EXCEPTION_CONTINUE_SEARCH;
        }
        g_fault_writes.fetch_add(1u, std::memory_order_relaxed);
    }
    context->Rip += access.length;
    return EXCEPTION_CONTINUE_EXECUTION;
}

}  // namespace

void install_fault_handler(const FaultAccessCallbacks& callbacks) {
    const std::lock_guard<std::mutex> lock(g_init_mutex);
    g_fault_callbacks = callbacks;
    if (!g_fault_handler_installed.load(std::memory_order_acquire)) {
        g_fault_handler_cookie = AddVectoredExceptionHandler(1u, &fastmem_vectored_handler);
        if (g_fault_handler_cookie == nullptr) {
            fail_win32("AddVectoredExceptionHandler for fastmem");
        }
        g_fault_handler_installed.store(true, std::memory_order_release);
    }
}

FaultStats fault_stats() noexcept {
    FaultStats stats{};
    stats.handled_reads = g_fault_reads.load(std::memory_order_relaxed);
    stats.handled_writes = g_fault_writes.load(std::memory_order_relaxed);
    stats.locked_cache = g_fault_locked_cache.load(std::memory_order_relaxed);
    stats.mmio = g_fault_mmio.load(std::memory_order_relaxed);
    stats.unhandled = g_fault_unhandled.load(std::memory_order_relaxed);
    bool taken[kFaultPageSlots]{};
    for (std::size_t rank = 0; rank < 8u; ++rank) {
        std::size_t best = kFaultPageSlots;
        for (std::size_t i = 0; i < g_fault_page_used; ++i) {
            if (taken[i]) continue;
            if (best == kFaultPageSlots || g_fault_page_counts[i] > g_fault_page_counts[best]) best = i;
        }
        if (best == kFaultPageSlots) break;
        taken[best] = true;
        stats.hot_pages[rank] = g_fault_pages[best];
        stats.hot_counts[rank] = g_fault_page_counts[best];
    }
    bool taken_address[kFaultAddressSlots]{};
    for (std::size_t rank = 0; rank < 16u; ++rank) {
        std::size_t best = kFaultAddressSlots;
        for (std::size_t i = 0; i < g_fault_address_used; ++i) {
            if (taken_address[i]) continue;
            if (best == kFaultAddressSlots || g_fault_address_counts[i] > g_fault_address_counts[best]) best = i;
        }
        if (best == kFaultAddressSlots) break;
        taken_address[best] = true;
        stats.hot_addresses[rank] = g_fault_addresses[best];
        stats.hot_address_counts[rank] = g_fault_address_counts[best];
    }
    return stats;
}

const char* last_unhandled_fault_message() noexcept {
    return g_last_unhandled;
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
