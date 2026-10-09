#pragma once

#include <atomic>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "galaxy/ppc_float.h"

#if defined(_WIN32)
#if defined(GALAXY_BUILDING_GAME_MODULE)
#define GALAXY_MODULE_EXPORT __declspec(dllexport)
#else
#define GALAXY_MODULE_EXPORT
#endif
#else
#define GALAXY_MODULE_EXPORT
#endif

#if defined(_MSC_VER)
#define GALAXY_ALWAYS_INLINE __forceinline
#define GALAXY_NOINLINE __declspec(noinline)
#define GALAXY_UNLIKELY [[unlikely]]
#define GALAXY_LIKELY [[likely]]
#else
#define GALAXY_ALWAYS_INLINE inline __attribute__((always_inline))
#define GALAXY_NOINLINE __attribute__((noinline))
#define GALAXY_UNLIKELY [[unlikely]]
#define GALAXY_LIKELY [[likely]]
#endif

namespace galaxy {

// Inline-only atomic accessors for hot generated-code helpers. clang-cl does
// not inline the MSVC STL's std::atomic member functions into very large
// translated functions, so each `->load()` became a real call per guest call
// return or store. On x86-64 an acquire/relaxed load is a plain aligned load
// plus a compiler barrier, and fetch_or is one locked instruction.
static_assert(sizeof(std::atomic<std::uint32_t>) == 4u &&
              sizeof(std::atomic<std::uint64_t>) == 8u &&
              std::atomic<std::uint32_t>::is_always_lock_free &&
              std::atomic<std::uint64_t>::is_always_lock_free);

GALAXY_ALWAYS_INLINE std::uint32_t atomic_load_u32(
    const std::atomic<std::uint32_t>* value) noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    const std::uint32_t loaded =
        *reinterpret_cast<const volatile std::uint32_t*>(value);
    _ReadWriteBarrier();
    return loaded;
#else
    return value->load(std::memory_order_acquire);
#endif
}

GALAXY_ALWAYS_INLINE std::uint64_t atomic_load_u64(
    const std::atomic<std::uint64_t>* value) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    const std::uint64_t loaded =
        *reinterpret_cast<const volatile std::uint64_t*>(value);
    _ReadWriteBarrier();
    return loaded;
#else
    return value->load(std::memory_order_relaxed);
#endif
}

GALAXY_ALWAYS_INLINE void atomic_or_u64(
    std::atomic<std::uint64_t>* value, std::uint64_t bits) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    (void)_InterlockedOr64(
        reinterpret_cast<volatile long long*>(value),
        static_cast<long long>(bits));
#else
    value->fetch_or(bits, std::memory_order_relaxed);
#endif
}

// ABI 23 requires every renderer dirty-word producer to publish its summary.
// ABI 22 modules used inline page-only stores: accepting them with a summary
// drain would silently lose invalidations. Rebuild both game and Home sidecars.
inline constexpr std::uint32_t kNativeAbiVersion = 23;
// Dynamic RSO sidecars have their own narrow ABI. They are generated at
// installation from a validated disc module and contain only native C++; this
// version is deliberately independent of the RMGE01 game-module manifest.
inline constexpr std::uint32_t kNativeRsoModuleAbiVersion = 2;

// PowerPC MSR[FP]. Generated code checks this before every statically decoded
// floating-point or paired-single instruction so Broadway's lazy-FPU owner
// protocol remains exact without any runtime instruction decoding.
inline constexpr std::uint32_t kMsrFloatingPointAvailable = 0x00002000u;
inline constexpr std::uint32_t kMsrExternalInterruptEnable = 0x00008000u;

// Trace-only proof flag. When enabled, generated call-return checkpoints expose
// the five exact WPADRead status copies, KPADRead's interrupt-protected queue
// snapshot, both classified KPADRead returns, and WPadPointer's gameplay
// status[0] selection. The runtime treats trace-only callbacks as passive
// observations when no hardware event was already due.
inline constexpr std::uint32_t kNativeServiceFlagTraceWpadReadCopies =
    1u << 1u;
// During a persistent VI transaction, static direct-call returns become
// adaptive scheduler safepoints.  A callback is still entered only when the
// existing shared threshold or a real device event requires it, so this never
// creates a runtime PPC execution path and does not turn every return into a
// host transition.
inline constexpr std::uint32_t kNativeServiceFlagAdaptiveCallReturnCheckpoints =
    1u << 2u;
// A finite set of statically selected generated direct calls may cross the
// existing resolved-call service so the host can record passive route markers.
// The runtime publishes this once before module initialization; generated code
// must never consult process environment state on its hot call path.
inline constexpr std::uint32_t kNativeServiceFlagTraceRouteMarkers =
    1u << 3u;
// Marker-timed native-input scripts use the same finite direct-call
// observation path to publish the first Mario-control VI. This covers both a
// relative stick anchor and termination of the long-form button script. The
// historical flag name is retained as an ABI source identifier; the bit is
// independent of route logging so functional input timing remains reachable
// when the optional route trace is disabled.
inline constexpr std::uint32_t
    kNativeServiceFlagRelativeMarioInputAnchor = 1u << 4u;

// A trace-only pre-call marker must never be a guest-executable address.  The
// five WPADRead copies invoke the translated memmove helper, whose volatile
// argument registers are not an architectural post-call observation.  The
// marker lets the runtime preserve the real pre-call operands and verify their
// bytes at the ordinary post-call checkpoint without altering guest state.
inline constexpr std::uint32_t kNativeInputTraceCopyPreCheckpointXor =
    0x40000000u;

// Passive FunctionAsync causality callbacks reuse the existing host callback
// pointer but are consumed before scheduler/checkpoint accounting. These
// values cannot be executable RMGE01 addresses.
inline constexpr std::uint32_t kFunctionAsyncTraceWorkerDequeued = 0xFFFFFFD0u;
inline constexpr std::uint32_t kFunctionAsyncTraceDoneMessagePosted = 0xFFFFFFD4u;
inline constexpr std::uint32_t kFunctionAsyncTraceEndFlagPublished = 0xFFFFFFD8u;
inline constexpr std::uint32_t kFunctionAsyncTraceReaped = 0xFFFFFFDCu;
inline constexpr std::uint32_t kFunctionAsyncTraceWorkerBegin = 0xFFFFFFE0u;
inline constexpr std::uint32_t kFunctionAsyncTraceWorkerComplete = 0xFFFFFFE4u;
inline constexpr std::uint32_t kFunctionAsyncTracePointerConsumer = 0xFFFFFFE8u;
inline constexpr std::uint32_t kFunctionAsyncTraceExecInfoPublished = 0xFFFFFFECu;

constexpr bool is_wpad_read_status_copy_call_pc(std::uint32_t guest_pc) noexcept {
    switch (guest_pc) {
    case 0x804D945Cu:
    case 0x804D947Cu:
    case 0x804D949Cu:
    case 0x804D94B0u:
    case 0x804D94C4u:
        return true;
    default:
        return false;
    }
}

constexpr std::uint32_t native_input_trace_copy_pre_checkpoint_pc(
    std::uint32_t call_pc) noexcept {
    return call_pc ^ kNativeInputTraceCopyPreCheckpointXor;
}

constexpr bool is_native_input_trace_copy_pre_checkpoint_pc(
    std::uint32_t guest_pc) noexcept {
    return is_wpad_read_status_copy_call_pc(
        guest_pc ^ kNativeInputTraceCopyPreCheckpointXor);
}

constexpr std::uint32_t native_input_trace_copy_call_pc_from_pre_checkpoint(
    std::uint32_t checkpoint_pc) noexcept {
    return checkpoint_pc ^ kNativeInputTraceCopyPreCheckpointXor;
}

constexpr bool is_native_input_trace_call_return_pc(
    std::uint32_t guest_pc) noexcept {
    return is_wpad_read_status_copy_call_pc(guest_pc) ||
           guest_pc == 0x8045080Cu ||  // OSDisableInterrupts; protected W/Q
           guest_pc == 0x803AA570u ||  // JUTException-only max-1 return
           guest_pc == 0x803AB2B0u ||  // WPadHolder gameplay max-120 return
           guest_pc == 0x803ABDF0u;    // WPadPointer getKPadStatus(0) return
}

constexpr bool is_native_input_trace_function_entry(
    std::uint32_t guest_address) noexcept {
    switch (guest_address & 0xFFFFFFFCu) {
    case 0x804506D8u:  // KPADRead
    case 0x80451198u:  // KPADiSamplingCallback
    case 0x804D8ACCu:  // WPADProbe
    case 0x804D93C8u:  // WPADRead
    case 0x804DE4F0u:  // WPAD report 0x20
    case 0x804DE910u:  // WPAD report 0x21
    case 0x804DED18u:  // WPAD report 0x22
    case 0x804DF3C4u:  // WPAD report 0x30
    case 0x804DF57Cu:  // WPAD report 0x31
    case 0x804DF7BCu:  // WPAD report 0x32
    case 0x804DFF98u:  // WPAD report 0x33
    case 0x804E020Cu:  // WPAD report 0x34
    case 0x804E0418u:  // WPAD report 0x35
    case 0x804E0C98u:  // WPAD report 0x36
    case 0x804E0ED0u:  // WPAD report 0x37
    case 0x804E175Cu:  // WPAD report 0x3D no-op
    case 0x804E1760u:  // WPAD report 0x3E
    case 0x804E1D44u:  // WPAD report 0x3F
        return true;
    default:
        return false;
    }
}

struct PpcContext;
struct GuestMemoryV1;
struct NativeServicesV1;

using NativeGameFunction =
    void (*)(PpcContext* context, GuestMemoryV1* memory, const NativeServicesV1* services);

constexpr std::uint16_t byte_swap_u16(std::uint16_t value) noexcept {
    return static_cast<std::uint16_t>((value >> 8) | (value << 8));
}

constexpr std::uint32_t byte_swap_u32(std::uint32_t value) noexcept {
    return ((value & 0x000000FFu) << 24) |
           ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) |
           ((value & 0xFF000000u) >> 24);
}

constexpr std::uint64_t byte_swap_u64(std::uint64_t value) noexcept {
    return (static_cast<std::uint64_t>(byte_swap_u32(static_cast<std::uint32_t>(value))) << 32) |
           byte_swap_u32(static_cast<std::uint32_t>(value >> 32));
}

struct PpcContext {
    std::uint32_t gpr[32]{};
    std::uint64_t fpr_bits[32]{};
    std::uint64_t ps1_bits[32]{};
    std::uint32_t cr{};
    std::uint32_t lr{};
    std::uint32_t ctr{};
    std::uint32_t xer{};
    std::uint32_t fpscr{};
    std::uint32_t gqr[8]{};
    std::uint32_t hid2{};
    std::uint32_t msr{};
    std::uint32_t segment_registers[16]{};
    std::uint32_t spr[1024]{};
    std::uint64_t time_base_offset{};
    // Raw native Wii-timeline tick at the most recent DEC write.  DEC and TB
    // share a clock source, but a guest write to TBL/TBU must not move DEC.
    std::uint64_t decrementer_start_ticks{};
    std::uint32_t decrementer_start_value{};
    std::uint32_t pc{};
    std::uint32_t reserved_address{};
    std::uint32_t reserved_value{};
};

struct PpcTimerState {
    std::uint64_t time_base_offset{};
    std::uint64_t decrementer_start_ticks{};
    std::uint32_t decrementer_start_value{};
    std::uint32_t hid2{};
};

inline PpcTimerState capture_timer_state(const PpcContext* context) {
    return {
        context->time_base_offset,
        context->decrementer_start_ticks,
        context->decrementer_start_value,
        context->hid2,
    };
}

inline void restore_timer_state(
    PpcContext* context,
    const PpcTimerState& state) {
    context->time_base_offset = state.time_base_offset;
    context->decrementer_start_ticks = state.decrementer_start_ticks;
    context->decrementer_start_value = state.decrementer_start_value;
    context->hid2 = state.hid2;
}

struct GuestMemoryRegionV1 {
    std::uint32_t guest_base{};
    std::uint32_t size{};
    std::byte* host_base{};
};

struct GuestMemoryFastRegionV1 {
    std::uint32_t size{};
    std::byte* host_base{};
};

struct GuestMemoryV1 {
    std::uint32_t struct_size{sizeof(GuestMemoryV1)};
    std::uint32_t region_count{};
    GuestMemoryRegionV1* regions{};
    void* user{};
    bool (*read_device)(
        void* user,
        std::uint32_t address,
        std::uint32_t size,
        std::byte* output){};
    bool (*write_device)(
        void* user,
        std::uint32_t address,
        std::uint32_t size,
        const std::byte* input){};
    GuestMemoryFastRegionV1 fast_regions[16]{};
    void (*notify_write)(
        void* user,
        std::uint32_t address,
        std::uint32_t size){};
    // Renderer invalidation map. The simulation thread alone drains/clears
    // this map when it captures a FIFO chunk; workers only add dirty bits.
    // Memory visibility is established by the frame/device handoff, not by
    // these relaxed invalidation bits. Keep that ownership when optimizing
    // repeated notifications from translated stores on the simulation thread.
    std::atomic_uint64_t* dirty_page_words{};
    std::uint32_t dirty_tracked_base{};
    std::uint32_t dirty_tracked_size{};
    std::uint32_t dirty_page_shift{};
    std::uint32_t dirty_page_word_count{};
    // Independent CPU-owner dirty tracker for native device-coherency
    // transactions. Translated guest code and the CPU/simulation thread are
    // the only writers and the CPU thread is the only consumer, so these
    // words intentionally avoid an atomic RMW on every guest store. A DSP,
    // renderer, or other worker must never inspect or clear them directly;
    // workers receive immutable staged copies at their hardware boundary.
    std::uint64_t* cpu_dirty_page_words{};
    std::uint32_t cpu_dirty_tracked_base{};
    std::uint32_t cpu_dirty_tracked_size{};
    std::uint32_t cpu_dirty_page_shift{};
    std::uint32_t cpu_dirty_page_word_count{};
    // Nullable capability for this exact GuestMemoryV1 owner's six shared
    // MEM1/MEM2 guest views. Only read helpers may use it; stores must retain
    // the checked device and dirty-publication path.
    const std::byte* flat_guest_read_base{};
    // Summary bitmap for `dirty_page_words`, one bit per 64-bit page word.
    // Non-null only for the renderer tracker; `dirty_page_words` itself is the
    // authoritative page bitmap. Every ABI-23 producer also publishes summary
    // bits; older modules are rejected by the game/Home ABI gates. Appending
    // preserves prior field offsets, but does not make page-only publication
    // compatible with a summary-only drain.
    std::atomic_uint64_t* dirty_word_summary{};
    std::uint32_t dirty_word_summary_count{};
    std::uint32_t dirty_word_summary_pad{};
};

enum class LogLevelV1 : std::uint32_t {
    Trace = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
};

struct NativeServicesV1 {
    std::uint32_t struct_size{sizeof(NativeServicesV1)};
    std::uint32_t abi_version{kNativeAbiVersion};
    void* user{};
    void (*log)(void* user, LogLevelV1 level, const char* message){};
    std::uint64_t (*time_base_ticks)(void* user){};
    void (*fatal)(void* user, std::uint32_t guest_pc, const char* message){};
    void (*call_guest)(
        void* user,
        std::uint32_t guest_address,
        PpcContext* context,
        GuestMemoryV1* memory){};
    void (*call_guest_resolved)(
        void* user,
        std::uint32_t guest_address,
        NativeGameFunction function,
        PpcContext* context,
        GuestMemoryV1* memory){};
    void (*call_guest_cached)(
        void* user,
        std::uint32_t guest_address,
        std::uint32_t* cached_address,
        NativeGameFunction* cached_function,
        PpcContext* context,
        GuestMemoryV1* memory){};
    void (*system_call)(
        void* user,
        std::uint32_t guest_pc,
        std::uint32_t instruction,
        PpcContext* context,
        GuestMemoryV1* memory){};
    void (*program_trap)(
        void* user,
        std::uint32_t guest_pc,
        std::uint32_t instruction,
        PpcContext* context,
        GuestMemoryV1* memory){};
    void (*return_from_interrupt)(
        void* user,
        std::uint32_t guest_pc,
        PpcContext* context,
        GuestMemoryV1* memory){};
    void (*branch_checkpoint)(
        void* user,
        std::uint32_t guest_pc,
        PpcContext* context,
        GuestMemoryV1* memory){};
    std::uint64_t* checkpoint_counter{};
    const std::uint64_t* checkpoint_next_slow{};
    const std::atomic_uint32_t* pending_event_mask{};
    std::uint32_t runtime_flags{};
    // Bounded main-frame diagnostics publish their current VI window through
    // this atomic so generated code can avoid the log callback entirely when
    // a trace is disabled or outside its requested window. The pointer is
    // owned by the native runtime for the full lifetime of this service table.
    const std::atomic_bool* main_frame_trace_active{};
    // Bounded direct-edge profiling is collected by the runtime executable,
    // not in header-local state inside a generated DLL.  A profile record is
    // an observation only: the caller/callee PCs and elapsed host cycles are
    // reported after the native callee has completed, with no guest-state
    // mutation or alternate dispatch path.  Generated code reaches this
    // callback only while its explicit main-frame VI window is active.
    void (*record_direct_edge_profile)(
        void* user,
        std::uint32_t caller_pc,
        std::uint32_t callee_pc,
        std::uint64_t cycles){};
    void (*gx_pe_finish)(void* user){};
    void (*gx_pe_token)(
        void* user,
        std::uint16_t token,
        bool interrupt){};
    // Exception 7 is synchronous and restartable: the runtime must save the
    // current OSContext at guest_pc, enter RMGE01's translated handler, and
    // execute its exact RFI. Once that RFI restores the same PC with MSR[FP]
    // enabled, the callback returns to the still-live AOT instruction guard;
    // any other return/transfer is a hard failure.
    void (*fpu_unavailable)(
        void* user,
        std::uint32_t guest_pc,
        PpcContext* context,
        GuestMemoryV1* memory){};
    // Statically emitted only for mtspr DEC.  The generated instruction has
    // already replaced DEC in `context`; the callback arms the native absolute
    // deadline and may enter the translated decrementer exception at the exact
    // next-instruction resume boundary.
    void (*decrementer_written)(
        void* user,
        std::uint32_t guest_pc,
        std::uint32_t resume_pc,
        std::uint64_t write_ticks,
        std::uint32_t previous_value,
        PpcContext* context,
        GuestMemoryV1* memory){};
    // Host process owns the VI-latched aspect snapshot. Generated AOT shards
    // live in another DLL and must not use their own header-local atomics.
    // Upper 32 bits: generation; lower 16+16: positive client width/height,
    // or zero for default fitted 16:9 on an unsupported shape.
    std::uint64_t (*experimental_aspect_word)(void* user){};
};

// Guest-execution trace instrumentation is a developer bring-up aid. In a
// shipping build it must compile out entirely: the trace_*_enabled() gates sit
// on the hottest paths (every guest load/store) and, even when their env var
// is unset, a Meyers-singleton guard check is re-evaluated on every call. With
// GALAXY_GUEST_TRACE==0 (the default) the hot gates fold to a compile-time
// false so the compiler deletes the branch and the trace call outright — a pure
// per-memory-op win, behavior-identical to normal play (where tracing is off).
// Define GALAXY_GUEST_TRACE=1 at build time to restore runtime env tracing.
#ifndef GALAXY_GUEST_TRACE
#define GALAXY_GUEST_TRACE 0
#endif

inline bool trace_fileloader_stack_enabled() {
#if GALAXY_GUEST_TRACE
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_FILELOADER_STACK") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
#else
    return false;
#endif
}

inline void trace_fileloader_stack_u32(
    const NativeServicesV1* services,
    const char* op,
    std::uint32_t guest_pc,
    std::uint32_t address,
    std::uint32_t value) {
    if (!trace_fileloader_stack_enabled()) {
        return;
    }
    const bool is_fileloader_stack =
        address >= 0x80900C40u && address < 0x80908D00u;
    const bool is_disc_word_offset =
        (value & 0xFFFF0000u) == 0x3F840000u;
    if (!is_fileloader_stack || !is_disc_word_offset ||
        services == nullptr || services->log == nullptr) {
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    if (index >= 64u) {
        return;
    }

    char message[160]{};
    std::snprintf(
        message,
        sizeof(message),
        "[fileloader-stack] %s pc=0x%08X addr=0x%08X value=0x%08X",
        op,
        guest_pc,
        address,
        value);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline bool trace_audio_pointer_store_enabled() {
#if GALAXY_GUEST_TRACE
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_AUDIO_PTR_WRITES") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
#else
    return false;
#endif
}

inline void trace_audio_pointer_store_u32(
    const NativeServicesV1* services,
    const char* op,
    std::uint32_t guest_pc,
    std::uint32_t address,
    std::uint32_t value) {
    if (!trace_audio_pointer_store_enabled() ||
        services == nullptr || services->log == nullptr) {
        return;
    }

    const bool watch_address =
        address == 0x806A2560u ||  // fn_803ACB88 callback slot (r13-0x2740)
        address == 0x806A2574u ||  // fn_803ACBC4 callback slot (r13-0x272C)
        address == 0x806A2BD0u ||  // JASDSP control pool pointer
        address == 0x806A2BD8u ||  // JASDSP channel table pointer
        address == 0x806A30B0u ||  // JAudio DSP active-task pointer
        address == 0x806A30B4u ||  // JAudio DSP next-task pointer
        (address >= 0x91EFB300u && address < 0x91EFB600u) ||
        (address >= 0x91F16000u && address < 0x91F16800u);
    const bool suspicious_value =
        (value >= 0x90000000u && value < 0x94000000u) ||
        (value >= 0x80000000u && value < 0x81800000u);
    if (!watch_address && !suspicious_value) {
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    if (index >= 2048u) {
        return;
    }

    char message[192]{};
    std::snprintf(
        message,
        sizeof(message),
        "[audio-ptr-store] %s pc=0x%08X addr=0x%08X value=0x%08X",
        op,
        guest_pc,
        address,
        value);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline bool trace_audio_control_writes_enabled() {
#if GALAXY_GUEST_TRACE
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_AUDIO_CONTROL_WRITES") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
#else
    return false;
#endif
}

inline bool trace_resource_status_writes_enabled() {
#if GALAXY_GUEST_TRACE
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_RESOURCE_STATUS_WRITES") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
#else
    return false;
#endif
}

inline bool trace_u32_store_enabled() {
    return trace_fileloader_stack_enabled() ||
           trace_audio_pointer_store_enabled() ||
           trace_audio_control_writes_enabled() ||
           trace_resource_status_writes_enabled();
}

inline bool trace_audio_function_entries_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_AUDIO_FUNCTION_ENTRIES") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

inline std::uint32_t trace_audio_env_u32(
    const char* name,
    std::uint32_t fallback) {
    char value[32]{};
    std::size_t length = 0;
    if (getenv_s(&length, value, sizeof(value), name) != 0 || length <= 1) {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 0);
    if (end == value) {
        return fallback;
    }
    return static_cast<std::uint32_t>(parsed);
}

inline bool trace_audio_function_entry_in_extra_range(
    std::uint32_t address) {
    static const std::uint32_t start =
        trace_audio_env_u32("GALAXY_TRACE_AUDIO_FUNCTION_RANGE_START", 0);
    static const std::uint32_t end =
        trace_audio_env_u32("GALAXY_TRACE_AUDIO_FUNCTION_RANGE_END", 0);
    return start != 0 && address >= start && (end == 0 || address < end);
}

inline std::byte* resolve_guest_fast(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size);

inline bool trace_audio_read_be_fast(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size,
    std::uint64_t& value) {
    const std::byte* direct = resolve_guest_fast(memory, address, size);
    if (direct == nullptr) {
        return false;
    }
    value = 0;
    for (std::uint32_t index = 0; index < size; ++index) {
        value = (value << 8) |
                std::to_integer<std::uint8_t>(direct[index]);
    }
    return true;
}

inline bool trace_audio_read_u32_fast(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t& value) {
    std::uint64_t wide = 0;
    if (!trace_audio_read_be_fast(memory, address, 4, wide)) {
        return false;
    }
    value = static_cast<std::uint32_t>(wide);
    return true;
}

inline bool trace_fileloader_temp_writes_enabled() {
#if GALAXY_GUEST_TRACE
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_FILELOADER_TEMP_WRITES") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
#else
    return false;
#endif
}

inline bool trace_fileloader_temp_range_overlaps(
    std::uint32_t address,
    std::uint32_t size) {
    if (size == 0u) {
        return false;
    }
    const std::uint64_t temp_start =
        trace_audio_env_u32("GALAXY_TRACE_FILELOADER_TEMP_BASE", 0x808DE180u);
    const std::uint64_t temp_size =
        trace_audio_env_u32("GALAXY_TRACE_FILELOADER_TEMP_SIZE", 0x00002000u);
    const std::uint64_t temp_end = temp_start + temp_size;
    const std::uint64_t begin = address;
    const std::uint64_t end = begin + size;
    return begin < temp_end && end > temp_start;
}

inline void trace_fileloader_temp_write(
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    const char* op,
    std::uint32_t guest_pc,
    std::uint32_t address,
    std::uint32_t size,
    std::uint64_t value) {
    if (!trace_fileloader_temp_writes_enabled() ||
        !trace_fileloader_temp_range_overlaps(address, size) ||
        services == nullptr || services->log == nullptr) {
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    const std::uint32_t limit =
        trace_audio_env_u32("GALAXY_TRACE_FILELOADER_TEMP_WRITE_LIMIT", 8192u);
    if (index >= limit) {
        return;
    }

    std::uint64_t old_value = 0;
    const bool old_known =
        size <= 8u && trace_audio_read_be_fast(memory, address, size, old_value);

    char message[256]{};
    if (old_known) {
        std::snprintf(
            message,
            sizeof(message),
            "[fileloader-temp-write] %s pc=0x%08X addr=0x%08X size=%u value=0x%llX old=0x%llX",
            op,
            guest_pc,
            address,
            size,
            static_cast<unsigned long long>(value),
            static_cast<unsigned long long>(old_value));
    } else {
        std::snprintf(
            message,
            sizeof(message),
            "[fileloader-temp-write] %s pc=0x%08X addr=0x%08X size=%u value=0x%llX old=<unavailable>",
            op,
            guest_pc,
            address,
            size,
            static_cast<unsigned long long>(value));
    }
    services->log(services->user, LogLevelV1::Warning, message);
}

inline void trace_fileloader_temp_memmove(
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    const char* op,
    std::uint32_t guest_pc,
    std::uint32_t dst,
    std::uint32_t src,
    std::uint32_t size) {
    if (!trace_fileloader_temp_writes_enabled() ||
        !trace_fileloader_temp_range_overlaps(dst, size) ||
        services == nullptr || services->log == nullptr) {
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    const std::uint32_t limit =
        trace_audio_env_u32("GALAXY_TRACE_FILELOADER_TEMP_MEMMOVE_LIMIT", 1024u);
    if (index >= limit) {
        return;
    }

    std::uint32_t head0 = 0;
    std::uint32_t head4 = 0;
    std::uint32_t head8 = 0;
    std::uint32_t headc = 0;
    const bool have0 = trace_audio_read_u32_fast(memory, dst + 0x00u, head0);
    const bool have4 = trace_audio_read_u32_fast(memory, dst + 0x04u, head4);
    const bool have8 = trace_audio_read_u32_fast(memory, dst + 0x08u, head8);
    const bool havec = trace_audio_read_u32_fast(memory, dst + 0x0Cu, headc);

    char message[320]{};
    std::snprintf(
        message,
        sizeof(message),
        "[fileloader-temp-memmove] %s pc=0x%08X dst=0x%08X src=0x%08X size=0x%08X dstHead=%s0x%08X,%s0x%08X,%s0x%08X,%s0x%08X",
        op,
        guest_pc,
        dst,
        src,
        size,
        have0 ? "" : "?",
        head0,
        have4 ? "" : "?",
        head4,
        have8 ? "" : "?",
        head8,
        havec ? "" : "?",
        headc);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline std::size_t trace_audio_read_string_fast(
    GuestMemoryV1* memory,
    std::uint32_t address,
    char* output,
    std::size_t output_size) {
    if (output == nullptr || output_size == 0) {
        return 0;
    }
    output[0] = '\0';
    if (address < 0x80000000u || address >= 0x94000000u) {
        return 0;
    }

    std::size_t written = 0;
    for (; written + 1 < output_size; ++written) {
        const std::byte* direct = resolve_guest_fast(
            memory, address + static_cast<std::uint32_t>(written), 1);
        if (direct == nullptr) {
            break;
        }
        const unsigned char c =
            std::to_integer<unsigned char>(*direct);
        if (c == '\0') {
            break;
        }
        output[written] =
            (c >= 0x20u && c < 0x7Fu) ? static_cast<char>(c) : '?';
    }
    output[written] = '\0';
    return written;
}

inline const char* trace_audio_function_entry_label(std::uint32_t address) {
    switch (address) {
    case 0x80026D54u: return "AudSystem::startSound";
    case 0x8002A72Cu: return "AudWrap::startStageBgm";
    case 0x8002A744u: return "AudWrap::startSubBgm";
    case 0x8002A770u: return "AudWrap::startLastStageBgm";
    case 0x80399058u: return "FunctionAsyncExecutor::start";
    case 0x803990A8u: return "FunctionAsyncExecutor::startOnMainThread";
    case 0x80399144u: return "FunctionAsyncExecutor::waitForEnd";
    case 0x80399280u: return "FunctionAsyncExecutor::isEnd";
    case 0x80399390u: return "FunctionAsyncExecutor::createAndAddExecInfo";
    case 0x80399444u: return "FunctionAsyncExecutor::fn_80399444";
    case 0x803994D0u: return "fn_803994D0 boot/init loop";
    case 0x80399AF0u: return "GameSystem::frameLoop";
    case 0x803FDCC4u: return "fn_803FDCC4 async-start wrapper";
    case 0x803FDD10u: return "fn_803FDD10 async-start-main wrapper";
    case 0x803FDD54u: return "fn_803FDD54 async-wait wrapper";
    case 0x803FDD88u: return "fn_803FDD88 async-is-end wrapper";
    case 0x803FDDBCu: return "fn_803FDDBC async-test-and-wait wrapper";
    case 0x803F3088u: return "MR::startSoundPlayer";
    case 0x803F3160u: return "MR::startSoundPlayerJ";
    case 0x803F8638u: return "MR::startSystemSE(id)";
    case 0x803F8860u: return "MR::startSoundObject";
    case 0x803F88B8u: return "MR::startSoundObjectLevel(name)";
    case 0x803F8910u: return "MR::startSoundObjectLevel(id)";
    case 0x803F8980u: return "MR::startSoundObjectLevelParam(name)";
    case 0x803F89D8u: return "MR::startSoundObjectLevelParam(id)";
    case 0x803F9358u: return "MR::startStageBGM";
    case 0x803F9580u: return "MR::startLastStageBGM";
    case 0x803F9868u: return "MR::startSubBGM";
    case 0x803F9DECu: return "MR::startSystemSE(name)";
    case 0x804878BCu: return "audio-interleave";
    case 0x804945CCu: return "audio-ring-output";
    // These are the RMGE01 entries proven from the generated US DOL bodies.
    // The corresponding RMGK01 JAudio cluster is +0x2244 and must not be used
    // here: those addresses name unrelated functions in RMGE01.
    case 0x80492A08u: return "JASChannel::play";
    case 0x80492A78u: return "JASChannel::playForce";
    case 0x80495038u: return "JASDSPChannel::start";
    case 0x80495150u: return "JASDSPChannel::alloc";
    case 0x804951BCu: return "JASDSPChannel::allocForce";
    case 0x80495354u: return "JASDSPChannel::updateProc";
    case 0x8049559Cu: return "JASDSPChannel::updateAll";
    case 0x804958E0u: return "JASDsp::TChannel::init";
    case 0x80495900u: return "JASDsp::TChannel::playStart";
    case 0x80495964u: return "JASDsp::TChannel::playStop";
    case 0x80495970u: return "JASDsp::TChannel::replyFinishRequest";
    case 0x80495980u: return "JASDsp::TChannel::forceStop";
    case 0x8049599Cu: return "JASDsp::TChannel::setWaveInfo";
    case 0x80495AB4u: return "JASDsp::TChannel::setOscInfo";
    case 0x804980F0u: return "JAISeMgr::startSound";
    case 0x80499208u: return "JAISeqMgr::startSound";
    case 0x8049AEF8u: return "JAIStreamMgr::startSound";
    default: return nullptr;
    }
}

enum class AudioTraceStringArgument : std::uint8_t {
    None,
    R3,
    R4,
};

inline AudioTraceStringArgument trace_audio_function_entry_string_argument(
    std::uint32_t address) {
    switch (address) {
    case 0x803F3088u:
    case 0x803F3160u:
    case 0x803F9358u:
    case 0x803F9868u:
    case 0x803F9DECu:
        return AudioTraceStringArgument::R3;
    case 0x803F8860u:
    case 0x803F88B8u:
    case 0x803F8980u:
        return AudioTraceStringArgument::R4;
    default:
        return AudioTraceStringArgument::None;
    }
}

inline void trace_audio_function_entry_slow(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address) {
    const char* label = trace_audio_function_entry_label(function_address);
    if (label == nullptr &&
        trace_audio_function_entry_in_extra_range(function_address)) {
        label = "audio-range";
    }
    if (label == nullptr) {
        return;
    }

    // The ring/interleave functions and the per-channel maintenance sweep run
    // continuously.  Sample them before the shared quota so they cannot hide
    // the comparatively rare sound-start and async-init entries that this
    // trace exists to diagnose.
    if (function_address == 0x804878BCu ||
        function_address == 0x804945CCu ||
        function_address == 0x80495354u ||
        function_address == 0x8049559Cu) {
        static std::atomic<std::uint32_t> periodic_count{0};
        const std::uint32_t periodic_index = periodic_count.fetch_add(1);
        if (periodic_index >= 32u && (periodic_index % 512u) != 0) {
            return;
        }
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    if (index >= 16384u) {
        return;
    }

    std::uint32_t control_pool = 0;
    std::uint32_t tchannel_table = 0;
    trace_audio_read_u32_fast(memory, 0x806A2BD0u, control_pool);
    trace_audio_read_u32_fast(memory, 0x806A2BD8u, tchannel_table);

    // Capture the fields that distinguish a requested JASChannel, its
    // JASDSPChannel wrapper, and the underlying TChannel. Reads are passive and
    // best-effort; '?' explicitly marks a non-mapped r3 rather than presenting
    // a fabricated zero. For TChannel, word 0 contains active/finished/pitch
    // and +0x118 is the sample-source pointer. For JASDSPChannel, +0x08 is the
    // flags word and +0x18 is its TChannel pointer.
    std::uint32_t object_word0 = 0;
    std::uint32_t object_word8 = 0;
    std::uint32_t object_word18 = 0;
    std::uint32_t object_word118 = 0;
    const bool have_object_word0 =
        trace_audio_read_u32_fast(memory, context->gpr[3], object_word0);
    const bool have_object_word8 = trace_audio_read_u32_fast(
        memory, context->gpr[3] + 0x08u, object_word8);
    const bool have_object_word18 = trace_audio_read_u32_fast(
        memory, context->gpr[3] + 0x18u, object_word18);
    const bool have_object_word118 = trace_audio_read_u32_fast(
        memory, context->gpr[3] + 0x118u, object_word118);

    char r3_string[80]{};
    char r4_string[80]{};
    std::size_t r3_string_length = 0;
    std::size_t r4_string_length = 0;
    const AudioTraceStringArgument string_argument =
        trace_audio_function_entry_string_argument(function_address);
    if (string_argument == AudioTraceStringArgument::R3) {
        r3_string_length = trace_audio_read_string_fast(
            memory, context->gpr[3], r3_string, sizeof(r3_string));
    } else if (string_argument == AudioTraceStringArgument::R4) {
        r4_string_length = trace_audio_read_string_fast(
            memory, context->gpr[4], r4_string, sizeof(r4_string));
    }
    const bool has_r3_string = r3_string_length != 0;
    const bool has_r4_string = r4_string_length != 0;
    const char* r3_prefix = has_r3_string ? " r3str=\"" : "";
    const char* r3_value = has_r3_string ? r3_string : "";
    const char* r4_prefix = has_r4_string
        ? (has_r3_string ? "\" r4str=\"" : " r4str=\"")
        : "";
    const char* r4_value = has_r4_string ? r4_string : "";
    const char* string_suffix =
        (has_r3_string || has_r4_string) ? "\"" : "";

    char message[640]{};
    std::snprintf(
        message,
        sizeof(message),
        "[audio-function] entry=%s pc=0x%08X lr=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X ctrl-pool=0x%08X chan-table=0x%08X obj0=%s0x%08X obj8=%s0x%08X obj18=%s0x%08X obj118=%s0x%08X%s%s%s%s%s",
        label,
        function_address,
        context->lr,
        context->gpr[3],
        context->gpr[4],
        context->gpr[5],
        context->gpr[6],
        control_pool,
        tchannel_table,
        have_object_word0 ? "" : "?",
        object_word0,
        have_object_word8 ? "" : "?",
        object_word8,
        have_object_word18 ? "" : "?",
        object_word18,
        have_object_word118 ? "" : "?",
        object_word118,
        r3_prefix,
        r3_value,
        r4_prefix,
        r4_value,
        string_suffix);
    services->log(services->user, LogLevelV1::Warning, message);
}

GALAXY_ALWAYS_INLINE void trace_audio_function_entry(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address) {
    if (!trace_audio_function_entries_enabled() ||
        services == nullptr || services->log == nullptr ||
        context == nullptr) {
        return;
    }
    trace_audio_function_entry_slow(
        services, context, memory, function_address);
}

// The generated main-loop trace deliberately uses the existing log callback
// as a compact, ABI-neutral transport.  The native host recognizes only the
// fixed outer-frame and frame-loop-subcall markers below and retains them in
// an in-memory ring; no per-frame console I/O is performed while the
// measurement is active.  This is a diagnostic hook in statically generated
// C++, not a runtime instruction decoder or dispatch fallback.
inline bool trace_main_frame_function_entries_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_MAIN_FRAME") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

// The generated module is permitted to emit the fixed main-frame markers only
// while the native runtime has opened its explicitly requested VI window.
// Checking the runtime-owned atomic before calling the log transport keeps
// diagnostic code from perturbing real-time guest execution outside that
// window. This is diagnostic gating only; it does not alter guest state, call
// routing, checkpoints, or native service availability.
GALAXY_ALWAYS_INLINE bool main_frame_trace_window_active(
    const NativeServicesV1* services) {
    return services != nullptr && services->main_frame_trace_active != nullptr &&
           services->main_frame_trace_active->load(std::memory_order_relaxed);
}

GALAXY_ALWAYS_INLINE void trace_main_frame_function_entry(
    const NativeServicesV1* services,
    std::uint32_t function_address) {
    if (!main_frame_trace_window_active(services) ||
        !trace_main_frame_function_entries_enabled() || services->log == nullptr) {
        return;
    }

    const char* marker = nullptr;
    switch (function_address) {
    case 0x80399AF0u: marker = "__galaxy_main_frame_entry:80399AF0"; break;
    case 0x80399B58u: marker = "__galaxy_main_frame_entry:80399B58"; break;
    case 0x8039D3D4u: marker = "__galaxy_main_frame_entry:8039D3D4"; break;
    case 0x8039DBE4u: marker = "__galaxy_main_frame_entry:8039DBE4"; break;
    case 0x803B462Cu: marker = "__galaxy_main_frame_entry:803B462C"; break;
    case 0x803B5478u: marker = "__galaxy_main_frame_entry:803B5478"; break;
    case 0x803B7818u: marker = "__galaxy_main_frame_entry:803B7818"; break;
    case 0x8039B9B0u: marker = "__galaxy_main_frame_entry:8039B9B0"; break;
    case 0x80385758u: marker = "__galaxy_main_frame_entry:80385758"; break;
    case 0x8039B9B8u: marker = "__galaxy_main_frame_entry:8039B9B8"; break;
    case 0x8039A288u: marker = "__galaxy_main_frame_entry:8039A288"; break;
    case 0x8039FD20u: marker = "__galaxy_main_frame_entry:8039FD20"; break;
    case 0x8039FEE0u: marker = "__galaxy_main_frame_entry:8039FEE0"; break;
    case 0x8039FF14u: marker = "__galaxy_main_frame_entry:8039FF14"; break;
    case 0x803A007Cu: marker = "__galaxy_main_frame_entry:803A007C"; break;
    case 0x803A00FCu: marker = "__galaxy_main_frame_entry:803A00FC"; break;
    default: return;
    }
    services->log(services->user, LogLevelV1::Trace, marker);
}

// A main-frame stage is an explicitly selected translated continuation, not a
// guest PC or a dispatch target. It is used only by bounded diagnostics to
// partition an already-observed direct native call without changing guest
// state, call routing, or checkpoint behaviour.
GALAXY_ALWAYS_INLINE void trace_main_frame_stage(
    const NativeServicesV1* services,
    const char* marker) {
    if (!main_frame_trace_window_active(services) ||
        !trace_main_frame_function_entries_enabled() || services->log == nullptr ||
        marker == nullptr) {
        return;
    }
    services->log(services->user, LogLevelV1::Trace, marker);
}

// JUTVideo's post-retrace callback uses the OS message helpers below to
// produce main's frame-tick queue.  Those helpers also serve unrelated guest
// queues, so the generated trace must prove r3 is JUTVideo's exact queue
// before it emits a marker.  The callback's own statically verified producer
// entry is also recorded, but it never inspects or changes the queue.  The
// marker is only an ABI-neutral transport to the runtime's bounded recorder;
// it is neither an OS-message substitute nor a runtime PPC dispatcher.
inline bool trace_jutvideo_mq_function_entries_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_JUTVIDEO_MQ") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

// The generic guest-load implementation appears later in this ABI header.
// Declare it here so the optional JUTVideo queue filter can validate r3
// against the live manager before emitting any diagnostic marker.
GALAXY_ALWAYS_INLINE std::uint32_t guest_load_u32(
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);

GALAXY_ALWAYS_INLINE void trace_jutvideo_mq_function_entry(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address) {
    if (!trace_jutvideo_mq_function_entries_enabled() ||
        services == nullptr || services->log == nullptr ||
        context == nullptr) {
        return;
    }

    constexpr std::uint32_t kJutVideoManagerPtr = 0x806A2850u;
    constexpr std::uint32_t kJutVideoMessageQueueOffset = 0x38u;
    constexpr std::uint32_t kFallbackJutVideoMessageQueue = 0x809AE098u;
    // This is the statically recovered post-retrace producer callback.  Its
    // r3 is its callback argument rather than an OSMessageQueue, so capture
    // its incoming LR before applying the queue-only filter used below.
    constexpr std::uint32_t kJutVideoProducerCallback = 0x80419554u;
    if (function_address == kJutVideoProducerCallback) {
        char marker[64]{};
        const int marker_length = std::snprintf(
            marker,
            sizeof(marker),
            "__galaxy_jutvideo_mq_entry:%08X:%08X",
            static_cast<unsigned>(function_address),
            static_cast<unsigned>(context->lr));
        if (marker_length > 0 &&
            static_cast<std::size_t>(marker_length) < sizeof(marker)) {
            services->log(services->user, LogLevelV1::Trace, marker);
        }
        return;
    }
    const std::uint32_t message_queue = context->gpr[3];
    if (message_queue != kFallbackJutVideoMessageQueue) {
        if (memory == nullptr) {
            return;
        }
        const std::uint32_t manager = guest_load_u32(
            memory, kJutVideoManagerPtr, services, function_address);
        if (manager < 0x80000000u || manager >= 0x94000000u ||
            message_queue != manager + kJutVideoMessageQueueOffset) {
            return;
        }
    }

    // The bounded runtime recorder needs the generated caller's return
    // address to distinguish the JUTVideo producer from consumers that use
    // the same OS queue helpers.  This remains trace-only and is emitted only
    // after the exact r3 queue identity check above has succeeded.
    switch (function_address) {
    case 0x804A88E4u:
    case 0x804A89ACu:
    case 0x804A8A88u:
        break;
    default: return;
    }
    char marker[64]{};
    const int marker_length = std::snprintf(
        marker,
        sizeof(marker),
        "__galaxy_jutvideo_mq_entry:%08X:%08X",
        static_cast<unsigned>(function_address),
        static_cast<unsigned>(context->lr));
    if (marker_length <= 0 ||
        static_cast<std::size_t>(marker_length) >= sizeof(marker)) {
        return;
    }
    services->log(services->user, LogLevelV1::Trace, marker);
}

inline bool trace_movie_function_entries_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_TRACE_MOVIE_FUNCTION_ENTRIES") == 0 &&
            length > 1) {
            return value[0] != '0';
        }
        length = 0;
        value[0] = '\0';
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_MOVIE") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

inline bool trace_movie_deep_function_entries_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_TRACE_MOVIE_DEEP_FUNCTION_ENTRIES") == 0 &&
            length > 1) {
            return value[0] != '0';
        }
        length = 0;
        value[0] = '\0';
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_MOVIE_DEEP") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

inline const char* trace_movie_function_entry_label(std::uint32_t address) {
    switch (address) {
    case 0x800BE334u: return "DemoStartRequestUtil::owner startDemoSystem(NameObj)";
    case 0x800BE3A4u: return "DemoStartRequestUtil::startDemoSystem(NameObj)";
    case 0x800BE450u: return "DemoStartRequestUtil::owner startDemoSystem/requestStartDemo";
    case 0x800BE4DCu: return "DemoStartRequestUtil::startDemoSystem(LiveActor)";
    case 0x800BE510u: return "DemoStartRequestUtil::startDemoSystem(LayoutActor)";
    case 0x800BE544u: return "DemoStartRequestUtil::requestStartDemo(LiveActor)";
    case 0x800BE568u: return "DemoStartRequestUtil::owner requestStartDemo(LayoutActor)";
    case 0x800BE674u: return "DemoStartRequestUtil::requestStartDemo(LayoutActor)";
    case 0x800BE764u: return "DemoStartRequestUtil::owner requestStartDemo/timeKeep";
    case 0x800BE7A4u: return "DemoStartRequestUtil::requestStartDemo(NerveExecutor)";
    case 0x800BE8C0u: return "DemoStartRequestUtil::requestStartTimeKeepDemo(LiveActor)";
    case 0x800BE9D8u: return "DemoStartRequestUtil::requestStartTimeKeepDemo(NerveExecutor)";
    case 0x800BEAE8u: return "DemoStartRequestUtil::owner requestStartTimeKeepDemo(NameObj)";
    case 0x800BEAF8u: return "DemoStartRequestUtil::requestStartTimeKeepDemo(NameObj)";
    case 0x800BEBC0u: return "DemoStartRequestUtil::owner startDemo(info)";
    case 0x800BEBD4u: return "DemoStartRequestUtil::startDemo(info)";
    case 0x800BECE8u: return "DemoStartRequestUtil::owner startDemo(holder)";
    case 0x800BED60u: return "DemoStartRequestUtil::startDemo(holder)";
    case 0x800BED7Cu: return "DemoStartRequestUtil::owner popStartDemoRequest";
    case 0x800BEDA0u: return "DemoStartRequestUtil::popStartDemoRequest";
    case 0x800BEDB0u: return "DemoStartRequestUtil::owner setDemoStartInfoCommon";
    case 0x800BEDE8u: return "DemoStartRequestUtil::setDemoStartInfoCommon";
    case 0x800BEE40u: return "DemoStartRequestUtil::owner starter query/setup";
    case 0x800BEE44u: return "DemoStartRequestUtil::isExistStartDemoRequest";
    case 0x800BEE48u: return "DemoStartRequestUtil::getDemoStarter";
    case 0x800BEEA0u: return "DemoStartRequestUtil::setNerveToStarter";
    case 0x800BEEBCu: return "DemoStartRequestUtil::owner movement/empty";
    case 0x800BEEECu: return "DemoStartRequestUtil::requestMovementOn";
    case 0x800BEF58u: return "DemoStartRequestUtil::isEmpty";
    case 0x800BEF84u: return "DemoStartRequestUtil::owner startDemoExecutorIfExist";
    case 0x800BEFA4u: return "DemoStartRequestUtil::startDemoExecutorIfExist";
    case 0x800C21DCu: return "ScenarioStarter::owner ctor/init";
    case 0x800C2310u: return "ScenarioStarter::ctor";
    case 0x800C23F8u: return "ScenarioStarter::init";
    case 0x800C250Cu: return "ScenarioStarter::owner exeWaitToStart";
    case 0x800C2588u: return "ScenarioStarter::exeWaitToStart";
    case 0x800C25B8u: return "ScenarioStarter::owner cinemaFrameBlank/railMove";
    case 0x800C25C8u: return "ScenarioStarter::exeCinemaFrameBlank";
    case 0x800C264Cu: return "ScenarioStarter::exeRailMove";
    case 0x800C27DCu: return "ScenarioStarter::owner exeRailMoveCanceled";
    case 0x800C297Cu: return "ScenarioStarter::exeRailMoveCanceled";
    case 0x800C29E4u: return "ScenarioStarter::owner exeShowWelcomeLayout";
    case 0x800C2A28u: return "ScenarioStarter::exeShowWelcomeLayout";
    case 0x800C2AD8u: return "ScenarioStarter::owner shoot/bind update";
    case 0x800C2B0Cu: return "ScenarioStarter::initShootPath";
    case 0x800C2B90u: return "ScenarioStarter::updateBindPosition";
    case 0x800C2C4Cu: return "ScenarioStarter::updateBindActorMtx";
    case 0x800C2E20u: return "ScenarioStarter::owner updateShootMotion";
    case 0x800C2E54u: return "ScenarioStarter::updateShootMotion";
    case 0x800C2ED8u: return "ScenarioStarter::owner turnBindHead";
    case 0x800C2F48u: return "ScenarioStarter::turnBindHead";
    case 0x800C3100u: return "ScenarioStarter::owner calcShootMotionTime";
    case 0x800C3108u: return "ScenarioStarter::calcShootMotionTime";
    case 0x800C3200u: return "ScenarioStarter::owner receiveMsg/skip";
    case 0x800C3290u: return "ScenarioStarter::receiveOtherMsg";
    case 0x800C3348u: return "ScenarioStarter::trySkipTrigger";
    case 0x800C33C4u: return "ScenarioStarter::owner isStartBgmOnWelcome";
    case 0x800C33CCu: return "ScenarioStarter::isStartBgmOnWelcome";
    case 0x800C3430u: return "ScenarioStarter::owner dtor";
    case 0x800C3488u: return "ScenarioStarter::dtor";
    case 0x800C34E4u: return "ScenarioStarter::sinit";
    case 0x800C3578u: return "NrvScenarioStarter::owner execute";
    case 0x800C3590u: return "NrvScenarioStarter::executeShowWelcomeLayout";
    case 0x800C3598u: return "NrvScenarioStarter::executeRailMoveCanceled";
    case 0x800C35A0u: return "NrvScenarioStarter::executeRailMove";
    case 0x800C35A8u: return "NrvScenarioStarter::executeCinemaFrameBlank";
    case 0x800C35B0u: return "NrvScenarioStarter::executeWaitToStart";
    case 0x800C35B8u: return "NrvScenarioStarter::executeWaitScenarioCameraEnd";
    case 0x8033EE8Cu: return "GameScene::draw3D";
    case 0x8033F040u: return "GameScene::draw2D";
    case 0x803402D0u: return "ScenarioOpening region ctor/dtor candidate";
    case 0x80340328u: return "ScenarioOpening region ctor candidate";
    case 0x80340388u: return "ScenarioOpening region start/end candidate";
    case 0x80340400u: return "ScenarioOpeningCameraState::ctor/init candidate";
    case 0x80340454u: return "ScenarioOpeningCameraState::update candidate";
    case 0x8034048Cu: return "ScenarioOpeningCameraState::end/isDone candidate";
    case 0x803404B0u: return "ScenarioOpeningCameraState::start candidate";
    case 0x80340530u: return "ScenarioOpeningCameraState::trySkip/isValid candidate";
    case 0x803405CCu: return "ScenarioOpeningCameraState::exePlay candidate";
    case 0x80340650u: return "ScenarioOpeningCameraState::exePlay alt candidate";
    case 0x803409BCu: return "ScenarioOpeningCameraState::wait manager predicate";
    case 0x80398C94u: return "AsyncExecInfo::ctor";
    case 0x80398D3Cu: return "AsyncExecInfo::execute";
    case 0x80398E00u: return "AsyncWorker::threadMain";
    case 0x80399058u: return "FunctionAsyncExecutor::start";
    case 0x80399144u: return "FunctionAsyncExecutor::waitForEnd";
    case 0x80399280u: return "FunctionAsyncExecutor::isEnd";
    case 0x80399390u: return "FunctionAsyncExecutor::createAndAddExecInfo";
    case 0x800317A0u: return "ResourceHolder::isAllRequestedDone";
    case 0x80394D2Cu: return "SceneResources::isAllRequestedDone";
    case 0x8039AFA8u: return "SceneManager::isSubSceneResourceDone";
    case 0x8039C284u: return "SubScene::exeD7F0";
    case 0x8039C548u: return "SubScene::exeD7F0_async";
    case 0x8039C5FCu: return "SubScene::markFirstD7F0Pass";
    case 0x8039C730u: return "SubScene::finishD7F0";
    case 0x8040E048u: return "ResourceHelper::allocArchiveBuffer";
    case 0x8040EDA8u: return "ResourceHelper::readArchiveRange";
    case 0x8040EE7Cu: return "ResourceHelper::releaseArchiveBuffer";
    case 0x80412EF8u: return "ResourceHelper::openStackCtor";
    case 0x80413124u: return "ResourceHelper::openFile";
    case 0x804140D4u: return "ResourceHelper::validateAndOpen";
    case 0x80414178u: return "ResourceHelper::openResource";
    case 0x8041421Cu: return "ResourceHelper::createRequest";
    case 0x804142D4u: return "ResourceHelper::executeRequest";
    case 0x80414628u: return "ResourceHelper::closeRequest";
    case 0x80414700u: return "ResourceHelper::requestCtor";
    case 0x80414740u: return "ResourceHelper::requestDtor";
    case 0x804147D0u: return "ResourceHelper::loadFromArchive";
    case 0x80415454u: return "ResourceHelper::parseArchiveHeader";
    case 0x80415674u: return "ResourceHelper::reportRequest";
    case 0x80415880u: return "ResourceHelper::reportClose";
    case 0x80415E84u: return "ResourceHelper::makeArchiveReader";
    case 0x804AAEA0u: return "ResourceHelper::osLockOrAlloc";
    case 0x804BF844u: return "ResourceHelper::read32Aligned";
    case 0x8003379Cu: return "ResourceItem::startLoad";
    case 0x8049251Cu: return "ResourceItem::markDoneIfToken";
    case 0x80492644u: return "ResourceItem::asyncCallback";
    case 0x804926D0u: return "ResourceItem::requestAsync";
    case 0x8049DADCu: return "ResourceLoader::isSlotDone";
    case 0x803A29B0u: return "Spine::setNerve";
    case 0x803FDCC4u: return "MR::startFunctionAsyncExecute";
    case 0x803FDDBCu: return "MR::isEndAndWaitFunctionAsyncExecute";
    case 0x8034DB5Cu: return "CaptureScreenDirector::owner ctor";
    case 0x8034DBA4u: return "CaptureScreenDirector::ctor";
    case 0x8034DC04u: return "CaptureScreenDirector::owner captureIfAllow";
    case 0x8034DC4Cu: return "CaptureScreenDirector::captureIfAllow";
    case 0x8034DC98u: return "CaptureScreenDirector::owner capture";
    case 0x8034DCC4u: return "CaptureScreenDirector::capture";
    case 0x8034DD40u: return "CaptureScreenDirector::requestCaptureTiming";
    case 0x8034DDB0u: return "CaptureScreenDirector::invalidateCaptureTiming";
    case 0x8034DDD4u: return "CaptureScreenDirector::owner texture/timing query";
    case 0x8034DE0Cu: return "CaptureScreenDirector::getResTIMG";
    case 0x8034DE18u: return "CaptureScreenDirector::getTexImage";
    case 0x8034DE24u: return "CaptureScreenDirector::getUsingTiming";
    case 0x8034DE28u: return "CaptureScreenDirector::owner current/find";
    case 0x8034DE3Cu: return "CaptureScreenDirector::getCurrentTiming";
    case 0x8034DE54u: return "CaptureScreenDirector::findFromName";
    case 0x8034DE7Cu: return "CaptureScreenActor::owner ctor";
    case 0x8034DEC4u: return "CaptureScreenActor::ctor";
    case 0x8034DED0u: return "CaptureScreenActor::owner draw";
    case 0x8034DF34u: return "CaptureScreenActor::draw";
    case 0x8034DF3Cu: return "CaptureScreenDirector::dtor";
    case 0x8034DF44u: return "CaptureScreenActor::owner dtor";
    case 0x8034DF94u: return "CaptureScreenActor::dtor";
    case 0x8034E30Cu: return "CinemaFrame::owner ctor/init";
    case 0x8034E38Cu: return "CinemaFrame::ctor";
    case 0x8034E400u: return "CinemaFrame::init";
    case 0x8034E410u: return "CinemaFrame::owner appear";
    case 0x8034E458u: return "CinemaFrame::appear";
    case 0x8034E470u: return "CinemaFrame::owner tryScreenToFrame";
    case 0x8034E490u: return "CinemaFrame::tryScreenToFrame";
    case 0x8034E524u: return "CinemaFrame::owner frameBlank transitions";
    case 0x8034E538u: return "CinemaFrame::tryFrameToBlank";
    case 0x8034E5CCu: return "CinemaFrame::tryBlankToFrame";
    case 0x8034E658u: return "CinemaFrame::owner tryFrameToScreen";
    case 0x8034E674u: return "CinemaFrame::tryFrameToScreen";
    case 0x8034E69Cu: return "CinemaFrame::owner forceToScreen";
    case 0x8034E708u: return "CinemaFrame::forceToScreen";
    case 0x8034E74Cu: return "CinemaFrame::owner forceToFrame";
    case 0x8034E75Cu: return "CinemaFrame::forceToFrame";
    case 0x8034E7A8u: return "CinemaFrame::owner forceToBlank";
    case 0x8034E7B0u: return "CinemaFrame::forceToBlank";
    case 0x8034E800u: return "CinemaFrame::owner isStop";
    case 0x8034E804u: return "CinemaFrame::isStop";
    case 0x8034E85Cu: return "CinemaFrame::owner exeScreen";
    case 0x8034E878u: return "CinemaFrame::exeScreen";
    case 0x8034E8F8u: return "CinemaFrame::owner dtor";
    case 0x8034E914u: return "CinemaFrame::dtor";
    case 0x8034E948u: return "CinemaFrame::owner sinit/Nrv execute";
    case 0x8034E970u: return "CinemaFrame::sinit";
    case 0x8034EA34u: return "NrvCinemaFrame::executeFrameToScreen";
    case 0x8034EA7Cu: return "NrvCinemaFrame::executeBlankToFrame";
    case 0x8034EAC8u: return "NrvCinemaFrame::executeFrameToBlank";
    case 0x8034EAECu: return "NrvCinemaFrame::owner executeScreenToFrame/cinema";
    case 0x8034EB14u: return "NrvCinemaFrame::executeScreenToFrame";
    case 0x8034EB50u: return "NrvCinemaFrame::owner executeBlank/Frame/Screen";
    case 0x8034EB60u: return "NrvCinemaFrame::executeBlank";
    case 0x8034EBBCu: return "NrvCinemaFrame::executeFrame";
    case 0x8034EBD0u: return "NrvCinemaFrame::executeScreen";
    case 0x803711FCu: return "MoviePlayerSimple::owner draw";
    case 0x80371240u: return "MoviePlayerSimple::draw";
    case 0x803712D4u: return "MoviePlayerSimple::owner startMovie";
    case 0x8037132Cu: return "MoviePlayerSimple::startMovie";
    case 0x803715D8u: return "MoviePlayerSimple::owner exeOpen";
    case 0x8037163Cu: return "MoviePlayerSimple::exeOpen";
    case 0x803716B4u: return "MoviePlayerSimple::owner exePreload";
    case 0x803716D8u: return "MoviePlayerSimple::exePreload";
    case 0x8037172Cu: return "MoviePlayerSimple::owner exePlaying";
    case 0x8037175Cu: return "MoviePlayerSimple::exePlaying";
    case 0x80371E44u: return "MoviePlayingSequence::owner appear/playWait";
    case 0x80371E5Cu: return "MoviePlayingSequence::appear";
    case 0x80371E98u: return "MoviePlayingSequence::exePlayWait";
    case 0x80371F04u: return "MoviePlayingSequence::owner exePlayStart";
    case 0x80371F7Cu: return "MoviePlayingSequence::exePlayStart";
    case 0x80371FDCu: return "MoviePlayingSequence::owner exePlay";
    case 0x80371FE8u: return "MoviePlayingSequence::exePlay";
    case 0x80372118u: return "MoviePlayingSequence::owner tryEnd";
    case 0x80372128u: return "MoviePlayingSequence::tryEnd";
    case 0x80372248u: return "MoviePlayingSequence::owner closeWipe";
    case 0x80372280u: return "MoviePlayingSequence::exeCloseWipeOnPlaying";
    case 0x803722ACu: return "MoviePlayingSequence::owner exeEndWait";
    case 0x803722DCu: return "MoviePlayingSequence::exeEndWait";
    case 0x803723CCu: return "MR::owner startMovie";
    case 0x80372510u: return "MR::startMovie";
    case 0x80372DB0u: return "MovieStarter::owner appear";
    case 0x80372F28u: return "MovieStarter::appear";
    case 0x8038CFB8u: return "THPSimplePlayerWrapper::decode";
    case 0x8038D1CCu: return "THPSimplePlayerWrapper::drawCurrentFrame";
    case 0x8038D264u: return "THPSimplePlayerWrapper::getVideoInfo";
    case 0x8038D2ACu: return "THPSimplePlayerWrapper::getFrameRate";
    case 0x8038D2C8u: return "THPSimplePlayerWrapper::getTotalFrame";
    case 0x8038D2E4u: return "THPSimplePlayerWrapper::videoDecode";
    case 0x8038D34Cu: return "THPSimplePlayerWrapper::readFrameAsync";
    case 0x8038D420u: return "THPSimplePlayerWrapper::checkPrefetch";
    case 0x8038D494u: return "THPSimplePlayerWrapper::dvdCallBack";
    case 0x8038D56Cu: return "THPSimplePlayerWrapper::readAsyncCallBack";
    case 0x8038D6FCu: return "THPSimplePlayerWrapper::getNextBuffer";
    case 0x8038D718u: return "THPSimplePlayerWrapper::tryDvdOpen";
    case 0x8038D7B8u: return "THPSimplePlayerWrapper::setupParams";
    case 0x8038D80Cu: return "THPSimplePlayerWrapper::exeReadHeader";
    case 0x8038D868u: return "THPSimplePlayerWrapper::exeReadFrameComp";
    case 0x8038D8C8u: return "THPSimplePlayerWrapper::exeReadVideoComp";
    case 0x8038D924u: return "THPSimplePlayerWrapper::exeReadAudioComp";
    case 0x8038D980u: return "THPSimplePlayerWrapper::endReadHeader";
    case 0x8038D9F4u: return "THPSimplePlayerWrapper::endReadFrameComp";
    case 0x8038DA44u: return "THPSimplePlayerWrapper::endReadVideoComp";
    case 0x8038DA98u: return "THPSimplePlayerWrapper::endReadAudioComp";
    case 0x8038DAF4u: return "THPSimplePlayerWrapper::exeReadPreLoad";
    case 0x8038DB74u: return "THPSimplePlayerWrapper::endReadPreLoadOne";
    case 0x8038DC44u: return "THPSimplePlayerWrapper::checkComponentsInFrame";
    case 0x8038DCC0u: return "THPSimplePlayerWrapper::tryFinishDvdOpen";
    case 0x80452B74u:
        return trace_movie_deep_function_entries_enabled()
            ? "THP decode paired-single kernel"
            : nullptr;
    case 0x80453258u:
        return trace_movie_deep_function_entries_enabled()
            ? "THP frame/scan/header parser"
            : nullptr;
    case 0x80453B10u:
        return trace_movie_deep_function_entries_enabled()
            ? "THP bitstream/decompress YUV"
            : nullptr;
    case 0x80454E64u:
    case 0x80454E84u:
        return trace_movie_deep_function_entries_enabled()
            ? "THP Huffman DCT decode"
            : nullptr;
    case 0x80455B64u:
    case 0x80455BC4u:
        return trace_movie_deep_function_entries_enabled()
            ? "THP Huffman V decode"
            : nullptr;
    case 0x80456138u:
    case 0x8045621Cu:
    case 0x8045622Cu:
        return trace_movie_deep_function_entries_enabled()
            ? "THP init/audio decode"
            : nullptr;
    default: return nullptr;
    }
}

inline bool trace_movie_pointer_sample(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t& hash,
    std::uint8_t& min_value,
    std::uint8_t& max_value) {
    const std::byte* direct = resolve_guest_fast(memory, address, 64u);
    if (direct == nullptr) {
        return false;
    }
    hash = 2166136261u;
    min_value = 0xFFu;
    max_value = 0u;
    for (std::uint32_t index = 0; index < 64u; ++index) {
        const std::uint8_t value =
            std::to_integer<std::uint8_t>(direct[index]);
        hash ^= value;
        hash *= 16777619u;
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
    }
    return true;
}

inline bool trace_movie_read_u8_fast(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint8_t& out) {
    const std::byte* direct = resolve_guest_fast(memory, address, 1u);
    if (direct == nullptr) {
        return false;
    }
    out = std::to_integer<std::uint8_t>(*direct);
    return true;
}

inline bool trace_strlen_stall_enabled() {
#if GALAXY_GUEST_TRACE
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_STRLEN_STALL") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
#else
    return false;
#endif
}

inline std::uint32_t trace_strlen_stall_min_len() {
#if GALAXY_GUEST_TRACE
    static const std::uint32_t min_len = [] {
        char value[32]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_TRACE_STRLEN_STALL_MIN") != 0 ||
            length <= 1) {
            return 4096u;
        }
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 0);
        return end == value ? 4096u : static_cast<std::uint32_t>(parsed);
    }();
    return min_len;
#else
    return 4096u;
#endif
}

inline void trace_strlen_preview(
    GuestMemoryV1* memory,
    std::uint32_t address,
    char* out,
    std::size_t out_size,
    bool& mapped,
    std::uint32_t& first_null_offset) {
    mapped = false;
    first_null_offset = 0xFFFFFFFFu;
    if (out == nullptr || out_size == 0u) {
        return;
    }
    out[0] = '\0';

    constexpr std::uint32_t kPreviewBytes = 48u;
    const std::byte* direct = resolve_guest_fast(memory, address, kPreviewBytes);
    if (direct == nullptr) {
        std::snprintf(out, out_size, "<unmapped>");
        return;
    }
    mapped = true;

    std::size_t written = 0;
    for (std::uint32_t index = 0; index < kPreviewBytes; ++index) {
        const std::uint8_t value =
            std::to_integer<std::uint8_t>(direct[index]);
        if (value == 0u && first_null_offset == 0xFFFFFFFFu) {
            first_null_offset = index;
        }
        const char ch =
            value >= 0x20u && value <= 0x7Eu ? static_cast<char>(value) : '.';
        if (written + 1u >= out_size) {
            break;
        }
        out[written++] = ch;
    }
    out[written] = '\0';
}

inline void trace_strlen_stall_checkpoint(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (!trace_strlen_stall_enabled() || guest_pc != 0x80516E94u ||
        services == nullptr || services->log == nullptr ||
        context == nullptr || memory == nullptr) {
        return;
    }

    const std::uint32_t length = context->gpr[3];
    if (length < trace_strlen_stall_min_len()) {
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    if (index >= 128u) {
        return;
    }

    const std::uint32_t current = context->gpr[4];
    const std::uint32_t start = current - length;
    char start_preview[64]{};
    char current_preview[64]{};
    bool start_mapped = false;
    bool current_mapped = false;
    std::uint32_t start_null = 0xFFFFFFFFu;
    std::uint32_t current_null = 0xFFFFFFFFu;
    trace_strlen_preview(
        memory,
        start,
        start_preview,
        sizeof(start_preview),
        start_mapped,
        start_null);
    trace_strlen_preview(
        memory,
        current,
        current_preview,
        sizeof(current_preview),
        current_mapped,
        current_null);

    char message[512]{};
    std::snprintf(
        message,
        sizeof(message),
        "[strlen-stall] #%u len=%u start=0x%08X current=0x%08X"
        " lr=0x%08X r1=0x%08X startMapped=%u currentMapped=%u"
        " startNull48=%d currentNull48=%d start='%s' current='%s'",
        index + 1u,
        length,
        start,
        current,
        context->lr,
        context->gpr[1],
        start_mapped ? 1u : 0u,
        current_mapped ? 1u : 0u,
        start_null == 0xFFFFFFFFu ? -1 : static_cast<int>(start_null),
        current_null == 0xFFFFFFFFu ? -1 : static_cast<int>(current_null),
        start_preview,
        current_preview);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline bool trace_movie_read_u32_fast(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t& out) {
    const std::byte* direct = resolve_guest_fast(memory, address, 4u);
    if (direct == nullptr) {
        return false;
    }
    out = (std::to_integer<std::uint32_t>(direct[0]) << 24u) |
          (std::to_integer<std::uint32_t>(direct[1]) << 16u) |
          (std::to_integer<std::uint32_t>(direct[2]) <<  8u) |
           std::to_integer<std::uint32_t>(direct[3]);
    return true;
}

inline bool trace_cinema_frame_state_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_TRACE_CINEMA_FRAME_STATE") == 0 &&
            length > 1) {
            return value[0] != '0';
        }
        length = 0;
        value[0] = '\0';
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_CINEMA_FRAME") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

inline bool trace_cinema_frame_function(std::uint32_t function_address) {
    switch (function_address) {
    case 0x8034E30Cu:
    case 0x8034E38Cu:
    case 0x8034E400u:
    case 0x8034E410u:
    case 0x8034E458u:
    case 0x8034E470u:
    case 0x8034E490u:
    case 0x8034E524u:
    case 0x8034E538u:
    case 0x8034E5CCu:
    case 0x8034E658u:
    case 0x8034E674u:
    case 0x8034E69Cu:
    case 0x8034E708u:
    case 0x8034E74Cu:
    case 0x8034E75Cu:
    case 0x8034E7A8u:
    case 0x8034E7B0u:
    case 0x8034E800u:
    case 0x8034E804u:
    case 0x8034E85Cu:
    case 0x8034E878u:
    case 0x8034E8F8u:
    case 0x8034E914u:
    case 0x8034E948u:
    case 0x8034E970u:
    case 0x8034EA34u:
    case 0x8034EA7Cu:
    case 0x8034EAC8u:
    case 0x8034EAECu:
    case 0x8034EB14u:
    case 0x8034EB50u:
    case 0x8034EB60u:
    case 0x8034EBBCu:
    case 0x8034EBD0u:
        return true;
    default:
        return false;
    }
}

inline const char* trace_cinema_frame_nerve_label(
    std::uint32_t r13,
    std::uint32_t nerve) {
    if (nerve == 0u) {
        return "null";
    }
    if (r13 == 0u) {
        return "unknown";
    }
    switch (nerve - r13) {
    case 0xFFFFD090u: return "Screen";
    case 0xFFFFD094u: return "Frame";
    case 0xFFFFD098u: return "Blank";
    case 0xFFFFD09Cu: return "ScreenToFrame";
    case 0xFFFFD0A0u: return "FrameToBlank";
    case 0xFFFFD0A4u: return "BlankToFrame";
    case 0xFFFFD0A8u: return "FrameToScreen";
    default: return "unknown";
    }
}

inline void trace_cinema_frame_state(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address,
    const char* label) {
    if (!trace_cinema_frame_state_enabled() ||
        !trace_cinema_frame_function(function_address) ||
        services == nullptr || services->log == nullptr ||
        context == nullptr || memory == nullptr) {
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t trace_index = count.fetch_add(1);
    if (trace_index >= 8192u) {
        return;
    }

    const std::uint32_t self = context->gpr[3];
    std::uint8_t flags[5]{};
    for (std::uint32_t offset = 0; offset < 5u; ++offset) {
        trace_movie_read_u8_fast(memory, self + 0x1Cu + offset, flags[offset]);
    }

    std::uint32_t spine = 0;
    std::uint32_t current_nerve = 0;
    std::uint32_t next_nerve = 0;
    std::uint32_t step = 0;
    trace_movie_read_u32_fast(memory, self + 0x10u, spine);
    if (spine != 0u) {
        trace_movie_read_u32_fast(memory, spine + 0x04u, current_nerve);
        trace_movie_read_u32_fast(memory, spine + 0x08u, next_nerve);
        trace_movie_read_u32_fast(memory, spine + 0x0Cu, step);
    }

    std::uint32_t fields[7]{};
    for (std::uint32_t field_index = 0; field_index < 7u; ++field_index) {
        trace_movie_read_u32_fast(
            memory,
            self + 0x20u + field_index * 4u,
            fields[field_index]);
    }

    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "[cinema-frame-state] entry=%s pc=0x%08X lr=0x%08X"
        " self=0x%08X flags[1c..20]=%u,%u,%u,%u,%u"
        " spine=0x%08X cur=0x%08X(%s) next=0x%08X(%s) step=%u"
        " f20=%u f24=%u f28=%u f2c=0x%08X f30=0x%08X f34=0x%08X f38=0x%08X"
        " r4=0x%08X r5=0x%08X r13=0x%08X",
        label != nullptr ? label : "CinemaFrame",
        function_address,
        context->lr,
        self,
        static_cast<unsigned>(flags[0]),
        static_cast<unsigned>(flags[1]),
        static_cast<unsigned>(flags[2]),
        static_cast<unsigned>(flags[3]),
        static_cast<unsigned>(flags[4]),
        spine,
        current_nerve,
        trace_cinema_frame_nerve_label(context->gpr[13], current_nerve),
        next_nerve,
        trace_cinema_frame_nerve_label(context->gpr[13], next_nerve),
        step,
        fields[0],
        fields[1],
        fields[2],
        fields[3],
        fields[4],
        fields[5],
        fields[6],
        context->gpr[4],
        context->gpr[5],
        context->gpr[13]);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline bool trace_scenario_opening_state_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_TRACE_SCENARIO_OPENING_STATE") == 0 &&
            length > 1) {
            return value[0] != '0';
        }
        length = 0;
        value[0] = '\0';
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_SCENARIO_OPENING") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

inline bool trace_scenario_opening_function(std::uint32_t function_address) {
    switch (function_address) {
    case 0x8033EE8Cu:
    case 0x8033F040u:
    case 0x803402D0u:
    case 0x80340328u:
    case 0x80340388u:
    case 0x80340400u:
    case 0x80340454u:
    case 0x8034048Cu:
    case 0x803404B0u:
    case 0x80340530u:
    case 0x803405CCu:
    case 0x80340650u:
    case 0x803409BCu:
    case 0x80398C94u:
    case 0x80398D3Cu:
    case 0x80398E00u:
    case 0x80399058u:
    case 0x80399144u:
    case 0x80399280u:
    case 0x80399390u:
    case 0x800317A0u:
    case 0x80394D2Cu:
    case 0x8039AFA8u:
    case 0x8039C284u:
    case 0x8039C548u:
    case 0x8039C5FCu:
    case 0x8039C730u:
    case 0x8049DADCu:
    case 0x803A29B0u:
    case 0x803FDCC4u:
    case 0x803FDDBCu:
        return true;
    default:
        return false;
    }
}

inline const char* trace_scenario_nerve_label(
    std::uint32_t r13,
    std::uint32_t nerve) {
    if (nerve == 0u) {
        return "null";
    }
    if (r13 == 0u) {
        return "unknown";
    }
    switch (nerve - r13) {
    case 0xFFFFCF28u: return "GameScene nerve CF28";
    case 0xFFFFCF2Cu: return "GameScene nerve CF2C";
    case 0xFFFFCF30u: return "GameScene::Action";
    case 0xFFFFCF34u: return "GameScene nerve CF34";
    case 0xFFFFCF38u: return "GameScene nerve CF38";
    case 0xFFFFCF3Cu: return "GameScene nerve CF3C";
    case 0xFFFFCF40u: return "GameScene nerve CF40";
    case 0xFFFFCF44u: return "GameScene nerve CF44";
    case 0xFFFFCF48u: return "GameScene nerve CF48";
    case 0xFFFFCF4Cu: return "GameScene nerve CF4C";
    case 0xFFFFCF50u: return "GameScene nerve CF50";
    case 0xFFFFCF54u: return "GameScene nerve CF54";
    case 0xFFFFCF58u: return "GameScene nerve CF58";
    case 0xFFFFCF70u: return "ScenarioCamera nerve CF70";
    case 0xFFFFCF74u: return "ScenarioCamera nerve CF74";
    case 0xFFFFCF78u: return "ScenarioCamera nerve CF78";
    case 0xFFFFCF7Cu: return "ScenarioCamera nerve CF7C";
    case 0xFFFFCF80u: return "ScenarioCamera wait-manager-predicate";
    case 0xFFFFCF84u: return "ScenarioCamera save-sequence";
    case 0xFFFFD7A8u: return "SceneManager D7A8";
    case 0xFFFFD7ACu: return "SceneManager D7AC";
    case 0xFFFFD7B0u: return "SceneManager wait-submanager";
    case 0xFFFFD7B4u: return "SceneManager D7B4";
    case 0xFFFFD7B8u: return "SceneManager target D7B8";
    case 0xFFFFD7F0u: return "SubScene D7F0";
    case 0xFFFFD7F4u: return "SubScene D7F4";
    case 0xFFFFD7F8u: return "SubScene D7F8";
    case 0xFFFFD7FCu: return "SubScene D7FC";
    case 0xFFFFD800u: return "SubScene target D800";
    case 0xFFFFD804u: return "SubScene D804";
    case 0xFFFFD808u: return "SubScene D808";
    default: return "unknown";
    }
}

inline void trace_scenario_spine_state(
    GuestMemoryV1* memory,
    std::uint32_t executor,
    std::uint32_t& spine,
    std::uint32_t& current_nerve,
    std::uint32_t& next_nerve,
    std::uint32_t& step) {
    spine = 0;
    current_nerve = 0;
    next_nerve = 0;
    step = 0;
    if (executor == 0u) {
        return;
    }
    trace_movie_read_u32_fast(memory, executor + 0x04u, spine);
    if (spine == 0u) {
        return;
    }
    trace_movie_read_u32_fast(memory, spine + 0x04u, current_nerve);
    trace_movie_read_u32_fast(memory, spine + 0x08u, next_nerve);
    trace_movie_read_u32_fast(memory, spine + 0x0Cu, step);
}

inline void trace_scenario_opening_state(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address,
    const char* label) {
    if (!trace_scenario_opening_state_enabled() ||
        !trace_scenario_opening_function(function_address) ||
        services == nullptr || services->log == nullptr ||
        context == nullptr || memory == nullptr) {
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t trace_index = count.fetch_add(1);
    if (trace_index >= 8192u) {
        return;
    }

    const bool manager_wait_entry = function_address == 0x803409BCu;
    std::uint32_t manager_wait_owner = 0;
    if (manager_wait_entry) {
        trace_movie_read_u32_fast(memory, context->gpr[4], manager_wait_owner);
    }
    const std::uint32_t self =
        manager_wait_entry && manager_wait_owner != 0u
            ? manager_wait_owner
            : context->gpr[3];
    std::uint32_t spine = 0;
    std::uint32_t current_nerve = 0;
    std::uint32_t next_nerve = 0;
    std::uint32_t step = 0;
    trace_scenario_spine_state(
        memory, self, spine, current_nerve, next_nerve, step);

    std::uint32_t fields[10]{};
    constexpr std::uint32_t offsets[10] = {
        0x04u, 0x08u, 0x0Cu, 0x10u, 0x14u,
        0x18u, 0x1Cu, 0x20u, 0x24u, 0x38u,
    };
    for (std::uint32_t field_index = 0; field_index < 10u; ++field_index) {
        trace_movie_read_u32_fast(
            memory,
            self + offsets[field_index],
            fields[field_index]);
    }

    std::uint32_t scene_scenario = fields[5];
    std::uint32_t scene_scenario_spine = 0;
    std::uint32_t scene_scenario_current = 0;
    std::uint32_t scene_scenario_next = 0;
    std::uint32_t scene_scenario_step = 0;
    if (function_address == 0x8033EE8Cu || function_address == 0x8033F040u) {
        trace_scenario_spine_state(
            memory,
            scene_scenario,
            scene_scenario_spine,
            scene_scenario_current,
            scene_scenario_next,
            scene_scenario_step);
    }

    std::uint32_t scene_manager = 0;
    trace_movie_read_u32_fast(memory, context->gpr[13] + 0xFFFFC588u, scene_manager);
    std::uint32_t manager_spine = 0;
    std::uint32_t manager_current = 0;
    std::uint32_t manager_next = 0;
    std::uint32_t manager_step = 0;
    trace_scenario_spine_state(
        memory, scene_manager, manager_spine, manager_current, manager_next,
        manager_step);
    const std::uint32_t manager_selected =
        manager_next != 0u ? manager_next : manager_current;
    const std::uint32_t manager_target =
        context->gpr[13] + 0xFFFFD7B8u;
    const std::uint32_t manager_matches =
        manager_selected == manager_target ? 1u : 0u;
    std::uint32_t manager_sub = 0;
    trace_movie_read_u32_fast(memory, scene_manager + 0x28u, manager_sub);
    std::uint32_t manager_sub_spine = 0;
    std::uint32_t manager_sub_current = 0;
    std::uint32_t manager_sub_next = 0;
    std::uint32_t manager_sub_step = 0;
    trace_scenario_spine_state(
        memory, manager_sub, manager_sub_spine, manager_sub_current,
        manager_sub_next, manager_sub_step);
    const std::uint32_t manager_sub_selected =
        manager_sub_next != 0u ? manager_sub_next : manager_sub_current;
    const std::uint32_t manager_sub_target =
        context->gpr[13] + 0xFFFFD800u;
    const std::uint32_t manager_sub_matches =
        manager_sub_selected == manager_sub_target ? 1u : 0u;

    std::uint32_t manager_scene_resource = 0;
    std::uint32_t async_manager = 0;
    std::uint32_t async_worker0 = 0;
    std::uint32_t async_worker1 = 0;
    std::uint32_t async_queue = 0;
    std::uint32_t async_pending_count = 0;
    std::uint32_t async_pending0 = 0;
    std::uint32_t async_pending0_desc = 0;
    std::uint32_t async_pending0_data = 0;
    std::uint32_t async_pending0_key = 0;
    std::uint8_t async_pending0_status = 0;
    std::uint32_t async_worker0_queue_depth = 0;
    std::uint32_t async_worker1_queue_depth = 0;
    std::uint8_t async_worker0_active = 0;
    std::uint8_t async_worker1_active = 0;
    std::uint32_t async_worker0_current = 0;
    std::uint32_t async_worker1_current = 0;
    std::uint8_t async_worker0_current_status = 0;
    std::uint8_t async_worker1_current_status = 0;
    trace_movie_read_u32_fast(
        memory, scene_manager + 0x20u, manager_scene_resource);
    trace_movie_read_u32_fast(
        memory, manager_scene_resource + 0x28u, async_manager);
    trace_movie_read_u32_fast(memory, async_manager + 0x00u, async_worker0);
    trace_movie_read_u32_fast(memory, async_manager + 0x04u, async_worker1);
    trace_movie_read_u32_fast(memory, async_manager + 0x08u, async_queue);
    trace_movie_read_u32_fast(
        memory, async_manager + 0x40Cu, async_pending_count);
    trace_movie_read_u32_fast(memory, async_manager + 0x0Cu, async_pending0);
    trace_movie_read_u32_fast(
        memory, async_pending0 + 0x00u, async_pending0_desc);
    trace_movie_read_u32_fast(
        memory, async_pending0 + 0x04u, async_pending0_data);
    trace_movie_read_u32_fast(
        memory, async_pending0 + 0x08u, async_pending0_key);
    trace_movie_read_u8_fast(
        memory, async_pending0 + 0x0Cu, async_pending0_status);
    trace_movie_read_u32_fast(
        memory, async_worker0 + 0x28u, async_worker0_queue_depth);
    trace_movie_read_u32_fast(
        memory, async_worker1 + 0x28u, async_worker1_queue_depth);
    trace_movie_read_u8_fast(
        memory, async_worker0 + 0x3Cu, async_worker0_active);
    trace_movie_read_u8_fast(
        memory, async_worker1 + 0x3Cu, async_worker1_active);
    trace_movie_read_u32_fast(
        memory, async_worker0 + 0x40u, async_worker0_current);
    trace_movie_read_u32_fast(
        memory, async_worker1 + 0x40u, async_worker1_current);
    trace_movie_read_u8_fast(
        memory, async_worker0_current + 0x0Cu, async_worker0_current_status);
    trace_movie_read_u8_fast(
        memory, async_worker1_current + 0x0Cu, async_worker1_current_status);

    char message[2400]{};
    std::snprintf(
        message,
        sizeof(message),
        "[scenario-opening-state] entry=%s pc=0x%08X lr=0x%08X"
        " self=0x%08X spine=0x%08X cur=0x%08X(%s)"
        " next=0x%08X(%s) step=%d"
        " f04=0x%08X f08=0x%08X f0c=0x%08X f10=0x%08X"
        " f14=0x%08X f18=0x%08X f1c=0x%08X f20=0x%08X"
        " f24=0x%08X f38=0x%08X"
        " sceneScenario=0x%08X sceneSpine=0x%08X sceneCur=0x%08X(%s)"
        " sceneNext=0x%08X(%s) sceneStep=%d"
        " manager=0x%08X managerSpine=0x%08X managerCur=0x%08X(%s)"
        " managerNext=0x%08X(%s) managerStep=%d"
        " managerSelected=0x%08X(%s) managerTarget=0x%08X(%s)"
        " managerMatches=%u managerOwner=0x%08X"
        " managerSub=0x%08X subSpine=0x%08X subCur=0x%08X(%s)"
        " subNext=0x%08X(%s) subStep=%d subSelected=0x%08X(%s)"
        " subTarget=0x%08X(%s) subMatches=%u"
        " asyncScene=0x%08X asyncMgr=0x%08X asyncWorkers=0x%08X/0x%08X"
        " asyncQueue=0x%08X asyncPendingCount=%u"
        " asyncPending0=0x%08X(desc=0x%08X data=0x%08X key=0x%08X status=%u)"
        " asyncW0(depth=%u active=%u current=0x%08X status=%u)"
        " asyncW1(depth=%u active=%u current=0x%08X status=%u)"
        " r4=0x%08X r5=0x%08X r13=0x%08X",
        label != nullptr ? label : "ScenarioOpening",
        function_address,
        context->lr,
        self,
        spine,
        current_nerve,
        trace_scenario_nerve_label(context->gpr[13], current_nerve),
        next_nerve,
        trace_scenario_nerve_label(context->gpr[13], next_nerve),
        static_cast<std::int32_t>(step),
        fields[0],
        fields[1],
        fields[2],
        fields[3],
        fields[4],
        fields[5],
        fields[6],
        fields[7],
        fields[8],
        fields[9],
        scene_scenario,
        scene_scenario_spine,
        scene_scenario_current,
        trace_scenario_nerve_label(context->gpr[13], scene_scenario_current),
        scene_scenario_next,
        trace_scenario_nerve_label(context->gpr[13], scene_scenario_next),
        static_cast<std::int32_t>(scene_scenario_step),
        scene_manager,
        manager_spine,
        manager_current,
        trace_scenario_nerve_label(context->gpr[13], manager_current),
        manager_next,
        trace_scenario_nerve_label(context->gpr[13], manager_next),
        static_cast<std::int32_t>(manager_step),
        manager_selected,
        trace_scenario_nerve_label(context->gpr[13], manager_selected),
        manager_target,
        trace_scenario_nerve_label(context->gpr[13], manager_target),
        manager_matches,
        manager_wait_owner,
        manager_sub,
        manager_sub_spine,
        manager_sub_current,
        trace_scenario_nerve_label(context->gpr[13], manager_sub_current),
        manager_sub_next,
        trace_scenario_nerve_label(context->gpr[13], manager_sub_next),
        static_cast<std::int32_t>(manager_sub_step),
        manager_sub_selected,
        trace_scenario_nerve_label(context->gpr[13], manager_sub_selected),
        manager_sub_target,
        trace_scenario_nerve_label(context->gpr[13], manager_sub_target),
        manager_sub_matches,
        manager_scene_resource,
        async_manager,
        async_worker0,
        async_worker1,
        async_queue,
        async_pending_count,
        async_pending0,
        async_pending0_desc,
        async_pending0_data,
        async_pending0_key,
        async_pending0_status,
        async_worker0_queue_depth,
        async_worker0_active,
        async_worker0_current,
        async_worker0_current_status,
        async_worker1_queue_depth,
        async_worker1_active,
        async_worker1_current,
        async_worker1_current_status,
        context->gpr[4],
        context->gpr[5],
        context->gpr[13]);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline void trace_async_append_string(
    char* output,
    std::size_t output_size,
    std::size_t& offset,
    GuestMemoryV1* memory,
    const char* label,
    std::uint32_t address) {
    if (output == nullptr || offset >= output_size || label == nullptr) {
        return;
    }
    char text[64]{};
    if (trace_audio_read_string_fast(memory, address, text, sizeof(text)) == 0) {
        return;
    }
    const int written = std::snprintf(
        output + offset,
        output_size - offset,
        " %s=\"%s\"",
        label,
        text);
    if (written > 0) {
        offset += static_cast<std::size_t>(written);
    }
}

inline void trace_async_append_manager(
    char* output,
    std::size_t output_size,
    std::size_t& offset,
    GuestMemoryV1* memory,
    const char* label,
    std::uint32_t async_manager) {
    if (output == nullptr || offset >= output_size || label == nullptr ||
        async_manager == 0u) {
        return;
    }

    std::uint32_t pending_count = 0;
    std::uint32_t pending0 = 0;
    std::uint32_t pending1 = 0;
    std::uint32_t pending0_desc = 0;
    std::uint32_t pending0_data = 0;
    std::uint32_t pending0_key = 0;
    std::uint8_t pending0_status = 0;
    std::uint32_t pending1_desc = 0;
    std::uint32_t pending1_data = 0;
    std::uint32_t pending1_key = 0;
    std::uint8_t pending1_status = 0;
    trace_movie_read_u32_fast(memory, async_manager + 0x40Cu, pending_count);
    trace_movie_read_u32_fast(memory, async_manager + 0x0Cu, pending0);
    trace_movie_read_u32_fast(memory, async_manager + 0x10u, pending1);
    trace_movie_read_u32_fast(memory, pending0 + 0x00u, pending0_desc);
    trace_movie_read_u32_fast(memory, pending0 + 0x04u, pending0_data);
    trace_movie_read_u32_fast(memory, pending0 + 0x08u, pending0_key);
    trace_movie_read_u8_fast(memory, pending0 + 0x0Cu, pending0_status);
    trace_movie_read_u32_fast(memory, pending1 + 0x00u, pending1_desc);
    trace_movie_read_u32_fast(memory, pending1 + 0x04u, pending1_data);
    trace_movie_read_u32_fast(memory, pending1 + 0x08u, pending1_key);
    trace_movie_read_u8_fast(memory, pending1 + 0x0Cu, pending1_status);

    const int written = std::snprintf(
        output + offset,
        output_size - offset,
        " %s=0x%08X count=%u p0=0x%08X(desc=0x%08X data=0x%08X key=0x%08X st=%u)"
        " p1=0x%08X(desc=0x%08X data=0x%08X key=0x%08X st=%u)",
        label,
        async_manager,
        pending_count,
        pending0,
        pending0_desc,
        pending0_data,
        pending0_key,
        pending0_status,
        pending1,
        pending1_desc,
        pending1_data,
        pending1_key,
        pending1_status);
    if (written > 0) {
        offset += static_cast<std::size_t>(written);
    }

    trace_async_append_string(
        output, output_size, offset, memory, "p0keyStr", pending0_key);
    trace_async_append_string(
        output, output_size, offset, memory, "p1keyStr", pending1_key);
}

inline void trace_async_executor_state(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address,
    const char* label) {
    if (!trace_scenario_opening_state_enabled() ||
        services == nullptr || services->log == nullptr ||
        context == nullptr || memory == nullptr) {
        return;
    }

    switch (function_address) {
    case 0x80398C94u:
    case 0x80398D3Cu:
    case 0x80398E00u:
    case 0x80399058u:
    case 0x80399144u:
    case 0x80399280u:
    case 0x80399390u:
    case 0x8039C548u:
    case 0x8039C730u:
    case 0x803A29B0u:
    case 0x803FDCC4u:
    case 0x803FDDBCu:
        break;
    default:
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t trace_index = count.fetch_add(1);
    if (trace_index >= 8192u) {
        return;
    }

    std::uint32_t scene_manager = 0;
    std::uint32_t manager_scene_resource = 0;
    std::uint32_t scene_async_manager = 0;
    trace_movie_read_u32_fast(memory, context->gpr[13] + 0xFFFFC588u, scene_manager);
    trace_movie_read_u32_fast(
        memory, scene_manager + 0x20u, manager_scene_resource);
    trace_movie_read_u32_fast(
        memory, manager_scene_resource + 0x28u, scene_async_manager);

    char extra[1800]{};
    std::size_t extra_offset = 0;
    trace_async_append_string(
        extra, sizeof(extra), extra_offset, memory, "r3str", context->gpr[3]);
    trace_async_append_string(
        extra, sizeof(extra), extra_offset, memory, "r4str", context->gpr[4]);
    trace_async_append_string(
        extra, sizeof(extra), extra_offset, memory, "r5str", context->gpr[5]);
    trace_async_append_string(
        extra, sizeof(extra), extra_offset, memory, "r6str", context->gpr[6]);
    trace_async_append_string(
        extra, sizeof(extra), extra_offset, memory, "r7str", context->gpr[7]);
    trace_async_append_manager(
        extra,
        sizeof(extra),
        extra_offset,
        memory,
        "sceneAsync",
        scene_async_manager);
    if (context->gpr[3] != scene_async_manager) {
        trace_async_append_manager(
            extra,
            sizeof(extra),
            extra_offset,
            memory,
            "r3Async",
            context->gpr[3]);
    }

    std::uint32_t request_desc = 0;
    std::uint32_t request_data = 0;
    std::uint32_t request_key = 0;
    std::uint8_t request_status = 0;
    if (function_address == 0x80398D3Cu) {
        trace_movie_read_u32_fast(memory, context->gpr[3] + 0x00u, request_desc);
        trace_movie_read_u32_fast(memory, context->gpr[3] + 0x04u, request_data);
        trace_movie_read_u32_fast(memory, context->gpr[3] + 0x08u, request_key);
        trace_movie_read_u8_fast(memory, context->gpr[3] + 0x0Cu, request_status);
        trace_async_append_string(
            extra, sizeof(extra), extra_offset, memory, "reqKeyStr", request_key);
    }

    char message[2400]{};
    std::snprintf(
        message,
        sizeof(message),
        "[async-exec-state] #%u entry=%s pc=0x%08X lr=0x%08X"
        " r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X"
        " req(desc=0x%08X data=0x%08X key=0x%08X st=%u)%s",
        trace_index,
        label != nullptr ? label : "async",
        function_address,
        context->lr,
        context->gpr[3],
        context->gpr[4],
        context->gpr[5],
        context->gpr[6],
        context->gpr[7],
        request_desc,
        request_data,
        request_key,
        request_status,
        extra);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline bool trace_movie_wrapper_function(std::uint32_t function_address) {
    switch (function_address) {
    case 0x8038CFB8u:
    case 0x8038D1CCu:
    case 0x8038D264u:
    case 0x8038D2ACu:
    case 0x8038D2C8u:
    case 0x8038D2E4u:
    case 0x8038D34Cu:
    case 0x8038D420u:
    case 0x8038D494u:
    case 0x8038D56Cu:
    case 0x8038D6FCu:
    case 0x8038D718u:
    case 0x8038D7B8u:
    case 0x8038D7FCu:
    case 0x8038D80Cu:
    case 0x8038D868u:
    case 0x8038D8C8u:
    case 0x8038D924u:
    case 0x8038D980u:
    case 0x8038D9F4u:
    case 0x8038DA44u:
    case 0x8038DA98u:
    case 0x8038DAF4u:
    case 0x8038DB74u:
    case 0x8038DC44u:
    case 0x8038DCC0u:
        return true;
    default:
        return false;
    }
}

inline bool trace_movie_player_simple_function(std::uint32_t function_address) {
    switch (function_address) {
    case 0x803711FCu:
    case 0x80371240u:
    case 0x803712D4u:
    case 0x8037132Cu:
    case 0x803715D8u:
    case 0x8037163Cu:
    case 0x803716B4u:
    case 0x803716D8u:
    case 0x8037172Cu:
    case 0x8037175Cu:
        return true;
    default:
        return false;
    }
}

inline bool trace_thp_wrapper_state_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_THP_WRAPPER") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

inline std::uint32_t trace_thp_wrapper_state_limit() {
    static const std::uint32_t limit = [] {
        char value[32]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_TRACE_THP_WRAPPER_LIMIT") != 0 ||
            length <= 1) {
            return 8192u;
        }
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (end == value || parsed == 0ul) {
            return 8192u;
        }
        return static_cast<std::uint32_t>(
            std::min<unsigned long>(parsed, 100000ul));
    }();
    return limit;
}

inline bool trace_movie_guest_pointer_plausible(std::uint32_t address) {
    return address >= 0x80000000u && address < 0x94000000u;
}

inline void trace_movie_append_wrapper_state(
    char* output,
    std::size_t output_size,
    std::size_t& offset,
    GuestMemoryV1* memory,
    std::uint32_t self) {
    if (output == nullptr || offset >= output_size ||
        memory == nullptr || self < 0x80000000u) {
        return;
    }

    std::uint8_t flag8 = 0;
    std::uint8_t flag9 = 0;
    std::uint8_t open = 0;
    std::uint8_t prefetch = 0;
    std::uint8_t audio_exist = 0;
    std::uint8_t tex_index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t num_frames = 0;
    std::uint32_t next_decode = 0;
    std::uint32_t read_index = 0;
    std::uint32_t read_size = 0;
    std::uint32_t total_read = 0;
    std::uint32_t current_offset = 0;
    std::uint32_t read_progress = 0;
    std::uint32_t dvd_offset = 0;
    std::uint32_t current_buffer = 0;
    std::uint8_t audio_state = 0;
    std::uint8_t loop = 0;
    std::uint8_t dvd_error = 0;
    std::uint32_t header_frames = 0;
    std::uint32_t header_first_frame_size = 0;
    std::uint32_t header_movie_offset = 0;
    std::uint32_t frame_components = 0;
    std::uint32_t read_ptr = 0;
    std::uint32_t read_frame = 0;
    std::uint32_t read_valid = 0;
    std::uint32_t y0 = 0;
    std::uint32_t u0 = 0;
    std::uint32_t v0 = 0;
    std::uint32_t f0 = 0;
    std::uint32_t y1 = 0;
    std::uint32_t u1 = 0;
    std::uint32_t v1 = 0;
    std::uint32_t f1 = 0;

    trace_movie_read_u8_fast(memory, self + 0x08u, flag8);
    trace_movie_read_u8_fast(memory, self + 0x09u, flag9);
    trace_movie_read_u32_fast(memory, self + 0x0Cu, dvd_offset);
    trace_movie_read_u32_fast(memory, self + 0x10u, current_buffer);
    trace_movie_read_u8_fast(memory, self + 0xB4u, open);
    trace_movie_read_u8_fast(memory, self + 0xB5u, prefetch);
    trace_movie_read_u8_fast(memory, self + 0xB6u, audio_state);
    trace_movie_read_u8_fast(memory, self + 0xB7u, loop);
    trace_movie_read_u8_fast(memory, self + 0xB8u, audio_exist);
    trace_movie_read_u8_fast(memory, self + 0xC0u, dvd_error);
    trace_movie_read_u8_fast(memory, self + 0x310u, tex_index);
    trace_movie_read_u32_fast(memory, self + 0x60u, header_frames);
    trace_movie_read_u32_fast(memory, self + 0x70u, header_first_frame_size);
    trace_movie_read_u32_fast(memory, self + 0x74u, header_movie_offset);
    trace_movie_read_u32_fast(memory, self + 0x80u, frame_components);
    trace_movie_read_u32_fast(memory, self + 0x64u, num_frames);
    trace_movie_read_u32_fast(memory, self + 0x94u, width);
    trace_movie_read_u32_fast(memory, self + 0x98u, height);
    trace_movie_read_u32_fast(memory, self + 0xBCu, current_offset);
    trace_movie_read_u32_fast(memory, self + 0xC4u, read_progress);
    trace_movie_read_u32_fast(memory, self + 0xC8u, next_decode);
    trace_movie_read_u32_fast(memory, self + 0xCCu, read_index);
    trace_movie_read_u32_fast(memory, self + 0xD0u, read_size);
    trace_movie_read_u32_fast(memory, self + 0xD4u, total_read);
    if (next_decode < 20u) {
        const std::uint32_t read_base = self + 0xE8u + next_decode * 12u;
        trace_movie_read_u32_fast(memory, read_base + 0x00u, read_ptr);
        trace_movie_read_u32_fast(memory, read_base + 0x04u, read_frame);
        trace_movie_read_u32_fast(memory, read_base + 0x08u, read_valid);
    }
    trace_movie_read_u32_fast(memory, self + 0x1D8u, y0);
    trace_movie_read_u32_fast(memory, self + 0x1DCu, u0);
    trace_movie_read_u32_fast(memory, self + 0x1E0u, v0);
    trace_movie_read_u32_fast(memory, self + 0x1E4u, f0);
    trace_movie_read_u32_fast(memory, self + 0x1E8u, y1);
    trace_movie_read_u32_fast(memory, self + 0x1ECu, u1);
    trace_movie_read_u32_fast(memory, self + 0x1F0u, v1);
    trace_movie_read_u32_fast(memory, self + 0x1F4u, f1);

    const int written = std::snprintf(
        output + offset,
        output_size - offset,
        " thp[self=0x%08X f8=%u f9=%u off=%u buf=0x%08X"
        " open=%u pre=%u aud=%u tex=%u"
        " audioState=%u loop=%u dvdErr=%u readProg=%u"
        " video=%ux%u frames=%u headerFrames=%u comps=%u"
        " curOff=%u firstSize=%u movieOff=%u next=%u read=%u size=%u total=%u"
        " rb=(%08X,f=%d,valid=%u)"
        " t0=(%08X,%08X,%08X,f=%d) t1=(%08X,%08X,%08X,f=%d)]",
        self,
        static_cast<unsigned>(flag8),
        static_cast<unsigned>(flag9),
        dvd_offset,
        current_buffer,
        static_cast<unsigned>(open),
        static_cast<unsigned>(prefetch),
        static_cast<unsigned>(audio_exist),
        static_cast<unsigned>(tex_index),
        static_cast<unsigned>(audio_state),
        static_cast<unsigned>(loop),
        static_cast<unsigned>(dvd_error),
        read_progress,
        width,
        height,
        num_frames,
        header_frames,
        frame_components,
        current_offset,
        header_first_frame_size,
        header_movie_offset,
        next_decode,
        read_index,
        read_size,
        total_read,
        read_ptr,
        static_cast<std::int32_t>(read_frame),
        read_valid,
        y0,
        u0,
        v0,
        static_cast<std::int32_t>(f0),
        y1,
        u1,
        v1,
        static_cast<std::int32_t>(f1));
    if (written > 0) {
        offset += std::min<std::size_t>(
            static_cast<std::size_t>(written),
            output_size - offset);
    }
}

inline void trace_movie_append_player_simple_state(
    char* output,
    std::size_t output_size,
    std::size_t& offset,
    GuestMemoryV1* memory,
    std::uint32_t self) {
    if (output == nullptr || offset >= output_size ||
        memory == nullptr || !trace_movie_guest_pointer_plausible(self)) {
        return;
    }

    std::uint32_t movie = 0;
    std::uint32_t heap = 0;
    std::uint32_t wrapper = 0;
    std::uint8_t flag44 = 0;
    std::uint8_t flag45 = 0;
    trace_movie_read_u32_fast(memory, self + 0x38u, movie);
    trace_movie_read_u32_fast(memory, self + 0x3Cu, heap);
    trace_movie_read_u32_fast(memory, self + 0x40u, wrapper);
    trace_movie_read_u8_fast(memory, self + 0x44u, flag44);
    trace_movie_read_u8_fast(memory, self + 0x45u, flag45);

    if (!trace_movie_guest_pointer_plausible(movie) &&
        !trace_movie_guest_pointer_plausible(wrapper)) {
        return;
    }

    std::uint32_t movie_name = 0;
    std::uint32_t video_width = 0;
    std::uint32_t video_height = 0;
    std::uint32_t video_type = 0;
    std::uint32_t buffer = 0;
    std::uint32_t current_frame = 0;
    std::uint8_t movie_flag18 = 0;
    std::uint8_t movie_flag19 = 0;
    std::uint32_t frame_rate_default = 0;
    std::uint32_t movie_step = 0;
    std::uint32_t movie_counter = 0;
    char movie_name_text[96]{};
    if (trace_movie_guest_pointer_plausible(movie)) {
        trace_movie_read_u32_fast(memory, movie + 0x00u, movie_name);
        trace_movie_read_u32_fast(memory, movie + 0x04u, video_width);
        trace_movie_read_u32_fast(memory, movie + 0x08u, video_height);
        trace_movie_read_u32_fast(memory, movie + 0x0Cu, video_type);
        trace_movie_read_u32_fast(memory, movie + 0x10u, buffer);
        trace_movie_read_u32_fast(memory, movie + 0x14u, current_frame);
        trace_movie_read_u8_fast(memory, movie + 0x18u, movie_flag18);
        trace_movie_read_u8_fast(memory, movie + 0x19u, movie_flag19);
        trace_movie_read_u32_fast(memory, movie + 0x1Cu, frame_rate_default);
        trace_movie_read_u32_fast(memory, movie + 0x20u, movie_step);
        trace_movie_read_u32_fast(memory, movie + 0x24u, movie_counter);
        trace_audio_read_string_fast(
            memory, movie_name, movie_name_text, sizeof(movie_name_text));
    }

    const int written = std::snprintf(
        output + offset,
        output_size - offset,
        " mps[self=0x%08X movie=0x%08X heap=0x%08X wrapper=0x%08X"
        " flags=%u/%u name=0x%08X \"%s\" video=%ux%u type=%u"
        " buffer=0x%08X current=%d movieFlags=%u/%u rateDefault=%d"
        " step=%d counter=%u]",
        self,
        movie,
        heap,
        wrapper,
        static_cast<unsigned>(flag44),
        static_cast<unsigned>(flag45),
        movie_name,
        movie_name_text,
        video_width,
        video_height,
        video_type,
        buffer,
        static_cast<std::int32_t>(current_frame),
        static_cast<unsigned>(movie_flag18),
        static_cast<unsigned>(movie_flag19),
        static_cast<std::int32_t>(frame_rate_default),
        static_cast<std::int32_t>(movie_step),
        movie_counter);
    if (written > 0) {
        offset += std::min<std::size_t>(
            static_cast<std::size_t>(written),
            output_size - offset);
    }

    if (trace_movie_guest_pointer_plausible(wrapper)) {
        trace_movie_append_wrapper_state(
            output, output_size, offset, memory, wrapper);
    }
}

inline void trace_thp_wrapper_state(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address,
    const char* label) {
    if (!trace_thp_wrapper_state_enabled() ||
        !trace_movie_wrapper_function(function_address) ||
        services == nullptr || services->log == nullptr ||
        context == nullptr || memory == nullptr) {
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    if (index >= trace_thp_wrapper_state_limit()) {
        return;
    }

    char state_text[768]{};
    std::size_t state_offset = 0;
    trace_movie_append_wrapper_state(
        state_text,
        sizeof(state_text),
        state_offset,
        memory,
        context->gpr[3]);

    char message[1152]{};
    std::snprintf(
        message,
        sizeof(message),
        "[thp-wrapper] #%u entry=%s pc=0x%08X lr=0x%08X"
        " r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X%s",
        index,
        label != nullptr ? label : "THPSimplePlayerWrapper",
        function_address,
        context->lr,
        context->gpr[3],
        context->gpr[4],
        context->gpr[5],
        context->gpr[6],
        context->gpr[7],
        state_text);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline void trace_thp_video_decode_result(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t call_site,
    std::uint32_t wrapper_self) {
    if (!trace_thp_wrapper_state_enabled() ||
        services == nullptr || services->log == nullptr ||
        context == nullptr || memory == nullptr ||
        !trace_movie_guest_pointer_plausible(wrapper_self)) {
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    if (index >= trace_thp_wrapper_state_limit()) {
        return;
    }

    std::uint32_t components = 0;
    std::uint32_t next_decode = 0;
    std::uint32_t read_ptr = 0;
    std::uint32_t read_frame = 0;
    std::uint32_t read_valid = 0;
    std::uint32_t init_flag = 0;
    std::uint32_t frame_header = 0;
    std::uint32_t frame_sample_hash = 0;
    std::uint8_t frame_sample_min = 0;
    std::uint8_t frame_sample_max = 0;

    trace_movie_read_u32_fast(memory, wrapper_self + 0x80u, components);
    trace_movie_read_u32_fast(memory, wrapper_self + 0xC8u, next_decode);
    if (next_decode < 20u) {
        const std::uint32_t read_base = wrapper_self + 0xE8u + next_decode * 12u;
        trace_movie_read_u32_fast(memory, read_base + 0x00u, read_ptr);
        trace_movie_read_u32_fast(memory, read_base + 0x04u, read_frame);
        trace_movie_read_u32_fast(memory, read_base + 0x08u, read_valid);
    }
    trace_movie_read_u32_fast(memory, context->gpr[13] + 0xFFFFDC80u, init_flag);

    const std::uint32_t frame_payload =
        read_ptr != 0u ? read_ptr + 8u + components * 4u : 0u;
    if (trace_movie_guest_pointer_plausible(frame_payload)) {
        trace_movie_read_u32_fast(memory, frame_payload, frame_header);
        trace_movie_pointer_sample(
            memory,
            frame_payload,
            frame_sample_hash,
            frame_sample_min,
            frame_sample_max);
    }

    char state_text[768]{};
    std::size_t state_offset = 0;
    trace_movie_append_wrapper_state(
        state_text,
        sizeof(state_text),
        state_offset,
        memory,
        wrapper_self);

    char message[1280]{};
    std::snprintf(
        message,
        sizeof(message),
        "[thp-video-decode] #%u call=0x%08X ret=%d hid2=0x%08X"
        " init=%u self=0x%08X comps=%u next=%u rb=(%08X,f=%d,valid=%u)"
        " payload=0x%08X head=%08X sample=%08X/%02X-%02X%s",
        index,
        call_site,
        static_cast<std::int32_t>(context->gpr[3]),
        context->hid2,
        init_flag,
        wrapper_self,
        components,
        next_decode,
        read_ptr,
        static_cast<std::int32_t>(read_frame),
        read_valid,
        frame_payload,
        frame_header,
        frame_sample_hash,
        frame_sample_min,
        frame_sample_max,
        state_text);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline void trace_movie_player_simple_state(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address,
    const char* label) {
    if (!trace_thp_wrapper_state_enabled() ||
        !trace_movie_player_simple_function(function_address) ||
        services == nullptr || services->log == nullptr ||
        context == nullptr || memory == nullptr) {
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    if (index >= trace_thp_wrapper_state_limit()) {
        return;
    }

    char state_text[1600]{};
    std::size_t state_offset = 0;
    trace_movie_append_player_simple_state(
        state_text,
        sizeof(state_text),
        state_offset,
        memory,
        context->gpr[3]);
    if (state_offset == 0) {
        return;
    }

    char message[2048]{};
    std::snprintf(
        message,
        sizeof(message),
        "[movie-player-simple] #%u entry=%s pc=0x%08X lr=0x%08X"
        " r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X%s",
        index,
        label != nullptr ? label : "MoviePlayerSimple",
        function_address,
        context->lr,
        context->gpr[3],
        context->gpr[4],
        context->gpr[5],
        context->gpr[6],
        context->gpr[7],
        state_text);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline void trace_resource_loader_state(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address,
    const char* label) {
    if (!trace_scenario_opening_state_enabled() ||
        services == nullptr || services->log == nullptr ||
        context == nullptr || memory == nullptr) {
        return;
    }
    switch (function_address) {
    case 0x800317A0u:
    case 0x80394D2Cu:
    case 0x8039AFA8u:
    case 0x8039C284u:
    case 0x8039C5FCu:
    case 0x8003379Cu:
    case 0x8040E048u:
    case 0x8040EDA8u:
    case 0x8040EE7Cu:
    case 0x80412EF8u:
    case 0x80413124u:
    case 0x804140D4u:
    case 0x80414178u:
    case 0x8041421Cu:
    case 0x804142D4u:
    case 0x80414628u:
    case 0x80414700u:
    case 0x80414740u:
    case 0x804147D0u:
    case 0x80415454u:
    case 0x80415674u:
    case 0x80415880u:
    case 0x80415E84u:
    case 0x804AAEA0u:
    case 0x804BF844u:
    case 0x8049251Cu:
    case 0x80492644u:
    case 0x804926D0u:
    case 0x8049DADCu:
        break;
    default:
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    if (function_address == 0x8049DADCu) {
        if (index >= 768u && (index % 120u) != 0u) {
            return;
        }
    } else if (index >= 1024u && (index % 120u) != 0u) {
        return;
    }

    const auto read_u32_or_zero = [memory](std::uint32_t address) {
        std::uint32_t value = 0;
        trace_movie_read_u32_fast(memory, address, value);
        return value;
    };

    const std::uint32_t r13 = context->gpr[13];
    const std::uint32_t scene_resources =
        r13 == 0u ? 0u : read_u32_or_zero(r13 - 14968u);
    const std::uint32_t scene_owner =
        scene_resources == 0u
            ? 0u
            : read_u32_or_zero(scene_resources + 0x20u);
    const std::uint32_t resource_holder =
        scene_owner == 0u
            ? 0u
            : read_u32_or_zero(scene_owner + 0x20u);
    const std::uint32_t resource_backend =
        resource_holder == 0u
            ? 0u
            : read_u32_or_zero(resource_holder + 0x09F8u);

    char slot_text[512]{};
    const bool resource_helper_function =
        function_address == 0x8040E048u ||
        function_address == 0x8040EDA8u ||
        function_address == 0x8040EE7Cu ||
        function_address == 0x80412EF8u ||
        function_address == 0x80413124u ||
        function_address == 0x804140D4u ||
        function_address == 0x80414178u ||
        function_address == 0x8041421Cu ||
        function_address == 0x804142D4u ||
        function_address == 0x80414628u ||
        function_address == 0x80414700u ||
        function_address == 0x80414740u ||
        function_address == 0x804147D0u ||
        function_address == 0x80415454u ||
        function_address == 0x80415674u ||
        function_address == 0x80415880u ||
        function_address == 0x80415E84u ||
        function_address == 0x804AAEA0u ||
        function_address == 0x804BF844u;
    if (resource_helper_function) {
        const std::uint32_t obj = context->gpr[3];
        const std::uint32_t owner_or_archive =
            obj == 0u ? 0u : read_u32_or_zero(obj + 0x28u);
        const std::uint32_t request_data =
            obj == 0u ? 0u : read_u32_or_zero(obj + 0x2Cu);
        const std::uint32_t request_buffer =
            obj == 0u ? 0u : read_u32_or_zero(obj + 0x30u);
        const std::uint32_t request_mode =
            obj == 0u ? 0u : read_u32_or_zero(obj + 0x34u);
        const std::uint32_t request_arg38 =
            obj == 0u ? 0u : read_u32_or_zero(obj + 0x38u);
        const std::uint32_t request_arg3c =
            obj == 0u ? 0u : read_u32_or_zero(obj + 0x3Cu);
        const std::uint32_t request_arg40 =
            obj == 0u ? 0u : read_u32_or_zero(obj + 0x40u);
        const std::uint32_t request_arg44 =
            obj == 0u ? 0u : read_u32_or_zero(obj + 0x44u);
        const std::uint32_t request_status =
            obj == 0u ? 0u : read_u32_or_zero(obj + 0x48u);
        const std::uint32_t request_result =
            obj == 0u ? 0u : read_u32_or_zero(obj + 0x50u);
        const std::uint32_t archive_open =
            owner_or_archive == 0u ? 0u : read_u32_or_zero(owner_or_archive + 0x50u);
        const std::uint32_t archive_reader =
            owner_or_archive == 0u ? 0u : read_u32_or_zero(owner_or_archive + 0x54u);
        const std::uint32_t archive_payload =
            owner_or_archive == 0u ? 0u : read_u32_or_zero(owner_or_archive + 0x4Cu);
        std::snprintf(
            slot_text,
            sizeof(slot_text),
            " helperObj=0x%08X h28=0x%08X h2c=0x%08X h30=0x%08X h34=0x%08X h38=0x%08X h3c=0x%08X"
            " h40=0x%08X h44=0x%08X h48=0x%08X h50=0x%08X archive50=0x%08X archive54=0x%08X archive4c=0x%08X"
            " r6=0x%08X r7=0x%08X r8=0x%08X r9=0x%08X",
            obj,
            owner_or_archive,
            request_data,
            request_buffer,
            request_mode,
            request_arg38,
            request_arg3c,
            request_arg40,
            request_arg44,
            request_status,
            request_result,
            archive_open,
            archive_reader,
            archive_payload,
            context->gpr[6],
            context->gpr[7],
            context->gpr[8],
            context->gpr[9]);
    } else if (function_address == 0x8049DADCu && context->gpr[3] != 0u) {
        const std::uint32_t loader = context->gpr[3];
        const std::uint32_t slot = context->gpr[4];
        const std::uint32_t sub_index = context->gpr[5];
        const std::uint32_t table = loader + 0xF4u;
        const std::uint32_t slot_base = read_u32_or_zero(table);
        const std::uint32_t slot_count =
            read_u32_or_zero(table + 0x04u);
        std::uint32_t group = 0;
        if (slot_base != 0u && slot < slot_count) {
            group = read_u32_or_zero(slot_base + slot * 4u);
        }
        const std::uint32_t group_vtable =
            group == 0u ? 0u : read_u32_or_zero(group);
        const std::uint32_t group_f04 =
            group == 0u ? 0u : read_u32_or_zero(group + 0x04u);
        const std::uint32_t group_f08 =
            group == 0u ? 0u : read_u32_or_zero(group + 0x08u);
        const std::uint32_t group_f0c =
            group == 0u ? 0u : read_u32_or_zero(group + 0x0Cu);
        const std::uint32_t group_f10 =
            group == 0u ? 0u : read_u32_or_zero(group + 0x10u);
        const std::uint32_t item =
            group != 0u && sub_index == 0u ? group + 0x04u : 0u;
        const std::uint32_t item_vtable =
            item == 0u ? 0u : read_u32_or_zero(item + 0x00u);
        const std::uint32_t item_f04 =
            item == 0u ? 0u : read_u32_or_zero(item + 0x04u);
        const std::uint32_t item_f08 =
            item == 0u ? 0u : read_u32_or_zero(item + 0x08u);
        const std::uint32_t item_f0c =
            item == 0u ? 0u : read_u32_or_zero(item + 0x0Cu);
        const std::uint32_t item_f10 =
            item == 0u ? 0u : read_u32_or_zero(item + 0x10u);
        const std::uint32_t item_f14 =
            item == 0u ? 0u : read_u32_or_zero(item + 0x14u);
        const std::uint32_t item_f18 =
            item == 0u ? 0u : read_u32_or_zero(item + 0x18u);
        const std::uint32_t item_f1c =
            item == 0u ? 0u : read_u32_or_zero(item + 0x1Cu);
        const std::uint32_t item_status =
            item == 0u ? 0u : read_u32_or_zero(item + 0x4Cu);
        char item_string0[64]{};
        char item_string1[64]{};
        trace_audio_read_string_fast(
            memory, item_f10, item_string0, sizeof(item_string0));
        trace_audio_read_string_fast(
            memory, item_f18, item_string1, sizeof(item_string1));
        std::snprintf(
            slot_text,
            sizeof(slot_text),
            " loader=0x%08X slot=%u sub=%u slotBase=0x%08X slotCount=%u"
            " group=0x%08X groupVt=0x%08X gf04=0x%08X gf08=0x%08X gf0c=0x%08X gf10=0x%08X"
            " item=0x%08X itemVt=0x%08X item04=0x%08X item08=0x%08X item0c=0x%08X"
            " item10=0x%08X item14=0x%08X item18=0x%08X item1c=0x%08X status=%u"
            " str10=\"%s\" str18=\"%s\"",
            loader,
            slot,
            sub_index,
            slot_base,
            slot_count,
            group,
            group_vtable,
            group_f04,
            group_f08,
            group_f0c,
            group_f10,
            item,
            item_vtable,
            item_f04,
            item_f08,
            item_f0c,
            item_f10,
            item_f14,
            item_f18,
            item_f1c,
            item_status,
            item_string0,
            item_string1);
    } else if (
        function_address == 0x8003379Cu || function_address == 0x8049251Cu ||
        function_address == 0x80492644u || function_address == 0x804926D0u) {
        std::uint32_t item = context->gpr[3];
        std::uint32_t callback_record = 0;
        std::uint32_t callback_file = 0;
        std::uint32_t callback_data = 0;
        std::uint32_t callback_token = 0;
        if (function_address == 0x80492644u) {
            callback_record = context->gpr[3];
            item = callback_record == 0u
                       ? 0u
                       : read_u32_or_zero(callback_record + 0x00u);
            callback_file = callback_record == 0u
                                ? 0u
                                : read_u32_or_zero(callback_record + 0x04u);
            callback_data = callback_record == 0u
                                ? 0u
                                : read_u32_or_zero(callback_record + 0x08u);
            callback_token = callback_record == 0u
                                 ? 0u
                                 : read_u32_or_zero(callback_record + 0x0Cu);
        } else if (function_address == 0x8049251Cu) {
            callback_token = context->gpr[4];
        }

        const std::uint32_t item_vtable =
            item == 0u ? 0u : read_u32_or_zero(item + 0x00u);
        const std::uint32_t item_done =
            item == 0u ? 0u : read_u32_or_zero(item + 0x48u);
        const std::uint32_t item_status =
            item == 0u ? 0u : read_u32_or_zero(item + 0x4Cu);
        const std::uint32_t item_file =
            item == 0u ? 0u : read_u32_or_zero(item + 0x50u);
        const std::uint32_t item_heap =
            item == 0u ? 0u : read_u32_or_zero(item + 0x54u);
        const std::uint32_t item_token =
            item == 0u ? 0u : read_u32_or_zero(item + 0x58u);
        const std::uint32_t item_pending =
            item == 0u ? 0u : read_u32_or_zero(item + 0x5Au);
        const std::uint32_t item_path0 =
            item == 0u ? 0u : read_u32_or_zero(item + 0x10u);
        const std::uint32_t item_path1 =
            item == 0u ? 0u : read_u32_or_zero(item + 0x18u);
        char item_string0[64]{};
        char item_string1[64]{};
        trace_audio_read_string_fast(
            memory, item_path0, item_string0, sizeof(item_string0));
        trace_audio_read_string_fast(
            memory, item_path1, item_string1, sizeof(item_string1));
        std::snprintf(
            slot_text,
            sizeof(slot_text),
            " item=0x%08X itemVt=0x%08X done=%u status=%u file=0x%08X heap=0x%08X"
            " token=%u pending=%u cbRecord=0x%08X cbFile=0x%08X cbData=0x%08X cbToken=%u"
            " str10=\"%s\" str18=\"%s\"",
            item,
            item_vtable,
            item_done,
            item_status,
            item_file,
            item_heap,
            item_token,
            item_pending,
            callback_record,
            callback_file,
            callback_data,
            callback_token,
            item_string0,
            item_string1);
    }

    char message[2048]{};
    std::snprintf(
        message,
        sizeof(message),
        "[resource-loader-state] #%u entry=%s pc=0x%08X lr=0x%08X"
        " r3=0x%08X r4=0x%08X r5=0x%08X sceneRes=0x%08X owner=0x%08X holder=0x%08X backend=0x%08X%s",
        index,
        label != nullptr ? label : "ResourceLoader",
        function_address,
        context->lr,
        context->gpr[3],
        context->gpr[4],
        context->gpr[5],
        scene_resources,
        scene_owner,
        resource_holder,
        resource_backend,
        slot_text);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline void trace_movie_function_entry_slow(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address) {
    const char* label = trace_movie_function_entry_label(function_address);
    if (label == nullptr || services == nullptr || services->log == nullptr ||
        context == nullptr) {
        return;
    }
    trace_cinema_frame_state(
        services, context, memory, function_address, label);
    trace_scenario_opening_state(
        services, context, memory, function_address, label);
    trace_resource_loader_state(
        services, context, memory, function_address, label);
    trace_async_executor_state(
        services, context, memory, function_address, label);
    trace_movie_player_simple_state(
        services, context, memory, function_address, label);
    trace_thp_wrapper_state(
        services, context, memory, function_address, label);
    if (!trace_movie_function_entries_enabled()) {
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    if (index >= 4096u) {
        return;
    }

    char sample_text[256]{};
    std::size_t sample_offset = 0;
    for (unsigned reg = 3; reg <= 7 && sample_offset < sizeof(sample_text); ++reg) {
        std::uint32_t hash = 0;
        std::uint8_t min_value = 0;
        std::uint8_t max_value = 0;
        if (!trace_movie_pointer_sample(
                memory, context->gpr[reg], hash, min_value, max_value)) {
            continue;
        }
        const int written = std::snprintf(
            sample_text + sample_offset,
            sizeof(sample_text) - sample_offset,
            " r%u[64]=%08X/%02X-%02X",
            reg,
            hash,
            min_value,
            max_value);
        if (written <= 0) {
            break;
        }
        sample_offset += static_cast<std::size_t>(written);
    }
    if (trace_movie_wrapper_function(function_address)) {
        trace_movie_append_wrapper_state(
            sample_text,
            sizeof(sample_text),
            sample_offset,
            memory,
            context->gpr[3]);
    }

    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "[movie-function] entry=%s pc=0x%08X lr=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X r9=0x%08X r10=0x%08X hid2=0x%08X%s",
        label,
        function_address,
        context->lr,
        context->gpr[3],
        context->gpr[4],
        context->gpr[5],
        context->gpr[6],
        context->gpr[7],
        context->gpr[8],
        context->gpr[9],
        context->gpr[10],
        context->hid2,
        sample_text);
    services->log(services->user, LogLevelV1::Warning, message);
}

GALAXY_ALWAYS_INLINE void trace_movie_function_entry(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address) {
    if (!trace_movie_function_entries_enabled() &&
        !trace_cinema_frame_state_enabled() &&
        !trace_scenario_opening_state_enabled() &&
        !trace_thp_wrapper_state_enabled()) {
        return;
    }
    trace_movie_function_entry_slow(
        services, context, memory, function_address);
}

inline bool trace_file_select_function_entries_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_TRACE_FILE_SELECT_FUNCTION_ENTRIES") == 0 &&
            length > 1) {
            return value[0] != '0';
        }
        length = 0;
        value[0] = '\0';
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_FILE_SELECT") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

inline bool trace_save_sequence_function_entries_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_SAVE_SEQUENCE") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

inline bool trace_file_select_save_sequence_entries_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_TRACE_FILE_SELECT_SAVE_SEQUENCE") != 0 ||
            length <= 1) {
            return true;
        }
        return value[0] != '0';
    }();
    return enabled;
}

inline bool trace_file_select_is_save_sequence_address(
    std::uint32_t function_address) {
    return function_address >= 0x803B4900u &&
           function_address <= 0x803B66ACu;
}

inline const char* trace_file_select_function_entry_label(
    std::uint32_t address) {
    switch (address) {
    case 0x801785ECu: return "FileSelectItem::onPointing";
    case 0x8017866Cu: return "FileSelectItem::offPointing";
    case 0x80178A14u: return "FileSelectItem::control";
    case 0x80178E64u: return "FileSelectItem::updatePointing";
    case 0x80178F5Cu: return "FileSelectItem::updateRotate";
    case 0x8017AB10u: return "FileSelector::notifyItem";
    case 0x8017AB94u: return "FileSelector::control";
    case 0x8017BA08u: return "FileSelector::onPoint";
    case 0x8017BA84u: return "FileSelector::onSelect";
    case 0x8017BB40u: return "FileSelector::clearPointing";
    case 0x8017C7B4u: return "FileSelector::exeTitle";
    case 0x8017C874u: return "FileSelector::exeTitleEnd";
    case 0x8017C984u: return "FileSelector::exeRFLWait";
    case 0x8017CA08u: return "FileSelector::exeRFLWaitEnd";
    case 0x8017CA60u: return "FileSelector::exeFileSelectStart";
    case 0x8017CAC4u: return "FileSelector::exeFileSelect";
    case 0x8017CB1Cu: return "FileSelector::exeFileConfirmStart";
    case 0x8017CD18u: return "FileSelector::exeFileConfirm";
    case 0x8017CF1Cu: return "FileSelector::exeDemo";
    case 0x8017D024u: return "FileSelector::exeCreateConfirmStart";
    case 0x8017D08Cu: return "FileSelector::exeCreateConfirm";
    case 0x8017D148u: return "FileSelector::exeCreate";
    case 0x8017D900u: return "FileSelector::exeMiiSelectStart";
    case 0x8017D9DCu: return "FileSelector::exeMiiSelect";
    case 0x8017DBACu: return "FileSelector::exeMiiConfirm";
    case 0x8017DD04u: return "FileSelector::exeMiiCreateWait";
    case 0x8017DD80u: return "FileSelector::exeMiiCreateDemo";
    case 0x8017DF14u: return "FileSelector::exeMiiInfo";
    case 0x8017E0C4u: return "FileSelector::exeDelete";
    case 0x8017E294u: return "FileSelector::exeManual";
    case 0x8036DA0Cu: return "MiiSelect::getSelectedID";
    case 0x8036E558u: return "MiiSelect::getIconID";
    case 0x8036ED14u: return "MiiSelectIcon::MiiSelectIcon";
    case 0x8036EE1Cu: return "MiiSelect::appear";
    case 0x8036EE48u: return "MiiSelect::exeAppear owner";
    case 0x8036F028u: return "MiiSelect::exeAppear";
    case 0x8036F054u: return "MiiSelect::exeWait owner";
    case 0x8036F0ACu: return "MiiSelect::exeWait";
    case 0x8036FB54u: return "MiiSelect::selection callbacks owner";
    case 0x8036FB70u: return "MiiSelect::onSelect";
    case 0x8036FBC4u: return "MiiSelect::onSelectDummy";
    case 0x8034BA38u: return "ButtonPaneController::setFileSelectGate";
    case 0x8034BA9Cu: return "ButtonPaneController::testPlayGateNerve";
    case 0x8034BAA4u: return "ButtonPaneController::readFileSelectGate";
    case 0x8034BAACu: return "ButtonPaneController::requestFileSelectIdle";
    case 0x8034C1A0u: return "ButtonPaneController::hitTest";
    case 0x8034C254u: return "ButtonPaneController::requestPointingState";
    case 0x80366244u: return "NerveKeeper::isCurrentNerve";
    case 0x803FB5BCu: return "StarPointerUtil::getPointingPos helper";
    case 0x803FB680u: return "StarPointerUtil::isPointing helper";
    case 0x8034C874u: return "ButtonPaneController::trySelect owner";
    case 0x8034C87Cu: return "ButtonPaneController::trySelect";
    case 0x8034C8E4u: return "ButtonPaneController::isPointing owner";
    case 0x8034C914u: return "ButtonPaneController::isPointing";
    case 0x803D42E8u: return "MR::testDPDMenuPadDecideTrigger owner";
    case 0x803D42ECu: return "MR::testDPDMenuPadDecideTrigger";
    case 0x803FBAACu: return "StarPointerUtil::checkPointingTarget owner";
    case 0x803FBB08u: return "StarPointerUtil::checkPointingTarget";
    case 0x803FC76Cu: return "MR::isStarPointerPointingFileSelect owner";
    case 0x803FC794u: return "MR::isStarPointerPointingFileSelect";
    case 0x803AE664u: return "ConfigDataMii::setMiiOrIconId owner";
    case 0x803AE6ACu: return "ConfigDataMii::setMiiOrIconId";
    case 0x803B49C8u: return "SaveDataHandleSequence::exeWait";
    case 0x803B49ECu: return "SaveDataHandleSequence::exeProcessing";
    case 0x803B49B0u: return "SaveDataHandleSequence::predicate49B0";
    case 0x803B49BCu: return "SaveDataHandleSequence::advance49BC";
    case 0x803B4A20u: return "SaveDataHandleSequence::exeSaveProcessingGameData";
    case 0x803B4A54u: return "SaveDataHandleSequence::exeSaveProcessingBanner";
    case 0x803B5498u: return "SaveDataHandler::notifyFallback5498";
    case 0x803B55ECu: return "SaveDataHandler::wait";
    case 0x803B5654u: return "SaveDataHandler::startRequest";
    case 0x803B56BCu: return "SaveDataHandler::pumpRequest";
    case 0x803B5958u: return "SaveDataHandler::requestRemoveGameData";
    case 0x803B596Cu: return "SaveDataHandler::exeD8F4";
    case 0x803B59B0u: return "SaveDataHandler::exeRemoveProcessingGameData";
    case 0x803B5A48u: return "SaveDataHandler::exeD8F8";
    case 0x803B5ADCu: return "SaveDataHandler::resetFlags";
    case 0x803B5E30u: return "SaveDataHandler::refreshStatusList";
    case 0x803B5FC0u: return "SaveDataHandlerNerve::D8F0orD8F8";
    case 0x803B5FC8u: return "SaveDataHandlerNerve::D8F8";
    case 0x803B5FD0u: return "SaveDataHandlerNerve::D8FC";
    case 0x803B5FD8u: return "SaveDataHandlerNerve::D900";
    case 0x803B6018u: return "SaveDataHandlerNerve::D8F4";
    case 0x803B6020u: return "SaveDataHandlerNerve::D8F0";
    case 0x803B4D84u: return "GameSequenceFunction::startGameDataLoadSequence";
    case 0x803B4DC8u: return "GameSequenceFunction::startCreateUserFileSequence";
    case 0x803B4F84u: return "GameSequenceFunction::isActiveSaveDataHandleSequence";
    case 0x803B4FA8u: return "GameSequenceFunction::isSuccessSaveDataHandleSequence";
    case 0x803B6404u: return "GameSequenceFunction::startCreateUserFileSequence owner";
    case 0x803B640Cu: return "GameSequenceFunction::startCreateUserFileSequence";
    case 0x803B64C0u: return "GameSequenceFunction::startSetMiiOrIconIdUserFileSequence";
    case 0x803B64D0u: return "GameSequenceFunction::startSetMiiOrIconIdUserFileSequence+0x10";
    case 0x803B64F8u: return "GameSequenceFunction::save/user-file helpers owner";
    case 0x803B6514u: return "GameSequenceFunction::storeMiiOrIconIdUserFileSequence";
    case 0x803B65A4u: return "GameSequenceFunction::startSaveAllUserFileSequence";
    case 0x803B65C8u: return "GameSequenceFunction::isActiveSaveDataHandleSequence";
    case 0x803B65ECu: return "GameSequenceFunction::isSuccessSaveDataHandleSequence";
    case 0x803B8C64u: return "SaveDataHandleSequence::update";
    case 0x803B8ED8u: return "SaveDataHandleSequence::startCreateUserFile owner";
    case 0x803B8EDCu: return "SaveDataHandleSequence::startCreateUserFile";
    case 0x803B90E0u: return "SaveDataHandleSequence::storeMiiOrIconId owner";
    case 0x803B90F0u: return "SaveDataHandleSequence::storeMiiOrIconId";
    case 0x803B9550u: return "SaveDataHandleSequence::operation states owner";
    case 0x803B9594u: return "SaveDataHandleSequence::exeNoOperation";
    case 0x803B95A8u: return "SaveDataHandleSequence::exeCheckEnableToCreate";
    case 0x803B96F4u: return "SaveDataHandleSequence::exeSave owner";
    case 0x803B9728u: return "SaveDataHandleSequence::exeSave";
    case 0x803B9AC4u: return "SaveDataHandleSequence::exePreLoad";
    case 0x803B9B30u: return "SaveDataHandleSequence::exePreLoadDone";
    case 0x803B9EACu: return "SaveDataHandleSequence::trySave";
    case 0x803BA560u: return "SaveDataHandleSequence executors owner";
    case 0x803BA60Cu: return "SaveDataHandleSequence executor type0";
    case 0x803BA604u: return "SaveDataHandleSequenceSave::execute";
    case 0x803BA614u: return "SaveDataHandleSequenceCheckEnableToCreate::execute";
    case 0x803BA61Cu: return "SaveDataHandleSequenceNoOperation::execute";
    case 0x803BA648u: return "SaveDataHandleSequence executor type3";
    case 0x803BA658u: return "SaveDataHandleSequence executor type4";
    case 0x803BA674u: return "SaveDataHandleSequence executor type5";
    case 0x803BA690u: return "SaveDataHandleSequence executor type6";
    case 0x803BA6CCu: return "SaveDataHandleSequence::executePendingNerve";
    case 0x803BA724u: return "SaveDataHandleSequence::resetState";
    case 0x803BB128u: return "SaveExecutor::saveOrCreate";
    case 0x803BB27Cu: return "SaveExecutor::removeAfterBanner";
    case 0x803BB2E0u: return "SaveExecutor::removeGameData";
    case 0x803BB370u: return "SaveExecutor::removeBanner";
    case 0x803BB464u: return "SaveExecutor::submitOrFinish";
    case 0x803BB4E0u: return "SaveExecutor::submitRequest";
    case 0x803BB56Cu: return "SaveExecutor::checkResultPath";
    case 0x803BB5F4u: return "SaveExecutor::isFatalStatus";
    case 0x803BB66Cu: return "SaveExecutor::setErrorStatus";
    case 0x803BB6B8u: return "SaveExecutor::markComplete";
    case 0x803BB718u: return "SaveExecutor::writeRequest";
    default: return nullptr;
    }
}

inline std::uint32_t trace_save_read_u32_or_zero(
    GuestMemoryV1* memory,
    std::uint32_t address) {
    std::uint32_t value = 0;
    return trace_audio_read_u32_fast(memory, address, value) ? value : 0;
}

inline std::uint8_t trace_save_read_u8_or_zero(
    GuestMemoryV1* memory,
    std::uint32_t address) {
    const std::byte* direct = resolve_guest_fast(memory, address, 1u);
    return direct == nullptr ? 0u : std::to_integer<std::uint8_t>(*direct);
}

inline void trace_resource_status_store_u32(
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    const char* op,
    std::uint32_t guest_pc,
    std::uint32_t address,
    std::uint32_t value) {
    if (!trace_resource_status_writes_enabled() ||
        services == nullptr || services->log == nullptr || memory == nullptr) {
        return;
    }

    const bool known_status_pc =
        guest_pc == 0x8049258Cu || guest_pc == 0x80492700u;
    const bool plausible_status_word =
        (address & 3u) == 0u && address >= 0x80700000u &&
        address < 0x81800000u && value <= 3u;
    if (!known_status_pc && !plausible_status_word) {
        return;
    }

    const std::uint32_t item = address >= 0x4Cu ? address - 0x4Cu : 0u;
    const std::uint32_t item_vtable =
        item == 0u ? 0u : trace_save_read_u32_or_zero(memory, item);
    if (!known_status_pc &&
        (item_vtable < 0x80000000u || item_vtable >= 0x81800000u)) {
        return;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    if (index >= 1024u && value != 2u && (index % 120u) != 0u) {
        return;
    }

    char path0[64]{};
    char path1[64]{};
    const std::uint32_t old_value =
        trace_save_read_u32_or_zero(memory, address);
    const std::uint32_t done_flag =
        item == 0u ? 0u : trace_save_read_u32_or_zero(memory, item + 0x48u);
    const std::uint32_t path0_ptr =
        item == 0u ? 0u : trace_save_read_u32_or_zero(memory, item + 0x10u);
    const std::uint32_t path1_ptr =
        item == 0u ? 0u : trace_save_read_u32_or_zero(memory, item + 0x18u);
    const std::uint32_t token =
        item == 0u ? 0u : trace_save_read_u32_or_zero(memory, item + 0x58u);
    const std::uint32_t pending =
        item == 0u ? 0u : trace_save_read_u32_or_zero(memory, item + 0x5Au);
    trace_audio_read_string_fast(memory, path0_ptr, path0, sizeof(path0));
    trace_audio_read_string_fast(memory, path1_ptr, path1, sizeof(path1));

    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "[resource-status-store] #%u %s pc=0x%08X item=0x%08X addr=0x%08X old=%u value=%u"
        " itemVt=0x%08X done=%u file=0x%08X heap=0x%08X token=%u pending=%u str10=\"%s\" str18=\"%s\"",
        index,
        op,
        guest_pc,
        item,
        address,
        old_value,
        value,
        item_vtable,
        done_flag,
        item == 0u ? 0u : trace_save_read_u32_or_zero(memory, item + 0x50u),
        item == 0u ? 0u : trace_save_read_u32_or_zero(memory, item + 0x54u),
        token,
        pending,
        path0,
        path1);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline std::uint16_t trace_file_select_read_u16_or_zero(
    GuestMemoryV1* memory,
    std::uint32_t address) {
    std::uint64_t wide = 0;
    return trace_audio_read_be_fast(memory, address, 2u, wide)
               ? static_cast<std::uint16_t>(wide)
               : 0u;
}

inline void trace_file_select_read_string(
    GuestMemoryV1* memory,
    std::uint32_t address,
    char* output,
    std::size_t output_size) {
    if (output_size == 0) {
        return;
    }
    output[0] = '\0';
    if (memory == nullptr || address < 0x80000000u ||
        address >= 0x94000000u) {
        return;
    }
    std::size_t written = 0;
    while (written + 1 < output_size) {
        const std::byte* direct =
            resolve_guest_fast(memory, address + static_cast<std::uint32_t>(written), 1u);
        if (direct == nullptr) {
            break;
        }
        const char c = static_cast<char>(std::to_integer<std::uint8_t>(*direct));
        if (c == '\0') {
            break;
        }
        output[written++] =
            (static_cast<unsigned char>(c) >= 0x20u &&
             static_cast<unsigned char>(c) < 0x7Fu)
                ? c
                : '?';
    }
    output[written] = '\0';
}

inline const char* trace_save_sequence_nerve_label(
    std::uint32_t r13,
    std::uint32_t nerve) {
    switch (nerve - r13) {
    case 0xFFFFD930u: return "seq:NoOperation";
    case 0xFFFFD934u: return "seq:CheckEnableToCreate";
    case 0xFFFFD938u: return "seq:SaveConfirm";
    case 0xFFFFD93Cu: return "seq:Save";
    case 0xFFFFD940u: return "seq:SaveWindowDisappear";
    case 0xFFFFD944u: return "seq:SaveDoneKeyWait";
    case 0xFFFFD948u: return "seq:SaveAllWithoutKeyWait";
    case 0xFFFFD94Cu: return "seq:SaveAllWithoutKeyWaitDisappear";
    case 0xFFFFD950u: return "seq:SaveAllWithoutWindow";
    case 0xFFFFD954u: return "seq:PreLoad";
    case 0xFFFFD958u: return "seq:PreLoadDone";
    case 0xFFFFD95Cu: return "seq:NoSaveConfirmRemind";
    case 0xFFFFD960u: return "seq:ErrorHandling";
    default: return "seq:?";
    }
}

inline const char* trace_save_handler_nerve_label(
    std::uint32_t r13,
    std::uint32_t nerve) {
    switch (nerve - r13) {
    case 0xFFFFD8F0u: return "handler:D8F0->803B6020";
    case 0xFFFFD8F4u: return "handler:D8F4->803B6018";
    case 0xFFFFD8F8u: return "handler:D8F8->803B5FC8";
    case 0xFFFFD8FCu: return "handler:D8FC->803B5FD0";
    case 0xFFFFD900u: return "handler:D900->803B5FD8";
    case 0xFFFFD904u: return "handler:D904->803B5FC0";
    default: return "handler:?";
    }
}

inline void trace_save_sequence_function_entry_slow(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address) {
    const char* label =
        trace_file_select_function_entry_label(function_address);
    if (label == nullptr) {
        return;
    }

    const auto should_log_hot_entry =
        [](std::atomic<std::uint32_t>& counter) {
            const std::uint32_t local_index = counter.fetch_add(1);
            return local_index < 12u || (local_index % 120u) == 0u;
        };
    switch (function_address) {
    case 0x803B4F84u: {
        static std::atomic<std::uint32_t> count_803B4F84{0};
        if (!should_log_hot_entry(count_803B4F84)) {
            return;
        }
        break;
    }
    case 0x803B64D0u: {
        static std::atomic<std::uint32_t> count_803B64D0{0};
        if (!should_log_hot_entry(count_803B64D0)) {
            return;
        }
        break;
    }
    case 0x803B8C64u: {
        static std::atomic<std::uint32_t> count_803B8C64{0};
        if (!should_log_hot_entry(count_803B8C64)) {
            return;
        }
        break;
    }
    case 0x803B9AC4u: {
        static std::atomic<std::uint32_t> count_803B9AC4{0};
        if (!should_log_hot_entry(count_803B9AC4)) {
            return;
        }
        break;
    }
    default:
        break;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    if (index >= 32768u) {
        return;
    }

    const std::uint32_t r13 = context->gpr[13];
    const std::uint32_t root =
        trace_save_read_u32_or_zero(memory, r13 + 0xFFFFC588u);
    const std::uint32_t scene =
        trace_save_read_u32_or_zero(memory, root + 0x0Cu);
    const std::uint32_t save_seq =
        trace_save_read_u32_or_zero(memory, scene + 0x08u);
    const std::uint32_t handler =
        trace_save_read_u32_or_zero(memory, save_seq + 0x14u);
    const std::uint32_t request =
        trace_save_read_u32_or_zero(memory, handler + 0x08u);
    const std::uint32_t arg_request = context->gpr[4];

    const std::uint32_t seq_spine =
        trace_save_read_u32_or_zero(memory, save_seq + 0x04u);
    const std::uint32_t seq_curr =
        trace_save_read_u32_or_zero(memory, seq_spine + 0x04u);
    const std::uint32_t seq_next =
        trace_save_read_u32_or_zero(memory, seq_spine + 0x08u);
    const std::uint32_t seq_step =
        trace_save_read_u32_or_zero(memory, seq_spine + 0x0Cu);

    const std::uint32_t handler_spine =
        trace_save_read_u32_or_zero(memory, handler + 0x04u);
    const std::uint32_t handler_curr =
        trace_save_read_u32_or_zero(memory, handler_spine + 0x04u);
    const std::uint32_t handler_next =
        trace_save_read_u32_or_zero(memory, handler_spine + 0x08u);
    const std::uint32_t handler_step =
        trace_save_read_u32_or_zero(memory, handler_spine + 0x0Cu);
    const std::uint32_t self = context->gpr[3];
    const std::uint32_t self_spine =
        trace_save_read_u32_or_zero(memory, self + 0x04u);
    const std::uint32_t self_curr =
        trace_save_read_u32_or_zero(memory, self_spine + 0x04u);
    const std::uint32_t self_next =
        trace_save_read_u32_or_zero(memory, self_spine + 0x08u);
    const std::uint32_t self_step =
        trace_save_read_u32_or_zero(memory, self_spine + 0x0Cu);
    const std::uint32_t self_op08 =
        trace_save_read_u32_or_zero(memory, self + 0x08u);
    const std::uint32_t self_nand0c =
        trace_save_read_u32_or_zero(memory, self + 0x0Cu);

    char message[3072]{};
    std::snprintf(
        message,
        sizeof(message),
        "[save-seq-function] entry=%s pc=0x%08X lr=0x%08X r3=0x%08X r4=0x%08X selfSpine=0x%08X selfCurr=0x%08X(%s) selfNext=0x%08X(%s) selfStep=%u selfFlags=%u%u%u selfWords=%08X,%08X,%08X,%08X,%08X,%08X,%08X selfOp08=0x%08X selfOp08Words=%08X,%08X,%08X,%08X,%08X selfNand0C=0x%08X selfNandWords=%08X,%08X,%08X,%08X,%08X root=0x%08X scene=0x%08X saveSeq=0x%08X seqCurr=0x%08X(%s) seqNext=0x%08X(%s) seqStep=%u result=%u flags=%u%u%u%u%u handler=0x%08X handlerCurr=0x%08X(%s) handlerNext=0x%08X(%s) handlerStep=%u handlerCheck=%u handlerWords=%08X,%08X,%08X,%08X req=0x%08X reqWords=%08X,%08X,%08X,%08X,%08X,%08X reqState=%u reqType=%u reqResult=%d reqOwner=0x%08X reqPath=0x%08X argReq=0x%08X argWords=%08X,%08X,%08X,%08X,%08X,%08X argState=%u argType=%u argResult=%d argOwner=0x%08X argPath=0x%08X",
        label,
        function_address,
        context->lr,
        context->gpr[3],
        context->gpr[4],
        self_spine,
        self_curr,
        trace_save_handler_nerve_label(r13, self_curr),
        self_next,
        trace_save_handler_nerve_label(r13, self_next),
        self_step,
        static_cast<unsigned>(trace_save_read_u8_or_zero(memory, self + 0x24u)),
        static_cast<unsigned>(trace_save_read_u8_or_zero(memory, self + 0x25u)),
        static_cast<unsigned>(trace_save_read_u8_or_zero(memory, self + 0x26u)),
        trace_save_read_u32_or_zero(memory, self + 0x08u),
        trace_save_read_u32_or_zero(memory, self + 0x0Cu),
        trace_save_read_u32_or_zero(memory, self + 0x10u),
        trace_save_read_u32_or_zero(memory, self + 0x14u),
        trace_save_read_u32_or_zero(memory, self + 0x18u),
        trace_save_read_u32_or_zero(memory, self + 0x1Cu),
        trace_save_read_u32_or_zero(memory, self + 0x20u),
        self_op08,
        trace_save_read_u32_or_zero(memory, self_op08 + 0x00u),
        trace_save_read_u32_or_zero(memory, self_op08 + 0x04u),
        trace_save_read_u32_or_zero(memory, self_op08 + 0x40u),
        trace_save_read_u32_or_zero(memory, self_op08 + 0x44u),
        trace_save_read_u32_or_zero(memory, self_op08 + 0x48u),
        self_nand0c,
        trace_save_read_u32_or_zero(memory, self_nand0c + 0x00u),
        trace_save_read_u32_or_zero(memory, self_nand0c + 0x04u),
        trace_save_read_u32_or_zero(memory, self_nand0c + 0x40u),
        trace_save_read_u32_or_zero(memory, self_nand0c + 0x44u),
        trace_save_read_u32_or_zero(memory, self_nand0c + 0x48u),
        root,
        scene,
        save_seq,
        seq_curr,
        trace_save_sequence_nerve_label(r13, seq_curr),
        seq_next,
        trace_save_sequence_nerve_label(r13, seq_next),
        seq_step,
        trace_save_read_u32_or_zero(memory, save_seq + 0x24u),
        static_cast<unsigned>(trace_save_read_u8_or_zero(memory, save_seq + 0x28u)),
        static_cast<unsigned>(trace_save_read_u8_or_zero(memory, save_seq + 0x29u)),
        static_cast<unsigned>(trace_save_read_u8_or_zero(memory, save_seq + 0x2Au)),
        static_cast<unsigned>(trace_save_read_u8_or_zero(memory, save_seq + 0x2Bu)),
        static_cast<unsigned>(trace_save_read_u8_or_zero(memory, save_seq + 0x2Cu)),
        handler,
        handler_curr,
        trace_save_handler_nerve_label(r13, handler_curr),
        handler_next,
        trace_save_handler_nerve_label(r13, handler_next),
        handler_step,
        trace_save_read_u32_or_zero(memory, handler + 0x10u),
        trace_save_read_u32_or_zero(memory, handler + 0x18u),
        trace_save_read_u32_or_zero(memory, handler + 0x1Cu),
        trace_save_read_u32_or_zero(memory, handler + 0x20u),
        trace_save_read_u32_or_zero(memory, handler + 0x24u),
        request,
        trace_save_read_u32_or_zero(memory, request + 0x00u),
        trace_save_read_u32_or_zero(memory, request + 0x04u),
        trace_save_read_u32_or_zero(memory, request + 0x08u),
        trace_save_read_u32_or_zero(memory, request + 0x0Cu),
        trace_save_read_u32_or_zero(memory, request + 0x10u),
        trace_save_read_u32_or_zero(memory, request + 0x14u),
        trace_save_read_u32_or_zero(memory, request + 0x40u),
        trace_save_read_u32_or_zero(memory, request + 0x44u),
        static_cast<std::int32_t>(
            trace_save_read_u32_or_zero(memory, request + 0x48u)),
        trace_save_read_u32_or_zero(memory, request + 0x4Cu),
        trace_save_read_u32_or_zero(memory, request + 0x50u),
        arg_request,
        trace_save_read_u32_or_zero(memory, arg_request + 0x00u),
        trace_save_read_u32_or_zero(memory, arg_request + 0x04u),
        trace_save_read_u32_or_zero(memory, arg_request + 0x08u),
        trace_save_read_u32_or_zero(memory, arg_request + 0x0Cu),
        trace_save_read_u32_or_zero(memory, arg_request + 0x10u),
        trace_save_read_u32_or_zero(memory, arg_request + 0x14u),
        trace_save_read_u32_or_zero(memory, arg_request + 0x40u),
        trace_save_read_u32_or_zero(memory, arg_request + 0x44u),
        static_cast<std::int32_t>(
            trace_save_read_u32_or_zero(memory, arg_request + 0x48u)),
        trace_save_read_u32_or_zero(memory, arg_request + 0x4Cu),
        trace_save_read_u32_or_zero(memory, arg_request + 0x50u));
    services->log(services->user, LogLevelV1::Warning, message);
}

inline void trace_file_select_function_entry_slow(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address) {
    const char* label =
        trace_file_select_function_entry_label(function_address);
    if (label == nullptr) {
        return;
    }

    const auto should_log_hot_entry =
        [](std::atomic<std::uint32_t>& counter) {
            const std::uint32_t local_index = counter.fetch_add(1);
            return local_index < 8u || (local_index % 512u) == 0u;
        };
    switch (function_address) {
    case 0x80178A14u:
    case 0x80178E64u:
    case 0x80178F5Cu: {
        static std::atomic<std::uint32_t> count_file_select_item_control{0};
        if (!should_log_hot_entry(count_file_select_item_control)) {
            return;
        }
        break;
    }
    case 0x8034C1A0u: {
        static std::atomic<std::uint32_t> count_8034C1A0{0};
        if (!should_log_hot_entry(count_8034C1A0)) {
            return;
        }
        break;
    }
    case 0x8034C254u: {
        static std::atomic<std::uint32_t> count_8034C254{0};
        if (!should_log_hot_entry(count_8034C254)) {
            return;
        }
        break;
    }
    case 0x8034C8E4u: {
        static std::atomic<std::uint32_t> count_8034C8E4{0};
        if (!should_log_hot_entry(count_8034C8E4)) {
            return;
        }
        break;
    }
    case 0x80366244u: {
        static std::atomic<std::uint32_t> count_80366244{0};
        if (!should_log_hot_entry(count_80366244)) {
            return;
        }
        break;
    }
    case 0x803FB5BCu: {
        static std::atomic<std::uint32_t> count_803FB5BC{0};
        if (!should_log_hot_entry(count_803FB5BC)) {
            return;
        }
        break;
    }
    case 0x803FB680u: {
        static std::atomic<std::uint32_t> count_803FB680{0};
        if (!should_log_hot_entry(count_803FB680)) {
            return;
        }
        break;
    }
    case 0x8034C914u: {
        static std::atomic<std::uint32_t> count_8034C914{0};
        if (!should_log_hot_entry(count_8034C914)) {
            return;
        }
        break;
    }
    case 0x803D42E8u: {
        static std::atomic<std::uint32_t> count_803D42E8{0};
        if (!should_log_hot_entry(count_803D42E8)) {
            return;
        }
        break;
    }
    case 0x803D42ECu: {
        static std::atomic<std::uint32_t> count_803D42EC{0};
        if (!should_log_hot_entry(count_803D42EC)) {
            return;
        }
        break;
    }
    case 0x803FBAACu: {
        static std::atomic<std::uint32_t> count_803FBAAC{0};
        if (!should_log_hot_entry(count_803FBAAC)) {
            return;
        }
        break;
    }
    case 0x803FBB08u: {
        static std::atomic<std::uint32_t> count_803FBB08{0};
        if (!should_log_hot_entry(count_803FBB08)) {
            return;
        }
        break;
    }
    case 0x803FC76Cu: {
        static std::atomic<std::uint32_t> count_803FC76C{0};
        if (!should_log_hot_entry(count_803FC76C)) {
            return;
        }
        break;
    }
    case 0x803FC794u: {
        static std::atomic<std::uint32_t> count_803FC794{0};
        if (!should_log_hot_entry(count_803FC794)) {
            return;
        }
        break;
    }
    case 0x803B9550u: {
        static std::atomic<std::uint32_t> count_803B9550{0};
        if (!should_log_hot_entry(count_803B9550)) {
            return;
        }
        break;
    }
    case 0x803B64F8u: {
        static std::atomic<std::uint32_t> count_803B64F8{0};
        if (!should_log_hot_entry(count_803B64F8)) {
            return;
        }
        break;
    }
    case 0x803B64D0u: {
        static std::atomic<std::uint32_t> count_803B64D0{0};
        if (!should_log_hot_entry(count_803B64D0)) {
            return;
        }
        break;
    }
    case 0x8017CF1Cu: {
        static std::atomic<std::uint32_t> count_8017CF1C{0};
        if (!should_log_hot_entry(count_8017CF1C)) {
            return;
        }
        break;
    }
    default:
        break;
    }

    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t index = count.fetch_add(1);
    if (index >= 32768u) {
        return;
    }

    char extra[1024]{};
    const auto append_file_select_item =
        [&](char* target,
            std::size_t target_size,
            const char* name,
            std::uint32_t item) {
            if (target_size == 0 || item == 0u) {
                return;
            }
            const std::size_t used = std::strlen(target);
            if (used >= target_size) {
                return;
            }
            std::snprintf(
                target + used,
                target_size - used,
                " %s=0x%08X(id=0x%08X new=%u legacy144=%u invalid=%u"
                " badMii=%u complete=%u flag154=%u flag155=%u flag156=%u"
                " gate164=%u timer168=0x%08X timer16c=0x%08X)",
                name,
                item,
                trace_save_read_u32_or_zero(memory, item + 0x140u),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, item + 0x8Cu)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, item + 0x144u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, item + 0x145u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, item + 0x146u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, item + 0x147u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, item + 0x154u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, item + 0x155u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, item + 0x156u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, item + 0x164u)),
                trace_save_read_u32_or_zero(memory, item + 0x168u),
                trace_save_read_u32_or_zero(memory, item + 0x16Cu));
        };
    const auto append_button_pane_controller_state =
        [&](char* target,
            std::size_t target_size,
            const char* name,
            std::uint32_t button) {
            if (target_size == 0 || button == 0u) {
                return;
            }
            const std::size_t used = std::strlen(target);
            if (used >= target_size) {
                return;
            }
            const std::uint32_t host =
                trace_save_read_u32_or_zero(memory, button + 0x08u);
            const std::uint32_t pane =
                trace_save_read_u32_or_zero(memory, button + 0x0Cu);
            const std::uint32_t nerve_keeper =
                trace_save_read_u32_or_zero(memory, button + 0x10u);
            const std::uint32_t target_pane =
                trace_save_read_u32_or_zero(memory, button + 0x3Cu);
            const std::uint32_t current_nerve =
                trace_save_read_u32_or_zero(memory, nerve_keeper + 0x08u);
            const std::uint32_t fallback_nerve =
                trace_save_read_u32_or_zero(memory, nerve_keeper + 0x04u);
            std::snprintf(
                target + used,
                target_size - used,
                " %s=0x%08X(host=0x%08X pane=0x%08X nerve=0x%08X"
                " curr=0x%08X fallback=0x%08X targetPane=0x%08X"
                " byte1c=%u gate20=%u pointing21=%u decide22=%u byte23=%u"
                " appearance24=%u words28=%08X,%08X,%08X,%08X"
                " nervesD010=0x%08X D014=0x%08X D018=0x%08X D020=0x%08X)",
                name,
                button,
                host,
                pane,
                nerve_keeper,
                current_nerve,
                fallback_nerve,
                target_pane,
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, button + 0x1Cu)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, button + 0x20u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, button + 0x21u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, button + 0x22u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, button + 0x23u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, button + 0x24u)),
                trace_save_read_u32_or_zero(memory, button + 0x28u),
                trace_save_read_u32_or_zero(memory, button + 0x2Cu),
                trace_save_read_u32_or_zero(memory, button + 0x30u),
                trace_save_read_u32_or_zero(memory, button + 0x34u),
                context->gpr[13] + 0xFFFFD010u,
                context->gpr[13] + 0xFFFFD014u,
                context->gpr[13] + 0xFFFFD018u,
                context->gpr[13] + 0xFFFFD020u);
        };
    const auto append_file_selector_state =
        [&](char* target, std::size_t target_size, std::uint32_t self) {
            if (target_size == 0 || self == 0u) {
                return;
            }
            const std::size_t used = std::strlen(target);
            if (used >= target_size) {
                return;
            }
            const std::uint32_t actor9c =
                trace_save_read_u32_or_zero(memory, self + 0x9Cu);
            const std::uint32_t actora0 =
                trace_save_read_u32_or_zero(memory, self + 0xA0u);
            const std::uint32_t buttona4 =
                trace_save_read_u32_or_zero(memory, self + 0xA4u);
            const std::uint32_t selected =
                trace_save_read_u32_or_zero(memory, self + 0xB4u);
            const std::uint32_t pointed =
                trace_save_read_u32_or_zero(memory, self + 0xC0u);
            const std::uint32_t scene =
                trace_save_read_u32_or_zero(memory, self + 0xC4u);
            const std::uint32_t file_slots =
                trace_save_read_u32_or_zero(memory, self + 0xC8u);
            std::snprintf(
                target + used,
                target_size - used,
                " selector=0x%08X actor9c=0x%08X actora0=0x%08X"
                " buttona4=0x%08X(button20=%u button24=%u button1c=%u)"
                " selectedPtr=0x%08X pointedPtr=0x%08X"
                " scene=0x%08X(sceneB8=%u sceneB9=%u sceneBC=0x%08X"
                " sceneC0=0x%08X scene1c=%u) fileSlots=0x%08X",
                self,
                actor9c,
                actora0,
                buttona4,
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, buttona4 + 0x20u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, buttona4 + 0x24u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, buttona4 + 0x1Cu)),
                selected,
                pointed,
                scene,
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, scene + 0xB8u)),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, scene + 0xB9u)),
                trace_save_read_u32_or_zero(memory, scene + 0xBCu),
                trace_save_read_u32_or_zero(memory, scene + 0xC0u),
                static_cast<unsigned>(
                    trace_save_read_u8_or_zero(memory, scene + 0x1Cu)),
                file_slots);
            append_button_pane_controller_state(
                target, target_size, "buttona4State", buttona4);
        };
    switch (function_address) {
    case 0x801785ECu:
    case 0x8017866Cu:
    case 0x80178A14u:
    case 0x80178E64u:
    case 0x80178F5Cu: {
        append_file_select_item(extra, sizeof(extra), "self", context->gpr[3]);
        break;
    }
    case 0x8017AB10u:
    case 0x8017AB94u:
    case 0x8017BA08u:
    case 0x8017BA84u:
    case 0x8017BB40u: {
        const std::uint32_t self = context->gpr[3];
        append_file_selector_state(extra, sizeof(extra), self);
        const std::uint32_t selected =
            trace_save_read_u32_or_zero(memory, self + 0xB4u);
        const std::uint32_t pointed =
            trace_save_read_u32_or_zero(memory, self + 0xC0u);
        append_file_select_item(extra, sizeof(extra), "selected", selected);
        append_file_select_item(extra, sizeof(extra), "pointed", pointed);
        append_file_select_item(extra, sizeof(extra), "r4item", context->gpr[4]);
        break;
    }
    case 0x8017C7B4u:
    case 0x8017C874u:
    case 0x8017C984u:
    case 0x8017CA08u:
    case 0x8017CA60u:
    case 0x8017CAC4u:
    case 0x8017CB1Cu:
    case 0x8017CD18u: {
        append_file_selector_state(extra, sizeof(extra), context->gpr[3]);
        break;
    }
    case 0x8034BA38u:
    case 0x8034BA9Cu:
    case 0x8034BAA4u:
    case 0x8034BAACu:
    case 0x8034C1A0u:
    case 0x8034C254u:
    case 0x8034C874u:
    case 0x8034C87Cu:
    case 0x8034C8E4u:
    case 0x8034C914u:
    case 0x80366244u: {
        std::uint32_t button = context->gpr[3];
        if (function_address == 0x8034C8E4u) {
            button = trace_save_read_u32_or_zero(memory, context->gpr[4]);
        } else if (function_address == 0x8034C914u) {
            button = context->gpr[31];
        }
        append_button_pane_controller_state(
            extra, sizeof(extra), "button", button);
        const std::size_t used = std::strlen(extra);
        if (used < sizeof(extra)) {
            std::snprintf(
                extra + used,
                sizeof(extra) - used,
                " targetArg=0x%08X targetArgRel=0x%08X",
                context->gpr[4],
                context->gpr[4] - context->gpr[13]);
        }
        break;
    }
    case 0x8036FB70u:
    case 0x8036FBC4u: {
        const std::uint32_t self = context->gpr[3];
        std::snprintf(
            extra,
            sizeof(extra),
            " miiSelect=0x%08X special=%u fav=%u normal=%u pageBase=%d selected=%d tex=0x%08X pageA=0x%08X pageB=0x%08X prohibit=%u selectedArg=%d texArg=0x%08X dummy=%u",
            self,
            static_cast<unsigned>(
                trace_file_select_read_u16_or_zero(memory, self + 0x2Cu)),
            static_cast<unsigned>(
                trace_file_select_read_u16_or_zero(memory, self + 0x2Eu)),
            static_cast<unsigned>(
                trace_file_select_read_u16_or_zero(memory, self + 0x58u)),
            static_cast<std::int32_t>(
                trace_save_read_u32_or_zero(memory, self + 0x1ECu)),
            static_cast<std::int32_t>(
                trace_save_read_u32_or_zero(memory, self + 0x1F0u)),
            trace_save_read_u32_or_zero(memory, self + 0x1F4u),
            trace_save_read_u32_or_zero(memory, self + 0x1F8u),
            trace_save_read_u32_or_zero(memory, self + 0x1FCu),
            static_cast<unsigned>(
                trace_save_read_u8_or_zero(memory, self + 0x200u)),
            static_cast<std::int32_t>(context->gpr[4]),
            context->gpr[5],
            function_address == 0x8036FBC4u ? 1u : 0u);
        break;
    }
    default:
        break;
    }

    char message[2048]{};
    std::snprintf(
        message,
        sizeof(message),
        "[file-select-function] entry=%s pc=0x%08X lr=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X%s",
        label,
        function_address,
        context->lr,
        context->gpr[3],
        context->gpr[4],
        context->gpr[5],
        context->gpr[6],
        extra);
    services->log(services->user, LogLevelV1::Warning, message);
}

GALAXY_ALWAYS_INLINE void trace_file_select_function_entry(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t function_address) {
    const bool trace_file_select = trace_file_select_function_entries_enabled();
    const bool trace_save_sequence =
        trace_save_sequence_function_entries_enabled();
    if ((!trace_file_select && !trace_save_sequence) ||
        services == nullptr || services->log == nullptr ||
        context == nullptr) {
        return;
    }
    if (trace_save_sequence) {
        trace_save_sequence_function_entry_slow(
            services, context, memory, function_address);
    }
    if (trace_file_select &&
        (trace_file_select_save_sequence_entries_enabled() ||
         !trace_file_select_is_save_sequence_address(function_address))) {
        trace_file_select_function_entry_slow(
            services, context, memory, function_address);
    }
}

inline bool audio_trace_overlaps(
    std::uint32_t offset,
    std::uint32_t size,
    std::uint32_t field_offset,
    std::uint32_t field_size) {
    return offset < field_offset + field_size &&
           field_offset < offset + size;
}

inline const char* audio_control_trace_field(
    std::uint32_t offset,
    std::uint32_t size) {
    if (audio_trace_overlaps(offset, size, 0x00u, 4u)) {
        return "mStatus";
    }
    if (audio_trace_overlaps(offset, size, 0x04u, 2u)) {
        return "mPriority";
    }
    if (audio_trace_overlaps(offset, size, 0x06u, 2u)) {
        return "field_0x06";
    }
    if (audio_trace_overlaps(offset, size, 0x08u, 4u)) {
        return "mFlags";
    }
    if (audio_trace_overlaps(offset, size, 0x0Cu, 4u)) {
        return "field_0x0C";
    }
    if (audio_trace_overlaps(offset, size, 0x10u, 4u)) {
        return "mCallback";
    }
    if (audio_trace_overlaps(offset, size, 0x14u, 4u)) {
        return "mCallbackData";
    }
    if (audio_trace_overlaps(offset, size, 0x18u, 4u)) {
        return "mChannel";
    }
    return nullptr;
}

inline const char* audio_tchannel_trace_field(
    std::uint32_t offset,
    std::uint32_t size) {
    if (audio_trace_overlaps(offset, size, 0x000u, 2u)) {
        return "active";
    }
    if (audio_trace_overlaps(offset, size, 0x002u, 2u)) {
        return "finished";
    }
    if (audio_trace_overlaps(offset, size, 0x004u, 2u)) {
        return "pitch";
    }
    if (audio_trace_overlaps(offset, size, 0x00Cu, 2u)) {
        return "pause";
    }
    if (offset < 0x040u && offset + size > 0x010u) {
        return "bus";
    }
    if (audio_trace_overlaps(offset, size, 0x064u, 2u)) {
        return "blockSamples";
    }
    if (audio_trace_overlaps(offset, size, 0x068u, 4u)) {
        return "position";
    }
    if (audio_trace_overlaps(offset, size, 0x100u, 2u)) {
        return "blockBytes";
    }
    if (audio_trace_overlaps(offset, size, 0x102u, 2u)) {
        return "loop";
    }
    if (audio_trace_overlaps(offset, size, 0x104u, 2u)) {
        return "adpcmHist1";
    }
    if (audio_trace_overlaps(offset, size, 0x106u, 2u)) {
        return "adpcmHist2";
    }
    if (audio_trace_overlaps(offset, size, 0x10Au, 2u)) {
        return "forced";
    }
    if (audio_trace_overlaps(offset, size, 0x110u, 4u)) {
        return "loopStart";
    }
    if (audio_trace_overlaps(offset, size, 0x114u, 4u)) {
        return "loopEnd";
    }
    if (audio_trace_overlaps(offset, size, 0x118u, 4u)) {
        return "source";
    }
    if (audio_trace_overlaps(offset, size, 0x11Cu, 4u)) {
        return "sampleCount";
    }
    return nullptr;
}

inline void trace_audio_control_write_line(
    const NativeServicesV1* services,
    const char* op,
    std::uint32_t guest_pc,
    std::uint32_t address,
    std::uint32_t size,
    std::uint64_t value,
    bool old_known,
    std::uint64_t old_value,
    const char* area,
    std::uint32_t index,
    std::uint32_t offset,
    const char* field) {
    static std::atomic<std::uint32_t> count{0};
    const std::uint32_t log_index = count.fetch_add(1);
    if (log_index >= 8192u) {
        return;
    }

    char message[256]{};
    if (old_known) {
        std::snprintf(
            message,
            sizeof(message),
            "[audio-control-write] %s pc=0x%08X addr=0x%08X size=%u value=0x%llX old=0x%llX target=%s[%u]+0x%03X field=%s",
            op,
            guest_pc,
            address,
            size,
            static_cast<unsigned long long>(value),
            static_cast<unsigned long long>(old_value),
            area,
            index,
            offset,
            field);
    } else {
        std::snprintf(
            message,
            sizeof(message),
            "[audio-control-write] %s pc=0x%08X addr=0x%08X size=%u value=0x%llX target=%s[%u]+0x%03X field=%s",
            op,
            guest_pc,
            address,
            size,
            static_cast<unsigned long long>(value),
            area,
            index,
            offset,
            field);
    }
    services->log(services->user, LogLevelV1::Warning, message);
}

inline void trace_audio_control_write(
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    const char* op,
    std::uint32_t guest_pc,
    std::uint32_t address,
    std::uint32_t size,
    std::uint64_t value) {
    if (!trace_audio_control_writes_enabled() ||
        services == nullptr || services->log == nullptr || size == 0) {
        return;
    }

    std::uint64_t old_value = 0;
    const bool old_known =
        trace_audio_read_be_fast(memory, address, size, old_value);

    if (address == 0x806A2BD0u && size == 4u) {
        trace_audio_control_write_line(
            services, op, guest_pc, address, size, value, old_known,
            old_value, "global", 0, 0, "JASDSPChannel::sDspChannels");
        return;
    }
    if (address == 0x806A2BD8u && size == 4u) {
        trace_audio_control_write_line(
            services, op, guest_pc, address, size, value, old_known,
            old_value, "global", 0, 0, "JASDsp::CH_BUF");
        return;
    }

    constexpr std::uint32_t kControlCount = 64;
    constexpr std::uint32_t kControlSize = 0x1Cu;
    constexpr std::uint32_t kTChannelCount = 64;
    constexpr std::uint32_t kTChannelSize = 0x180u;

    std::uint32_t control_pool = 0;
    std::uint32_t tchannel_table = 0;
    trace_audio_read_u32_fast(memory, 0x806A2BD0u, control_pool);
    trace_audio_read_u32_fast(memory, 0x806A2BD8u, tchannel_table);

    const auto in_range = [address, size](
                              std::uint32_t base,
                              std::uint32_t byte_count) {
        if (base == 0 || size > byte_count) {
            return false;
        }
        return address >= base && address - base <= byte_count - size;
    };

    if (in_range(control_pool, kControlCount * kControlSize)) {
        const std::uint32_t relative = address - control_pool;
        const std::uint32_t offset = relative % kControlSize;
        if (const char* field = audio_control_trace_field(offset, size)) {
            trace_audio_control_write_line(
                services, op, guest_pc, address, size, value, old_known,
                old_value, "JASDSPChannel", relative / kControlSize, offset,
                field);
        }
        return;
    }

    if (in_range(tchannel_table, kTChannelCount * kTChannelSize)) {
        const std::uint32_t relative = address - tchannel_table;
        const std::uint32_t offset = relative % kTChannelSize;
        if (const char* field = audio_tchannel_trace_field(offset, size)) {
            trace_audio_control_write_line(
                services, op, guest_pc, address, size, value, old_known,
                old_value, "JASDsp::TChannel",
                relative / kTChannelSize, offset, field);
        }
    }
}

inline void trace_audio_control_zero(
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t address,
    std::uint32_t size) {
    if (!trace_audio_control_writes_enabled() ||
        services == nullptr || services->log == nullptr || size == 0) {
        return;
    }

    std::uint32_t control_pool = 0;
    std::uint32_t tchannel_table = 0;
    trace_audio_read_u32_fast(memory, 0x806A2BD0u, control_pool);
    trace_audio_read_u32_fast(memory, 0x806A2BD8u, tchannel_table);

    constexpr std::uint32_t kControlBytes = 64u * 0x1Cu;
    constexpr std::uint32_t kTChannelBytes = 64u * 0x180u;
    const auto log_overlap = [services, guest_pc, address, size](
                                 const char* area,
                                 std::uint32_t base,
                                 std::uint32_t byte_count,
                                 std::uint32_t slot_size) {
        if (base == 0) {
            return;
        }
        const std::uint64_t clear_start = address;
        const std::uint64_t clear_end = clear_start + size;
        const std::uint64_t range_start = base;
        const std::uint64_t range_end =
            static_cast<std::uint64_t>(base) + byte_count;
        if (clear_start >= range_end || clear_end <= range_start) {
            return;
        }
        const std::uint64_t overlap_start =
            clear_start > range_start ? clear_start : range_start;
        const std::uint64_t overlap_end =
            clear_end < range_end ? clear_end : range_end;
        const std::uint32_t relative =
            static_cast<std::uint32_t>(overlap_start - range_start);
        trace_audio_control_write_line(
            services, "zero", guest_pc,
            static_cast<std::uint32_t>(overlap_start),
            static_cast<std::uint32_t>(overlap_end - overlap_start), 0,
            false, 0, area, relative / slot_size, relative % slot_size,
            "bulk-clear");
    };

    log_overlap("JASDSPChannel", control_pool, kControlBytes, 0x1Cu);
    log_overlap("JASDsp::TChannel", tchannel_table, kTChannelBytes, 0x180u);
}

[[noreturn]] inline void guest_memory_fault(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t address) {
    char message[64]{};
    std::snprintf(
        message,
        sizeof(message),
        "guest memory address 0x%08X is not mapped",
        static_cast<unsigned int>(address));
    if (services != nullptr && services->fatal != nullptr) {
        services->fatal(services->user, guest_pc, message);
    }
    std::abort();
}

[[noreturn]] inline void guest_execution_fault(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    const char* message) {
    if (services != nullptr && services->fatal != nullptr) {
        services->fatal(services->user, guest_pc, message);
    }
    std::abort();
}

// Admission for one fixed-width scalar guest access. `out` is written only when
// the span is admitted, so a caller's fast path can branch straight to its own
// store/load: the pointer it receives is already proven in range, and the
// caller never re-tests it.
//
// This is shared by generated scalar loads/stores and native span helpers.
// A static opcode or call-site census does not establish invocation frequency
// or exclusive frame cost; those require a matched runtime measurement.
//
// The accepted set is exactly
// `host_base != nullptr && offset + size <= region.size` with `offset =
// address & 0x0FFFFFFFu`, evaluated in 64 bits so the sum cannot wrap.
//
// Two rewrites of this test were tried and BOTH ARE WRONG; do not retry them:
//   * `size > region.size - offset` silently ACCEPTS spans with
//     `offset > region.size`, because the unsigned subtraction wraps to a value
//     `>= 0x8000_0000` while `size` is at most 64. Verified over 702
//     (offset, region.size, size) triples: 420 mismatches, every one of them a
//     false accept, i.e. an out-of-bounds host access.
//   * dropping `size <= region.size` as "implied by the offset test" has the
//     same failure mode through the same wrap.
// The predicate is a guard, not a hot arithmetic path: it must stay exact.
GALAXY_ALWAYS_INLINE bool guest_region_admit(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size,
    std::byte*& out) noexcept {
    if (memory == nullptr) GALAXY_UNLIKELY {
        return false;
    }
    const GuestMemoryFastRegionV1& region =
        memory->fast_regions[address >> 28];
    std::byte* const host_base = region.host_base;
    if (host_base == nullptr) GALAXY_UNLIKELY {
        return false;
    }
    const std::uint32_t offset = address & 0x0FFFFFFFu;
    if (static_cast<std::uint64_t>(offset) + size > region.size) GALAXY_UNLIKELY {
        return false;
    }
    out = host_base + offset;
    return true;
}

GALAXY_ALWAYS_INLINE std::byte* resolve_guest_fast(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size) {
    std::byte* direct = nullptr;
    return guest_region_admit(memory, address, size, direct) ? direct : nullptr;
}

inline std::byte* resolve_guest(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    // Native bulk/matrix/particle helpers use the same checked RAM capability
    // as scalar guest accesses. This is a span lookup, not a device access;
    // unmapped/partial spans retain the complete region search and fault path.
    // Empty spans keep the list's original one-past-region precedence.
    if (size != 0u) {
        if (std::byte* direct = resolve_guest_fast(memory, address, size)) return direct;
    }
    if (memory != nullptr && memory->regions != nullptr) {
        const std::uint64_t request_end = static_cast<std::uint64_t>(address) + size;
        for (std::uint32_t index = 0; index < memory->region_count; ++index) {
            const GuestMemoryRegionV1& region = memory->regions[index];
            const std::uint64_t region_end =
                static_cast<std::uint64_t>(region.guest_base) + region.size;
            if (address >= region.guest_base && request_end <= region_end &&
                region.host_base != nullptr) {
                return region.host_base + (address - region.guest_base);
            }
        }
    }
    guest_memory_fault(services, guest_pc, address);
}

// Cold tails of scalar guest accesses: device (MMIO) transfers, partial or
// aliased mappings and faults. Kept out of line so every translated load and
// store carries only the checked-RAM fast path instead of the device call and
// the complete region search; behaviour is unchanged.
template <typename T>
GALAXY_NOINLINE inline T guest_load_slow(
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::byte device_value[sizeof(T)]{};
    const std::byte* value =
        memory != nullptr && memory->read_device != nullptr &&
                memory->read_device(
                    memory->user, address, sizeof(T), device_value)
            ? device_value
            : resolve_guest(memory, address, sizeof(T), services, guest_pc);
    T result{};
    std::memcpy(&result, value, sizeof(result));
    if constexpr (sizeof(T) == 2u) {
        return byte_swap_u16(result);
    } else if constexpr (sizeof(T) == 4u) {
        return byte_swap_u32(result);
    } else if constexpr (sizeof(T) == 8u) {
        return byte_swap_u64(result);
    } else {
        return result;
    }
}

GALAXY_ALWAYS_INLINE std::uint8_t guest_load_u8(
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::byte* direct = nullptr;
    if (guest_region_admit(memory, address, 1, direct)) GALAXY_LIKELY {
        return static_cast<std::uint8_t>(*direct);
    }
    return guest_load_slow<std::uint8_t>(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE std::uint16_t guest_load_u16(
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::byte* direct = nullptr;
    if (guest_region_admit(memory, address, 2, direct)) GALAXY_LIKELY {
        std::uint16_t result{};
        std::memcpy(&result, direct, sizeof(result));
        return byte_swap_u16(result);
    }
    return guest_load_slow<std::uint16_t>(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE std::uint32_t guest_load_u32(
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::uint32_t result{};
    std::byte* direct = nullptr;
    if (guest_region_admit(memory, address, 4, direct)) GALAXY_LIKELY {
        std::memcpy(&result, direct, sizeof(result));
        result = byte_swap_u32(result);
    } else {
        result = guest_load_slow<std::uint32_t>(
            memory, address, services, guest_pc);
    }
    if (trace_fileloader_stack_enabled()) {
        trace_fileloader_stack_u32(
            services, "load32", guest_pc, address, result);
    }
    return result;
}

GALAXY_ALWAYS_INLINE std::uint64_t guest_load_u64(
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::byte* direct = nullptr;
    if (guest_region_admit(memory, address, 8, direct)) GALAXY_LIKELY {
        std::uint64_t result{};
        std::memcpy(&result, direct, sizeof(result));
        return byte_swap_u64(result);
    }
    return guest_load_slow<std::uint64_t>(memory, address, services, guest_pc);
}

// WiiCompiled GPLv3 flat-read adaptation at an explicit Galaxy RAM boundary.
// The host publishes this pointer only for the GuestMemoryV1 instance whose
// MEM1/MEM2 regions share the fixed 4-GiB view. A translated function can
// capture it once, then every admitted read bypasses the fast-region lookup.
GALAXY_ALWAYS_INLINE const std::byte* guest_flat_read_base(
    const GuestMemoryV1* memory) noexcept {
    return memory != nullptr &&
                   memory->struct_size >= sizeof(GuestMemoryV1)
        ? memory->flat_guest_read_base
        : nullptr;
}

GALAXY_ALWAYS_INLINE bool guest_flat_ram_span(
    std::uint32_t address,
    std::uint32_t width) noexcept {
    if (width == 0u) {
        return false;
    }
    const std::uint32_t alias = address & 0xF0000000u;
    const std::uint32_t offset = address & 0x0FFFFFFFu;
    const std::uint32_t size =
        alias == 0x00000000u || alias == 0x80000000u ||
                alias == 0xC0000000u
            ? 0x01800000u
            : alias == 0x10000000u || alias == 0x90000000u ||
                      alias == 0xD0000000u
                  ? 0x04000000u
                  : 0u;
    return size != 0u && width <= size && offset <= size - width;
}

template <typename T>
GALAXY_ALWAYS_INLINE T guest_flat_load_bytes(
    const std::byte* base,
    std::uint32_t address) noexcept {
    static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
    T value{};
    std::memcpy(&value, base + address, sizeof(value));
    if constexpr (sizeof(T) == 2u) {
        return byte_swap_u16(value);
    } else if constexpr (sizeof(T) == 4u) {
        return byte_swap_u32(value);
    } else if constexpr (sizeof(T) == 8u) {
        return byte_swap_u64(value);
    } else {
        static_assert(sizeof(T) == 1u);
        return value;
    }
}

GALAXY_ALWAYS_INLINE std::uint8_t guest_load_flat_or_checked_u8(
    const std::byte* flat_base,
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    return flat_base != nullptr && guest_flat_ram_span(address, 1u)
        ? guest_flat_load_bytes<std::uint8_t>(flat_base, address)
        : guest_load_u8(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE std::uint16_t guest_load_flat_or_checked_u16(
    const std::byte* flat_base,
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    return flat_base != nullptr && guest_flat_ram_span(address, 2u)
        ? guest_flat_load_bytes<std::uint16_t>(flat_base, address)
        : guest_load_u16(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE std::uint32_t guest_load_flat_or_checked_u32(
    const std::byte* flat_base,
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    return flat_base != nullptr && guest_flat_ram_span(address, 4u) &&
                   !trace_fileloader_stack_enabled()
        ? guest_flat_load_bytes<std::uint32_t>(flat_base, address)
        : guest_load_u32(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE std::uint64_t guest_load_flat_or_checked_u64(
    const std::byte* flat_base,
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    return flat_base != nullptr && guest_flat_ram_span(address, 8u)
        ? guest_flat_load_bytes<std::uint64_t>(flat_base, address)
        : guest_load_u64(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE bool guest_dirty_page_range_fast(
    std::uint32_t address,
    std::uint32_t size,
    std::uint32_t tracked_base,
    std::uint32_t tracked_size,
    std::uint32_t page_shift,
    std::uint32_t page_word_count,
    std::uint32_t* first_page,
    std::uint32_t* last_page,
    bool clip_to_tracker = false) {
    if (size == 0u || page_word_count == 0u || page_shift >= 32u ||
        first_page == nullptr || last_page == nullptr) {
        return false;
    }

    const std::uint64_t write_begin = address;
    const std::uint64_t write_last = write_begin + size - 1u;
    if (write_last >= UINT64_C(0x100000000)) {
        return false;
    }

    const std::uint32_t alias_tag = address & 0xE0000000u;
    if (alias_tag != 0u && alias_tag != 0x80000000u &&
        alias_tag != 0xC0000000u) {
        return false;
    }
    if (((address ^ static_cast<std::uint32_t>(write_last)) & 0xE0000000u) != 0u) {
        return false; // A span cannot cross physical/cached/uncached aliases.
    }
    const std::uint32_t physical_begin =
        static_cast<std::uint32_t>(address & 0x1FFFFFFFu);
    const std::uint32_t physical_last =
        static_cast<std::uint32_t>(
            static_cast<std::uint32_t>(write_last) & 0x1FFFFFFFu);
    if (physical_last < physical_begin) {
        return false;
    }

    const bool in_mem1 = physical_last < 0x01800000u;
    const bool in_mem2 =
        physical_begin >= 0x10000000u && physical_last < 0x14000000u;
    if (!in_mem1 && !in_mem2) {
        return false;
    }

    const std::uint64_t tracked_begin = tracked_base;
    const std::uint64_t tracked_end = tracked_begin + tracked_size;
    const std::uint64_t physical_end =
        static_cast<std::uint64_t>(physical_last) + 1u;
    if (!clip_to_tracker &&
        (physical_begin < tracked_begin || physical_end > tracked_end)) {
        return false;
    }
    const std::uint64_t overlap_begin = std::max<std::uint64_t>(physical_begin, tracked_begin);
    const std::uint64_t overlap_end = std::min(physical_end, tracked_end);
    if (overlap_begin >= overlap_end) return false;

    const auto first = static_cast<std::uint32_t>(
        (overlap_begin - tracked_begin) >> page_shift);
    const auto last = static_cast<std::uint32_t>(
        (overlap_end - 1u - tracked_begin) >> page_shift);
    if (last / 64u >= page_word_count) {
        return false;
    }
    *first_page = first;
    *last_page = last;
    return true;
}

// Multi-page bulk publication touches each bitmap word once. Keep this loop
// outside scalar-store machine code; the single-page case remains inlined.
// Readers cannot clear bits during CPU-owned publication. Other producers may
// add bits, so renderer words still require atomic OR on a missing mask.
template <typename Word>
GALAXY_NOINLINE inline void guest_mark_dirty_page_words(
    Word* words, std::uint32_t first_page, std::uint32_t last_page) {
    const auto first_word = first_page / 64u;
    const auto last_word = last_page / 64u;
    for (std::uint32_t word = first_word;; ++word) {
        const auto low = word == first_word ? first_page % 64u : 0u;
        const auto high = word == last_word ? last_page % 64u : 63u;
        const std::uint64_t mask = (UINT64_MAX << low) & (UINT64_MAX >> (63u - high));
        if constexpr (std::is_same_v<Word, std::atomic_uint64_t>) {
            if ((words[word].load(std::memory_order_relaxed) & mask) != mask)
                words[word].fetch_or(mask, std::memory_order_relaxed);
        } else {
            words[word] |= mask;
        }
        if (word == last_word) break;
    }
}

// Two-level publication for the renderer tracker. Besides the page bit in
// `words`, set a summary bit that covers the whole 64-bit page word, so the
// consumer can skip empty groups of words instead of reading all 131,072 of
// them. The bit is published unconditionally: it is only ever cleared by the
// frame drain, and only in the same word-group iteration in which that word's
// bits were exchanged, so a set bit always implies that some producer has
// published into the group since the last drain. The summary bitmap is reached
// through the tracker pointer itself. Legacy page-only producers are not
// compatible with summary skipping: ABI 23 rejects older game/Home modules.
GALAXY_ALWAYS_INLINE void guest_publish_dirty_word_summary(
    std::atomic_uint64_t* summary, std::uint32_t word_index) {
    if (summary == nullptr) {
        return;
    }
    std::atomic_uint64_t& group = summary[word_index / 64u];
    const std::uint64_t bit = 1ull << (word_index % 64u);
    // A summary bit stays set for the rest of the frame once published, so the
    // load hits an L1-resident word for every store after the first one into
    // that group and the locked OR is skipped. That keeps the steady-state
    // scalar store no more expensive than its page-bit publication beside it.
    if ((atomic_load_u64(&group) & bit) == 0u) {
        atomic_or_u64(&group, bit);
    }
}

// Rare span crossing a tracker boundary. Keep this out of scalar-store code
// and retain the full-coverage return value of the fast helpers: a partial
// mark must not suppress the original notify_write fallback. Each tracker
// owns its overlap independently, even when the other covers the whole write.
template <typename Word>
GALAXY_NOINLINE inline void guest_mark_partial_dirty_pages(
    Word* words, std::uint32_t address, std::uint32_t size,
    std::uint32_t base, std::uint32_t extent,
    std::uint32_t shift, std::uint32_t word_count) {
    std::uint32_t first = 0u, last = 0u;
    if (words == nullptr || !guest_dirty_page_range_fast(
            address, size, base, extent, shift, word_count, &first, &last, true)) return;
    guest_mark_dirty_page_words(words, first, last);
}

// Renderer-tracker partial span: same overlap rules, plus the summary bits.
GALAXY_NOINLINE inline void guest_mark_partial_dirty_pages_summarized(
    std::atomic_uint64_t* words, std::atomic_uint64_t* summary,
    std::uint32_t address, std::uint32_t size,
    std::uint32_t base, std::uint32_t extent,
    std::uint32_t shift, std::uint32_t word_count) {
    std::uint32_t first = 0u, last = 0u;
    if (words == nullptr || !guest_dirty_page_range_fast(
            address, size, base, extent, shift, word_count, &first, &last, true)) return;
    const std::uint32_t first_word = first / 64u;
    const std::uint32_t last_word = last / 64u;
    for (std::uint32_t word = first_word;; ++word) {
        const std::uint32_t low = word == first_word ? first % 64u : 0u;
        const std::uint32_t high = word == last_word ? last % 64u : 63u;
        const std::uint64_t mask =
            (UINT64_MAX << low) & (UINT64_MAX >> (63u - high));
        if ((words[word].load(std::memory_order_relaxed) & mask) != mask)
            words[word].fetch_or(mask, std::memory_order_relaxed);
        guest_publish_dirty_word_summary(summary, word);
        if (word == last_word) break;
    }
}

GALAXY_ALWAYS_INLINE bool guest_mark_dirty_page_fast(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size) {
    if (memory == nullptr || memory->dirty_page_words == nullptr) {
        return false;
    }
    std::uint32_t first_page = 0u;
    std::uint32_t last_page = 0u;
    if (!guest_dirty_page_range_fast(
            address,
            size,
            memory->dirty_tracked_base,
            memory->dirty_tracked_size,
            memory->dirty_page_shift,
            memory->dirty_page_word_count,
            &first_page,
            &last_page)) {
        guest_mark_partial_dirty_pages_summarized(
            memory->dirty_page_words, memory->dirty_word_summary,
            address, size,
            memory->dirty_tracked_base, memory->dirty_tracked_size,
            memory->dirty_page_shift, memory->dirty_page_word_count);
        return false;
    }

    if (first_page == last_page) [[likely]] {
        const std::uint32_t word = first_page / 64u;
        const std::uint64_t bit = 1ull << (first_page % 64u);
        auto& dirty_word = memory->dirty_page_words[word];
        // Translated stores and frame capture share the simulation thread.
        // No consumer can clear a previously observed bit in the middle of
        // this notification. A worker may add other bits, so the first mark
        // remains an atomic OR rather than a load/modify/store. Once marked,
        // avoid another locked instruction for every store to the same page.
        if ((atomic_load_u64(&dirty_word) & bit) == 0u) {
            atomic_or_u64(&dirty_word, bit);
        }
        // The summary bit must be re-armed even when the page bit is already
        // set: the drain clears summary bits for every group it visits, so a
        // producer that skips this store could leave the group marked empty
        // while its word still holds bits published after the drain's read.
        guest_publish_dirty_word_summary(memory->dirty_word_summary, word);
    } else {
        guest_mark_dirty_page_words(memory->dirty_page_words, first_page, last_page);
        for (std::uint32_t word = first_page / 64u;; ++word) {
            guest_publish_dirty_word_summary(memory->dirty_word_summary, word);
            if (word == last_page / 64u) break;
        }
    }
    return true;
}
// Kept out of line on purpose: its remaining in-line work is the complete
// alias/window/tracker/page validation, and force-inlining it put ~90
// instruction slots and a stack-frame adjustment between every guest store and
// that store's exit. Callers reach it only after their own cheap reject, so the
// shared body is the only thing a scalar store carries.
inline bool guest_mark_cpu_dirty_page_fast(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size) {
    if (memory == nullptr || memory->cpu_dirty_page_words == nullptr) {
        return false;
    }
    // This tracker normally covers the pinned MEM2 audio backing. Most game
    // object/stack stores are outside it; reject those before repeating the
    // complete alias, extent, and page-table validation below.
    const std::uint32_t physical_address = address & 0x1FFFFFFFu;
    if (physical_address < memory->cpu_dirty_tracked_base ||
        physical_address - memory->cpu_dirty_tracked_base >=
            memory->cpu_dirty_tracked_size) {
        if (physical_address < memory->cpu_dirty_tracked_base &&
            static_cast<std::uint64_t>(physical_address) + size > memory->cpu_dirty_tracked_base) {
            guest_mark_partial_dirty_pages(memory->cpu_dirty_page_words, address, size,
                memory->cpu_dirty_tracked_base, memory->cpu_dirty_tracked_size,
                memory->cpu_dirty_page_shift, memory->cpu_dirty_page_word_count);
        }
        return false;
    }
    std::uint32_t first_page = 0u;
    std::uint32_t last_page = 0u;
    if (!guest_dirty_page_range_fast(
            address,
            size,
            memory->cpu_dirty_tracked_base,
            memory->cpu_dirty_tracked_size,
            memory->cpu_dirty_page_shift,
            memory->cpu_dirty_page_word_count,
            &first_page,
            &last_page)) {
        guest_mark_partial_dirty_pages(memory->cpu_dirty_page_words, address, size,
            memory->cpu_dirty_tracked_base, memory->cpu_dirty_tracked_size,
            memory->cpu_dirty_page_shift, memory->cpu_dirty_page_word_count);
        return false;
    }

    if (first_page == last_page) [[likely]] {
        memory->cpu_dirty_page_words[first_page / 64u] |=
            1ull << (first_page % 64u);
    } else {
        guest_mark_dirty_page_words(memory->cpu_dirty_page_words, first_page, last_page);
    }
    return true;
}

// Single-page admission for one tracker. Returns true, and the page index, only
// when `guest_dirty_page_range_fast` (clip_to_tracker == false) accepts the span
// and the span lies in one page; every other span returns false and takes the
// complete policy. The terms below restate that function one for one, so there is
// no sentinel: a caller reads `page` only when this returns true.
//   * a size outside 1..32, a shift of 32 or more or an empty bitmap admits
//     nothing (every caller sends 1..32 bytes; larger spans use the full policy);
//   * only alias tags 0x00000000, 0x80000000 and 0xC0000000 are valid;
//   * a span must not cross the 29-bit physical alias;
//   * the span lies in MEM1 [0, 0x01800000) or MEM2 [0x10000000, 0x14000000);
//   * the span lies inside the tracked extent (the sum is formed in 64 bits, so a
//     span starting below `tracked_base` cannot wrap back inside);
//   * the page index lies inside the bitmap (`page / 64 < page_word_count`).
GALAXY_ALWAYS_INLINE bool guest_tracker_single_page(
    std::uint32_t address,
    std::uint32_t size,
    std::uint32_t tracked_base,
    std::uint32_t tracked_size,
    std::uint32_t page_shift,
    std::uint32_t page_word_count,
    std::uint32_t& page) noexcept {
    if (size - 1u >= 32u || page_shift >= 32u || page_word_count == 0u) GALAXY_UNLIKELY {
        return false;
    }
    // Tag bits 31..29: 000, 100 and 110 are valid; 001, 010, 011, 101 and 111 are not.
    if ((address & 0x20000000u) != 0u || (address >> 30) == 1u) GALAXY_UNLIKELY {
        return false;
    }
    const std::uint32_t physical = address & 0x1FFFFFFFu;
    const std::uint32_t physical_last = physical + (size - 1u);
    if (physical_last > 0x1FFFFFFFu) GALAXY_UNLIKELY {
        return false;  // Crosses a physical/cached/uncached alias boundary.
    }
    const bool in_mem1 = physical_last < 0x01800000u;
    const bool in_mem2 = physical >= 0x10000000u && physical_last < 0x14000000u;
    if (!in_mem1 && !in_mem2) GALAXY_UNLIKELY {
        return false;
    }
    if (physical < tracked_base) GALAXY_UNLIKELY {
        return false;
    }
    const std::uint32_t relative = physical - tracked_base;
    const std::uint64_t relative_last = std::uint64_t{relative} + (size - 1u);
    if (relative_last >= tracked_size) GALAXY_UNLIKELY {
        return false;
    }
    const std::uint32_t first = relative >> page_shift;
    if (first != static_cast<std::uint32_t>(relative_last >> page_shift)) GALAXY_UNLIKELY {
        return false;  // Spans more than one page.
    }
    if ((first >> 6u) >= page_word_count) GALAXY_UNLIKELY {
        return false;
    }
    page = first;
    return true;
}

// Compatibility spelling for callers that only need the single-page answer:
// the page index, or 0xFFFFFFFF when the span is not admitted as one page. Both
// the sentinel and any admitted page are distinguishable (an admitted page is
// below 2^29). Currently unreferenced.
GALAXY_ALWAYS_INLINE std::uint32_t guest_single_page_dirty_index(
    std::uint32_t address,
    std::uint32_t size,
    std::uint32_t tracked_base,
    std::uint32_t tracked_size,
    std::uint32_t page_shift,
    std::uint32_t page_word_count) {
    std::uint32_t page = 0u;
    return guest_tracker_single_page(
               address, size, tracked_base, tracked_size, page_shift,
               page_word_count, page)
        ? page
        : 0xFFFFFFFFu;
}

// Same admission for the independent CPU tracker, read from its own geometry.
GALAXY_ALWAYS_INLINE bool guest_cpu_tracker_single_page(
    const GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size,
    std::uint32_t& page) noexcept {
    return guest_tracker_single_page(
        address,
        size,
        memory->cpu_dirty_tracked_base,
        memory->cpu_dirty_tracked_size,
        memory->cpu_dirty_page_shift,
        memory->cpu_dirty_page_word_count,
        page);
}

// Complete notification policy: both trackers, partial overlaps, multi-page
// spans and the host callback for writes no tracker fully covers. Kept out of
// line so scalar stores carry only the single-page shortcut below.
GALAXY_NOINLINE inline void guest_notify_write_general(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size) {
    const bool marked_shared =
        guest_mark_dirty_page_fast(memory, address, size);
    const bool marked_cpu =
        guest_mark_cpu_dirty_page_fast(memory, address, size);
    if (marked_shared || marked_cpu) {
        return;
    }

    if (memory->notify_write != nullptr) {
        memory->notify_write(memory->user, address, size);
    }
}

GALAXY_ALWAYS_INLINE void guest_notify_write(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size) {
    if (memory == nullptr || size == 0u) {
        return;
    }
    if (size <= 32u && memory->dirty_page_words != nullptr) [[likely]] {
        std::uint32_t shared_page = 0u;
        // `shared_page` is read only when the span is admitted as one page.
        if (guest_tracker_single_page(
                address, size,
                memory->dirty_tracked_base, memory->dirty_tracked_size,
                memory->dirty_page_shift, memory->dirty_page_word_count,
                shared_page)) GALAXY_LIKELY {
            const std::uint64_t bit = 1ull << (shared_page & 63u);
            auto& dirty_word = memory->dirty_page_words[shared_page >> 6u];
            if ((atomic_load_u64(&dirty_word) & bit) == 0u) {
                atomic_or_u64(&dirty_word, bit);
            }
            guest_publish_dirty_word_summary(memory->dirty_word_summary, shared_page >> 6u);
            // The renderer tracker fully covered this write, so the host
            // callback is suppressed. Only the independent CPU tracker (a small
            // MEM2 window) may still need its own overlap marked.
            if (memory->cpu_dirty_page_words == nullptr) [[likely]] {
                return;
            }
            // Cheap window reject first. The CPU tracker normally covers the
            // pinned MEM2 audio backing, so almost every MEM1/stack/object store
            // is outside it — and `guest_cpu_tracker_single_page` below would walk
            // its complete admission kernel only to be
            // discarded, after which `guest_mark_cpu_dirty_page_fast` would
            // re-derive this identical predicate as its own first six
            // instructions. This is the same test, on the same two fields, so it
            // admits and rejects exactly the same set; the only stores that now
            // enter the kernel are the ones whose result is used.
            //
            // The one case this must not swallow is a span that starts below the
            // tracked base and reaches into it: that overlaps the tracker and
            // still owns work, so it falls through to the complete policy rather
            // than returning here.
            const std::uint32_t cpu_physical = address & 0x1FFFFFFFu;
            if (cpu_physical < memory->cpu_dirty_tracked_base ||
                cpu_physical - memory->cpu_dirty_tracked_base >=
                    memory->cpu_dirty_tracked_size) {
                if (cpu_physical < memory->cpu_dirty_tracked_base &&
                    static_cast<std::uint64_t>(cpu_physical) + size >
                        memory->cpu_dirty_tracked_base) {
                    (void)guest_mark_cpu_dirty_page_fast(memory, address, size);
                    return;
                }
                // The shared tracker already covers the span (so the callback is
                // suppressed) and the CPU window is disjoint from it: nothing to mark.
                return;
            }
            // The same kernel rejects every MEM1 store and every store below the
            // ARAM window, and marks the same bit the complete check would.
            std::uint32_t cpu_page = 0u;
            if (guest_cpu_tracker_single_page(memory, address, size, cpu_page)) GALAXY_LIKELY {
                memory->cpu_dirty_page_words[cpu_page >> 6u] |=
                    1ull << (cpu_page & 63u);
                return;
            }
            (void)guest_mark_cpu_dirty_page_fast(memory, address, size);
            return;
        }
    }
    guest_notify_write_general(memory, address, size);
}

// Two adjacent 4-byte stores that are known to land in the same tracker page,
// published with one page lookup instead of two.
//
// This exists for the paired-single store path, whose fast path writes its two
// halves to `address` and `address + 4`. They share a tracker page unless the
// pair straddles a page boundary; the caller falls back to the two-call form in that
// case. Nothing else changes: the same page bit and the same summary bit are
// published that `guest_notify_write(memory, address, 4)` followed by
// `guest_notify_write(memory, address + 4, 4)` would publish, because the
// single-page shortcut is decided solely by the first address's page when the
// span stays inside that page.
//
// The caller must already have admitted the complete 8-byte span to this
// renderer tracker page. Tracker metadata remains owned by this CPU write;
// no observer callback may run between admission and publication.
GALAXY_ALWAYS_INLINE void guest_notify_write_pair_admitted(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t page) {
    const std::uint32_t bit_index = page & 63u;
    const std::uint64_t bit = 1ull << bit_index;
    auto& dirty_word = memory->dirty_page_words[page >> 6u];
    if ((atomic_load_u64(&dirty_word) & bit) == 0u) {
        atomic_or_u64(&dirty_word, bit);
    }
    guest_publish_dirty_word_summary(memory->dirty_word_summary, page >> 6u);
    // Both halves are inside the renderer tracker, so the host callback is
    // suppressed. Only the independent CPU tracker (a small MEM2 window) may
    // still need its own overlap, and only when either half reaches it.
    if (memory->cpu_dirty_page_words == nullptr) GALAXY_LIKELY {
        return;
    }
    // The complete pair was admitted to ordinary RAM above. Most pairs are
    // MEM1/stack writes, outside the independent MEM2 CPU tracker. Reject only
    // a disjoint span; a pair reaching into the window still needs the exact
    // per-lane partial-publication policy below.
    const std::uint32_t cpu_physical = address & 0x1FFFFFFFu;
    if (cpu_physical < memory->cpu_dirty_tracked_base) {
        if (static_cast<std::uint64_t>(cpu_physical) + 8u <=
            memory->cpu_dirty_tracked_base) {
            return;
        }
    } else if (cpu_physical - memory->cpu_dirty_tracked_base >=
               memory->cpu_dirty_tracked_size) {
        return;
    }
    // A fully admitted single CPU page for the 8-byte span covers both halves
    // with that one page. Anything else (partial overlap, a span past a
    // truncated bitmap, a wrapped or malformed window) keeps the two original
    // 4-byte marks: clipping the combined span is not equivalent when the
    // second half is rejected but the first is not.
    std::uint32_t cpu_page = 0u;
    if (guest_cpu_tracker_single_page(memory, address, 8u, cpu_page)) {
        memory->cpu_dirty_page_words[cpu_page >> 6u] |= 1ull << (cpu_page & 63u);
        return;
    }
    (void)guest_mark_cpu_dirty_page_fast(memory, address, 4u);
    (void)guest_mark_cpu_dirty_page_fast(memory, address + 4u, 4u);
}

GALAXY_ALWAYS_INLINE bool guest_notify_write_pair_same_page(
    GuestMemoryV1* memory,
    std::uint32_t address) {
    if (memory == nullptr || memory->dirty_page_words == nullptr) {
        return false;
    }
    // Same single-page admission as the scalar store path, over both lanes.
    std::uint32_t page = 0u;
    if (!guest_tracker_single_page(
            address, 8u, memory->dirty_tracked_base, memory->dirty_tracked_size,
            memory->dirty_page_shift, memory->dirty_page_word_count, page)) GALAXY_UNLIKELY {
        return false;
    }
    guest_notify_write_pair_admitted(memory, address, page);
    return true;
}

GALAXY_ALWAYS_INLINE bool guest_is_wgpipe_address(std::uint32_t address) {
    const std::uint32_t high = address & 0xFF000000u;
    return (high == 0x0C000000u || high == 0xCC000000u) &&
           (address & 0x00FFF000u) == 0x00008000u;
}

// The WGPIPE ownership trace is a guest-execution trace, exactly like
// trace_fileloader_stack_enabled / trace_audio_pointer_store_enabled /
// trace_audio_control_writes_enabled / trace_resource_status_writes_enabled
// above, so it carries the same compile-time fold. Without it this was the one
// gate in `guest_wgpipe_batch_requires_scalar_path()` that could NOT fold: the
// other three already compile to a literal `false` under the default
// GALAXY_GUEST_TRACE==0, leaving this one Meyers-singleton load as the entire
// surviving body of a predicate the translator emits at every proven WGPIPE
// batch (117 sites in the current generated corpus).
//
// The comment at the top of this trace block states the invariant this restores:
// "In a shipping build it must compile out entirely: the trace_*_enabled() gates
// sit on the hottest paths ... With GALAXY_GUEST_TRACE==0 (the default) the hot
// gates fold to a compile-time false so the compiler deletes the branch and the
// trace call outright." This one did not.
//
// Behaviour is unchanged in both configurations. With tracing compiled in
// (GALAXY_GUEST_TRACE=1) the env lookup and its meaning are byte-identical. With
// it compiled out, the ownership trace was already unreachable through this
// predicate - the runtime switch that actually drives the trace is
// `GuestAddressSpace::wgpipe_pe_ownership_trace_enabled_`, set from the same env
// var on the host side and untouched here.
inline bool trace_wgpipe_pe_ownership_enabled() {
#if GALAXY_GUEST_TRACE
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_WGPIPE_PE_OWNERSHIP") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
#else
    return false;
#endif
}

// A traced scalar store observes the instruction before its device write. The
// generated caller checks this once, before any side effect, and retains the
// original scalar trace/write interleaving whenever one of those observers is
// active. WGPIPE ownership tracing also observes individual host publications.
GALAXY_ALWAYS_INLINE bool guest_wgpipe_batch_requires_scalar_path() {
    return trace_fileloader_temp_writes_enabled() ||
           trace_u32_store_enabled() || trace_wgpipe_pe_ownership_enabled();
}

// Commit an install-time-proven WGPIPE run. The translator emits this only
// when local constant propagation proves the exact device address; ordinary
// dynamic object and stack stores never call it. Once dispatch begins there is
// no scalar retry: a host that violates the WGPIPE device contract fails closed
// rather than risking a duplicate partially-consumed command stream.
template <std::size_t ByteCount>
GALAXY_ALWAYS_INLINE void guest_write_wgpipe_bytes(
    GuestMemoryV1* memory,
    std::uint32_t address,
    const std::array<std::byte, ByteCount>& bytes,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_assert(ByteCount >= 2u);
    static_assert(ByteCount <= 64u);
    if (memory == nullptr || memory->write_device == nullptr) {
        guest_execution_fault(
            services, guest_pc, "proven WGPIPE batch has no device writer");
    }
    if (!memory->write_device(
            memory->user,
            address,
            static_cast<std::uint32_t>(ByteCount),
            bytes.data())) {
        guest_execution_fault(
            services, guest_pc, "proven WGPIPE batch was rejected by host");
    }
}

// Cold tail of a scalar store (see guest_load_slow): device write, otherwise a
// checked resolve (faulting when unmapped), byte copy and dirty notification.
// `Reversed` selects the little-endian byte order used by stwbrx/sthbrx.
template <typename T, bool Reversed = false>
GALAXY_NOINLINE inline void guest_store_slow(
    GuestMemoryV1* memory,
    std::uint32_t address,
    T value,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::byte output[sizeof(T)]{};
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        const std::size_t shift =
            (Reversed ? index : sizeof(T) - 1u - index) * 8u;
        output[index] = static_cast<std::byte>(value >> shift);
    }
    if (memory != nullptr && memory->write_device != nullptr &&
        memory->write_device(memory->user, address, sizeof(T), output)) {
        return;
    }
    std::byte* destination =
        resolve_guest(memory, address, sizeof(T), services, guest_pc);
    if constexpr (sizeof(T) == 4u && !Reversed) {
        if (trace_u32_store_enabled()) {
            trace_fileloader_stack_u32(
                services, "store32", guest_pc, address, value);
            trace_audio_pointer_store_u32(
                services, "store32", guest_pc, address, value);
            trace_audio_control_write(
                memory, services, "store32", guest_pc, address, 4, value);
            trace_resource_status_store_u32(
                memory, services, "store32", guest_pc, address, value);
        }
    }
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        destination[index] = output[index];
    }
    guest_notify_write(memory, address, sizeof(T));
}

GALAXY_ALWAYS_INLINE void guest_store_u8(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint8_t value,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    trace_fileloader_temp_write(
        memory, services, "store8", guest_pc, address, 1u, value);
    if (trace_audio_control_writes_enabled()) {
        trace_audio_control_write(
            memory, services, "store8", guest_pc, address, 1, value);
    }
    std::byte* direct = nullptr;
    if (guest_region_admit(memory, address, 1, direct)) GALAXY_LIKELY {
        *direct = static_cast<std::byte>(value);
        guest_notify_write(memory, address, 1);
        return;
    }
    guest_store_slow<std::uint8_t>(memory, address, value, services, guest_pc);
}

GALAXY_ALWAYS_INLINE void guest_store_u16(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint16_t value,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    trace_fileloader_temp_write(
        memory, services, "store16", guest_pc, address, 2u, value);
    if (trace_audio_control_writes_enabled()) {
        trace_audio_control_write(
            memory, services, "store16", guest_pc, address, 2, value);
    }
    std::byte* direct = nullptr;
    if (guest_region_admit(memory, address, 2, direct)) GALAXY_LIKELY {
        const std::uint16_t swapped = byte_swap_u16(value);
        std::memcpy(direct, &swapped, sizeof(swapped));
        guest_notify_write(memory, address, 2);
        return;
    }
    guest_store_slow<std::uint16_t>(memory, address, value, services, guest_pc);
}

GALAXY_ALWAYS_INLINE void guest_store_u32(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t value,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    trace_fileloader_temp_write(
        memory, services, "store32", guest_pc, address, 4u, value);
    std::byte* direct = nullptr;
    if (guest_region_admit(memory, address, 4, direct)) GALAXY_LIKELY {
        if (trace_u32_store_enabled()) {
            trace_fileloader_stack_u32(
                services, "store32", guest_pc, address, value);
            trace_audio_pointer_store_u32(
                services, "store32", guest_pc, address, value);
            trace_audio_control_write(
                memory, services, "store32", guest_pc, address, 4, value);
            trace_resource_status_store_u32(
                memory, services, "store32", guest_pc, address, value);
        }
        const std::uint32_t swapped = byte_swap_u32(value);
        std::memcpy(direct, &swapped, sizeof(swapped));
        guest_notify_write(memory, address, 4);
        return;
    }
    guest_store_slow<std::uint32_t>(memory, address, value, services, guest_pc);
}

GALAXY_ALWAYS_INLINE void guest_store_u16_reversed(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint16_t value,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::byte* direct = nullptr;
    if (guest_region_admit(memory, address, 2, direct)) GALAXY_LIKELY {
        std::memcpy(direct, &value, sizeof(value));
        guest_notify_write(memory, address, 2);
        return;
    }
    guest_store_slow<std::uint16_t, true>(
        memory, address, value, services, guest_pc);
}

GALAXY_ALWAYS_INLINE void guest_store_u32_reversed(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t value,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::byte* direct = nullptr;
    if (guest_region_admit(memory, address, 4, direct)) GALAXY_LIKELY {
        std::memcpy(direct, &value, sizeof(value));
        guest_notify_write(memory, address, 4);
        return;
    }
    guest_store_slow<std::uint32_t, true>(
        memory, address, value, services, guest_pc);
}

inline void guest_store_u64(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint64_t value,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    trace_fileloader_temp_write(
        memory, services, "store64", guest_pc, address, 8u, value);
    if (trace_audio_control_writes_enabled()) {
        trace_audio_control_write(
            memory, services, "store64", guest_pc, address, 8, value);
    }
    std::byte* direct = nullptr;
    if (guest_region_admit(memory, address, 8, direct)) GALAXY_LIKELY {
        const std::uint64_t swapped = byte_swap_u64(value);
        std::memcpy(direct, &swapped, sizeof(swapped));
        guest_notify_write(memory, address, 8);
        return;
    }
    guest_store_slow<std::uint64_t>(memory, address, value, services, guest_pc);
}

// Compare live host object ranges without overflowing an end pointer.
inline bool guest_host_spans_overlap(
    const void* first, std::size_t first_size,
    const void* second, std::size_t second_size) noexcept {
    if (first == nullptr || second == nullptr || first_size == 0u || second_size == 0u) {
        return false;
    }
    const auto a = reinterpret_cast<std::uintptr_t>(first);
    const auto b = reinterpret_cast<std::uintptr_t>(second);
    return a <= b ? b - a < first_size : a - b < second_size;
}

// Publication may touch bitmap storage but cannot rewrite the capability
// metadata used by subsequent words. Foreign host layouts use scalar policy.
inline bool guest_store_multiple_policy_is_stable(
    const PpcContext* context, const GuestMemoryV1* memory,
    const std::byte* direct, std::uint32_t bytes) noexcept {
    return !guest_host_spans_overlap(context, sizeof(*context), memory, sizeof(*memory)) &&
        !guest_host_spans_overlap(direct, bytes, memory, sizeof(*memory)) &&
        !guest_host_spans_overlap(memory->dirty_page_words,
            std::size_t{memory->dirty_page_word_count} * sizeof(std::atomic_uint64_t),
            memory, sizeof(*memory)) &&
        !guest_host_spans_overlap(memory->dirty_word_summary,
            std::size_t{memory->dirty_word_summary_count} * sizeof(std::atomic_uint64_t),
            memory, sizeof(*memory));
}

// A PPC lmw has no recognition boundary between words. Admit the complete
// RAM span once, but keep each load and context assignment ordered, including
// source/context aliasing. Incomplete spans retain partial effects and faults.
template <std::uint32_t FirstRegister>
inline void guest_load_multiple_gprs(
    PpcContext* context, GuestMemoryV1* memory, std::uint32_t address,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    static_assert(FirstRegister < 32u);
    constexpr std::uint32_t bytes = (32u - FirstRegister) * 4u;
    if constexpr (FirstRegister <= 16u) {
        if (!trace_fileloader_stack_enabled() && memory != nullptr &&
            !guest_host_spans_overlap(context, sizeof(*context), memory, sizeof(*memory))) {
            if (const std::byte* direct = resolve_guest_fast(memory, address, bytes)) {
                for (std::uint32_t reg = FirstRegister; reg < 32u; ++reg) {
                    std::uint32_t value;
                    std::memcpy(&value, direct + (reg - FirstRegister) * 4u, 4u);
                    context->gpr[reg] = byte_swap_u32(value);
                }
                return;
            }
        }
    }
    for (std::uint32_t reg = FirstRegister; reg < 32u; ++reg) {
        context->gpr[reg] = guest_load_u32(
            memory, address + (reg - FirstRegister) * 4u, services, guest_pc);
    }
}

// Preserve source reads, writes and publication after every word. The fast
// span needs complete shared coverage, stable policy metadata and no observer.
// CPU overlap, devices, partial trackers, wraps and small suffixes stay scalar.
template <std::uint32_t FirstRegister>
inline void guest_store_multiple_gprs(
    PpcContext* context, GuestMemoryV1* memory, std::uint32_t address,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    static_assert(FirstRegister < 32u);
    constexpr std::uint32_t bytes = (32u - FirstRegister) * 4u;
    if constexpr (FirstRegister <= 24u) {
        if (memory != nullptr && memory->dirty_page_words != nullptr &&
            !trace_fileloader_temp_writes_enabled() && !trace_u32_store_enabled()) {
            if (std::byte* direct = resolve_guest_fast(memory, address, bytes)) {
                const std::uint32_t physical = address & 0x1FFFFFFFu;
                const bool outside_cpu = memory->cpu_dirty_page_words == nullptr ||
                    std::uint64_t{physical} + bytes <= memory->cpu_dirty_tracked_base ||
                    physical >= std::uint64_t{memory->cpu_dirty_tracked_base} +
                        memory->cpu_dirty_tracked_size;
                std::uint32_t first_page = 0u, last_page = 0u;
                if (outside_cpu && memory->dirty_page_shift >= 2u &&
                    ((physical - memory->dirty_tracked_base) & 3u) == 0u &&
                    guest_store_multiple_policy_is_stable(context, memory, direct, bytes) &&
                    guest_dirty_page_range_fast(address, bytes,
                        memory->dirty_tracked_base, memory->dirty_tracked_size,
                        memory->dirty_page_shift, memory->dirty_page_word_count,
                        &first_page, &last_page) &&
                    (memory->dirty_word_summary == nullptr ||
                     (last_page >> 12u) < memory->dirty_word_summary_count)) {
                    const std::uint32_t relative = physical - memory->dirty_tracked_base;
                    for (std::uint32_t reg = FirstRegister; reg < 32u; ++reg) {
                        const std::uint32_t offset = (reg - FirstRegister) * 4u;
                        const std::uint32_t value = byte_swap_u32(context->gpr[reg]);
                        std::memcpy(direct + offset, &value, 4u);
                        const std::uint32_t page = (relative + offset) >> memory->dirty_page_shift;
                        const std::uint64_t bit = 1ull << (page & 63u);
                        auto& word = memory->dirty_page_words[page >> 6u];
                        if ((atomic_load_u64(&word) & bit) == 0u) atomic_or_u64(&word, bit);
                        guest_publish_dirty_word_summary(memory->dirty_word_summary, page >> 6u);
                    }
                    return;
                }
            }
        }
    }
    for (std::uint32_t reg = FirstRegister; reg < 32u; ++reg) {
        guest_store_u32(memory, address + (reg - FirstRegister) * 4u,
            context->gpr[reg], services, guest_pc);
    }
}

inline void guest_zero(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    if (trace_audio_control_writes_enabled()) {
        trace_audio_control_zero(memory, services, guest_pc, address, size);
    }
    std::byte* output = resolve_guest(memory, address, size, services, guest_pc);
    for (std::uint32_t index = 0; index < size; ++index) {
        output[index] = std::byte{0};
    }
    guest_notify_write(memory, address, size);
}

inline std::uint32_t narrow_f64_to_f32_bits(
    std::uint64_t bits,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);

inline void native_store_guest_u32_fast(
    std::byte* base,
    std::uint32_t offset,
    std::uint32_t value) {
    const std::uint32_t swapped = byte_swap_u32(value);
    std::memcpy(base + offset, &swapped, sizeof(swapped));
}

inline std::uint32_t native_load_guest_u32_fast(
    const std::byte* base,
    std::uint32_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, base + offset, sizeof(value));
    return byte_swap_u32(value);
}

inline float native_load_guest_f32_fast(
    const std::byte* base,
    std::uint32_t offset) {
    return std::bit_cast<float>(native_load_guest_u32_fast(base, offset));
}

inline void native_store_guest_f32_fast(
    std::byte* base,
    std::uint32_t offset,
    float value) {
    native_store_guest_u32_fast(base, offset, std::bit_cast<std::uint32_t>(value));
}

inline void full_memory_fence();
// The attribute must match the definitions below, or a caller reached through
// this forward declaration (e.g. `native_audio_interleave_i16_804878BC` at
// ~6228) would not be force-inlined.
GALAXY_ALWAYS_INLINE void compare_unsigned(
    PpcContext* context,
    std::uint32_t field_index,
    std::uint32_t left,
    std::uint32_t right);
GALAXY_ALWAYS_INLINE void compare_signed(
    PpcContext* context,
    std::uint32_t field_index,
    std::int32_t left,
    std::int32_t right);
GALAXY_ALWAYS_INLINE void compare_f64(
    PpcContext* context,
    std::uint32_t field_index,
    std::uint64_t left,
    std::uint64_t right,
    bool ordered);
GALAXY_ALWAYS_INLINE bool cr_bit(const PpcContext* context, std::uint32_t index);

struct NativeCacheRangeV1 {
    std::uint32_t start{};
    std::uint32_t line_count{};
    std::uint32_t byte_count{};
};

inline void native_cache_maintenance_post_system_call(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::uint32_t system_call_pc = 0;
    switch (guest_pc) {
        case 0x804A2F20u:
        case 0x804A2F44u:
            system_call_pc = 0x804A2F48u;
            break;
        case 0x804A2F50u:
        case 0x804A2F74u:
            system_call_pc = 0x804A2F78u;
            break;
        default:
            return;
    }

    if (services == nullptr || services->system_call == nullptr) {
        guest_execution_fault(
            services,
            system_call_pc,
            "native system-call service is unavailable");
    }
    services->system_call(
        services->user, system_call_pc, 0x44000002u, context, memory);
}

inline NativeCacheRangeV1 native_cache_range_enter(
    PpcContext* context,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t address = context->gpr[3];
    const std::uint32_t size = context->gpr[4];
    compare_unsigned(context, 0u, size, 0u);
    if (!cr_bit(context, 1u)) {
        return {};
    }

    const std::uint32_t line_offset = address & 0x1Fu;
    const std::uint32_t line_count = (size + line_offset + 0x1Fu) >> 5u;
    if (line_count == 0u) {
        guest_execution_fault(
            services,
            guest_pc,
            "cache-maintenance range wrapped the 32-bit address space");
    }

    context->gpr[5] = line_offset;
    context->gpr[4] = line_count;
    context->gpr[3] = address + line_count * 32u;
    context->ctr = 0u;
    return {
        address & 0xFFFFFFE0u,
        line_count,
        line_count * 32u,
    };
}

inline void native_cache_maintenance_range(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    (void)memory;
    const NativeCacheRangeV1 range =
        native_cache_range_enter(context, services, guest_pc);
    if (range.line_count != 0u) {
        full_memory_fence();
    }
    native_cache_maintenance_post_system_call(context, memory, services, guest_pc);
}

inline void native_cache_maintenance_loop_tail(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    (void)memory;
    if (context->ctr == 0u) {
        guest_execution_fault(
            services,
            guest_pc,
            "cache-maintenance loop resumed with zero CTR");
    }

    const std::uint32_t remaining_line_increments = context->ctr - 1u;
    if (remaining_line_increments != 0u) {
        context->gpr[3] += remaining_line_increments * 32u;
        full_memory_fence();
    }
    context->ctr = 0u;
    native_cache_maintenance_post_system_call(context, memory, services, guest_pc);
}

inline void native_memmove(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t size = context->gpr[5];
    if (size == 0u) {
        return;
    }
    const std::byte* src = resolve_guest(memory, context->gpr[4], size, services, guest_pc);
    std::byte* dst = resolve_guest(memory, context->gpr[3], size, services, guest_pc);
    std::memmove(dst, src, size);
    guest_notify_write(memory, context->gpr[3], size);
    trace_fileloader_temp_memmove(
        memory,
        services,
        "native_memmove",
        guest_pc,
        context->gpr[3],
        context->gpr[4],
        size);
}

// Invocation-local RAM capability for callback-free native byte decoders.
// A hit reads the CURRENT byte from the admitted backing; it never snapshots
// values, prefetches an unrequested byte or admits a future faulting span.
// Mappings are stable until the decoder's final/partial write notification.
class NativeGuestByteReader {
public:
    NativeGuestByteReader(GuestMemoryV1* memory, const NativeServicesV1* services,
        std::uint32_t guest_pc) : memory_(memory), services_(services), pc_(guest_pc) {}

    GALAXY_ALWAYS_INLINE std::uint8_t read(std::uint32_t address) {
        const auto offset = address - base_;
        if (offset < size_) GALAXY_LIKELY {
            return static_cast<std::uint8_t>(host_[offset]);
        }
        auto* const byte = resolve_guest(memory_, address, 1u, services_, pc_);
        // Only retain the first-precedence fixed fast mapping, bounded by its
        // high-nibble window. List mappings can overlap or have different next-
        // address precedence, so those keep their original per-byte lookup.
        if (memory_ != nullptr) {
            const auto& region = memory_->fast_regions[address >> 28u];
            const auto local = address & 0x0FFFFFFFu;
            if (region.host_base != nullptr && local < region.size) {
                base_ = address & 0xF0000000u;
                size_ = std::min(region.size, std::uint32_t{0x10000000u});
                host_ = region.host_base;
            }
        }
        return static_cast<std::uint8_t>(*byte);
    }
private:
    GuestMemoryV1* memory_;
    const NativeServicesV1* services_;
    std::uint32_t pc_;
    std::uint32_t base_ = 0u;
    std::uint32_t size_ = 0u;
    const std::byte* host_ = nullptr;
};

inline bool native_yaz0_decode_803989BC(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t source_address = context->gpr[3];
    const std::uint32_t dest_address = context->gpr[4];
    const std::uint32_t sda_base = context->gpr[13];

    const std::byte* header =
        resolve_guest(memory, source_address, 0x10u, services, guest_pc);
    if (header[0] != std::byte{'Y'} ||
        header[1] != std::byte{'a'} ||
        header[2] != std::byte{'z'} ||
        header[3] != std::byte{'0'}) {
        context->gpr[3] = 0u;
        return true;
    }

    const std::uint32_t refill_remaining =
        guest_load_u32(memory, sda_base + 0xFFFFD7A0u, services, guest_pc);
    if (refill_remaining != 0u) {
        return false;
    }

    const std::uint32_t output_size =
        guest_load_u32(memory, source_address + 0x04u, services, guest_pc);
    if (output_size == 0u) {
        context->gpr[3] = 1u;
        return true;
    }
    if (dest_address > std::numeric_limits<std::uint32_t>::max() - output_size) {
        guest_execution_fault(
            services,
            guest_pc,
            "native_yaz0_decode_803989BC destination range wraps");
    }

    std::byte* dest =
        resolve_guest(memory, dest_address, output_size, services, guest_pc);
    std::uint32_t source_cursor = source_address + 0x10u;
    std::uint32_t dest_cursor = 0u;
    std::uint32_t valid_bits = 0u;
    std::uint32_t code_byte = 0u;

    NativeGuestByteReader source_bytes(memory, services, guest_pc);
    const auto read_byte = [&](std::uint32_t address) -> std::uint8_t {
        return source_bytes.read(address);
    };

    while (dest_cursor < output_size) {
        if (valid_bits == 0u) {
            code_byte = read_byte(source_cursor++);
            valid_bits = 8u;
        }

        if ((code_byte & 0x80u) != 0u) {
            dest[dest_cursor++] = std::byte{read_byte(source_cursor++)};
        } else {
            const std::uint8_t byte1 = read_byte(source_cursor++);
            const std::uint8_t byte2 = read_byte(source_cursor++);
            const std::uint32_t distance =
                ((static_cast<std::uint32_t>(byte1) & 0x0Fu) << 8u) |
                static_cast<std::uint32_t>(byte2);
            std::uint32_t count = static_cast<std::uint32_t>(byte1 >> 4u);
            if (count == 0u) {
                count = static_cast<std::uint32_t>(read_byte(source_cursor++)) +
                    0x12u;
            } else {
                count += 2u;
            }
            std::uint32_t copy_address =
                dest_address + dest_cursor - distance - 1u;
            while (count != 0u && dest_cursor < output_size) {
                const std::uint8_t value =
                    copy_address >= dest_address &&
                        copy_address < dest_address + dest_cursor
                    ? static_cast<std::uint8_t>(
                          dest[copy_address - dest_address])
                    : read_byte(copy_address);
                dest[dest_cursor++] = std::byte{value};
                ++copy_address;
                --count;
            }
        }

        code_byte = (code_byte << 1u) & 0xFFu;
        --valid_bits;
    }

    guest_notify_write(memory, dest_address, output_size);
    context->gpr[3] = 1u;
    return true;
}

inline bool native_yaz0_decode_tail_803989BC(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t sda_base = context->gpr[13];
    const std::uint32_t refill_remaining =
        guest_load_u32(memory, sda_base + 0xFFFFD7A0u, services, guest_pc);
    const bool streaming_refill = refill_remaining != 0u;
    const std::uint32_t streaming_input_end = streaming_refill
        ? guest_load_u32(memory, sda_base + 0xFFFFD798u, services, guest_pc)
        : 0u;

    std::uint32_t source_cursor = context->gpr[3];
    const std::uint32_t dest_address = context->gpr[31];
    const std::uint32_t dest_end = context->gpr[30];
    if (dest_address >= dest_end) {
        return false;
    }
    const std::uint32_t output_size = dest_end - dest_address;
    std::byte* dest =
        resolve_guest(memory, dest_address, output_size, services, guest_pc);
    std::uint32_t dest_cursor = 0u;
    std::uint32_t valid_bits = context->gpr[7];
    std::uint32_t code_byte = context->gpr[8] & 0xFFu;
    if (valid_bits > 8u) {
        guest_execution_fault(
            services,
            guest_pc,
            "native_yaz0_decode_tail_803989BC invalid bit count");
    }

    NativeGuestByteReader source_bytes(memory, services, guest_pc);
    const auto read_byte = [&](std::uint32_t address) -> std::uint8_t {
        return source_bytes.read(address);
    };

    const auto can_read_compressed = [&](std::uint32_t count) -> bool {
        if (!streaming_refill) {
            return true;
        }
        return source_cursor <= streaming_input_end &&
            count <= streaming_input_end - source_cursor;
    };

    const auto finish_partial = [&] {
        if (dest_cursor != 0u) {
            guest_notify_write(memory, dest_address, dest_cursor);
        }
        context->gpr[3] = source_cursor;
        context->gpr[31] = dest_address + dest_cursor;
        context->gpr[7] = valid_bits;
        context->gpr[8] = code_byte;
        compare_unsigned(context, 0u, context->gpr[31], dest_end);
    };

    while (dest_cursor < output_size) {
        if (valid_bits == 0u) {
            if (!can_read_compressed(1u)) {
                finish_partial();
                return false;
            }
            code_byte = read_byte(source_cursor++);
            valid_bits = 8u;
        }

        if ((code_byte & 0x80u) != 0u) {
            if (!can_read_compressed(1u)) {
                finish_partial();
                return false;
            }
            dest[dest_cursor++] = std::byte{read_byte(source_cursor++)};
        } else {
            if (!can_read_compressed(2u)) {
                finish_partial();
                return false;
            }
            const std::uint8_t byte1 = read_byte(source_cursor);
            const std::uint8_t byte2 = read_byte(source_cursor + 1u);
            const std::uint32_t distance =
                ((static_cast<std::uint32_t>(byte1) & 0x0Fu) << 8u) |
                static_cast<std::uint32_t>(byte2);
            std::uint32_t count = static_cast<std::uint32_t>(byte1 >> 4u);
            if (count == 0u) {
                if (!can_read_compressed(3u)) {
                    finish_partial();
                    return false;
                }
                count = static_cast<std::uint32_t>(
                    read_byte(source_cursor + 2u)) + 0x12u;
                source_cursor += 3u;
            } else {
                count += 2u;
                source_cursor += 2u;
            }
            std::uint32_t copy_address =
                dest_address + dest_cursor - distance - 1u;
            while (count != 0u && dest_cursor < output_size) {
                const std::uint8_t value =
                    copy_address >= dest_address &&
                        copy_address < dest_address + dest_cursor
                    ? static_cast<std::uint8_t>(
                          dest[copy_address - dest_address])
                    : read_byte(copy_address);
                dest[dest_cursor++] = std::byte{value};
                ++copy_address;
                --count;
            }
        }

        code_byte = (code_byte << 1u) & 0xFFu;
        --valid_bits;
    }

    guest_notify_write(memory, dest_address, output_size);
    context->gpr[3] = source_cursor;
    context->gpr[31] = dest_end;
    context->gpr[7] = valid_bits;
    context->gpr[8] = code_byte;
    return true;
}

inline void call_guest(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t caller_pc);

GALAXY_ALWAYS_INLINE void call_guest_cached(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    NativeGameFunction* cached_function,
    PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t caller_pc);

GALAXY_ALWAYS_INLINE void branch_checkpoint(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    PpcContext* context,
    GuestMemoryV1* memory);

GALAXY_ALWAYS_INLINE void branch_checkpoint_taken(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t resume_pc,
    PpcContext* context,
    GuestMemoryV1* memory);

// A completed linked call returns at an architectural instruction boundary
// even when the statically compiled callee contains no backward branch of its
// own. Observe only already-published hardware events here; periodic/idle
// policy remains owned by the ordinary branch checkpoints. This prevents a
// long straight-line native call chain from starving AI/VI/input deadlines
// without turning every call into an unconditional host callback.
GALAXY_ALWAYS_INLINE void call_return_checkpoint(
    const NativeServicesV1* services,
    std::uint32_t call_pc,
    std::uint32_t return_pc,
    PpcContext* context,
    GuestMemoryV1* memory);

// Statically emitted only before the five RMGE01 WPADRead memmove calls.  This
// is a diagnostic observation, not a scheduler boundary: in a normal build it
// folds to a single disabled trace-flag check, and in a proof run it invokes
// the existing callback with a non-guest sentinel that the runtime consumes
// before any guest-visible service work.
GALAXY_ALWAYS_INLINE void native_input_trace_copy_pre_checkpoint(
    const NativeServicesV1* services,
    std::uint32_t call_pc,
    PpcContext* context,
    GuestMemoryV1* memory);

GALAXY_ALWAYS_INLINE void update_cr0(PpcContext* context, std::uint32_t value);

inline void native_yaz0_decode_or_fallback_803989BC(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t refill_remaining =
        guest_load_u32(memory, context->gpr[13] + 0xFFFFD7A0u, services, guest_pc);
    if (refill_remaining != 0u) {
        call_guest(services, 0x803989BCu, context, memory, guest_pc);
        return;
    }
    if (!native_yaz0_decode_803989BC(context, memory, services, guest_pc)) {
        guest_execution_fault(
            services,
            guest_pc,
            "native_yaz0_decode_803989BC failed on non-streaming input");
    }
}

inline void native_memset_80004388(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t dst_addr = context->gpr[3];
    const std::uint32_t size = context->gpr[5];
    if (size == 0u) {
        return;
    }
    std::byte* dst = resolve_guest(memory, dst_addr, size, services, guest_pc);
    std::memset(dst, static_cast<int>(context->gpr[4] & 0xFFu), size);
    guest_notify_write(memory, dst_addr, size);
}

inline void native_audio_interleave_i16_804878BC(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    trace_audio_function_entry(services, context, memory, guest_pc);
    const std::uint32_t count = context->gpr[6];
    context->ctr = count;
    compare_signed(context, 0u, static_cast<std::int32_t>(count), 0);
    if (count == 0u) {
        return;
    }
#if !GALAXY_GUEST_TRACE
    const auto read_be_i16 = [](const std::byte* bytes) {
        const std::uint16_t value =
            (static_cast<std::uint16_t>(
                 std::to_integer<std::uint8_t>(bytes[0])) << 8u) |
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(bytes[1]));
        return static_cast<std::uint32_t>(
            static_cast<std::int32_t>(static_cast<std::int16_t>(value)));
    };
    if (count <= (std::numeric_limits<std::uint32_t>::max)() / 4u) {
        const std::uint32_t source_bytes = count * 2u;
        const std::uint32_t destination_bytes = count * 4u;
        const std::byte* left = resolve_guest_fast(memory, context->gpr[3], source_bytes);
        const std::byte* right = resolve_guest_fast(memory, context->gpr[4], source_bytes);
        std::byte* dst = resolve_guest_fast(memory, context->gpr[5], destination_bytes);
        // Coalesce publication only when every scalar store would suppress
        // the external callback. The CPU owns both trackers until return;
        // renderer/DSP consumers receive staged bytes at their handoff.
        const auto fully_tracked = [&](bool cpu) {
            std::uint32_t first = 0u, last = 0u;
            if (cpu ? memory->cpu_dirty_page_words == nullptr : memory->dirty_page_words == nullptr)
                return false;
            return guest_dirty_page_range_fast(context->gpr[5], destination_bytes,
                cpu ? memory->cpu_dirty_tracked_base : memory->dirty_tracked_base,
                cpu ? memory->cpu_dirty_tracked_size : memory->dirty_tracked_size,
                cpu ? memory->cpu_dirty_page_shift : memory->dirty_page_shift,
                cpu ? memory->cpu_dirty_page_word_count : memory->dirty_page_word_count, &first, &last);
        };
        if (left != nullptr && right != nullptr && dst != nullptr &&
            (memory->notify_write == nullptr || fully_tracked(false) || fully_tracked(true))) {
            std::array<std::byte, 4> stereo{};
            for (std::uint32_t index = 0u; index < count; ++index) {
                const std::uint32_t source_offset = index * 2u;
                // The DOL loads both halfwords before either store. Keep the
                // loaded values even if output aliases either input stream.
                stereo = {left[source_offset], left[source_offset + 1u],
                    right[source_offset], right[source_offset + 1u]};
                std::memcpy(dst + index * 4u, stereo.data(), stereo.size());
            }
            guest_notify_write(memory, context->gpr[5], destination_bytes);
            context->gpr[6] = read_be_i16(stereo.data());
            context->gpr[0] = read_be_i16(stereo.data() + 2u);
            context->gpr[3] += source_bytes;
            context->gpr[4] += source_bytes;
            context->gpr[5] += destination_bytes;
            context->ctr = 0u;
            return;
        }
    }
#endif
    // Device, callback, partial mapping and wrap cases must execute the exact
    // ordered halfword transactions and retain state at the faulting PC.
    do {
        context->gpr[6] = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::int16_t>(guest_load_u16(memory, context->gpr[3], services, 0x804878C8u))));
        context->gpr[3] += 2u;
        context->gpr[0] = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::int16_t>(guest_load_u16(memory, context->gpr[4], services, 0x804878D0u))));
        context->gpr[4] += 2u;
        guest_store_u16(memory, context->gpr[5], static_cast<std::uint16_t>(context->gpr[6]), services, 0x804878D8u);
        guest_store_u16(memory, context->gpr[5] + 2u, static_cast<std::uint16_t>(context->gpr[0]), services, 0x804878DCu);
        context->gpr[5] += 4u;
    } while (--context->ctr != 0u);
}

inline void native_audio_ring_output_804945CC(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    trace_audio_function_entry(services, context, memory, guest_pc);

    const std::uint32_t frame = context->gpr[1] - 0x10u;
    guest_store_u32(memory, frame, context->gpr[1], services, 0x804945CCu);
    context->gpr[1] = frame;
    const std::uint32_t saved_lr = context->lr;
    const std::uint32_t saved_r31 = context->gpr[31];
    const std::uint32_t saved_r30 = context->gpr[30];
    guest_store_u32(memory, frame + 0x14u, saved_lr, services, 0x804945D4u);
    guest_store_u32(memory, frame + 0x0Cu, saved_r31, services, 0x804945D8u);
    guest_store_u32(memory, frame + 0x08u, saved_r30, services, 0x804945E0u);

    const std::uint32_t output_address = context->gpr[3];
    const std::uint32_t sample_count = context->gpr[4];
    if (sample_count > (std::numeric_limits<std::uint32_t>::max)() / 4u) {
        guest_execution_fault(
            services,
            guest_pc,
            "native audio ring sample count overflows address range");
    }

    context->gpr[31] = sample_count;
    context->gpr[30] = output_address;

    const std::uint32_t current_index_addr = context->gpr[13] + 0xFFFFDF00u;
    const std::uint32_t read_index_addr = context->gpr[13] + 0xFFFFDEFCu;
    const std::uint32_t buffer_table_addr = context->gpr[13] + 0xFFFFDEF8u;
    const std::uint32_t buffer_count_addr = context->gpr[13] + 0xFFFF9388u;
    const std::uint32_t current_index =
        guest_load_u32(memory, current_index_addr, services, 0x804945E8u);
    const std::uint32_t buffer_count =
        guest_load_u8(memory, buffer_count_addr, services, 0x804945ECu);
    std::uint32_t next_index = current_index + 1u;
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(next_index),
        static_cast<std::int32_t>(buffer_count));
    if (cr_bit(context, 2u)) {
        next_index = 0u;
    }

    const std::uint32_t read_index =
        guest_load_u32(memory, read_index_addr, services, 0x80494600u);
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(next_index),
        static_cast<std::int32_t>(read_index));
    const bool ring_has_room = !cr_bit(context, 2u);
    compare_unsigned(context, 0u, buffer_count, 3u);
    const bool repeat_current_buffer = !ring_has_room && !cr_bit(context, 0u);

    const std::uint32_t buffer_table =
        guest_load_u32(memory, buffer_table_addr, services, 0x80494614u);
    const std::uint32_t mono_bytes = sample_count * 2u;
    const std::uint32_t stereo_bytes = sample_count * 4u;

    if (repeat_current_buffer) {
        const std::uint32_t current_buffer =
            guest_load_u32(
                memory,
                buffer_table + ((current_index << 2u) & 0xFFFFFFFCu),
                services,
                0x80494624u);
        if (stereo_bytes != 0u) {
            std::byte* buffer =
                resolve_guest(memory, current_buffer, stereo_bytes, services, guest_pc);
            const std::uint16_t first_sample =
                guest_load_u16(
                    memory,
                    current_buffer - 2u + (sample_count & 0xFFFFFFFEu),
                    services,
                    0x80494630u);
            const std::uint16_t second_sample =
                guest_load_u16(
                    memory,
                    current_buffer - 2u + ((sample_count << 1u) & 0xFFFFFFFEu),
                    services,
                    0x80494634u);
            const auto store_be_u16 = [](std::byte* dst, std::uint32_t offset,
                                         std::uint16_t value) {
                dst[offset + 0u] = static_cast<std::byte>(value >> 8u);
                dst[offset + 1u] = static_cast<std::byte>(value & 0xFFu);
            };
            for (std::uint32_t index = 0; index < sample_count; ++index) {
                store_be_u16(buffer, index * 2u, first_sample);
                store_be_u16(buffer, mono_bytes + index * 2u, second_sample);
            }
            guest_notify_write(memory, current_buffer, stereo_bytes);
            context->gpr[7] = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int16_t>(first_sample)));
            context->gpr[6] = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int16_t>(second_sample)));
        }
    } else {
        guest_store_u32(memory, current_index_addr, next_index, services, 0x80494694u);
        const std::uint32_t flush_buffer =
            guest_load_u32(
                memory,
                buffer_table + ((next_index << 2u) & 0xFFFFFFFCu),
                services,
                0x804946A4u);
        context->gpr[3] = flush_buffer;
        context->gpr[4] = stereo_bytes;
        native_cache_maintenance_range(context, memory, services, 0x804A2EF4u);
    }

    const std::uint32_t active_index =
        guest_load_u32(memory, current_index_addr, services, 0x804946ACu);
    const std::uint32_t active_buffer =
        guest_load_u32(
            memory,
            buffer_table + ((active_index << 2u) & 0xFFFFFFFCu),
            services,
            0x804946C4u);
    context->gpr[3] = active_buffer + mono_bytes;
    context->gpr[4] = active_buffer;
    context->gpr[5] = output_address;
    context->gpr[6] = sample_count;
    native_audio_interleave_i16_804878BC(context, memory, services, 0x804878BCu);

    context->gpr[0] = guest_load_u32(memory, frame + 0x14u, services, 0x804946D0u);
    context->gpr[31] = guest_load_u32(memory, frame + 0x0Cu, services, 0x804946D4u);
    context->gpr[30] = guest_load_u32(memory, frame + 0x08u, services, 0x804946D8u);
    context->lr = context->gpr[0];
    context->gpr[1] = frame + 0x10u;
}

inline void native_audio_voice_update_8049559C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    trace_audio_function_entry(services, context, memory, guest_pc);
    if (context->pc != 0x8049559Cu) [[unlikely]] {
        switch (context->pc) {
            case 0x804955D4u:
                goto label_804955D4;
            case 0x804955E0u:
                goto label_804955E0;
            case 0x804955ECu:
                goto label_804955EC;
            case 0x804955F8u:
                goto label_804955F8;
            default:
                guest_execution_fault(
                    services, context->pc, "invalid interior function entry");
        }
    }

    {
        const std::uint32_t frame = context->gpr[1] + 0xFFFFFFF0u;
        guest_store_u32(memory, frame, context->gpr[1], services, 0x8049559Cu);
        context->gpr[1] = frame;
    }
    context->gpr[0] = context->lr;
    guest_store_u32(
        memory, context->gpr[1] + 0x00000014u, context->gpr[0], services, 0x804955A4u);
    guest_store_u32(
        memory, context->gpr[1] + 0x0000000Cu, context->gpr[31], services, 0x804955A8u);
    context->gpr[31] = 0x00000000u;
    guest_store_u32(
        memory, context->gpr[1] + 0x00000008u, context->gpr[30], services, 0x804955B0u);
    context->gpr[30] = 0x00000000u;

label_804955B8:
    context->gpr[0] = std::rotl(context->gpr[30], 0) & 0x0000000Fu;
    update_cr0(context, context->gpr[0]);
    if (!cr_bit(context, 2u)) {
        goto label_804955D4;
    }
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[30]),
        static_cast<std::int32_t>(0x00000000u));
    if (cr_bit(context, 2u)) {
        goto label_804955D4;
    }
    context->gpr[0] = context->gpr[30] + 0xFFFFFFFFu;
    context->gpr[3] = std::rotl(context->gpr[0], 28) & 0x0FFFFFFFu;
    {
        context->lr = 0x804955D4u;
        static std::uint32_t cached_target_804955D0 = 0u;
        static NativeGameFunction cached_function_804955D0 = nullptr;
        call_guest_cached(
            services,
            0x80495EA0u,
            &cached_target_804955D0,
            &cached_function_804955D0,
            context,
            memory,
            0x804955D0u);
    }
label_804955D4:
    context->gpr[0] =
        guest_load_u32(memory, context->gpr[13] + 0xFFFFDF30u, services, 0x804955D4u);
    context->gpr[3] = context->gpr[0] + context->gpr[31];
    {
        context->lr = 0x804955E0u;
        static std::uint32_t cached_target_804955DC = 0u;
        static NativeGameFunction cached_function_804955DC = nullptr;
        call_guest_cached(
            services,
            0x80495354u,
            &cached_target_804955DC,
            &cached_function_804955DC,
            context,
            memory,
            0x804955DCu);
    }
label_804955E0:
    context->gpr[30] = context->gpr[30] + 0x00000001u;
    context->gpr[31] = context->gpr[31] + 0x0000001Cu;
    compare_unsigned(context, 0u, context->gpr[30], 0x00000040u);
label_804955EC:
    if (cr_bit(context, 0u)) {
        goto label_804955B8;
    }
    context->gpr[3] = 0x00000003u;
    {
        context->lr = 0x804955F8u;
        static std::uint32_t cached_target_804955F4 = 0u;
        static NativeGameFunction cached_function_804955F4 = nullptr;
        call_guest_cached(
            services,
            0x80495EA0u,
            &cached_target_804955F4,
            &cached_function_804955F4,
            context,
            memory,
            0x804955F4u);
    }
label_804955F8:
    context->gpr[0] =
        guest_load_u32(memory, context->gpr[1] + 0x00000014u, services, 0x804955F8u);
    context->gpr[31] =
        guest_load_u32(memory, context->gpr[1] + 0x0000000Cu, services, 0x804955FCu);
    context->gpr[30] =
        guest_load_u32(memory, context->gpr[1] + 0x00000008u, services, 0x80495600u);
    context->lr = context->gpr[0];
    context->gpr[1] = context->gpr[1] + 0x00000010u;
}

inline void native_audio_voice_flag_mask_80495C5C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    trace_audio_function_entry(services, context, memory, guest_pc);
    if (context->pc != 0x80495C5Cu) [[unlikely]] {
        switch (context->pc) {
            case 0x80495C90u:
                goto label_80495C90;
            default:
                guest_execution_fault(
                    services, context->pc, "invalid interior function entry");
        }
    }

    context->gpr[0] = std::rotl(context->gpr[3], 4) & 0xFFFFFFF0u;
    context->gpr[5] =
        guest_load_u32(memory, context->gpr[13] + 0xFFFFDF38u, services, 0x80495C60u);
    {
        const std::int64_t result =
            static_cast<std::int64_t>(static_cast<std::int32_t>(context->gpr[0])) *
            static_cast<std::int32_t>(0x00000180u);
        context->gpr[4] = static_cast<std::uint32_t>(result);
    }
    context->gpr[3] = 0x00000000u;
    context->gpr[0] = 0x00000010u;
    context->gpr[4] = context->gpr[5] + context->gpr[4];
    context->ctr = context->gpr[0];

label_80495C78:
    context->gpr[0] = guest_load_u16(memory, context->gpr[4], services, 0x80495C78u);
    context->gpr[3] = std::rotl(context->gpr[3], 1) & 0x0000FFFEu;
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[0]),
        static_cast<std::int32_t>(0x00000000u));
    if (!cr_bit(context, 2u)) {
        context->gpr[3] = context->gpr[3] | 0x00000001u;
    }
    context->gpr[4] = context->gpr[4] + 0x00000180u;
label_80495C90:
    context->ctr = context->ctr - 1u;
    if (context->ctr != 0u) {
        goto label_80495C78;
    }
}

inline void native_audio_message_loop_80494B2C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    trace_audio_function_entry(services, context, memory, guest_pc);
    if (context->pc != 0x80494B2Cu) [[unlikely]] {
        switch (context->pc) {
            case 0x80494B80u:
                goto label_80494B80;
            case 0x80494B8Cu:
                goto label_80494B8C;
            case 0x80494B90u:
                goto label_80494B90;
            case 0x80494B94u:
                goto label_80494B94;
            case 0x80494B98u:
                goto label_80494B98;
            case 0x80494BB0u:
                goto label_80494BB0;
            case 0x80494BB8u:
                goto label_80494BB8;
            case 0x80494BBCu:
                goto label_80494BBC;
            case 0x80494BD0u:
                goto label_80494BD0;
            case 0x80494BE8u:
                goto label_80494BE8;
            case 0x80494BF0u:
                goto label_80494BF0;
            case 0x80494C08u:
                goto label_80494C08;
            case 0x80494C10u:
                goto label_80494C10;
            case 0x80494C14u:
                goto label_80494C14;
            case 0x80494C18u:
                goto label_80494C18;
            case 0x80494C3Cu:
                goto label_80494C3C;
            case 0x80494C40u:
                goto label_80494C40;
            case 0x80494C50u:
                goto label_80494C50;
            case 0x80494C54u:
                goto label_80494C54;
            case 0x80494C5Cu:
                goto label_80494C5C;
            case 0x80494C64u:
                goto label_80494C64;
            case 0x80494C6Cu:
                goto label_80494C6C;
            default:
                guest_execution_fault(
                    services, context->pc, "invalid interior function entry");
        }
    }

    {
        const std::uint32_t frame = context->gpr[1] + 0xFFFFFFE0u;
        guest_store_u32(memory, frame, context->gpr[1], services, 0x80494B2Cu);
        context->gpr[1] = frame;
    }
    context->gpr[0] = context->lr;
    guest_store_u32(
        memory, context->gpr[1] + 0x00000024u, context->gpr[0], services, 0x80494B34u);
    guest_store_u32(
        memory, context->gpr[1] + 0x0000001Cu, context->gpr[31], services, 0x80494B38u);
    guest_store_u32(
        memory, context->gpr[1] + 0x00000018u, context->gpr[30], services, 0x80494B3Cu);
    context->gpr[30] = context->gpr[3] | context->gpr[3];
    context->gpr[3] = 0x00000004u;
    context->gpr[3] = context->gpr[3] | 0x00040000u;
    context->gqr[2] = context->gpr[3];
    context->gpr[3] = 0x00000005u;
    context->gpr[3] = context->gpr[3] | 0x00050000u;
    context->gqr[3] = context->gpr[3];
    context->gpr[3] = 0x00000006u;
    context->gpr[3] = context->gpr[3] | 0x00060000u;
    context->gqr[4] = context->gpr[3];
    context->gpr[3] = 0x00000007u;
    context->gpr[3] = context->gpr[3] | 0x00070000u;
    context->gqr[5] = context->gpr[3];
    context->gpr[3] = 0x80490000u;
    context->gpr[3] = context->gpr[3] + 0x00004C70u;
    {
        context->lr = 0x80494B80u;
        static std::uint32_t cached_target_80494B7C = 0u;
        static NativeGameFunction cached_function_80494B7C = nullptr;
        call_guest_cached(
            services,
            0x804941F8u,
            &cached_target_80494B7C,
            &cached_function_80494B7C,
            context,
            memory,
            0x80494B7Cu);
    }
label_80494B80:
    context->gpr[3] = 0x80490000u;
    context->gpr[3] = context->gpr[3] + 0x00004CC0u;
    {
        context->lr = 0x80494B8Cu;
        static std::uint32_t cached_target_80494B88 = 0u;
        static NativeGameFunction cached_function_80494B88 = nullptr;
        call_guest_cached(
            services,
            0x80495658u,
            &cached_target_80494B88,
            &cached_function_80494B88,
            context,
            memory,
            0x80494B88u);
    }
label_80494B8C:
    {
        context->lr = 0x80494B90u;
        static std::uint32_t cached_target_80494B8C = 0u;
        static NativeGameFunction cached_function_80494B8C = nullptr;
        call_guest_cached(
            services,
            0x804956F8u,
            &cached_target_80494B8C,
            &cached_function_80494B8C,
            context,
            memory,
            0x80494B8Cu);
    }
label_80494B90:
    {
        context->lr = 0x80494B94u;
        static std::uint32_t cached_target_80494B90 = 0u;
        static NativeGameFunction cached_function_80494B90 = nullptr;
        call_guest_cached(
            services,
            0x804950CCu,
            &cached_target_80494B90,
            &cached_function_80494B90,
            context,
            memory,
            0x80494B90u);
    }
label_80494B94:
    {
        context->lr = 0x80494B98u;
        static std::uint32_t cached_target_80494B94 = 0u;
        static NativeGameFunction cached_function_80494B94 = nullptr;
        call_guest_cached(
            services,
            0x804A80E8u,
            &cached_target_80494B94,
            &cached_function_80494B94,
            context,
            memory,
            0x80494B94u);
    }
label_80494B98:
    context->gpr[4] = 0x805F0000u;
    context->gpr[31] = context->gpr[3] | context->gpr[3];
    context->gpr[3] = context->gpr[4] + 0x00005BD4u;
    context->gpr[5] = 0x00000048u;
    context->gpr[4] = 0x0000010Cu;
    {
        context->lr = 0x80494BB0u;
        static std::uint32_t cached_target_80494BAC = 0u;
        static NativeGameFunction cached_function_80494BAC = nullptr;
        call_guest_cached(
            services,
            0x80488A74u,
            &cached_target_80494BAC,
            &cached_function_80494BAC,
            context,
            memory,
            0x80494BACu);
    }
label_80494BB0:
    context->gpr[3] = context->gpr[31] | context->gpr[31];
    {
        context->lr = 0x80494BB8u;
        static std::uint32_t cached_target_80494BB4 = 0u;
        static NativeGameFunction cached_function_80494BB4 = nullptr;
        call_guest_cached(
            services,
            0x804A8110u,
            &cached_target_80494BB4,
            &cached_function_80494BB4,
            context,
            memory,
            0x80494BB4u);
    }
label_80494BB8:
    {
        context->lr = 0x80494BBCu;
        static std::uint32_t cached_target_80494BB8 = 0u;
        static NativeGameFunction cached_function_80494BB8 = nullptr;
        call_guest_cached(
            services,
            0x8049434Cu,
            &cached_target_80494BB8,
            &cached_function_80494BB8,
            context,
            memory,
            0x80494BB8u);
    }
label_80494BBC:
    context->gpr[31] = 0x805E0000u;

label_80494BC0:
    context->gpr[3] = context->gpr[30] + 0x00000030u;
    context->gpr[4] = context->gpr[1] + 0x00000008u;
    context->gpr[5] = 0x00000001u;
    {
        context->lr = 0x80494BD0u;
        static std::uint32_t cached_target_80494BCC = 0u;
        static NativeGameFunction cached_function_80494BCC = nullptr;
        call_guest_cached(
            services,
            0x804A89ACu,
            &cached_target_80494BCC,
            &cached_function_80494BCC,
            context,
            memory,
            0x80494BCCu);
    }
label_80494BD0:
    context->gpr[0] =
        guest_load_u32(memory, context->gpr[1] + 0x00000008u, services, 0x80494BD0u);
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[0]),
        static_cast<std::int32_t>(0x00000001u));
    if (cr_bit(context, 2u)) {
        goto label_80494C1C;
    }
    if (!cr_bit(context, 0u)) {
        goto label_80494BEC;
    }
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[0]),
        static_cast<std::int32_t>(0x00000000u));
    if (!cr_bit(context, 0u)) {
        goto label_80494BF8;
    }
label_80494BE8:
    goto label_80494BC0;

label_80494BEC:
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[0]),
        static_cast<std::int32_t>(0x00000003u));
label_80494BF0:
    if (!cr_bit(context, 0u)) {
        goto label_80494BC0;
    }
    goto label_80494C60;

label_80494BF8:
    context->gpr[0] =
        guest_load_u8(memory, context->gpr[30] + 0x00000084u, services, 0x80494BF8u);
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[0]),
        static_cast<std::int32_t>(0x00000000u));
    if (cr_bit(context, 2u)) {
        goto label_80494C10;
    }
    {
        context->lr = 0x80494C08u;
        static std::uint32_t cached_target_80494C04 = 0u;
        static NativeGameFunction cached_function_80494C04 = nullptr;
        call_guest_cached(
            services,
            0x80494350u,
            &cached_target_80494C04,
            &cached_function_80494C04,
            context,
            memory,
            0x80494C04u);
    }
label_80494C08:
    context->gpr[3] = context->gpr[30] + 0x0000007Cu;
    {
        context->lr = 0x80494C10u;
        static std::uint32_t cached_target_80494C0C = 0u;
        static NativeGameFunction cached_function_80494C0C = nullptr;
        call_guest_cached(
            services,
            0x804ABFBCu,
            &cached_target_80494C0C,
            &cached_function_80494C0C,
            context,
            memory,
            0x80494C0Cu);
    }
label_80494C10:
    {
        context->lr = 0x80494C14u;
        static std::uint32_t cached_target_80494C10 = 0u;
        static NativeGameFunction cached_function_80494C10 = nullptr;
        call_guest_cached(
            services,
            0x80494390u,
            &cached_target_80494C10,
            &cached_function_80494C10,
            context,
            memory,
            0x80494C10u);
    }
label_80494C14:
    {
        context->lr = 0x80494C18u;
        static std::uint32_t cached_target_80494C14 = 0u;
        static NativeGameFunction cached_function_80494C14 = nullptr;
        call_guest_cached(
            services,
            0x80495DB4u,
            &cached_target_80494C14,
            &cached_function_80494C14,
            context,
            memory,
            0x80494C14u);
    }
label_80494C18:
    goto label_80494BC0;

label_80494C1C:
    context->gpr[4] =
        guest_load_u32(memory, context->gpr[13] + 0xFFFFDF28u, services, 0x80494C1Cu);
    context->gpr[0] = context->gpr[4] + 0xFFFFFFFFu;
    guest_store_u32(
        memory, context->gpr[13] + 0xFFFFDF28u, context->gpr[0], services, 0x80494C24u);
    context->gpr[0] =
        guest_load_u32(memory, context->gpr[13] + 0xFFFFDF28u, services, 0x80494C28u);
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[0]),
        static_cast<std::int32_t>(0x00000000u));
    if (!cr_bit(context, 2u)) {
        goto label_80494C44;
    }
    context->gpr[3] = 0x00000007u;
    {
        context->lr = 0x80494C3Cu;
        static std::uint32_t cached_target_80494C38 = 0u;
        static NativeGameFunction cached_function_80494C38 = nullptr;
        call_guest_cached(
            services,
            0x8048903Cu,
            &cached_target_80494C38,
            &cached_function_80494C38,
            context,
            memory,
            0x80494C38u);
    }
label_80494C3C:
    {
        context->lr = 0x80494C40u;
        static std::uint32_t cached_target_80494C3C = 0u;
        static NativeGameFunction cached_function_80494C3C = nullptr;
        call_guest_cached(
            services,
            0x804946E8u,
            &cached_target_80494C3C,
            &cached_function_80494C3C,
            context,
            memory,
            0x80494C3Cu);
    }
label_80494C40:
    goto label_80494BC0;

label_80494C44:
    context->gpr[4] = context->gpr[31] + 0xFFFF8410u;
    context->gpr[3] = 0x00000002u;
    {
        context->lr = 0x80494C50u;
        static std::uint32_t cached_target_80494C4C = 0u;
        static NativeGameFunction cached_function_80494C4C = nullptr;
        call_guest_cached(
            services,
            0x80489014u,
            &cached_target_80494C4C,
            &cached_function_80494C4C,
            context,
            memory,
            0x80494C4Cu);
    }
label_80494C50:
    {
        context->lr = 0x80494C54u;
        static std::uint32_t cached_target_80494C50 = 0u;
        static NativeGameFunction cached_function_80494C50 = nullptr;
        call_guest_cached(
            services,
            0x804944D0u,
            &cached_target_80494C50,
            &cached_function_80494C50,
            context,
            memory,
            0x80494C50u);
    }
label_80494C54:
    context->gpr[3] = 0x00000002u;
    {
        context->lr = 0x80494C5Cu;
        static std::uint32_t cached_target_80494C58 = 0u;
        static NativeGameFunction cached_function_80494C58 = nullptr;
        call_guest_cached(
            services,
            0x8048903Cu,
            &cached_target_80494C58,
            &cached_function_80494C58,
            context,
            memory,
            0x80494C58u);
    }
label_80494C5C:
    goto label_80494BC0;

label_80494C60:
    {
        context->lr = 0x80494C64u;
        static std::uint32_t cached_target_80494C60 = 0u;
        static NativeGameFunction cached_function_80494C60 = nullptr;
        call_guest_cached(
            services,
            0x80494350u,
            &cached_target_80494C60,
            &cached_function_80494C60,
            context,
            memory,
            0x80494C60u);
    }
label_80494C64:
    context->gpr[3] = 0x00000000u;
    {
        context->lr = 0x80494C6Cu;
        static std::uint32_t cached_target_80494C68 = 0u;
        static NativeGameFunction cached_function_80494C68 = nullptr;
        call_guest_cached(
            services,
            0x804AB6F4u,
            &cached_target_80494C68,
            &cached_function_80494C68,
            context,
            memory,
            0x80494C68u);
    }
label_80494C6C:
    goto label_80494BC0;
}

inline void native_jpa_resource_find_index_803A7494(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t table_owner = context->gpr[3];
    const std::uint32_t saved_lr = context->lr;
    context->gpr[3] = context->gpr[4];
    context->lr = 0x803A74B0u;
    call_guest(services, 0x803D3C90u, context, memory, 0x803A74ACu);
    context->lr = saved_lr;
    const std::uint32_t target_hash = context->gpr[3];
    const std::uint32_t count =
        guest_load_u32(memory, table_owner + 0x04u, services, guest_pc + 0x1Cu);
    const std::uint32_t entries =
        guest_load_u32(memory, table_owner, services, guest_pc + 0x34u);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint32_t entry = entries + index * 0x18u;
        if (guest_load_u32(memory, entry + 0x14u, services, guest_pc + 0x3Cu) ==
            target_hash) {
            context->gpr[3] = index;
            context->lr = saved_lr;
            return;
        }
    }
    context->gpr[3] = 0xFFFFFFFFu;
    context->lr = saved_lr;
}

inline void native_jpa_resource_manager_get_resource_80441400(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t table = context->gpr[3];
    const std::uint32_t id = context->gpr[4] & 0xFFFFu;
    const std::uint32_t count =
        guest_load_u16(memory, table + 0x0Eu, services, guest_pc);
    const std::uint32_t entries =
        guest_load_u32(memory, table + 0x04u, services, guest_pc + 0x0Cu);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint32_t entry =
            guest_load_u32(memory, entries + index * 4u, services, guest_pc + 0x14u);
        if (guest_load_u16(memory, entry + 0x3Cu, services, guest_pc + 0x18u) ==
            id) {
            context->gpr[3] = entry;
            return;
        }
    }
    context->gpr[3] = 0u;
}

inline void native_resource_entry_count_8040FC80(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t resource = context->gpr[3];
    const std::uint32_t owner =
        guest_load_u32(memory, resource + 0x44u, services, guest_pc);
    const std::uint32_t count =
        guest_load_u32(memory, owner + 0x08u, services, guest_pc + 0x0Cu);
    const std::uint32_t entries =
        guest_load_u32(memory, resource + 0x4Cu, services, guest_pc + 0x1Cu);

    std::uint32_t visible = 0u;
    std::uint32_t offset = 0u;
    std::uint32_t current_entry = owner;
    std::uint32_t flag_bit = count;
    for (std::uint32_t index = 0; index < count; ++index) {
        current_entry = entries + offset;
        const std::uint32_t flags =
            guest_load_u32(memory, current_entry + 0x04u, services, guest_pc + 0x24u);
        flag_bit = std::rotl(flags, 8) & 0x00000001u;
        if (flag_bit == 0u) {
            ++visible;
        }
        offset += 0x14u;
    }
    context->gpr[0] = flag_bit;
    context->gpr[3] = visible;
    context->gpr[4] = offset;
    context->gpr[5] = current_entry;
    context->gpr[6] = visible;
    context->ctr = 0u;
}

GALAXY_ALWAYS_INLINE void ensure_fpu_available(const NativeServicesV1* services,
    std::uint32_t guest_pc, PpcContext* context, GuestMemoryV1* memory);
GALAXY_ALWAYS_INLINE void load_fpr_single(PpcContext* context, std::uint32_t index,
    GuestMemoryV1* memory, std::uint32_t address,
    const NativeServicesV1* services, std::uint32_t guest_pc);
GALAXY_ALWAYS_INLINE void store_fpr_single(const PpcContext* context, std::uint32_t index,
    GuestMemoryV1* memory, std::uint32_t address,
    const NativeServicesV1* services, std::uint32_t guest_pc);
inline void psq_load(PpcContext* context, std::uint32_t target,
    GuestMemoryV1* memory, std::uint32_t address, std::uint32_t gqr_index,
    bool one_element, bool d_form, const NativeServicesV1* services,
    std::uint32_t guest_pc);
inline void psq_store(const PpcContext* context, std::uint32_t source,
    GuestMemoryV1* memory, std::uint32_t address, std::uint32_t gqr_index,
    bool one_element, bool d_form, const NativeServicesV1* services,
    std::uint32_t guest_pc);

inline void native_vec_copy_12(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    // These two DOL routines copy a vec3 through different volatile FPRs.
    // A raw memmove lost their FPR/PS1 effects, quantization and fault order.
    if (guest_pc == 0x80018B8Cu) {
        ensure_fpu_available(services, 0x80018B8Cu, context, memory);
        psq_load(context, 1u, memory, context->gpr[4], 0u, false, true, services, 0x80018B8Cu);
        ensure_fpu_available(services, 0x80018B90u, context, memory);
        load_fpr_single(context, 0u, memory, context->gpr[4] + 8u, services, 0x80018B90u);
        ensure_fpu_available(services, 0x80018B94u, context, memory);
        psq_store(context, 1u, memory, context->gpr[3], 0u, false, true, services, 0x80018B94u);
        ensure_fpu_available(services, 0x80018B98u, context, memory);
        store_fpr_single(context, 0u, memory, context->gpr[3] + 8u, services, 0x80018B98u);
        return;
    }
    if (guest_pc != 0x8001CF64u) {
        guest_execution_fault(services, guest_pc, "native vector copy has no verified entry");
    }
    for (std::uint32_t word = 0u; word < 3u; ++word) {
        const auto pc = 0x8001CF64u + word * 4u;
        ensure_fpu_available(services, pc, context, memory);
        load_fpr_single(context, 2u - word, memory, context->gpr[4] + word * 4u, services, pc);
    }
    for (std::uint32_t word = 0u; word < 3u; ++word) {
        const auto pc = 0x8001CF70u + word * 4u;
        ensure_fpu_available(services, pc, context, memory);
        store_fpr_single(context, 2u - word, memory, context->gpr[3] + word * 4u, services, pc);
    }
}

inline void native_add_12_80097278(
    PpcContext* context,
    GuestMemoryV1*,
    const NativeServicesV1*,
    std::uint32_t) {
    context->gpr[3] += 0x0Cu;
}

inline void native_strncmp_eq_16_80015394(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    const std::uint32_t lhs = context->gpr[3];
    const std::uint32_t rhs = context->gpr[4];
    bool equal = true;
    for (std::uint32_t i = 0; i < 16u; ++i) {
        const std::uint8_t a =
            guest_load_u8(memory, lhs + i, services, 0x8051E94Cu);
        const std::uint8_t b =
            guest_load_u8(memory, rhs + i, services, 0x8051E94Cu);
        if (a != b) {
            equal = false;
            break;
        }
        if (a == 0u) {
            break;
        }
    }
    context->gpr[3] = equal ? 1u : 0u;
}

inline void native_strlen_80516E80(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::uint32_t cursor = context->gpr[3];
    std::uint32_t length = 0u;
    while (guest_load_u8(memory, cursor, services, guest_pc + 0x08u) != 0u) {
        ++cursor;
        ++length;
    }
    context->gpr[0] = 0u;
    context->gpr[3] = length;
    context->gpr[4] = cursor;
    compare_signed(context, 0u, 0, 0);
}

inline void native_strcmp_8051E830(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::uint32_t lhs = context->gpr[3];
    std::uint32_t rhs = context->gpr[4];
    for (;;) {
        const std::uint32_t a =
            guest_load_u8(memory, lhs, services, guest_pc);
        const std::uint32_t b =
            guest_load_u8(memory, rhs, services, guest_pc);
        if (a != b || a == 0u) {
            context->gpr[3] = a - b;
            return;
        }
        ++lhs;
        ++rhs;
    }
}

inline void native_indexed_word_load_80344A54(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    context->gpr[0] = context->gpr[4] << 2u;
    context->gpr[3] =
        guest_load_u32(memory, context->gpr[3] + context->gpr[0], services, 0x80344A58u);
}

inline void native_particle_list_apply_8000C53C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    if (guest_pc != 0x8000C53Cu) [[unlikely]] {
        switch (guest_pc) {
        case 0x8000C578u:
            goto label_8000C578;
        case 0x8000C598u:
            goto label_8000C598;
        case 0x8000C5A0u:
            goto label_8000C5A0;
        default:
            guest_execution_fault(
                services, guest_pc, "invalid interior function entry");
        }
    }

    {
        const std::uint32_t frame = context->gpr[1] + 0xFFFFFFE0u;
        guest_store_u32(memory, frame, context->gpr[1], services, guest_pc);
        context->gpr[1] = frame;
    }
    context->gpr[0] = context->lr;
    guest_store_u32(memory, context->gpr[1] + 0x24u, context->gpr[0], services, 0x8000C544u);
    guest_store_u32(memory, context->gpr[1] + 0x1Cu, context->gpr[31], services, 0x8000C548u);
    guest_store_u32(memory, context->gpr[1] + 0x18u, context->gpr[30], services, 0x8000C54Cu);
    context->gpr[30] = context->gpr[3] | context->gpr[3];
    guest_store_u32(memory, context->gpr[1] + 0x14u, context->gpr[29], services, 0x8000C554u);
    context->gpr[29] = context->gpr[4] | context->gpr[4];

    context->gpr[0] = guest_load_u8(memory, context->gpr[3] + 0xB7u, services, 0x8000C55Cu);
    context->gpr[0] &= 0x00000001u;
    update_cr0(context, context->gpr[0]);
    if (cr_bit(context, 2u)) {
        goto label_8000C5A4;
    }

    context->gpr[12] = guest_load_u32(memory, context->gpr[3], services, 0x8000C568u);
    context->gpr[12] =
        guest_load_u32(memory, context->gpr[12] + 0x18u, services, 0x8000C56Cu);
    context->ctr = context->gpr[12];
    {
        const std::uint32_t branch_target = context->ctr & 0xFFFFFFFCu;
        context->lr = 0x8000C578u;
        static std::uint32_t cached_target_8000C574 = 0u;
        static NativeGameFunction cached_function_8000C574 = nullptr;
        call_guest_cached(
            services,
            branch_target,
            &cached_target_8000C574,
            &cached_function_8000C574,
            context,
            memory,
            0x8000C574u);
    }

label_8000C578:
    {
        const std::uint32_t list = context->gpr[30] + 0x14u;
        context->gpr[31] = guest_load_u32(memory, list, services, 0x8000C578u);
        context->gpr[30] = list;
    }
    goto label_8000C59C;

label_8000C580:
    context->gpr[12] =
        guest_load_u32(memory, context->gpr[31] + 0xFFFFFFFCu, services, 0x8000C580u);
    context->gpr[3] = context->gpr[31] + 0xFFFFFFFCu;
    context->gpr[4] = context->gpr[29] | context->gpr[29];
    context->gpr[12] =
        guest_load_u32(memory, context->gpr[12] + 0x14u, services, 0x8000C58Cu);
    context->ctr = context->gpr[12];
    {
        const std::uint32_t branch_target = context->ctr & 0xFFFFFFFCu;
        context->lr = 0x8000C598u;
        static std::uint32_t cached_target_8000C594 = 0u;
        static NativeGameFunction cached_function_8000C594 = nullptr;
        call_guest_cached(
            services,
            branch_target,
            &cached_target_8000C594,
            &cached_function_8000C594,
            context,
            memory,
            0x8000C594u);
    }

label_8000C598:
    context->gpr[31] = guest_load_u32(memory, context->gpr[31], services, 0x8000C598u);
label_8000C59C:
    compare_unsigned(context, 0u, context->gpr[31], context->gpr[30]);
label_8000C5A0:
    if (!cr_bit(context, 2u)) {
        branch_checkpoint_taken(
            services, 0x8000C5A0u, 0x8000C580u, context, memory);
        goto label_8000C580;
    }

label_8000C5A4:
    context->gpr[0] =
        guest_load_u32(memory, context->gpr[1] + 0x24u, services, 0x8000C5A4u);
    context->gpr[31] =
        guest_load_u32(memory, context->gpr[1] + 0x1Cu, services, 0x8000C5A8u);
    context->gpr[30] =
        guest_load_u32(memory, context->gpr[1] + 0x18u, services, 0x8000C5ACu);
    context->gpr[29] =
        guest_load_u32(memory, context->gpr[1] + 0x14u, services, 0x8000C5B0u);
    context->lr = context->gpr[0];
    context->gpr[1] = context->gpr[1] + 0x20u;
}

GALAXY_ALWAYS_INLINE void call_guest_cached(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    NativeGameFunction* cached_function,
    PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t caller_pc);

inline void native_noop_803A33AC(
    PpcContext*,
    GuestMemoryV1*,
    const NativeServicesV1*,
    std::uint32_t) {
}

inline void native_jpa_vec_copy_803A35DC(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    context->gpr[3] = context->gpr[5];
    context->gpr[4] = context->gpr[4] + 0x24u;
    native_vec_copy_12(context, memory, services, 0x8001CF64u);
}

inline void native_jpa_vec_copy_803A35E8(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    context->gpr[3] = context->gpr[5];
    context->gpr[4] = context->gpr[4] + 0x0Cu;
    native_vec_copy_12(context, memory, services, 0x8001CF64u);
}

inline void native_savegpr_805174FC(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    constexpr std::uint32_t kBase = 0x805174FCu;
    constexpr std::uint32_t kLast = 0x80517538u;
    std::uint32_t entry = guest_pc;
    if (entry < kBase || entry > kLast || ((entry - kBase) & 0x3u) != 0u) {
        entry = kBase;
    }
    context->pc = entry;

    const std::uint32_t first_reg = 14u + ((entry - kBase) / 4u);
    const std::uint32_t first_address =
        context->gpr[11] + 0xFFFFFF80u + first_reg * 4u;
    const std::uint32_t bytes = (32u - first_reg) * 4u;
    // This straight-line SDK helper has no interrupt recognition boundary.
    // Admit only a complete fast RAM span; faults, devices and scalar tracing
    // retain the ordered instruction path below, including partial stores.
    if (bytes <= 0x10000000u - (first_address & 0x0FFFFFFFu) &&
        !trace_fileloader_temp_writes_enabled() && !trace_u32_store_enabled()) {
        if (std::byte* direct = resolve_guest_fast(memory, first_address, bytes);
            direct != nullptr) {
            const std::uint32_t physical_begin = first_address & 0x1FFFFFFFu;
            const std::uint64_t physical_end =
                static_cast<std::uint64_t>(physical_begin) + bytes;
            const bool outside_cpu_tracker =
                memory->cpu_dirty_page_words == nullptr ||
                physical_end <= memory->cpu_dirty_tracked_base ||
                physical_begin >=
                    static_cast<std::uint64_t>(memory->cpu_dirty_tracked_base) +
                        memory->cpu_dirty_tracked_size;
            std::uint32_t first_page = 0u;
            std::uint32_t last_page = 0u;
            // Validate the entire tracker span once only when every word has
            // shared coverage and none has CPU coverage. No callback can then
            // observe or change tracker configuration during this helper.
            // Word alignment relative to the tracker and pages >= 4 bytes
            // ensure that each individual store occupies exactly one page.
            if (memory->dirty_page_words != nullptr && outside_cpu_tracker &&
                memory->dirty_page_shift >= 2u &&
                ((physical_begin - memory->dirty_tracked_base) & 3u) == 0u &&
                guest_dirty_page_range_fast(
                    first_address, bytes, memory->dirty_tracked_base,
                    memory->dirty_tracked_size, memory->dirty_page_shift,
                    memory->dirty_page_word_count, &first_page, &last_page)) {
                const std::uint32_t tracker_offset =
                    physical_begin - memory->dirty_tracked_base;
                for (std::uint32_t reg = first_reg; reg <= 31u; ++reg) {
                    const std::uint32_t offset = (reg - first_reg) * 4u;
                    const std::uint32_t swapped = byte_swap_u32(context->gpr[reg]);
                    std::memcpy(direct + offset, &swapped, 4u);
                    const std::uint32_t page =
                        (tracker_offset + offset) >> memory->dirty_page_shift;
                    const std::uint64_t bit = 1ull << (page % 64u);
                    auto& word = memory->dirty_page_words[page / 64u];
                    // Retain the original mark after each word, including the
                    // atomic first publication and concurrent worker bits.
                    if ((atomic_load_u64(&word) & bit) == 0u) {
                        atomic_or_u64(&word, bit);
                    }
                    guest_publish_dirty_word_summary(
                        memory->dirty_word_summary, page / 64u);
                }
                return;
            }
            for (std::uint32_t reg = first_reg; reg <= 31u; ++reg) {
                const std::uint32_t swapped = byte_swap_u32(context->gpr[reg]);
                std::memcpy(direct + (reg - first_reg) * 4u, &swapped, 4u);
                // Preserve per-word notification ordering and tracker coverage
                // even when a tracker covers only part of this RAM span.
                guest_notify_write(memory, first_address + (reg - first_reg) * 4u, 4u);
            }
            return;
        }
    }
    for (std::uint32_t reg = first_reg; reg <= 31u; ++reg) {
        const std::uint32_t pc = kBase + ((reg - 14u) * 4u);
        const std::uint32_t offset = 0xFFFFFF80u + (reg * 4u);
        guest_store_u32(
            memory,
            context->gpr[11] + offset,
            context->gpr[reg],
            services,
            pc);
    }
}

inline void native_restgpr_80517548(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    constexpr std::uint32_t kBase = 0x80517548u;
    constexpr std::uint32_t kLast = 0x80517584u;
    std::uint32_t entry = guest_pc;
    if (entry < kBase || entry > kLast || ((entry - kBase) & 0x3u) != 0u) {
        entry = kBase;
    }
    context->pc = entry;

    const std::uint32_t first_reg = 14u + ((entry - kBase) / 4u);
    const std::uint32_t first_address =
        context->gpr[11] + 0xFFFFFF80u + first_reg * 4u;
    const std::uint32_t bytes = (32u - first_reg) * 4u;
    // A failed span admission changes no registers. The scalar path preserves
    // the first faulting instruction and every preceding successful load.
    if (bytes <= 0x10000000u - (first_address & 0x0FFFFFFFu) &&
        !trace_fileloader_stack_enabled()) {
        if (const std::byte* direct = resolve_guest_fast(memory, first_address, bytes);
            direct != nullptr) {
            for (std::uint32_t reg = first_reg; reg <= 31u; ++reg) {
                std::uint32_t swapped;
                std::memcpy(&swapped, direct + (reg - first_reg) * 4u, 4u);
                context->gpr[reg] = byte_swap_u32(swapped);
            }
            return;
        }
    }
    for (std::uint32_t reg = first_reg; reg <= 31u; ++reg) {
        const std::uint32_t pc = kBase + ((reg - 14u) * 4u);
        const std::uint32_t offset = 0xFFFFFF80u + (reg * 4u);
        context->gpr[reg] =
            guest_load_u32(memory, context->gpr[11] + offset, services, pc);
    }
}

inline void native_callback_list_process_80488344(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);
    if (context->pc != 0x80488344u) [[unlikely]] {
        switch (context->pc) {
            case 0x80488358u:
                goto label_80488358;
            case 0x80488384u:
                goto label_80488384;
            case 0x804883A0u:
                goto label_804883A0;
            case 0x804883ACu:
                goto label_804883AC;
            default:
                guest_execution_fault(
                    services, context->pc, "invalid interior function entry");
        }
    }

    {
        const std::uint32_t frame = context->gpr[1] + 0xFFFFFFE0u;
        guest_store_u32(memory, frame, context->gpr[1], services, 0x80488344u);
        context->gpr[1] = frame;
    }
    context->gpr[0] = context->lr;
    guest_store_u32(
        memory, context->gpr[1] + 0x00000024u, context->gpr[0], services, 0x8048834Cu);
    context->gpr[11] = context->gpr[1] + 0x00000020u;
    context->lr = 0x80488358u;
    native_savegpr_805174FC(context, memory, services, 0x80517530u);

label_80488358:
    context->gpr[28] = 0x00000000u;
    context->gpr[27] = context->gpr[3] | context->gpr[3];
    context->gpr[30] = context->gpr[28] | context->gpr[28];
    context->gpr[31] = 0x00000000u;

label_80488368:
    context->gpr[12] =
        guest_load_u32(memory, context->gpr[27] + context->gpr[31], services, 0x80488368u);
    context->gpr[29] = context->gpr[27] + context->gpr[31];
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[12]),
        static_cast<std::int32_t>(0x00000000u));
    if (cr_bit(context, 2u)) {
        goto label_80488394;
    }
    context->gpr[3] =
        guest_load_u32(memory, context->gpr[29] + 0x00000004u, services, 0x80488378u);
    context->ctr = context->gpr[12];
    {
        const std::uint32_t branch_target = context->ctr & 0xFFFFFFFCu;
        context->lr = 0x80488384u;
        static std::uint32_t cached_target_80488380 = 0u;
        static NativeGameFunction cached_function_80488380 = nullptr;
        call_guest_cached(
            services,
            branch_target,
            &cached_target_80488380,
            &cached_function_80488380,
            context,
            memory,
            0x80488380u);
    }

label_80488384:
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[3]),
        static_cast<std::int32_t>(0x00000000u));
    if (!cr_bit(context, 0u)) {
        goto label_80488394;
    }
    guest_store_u32(memory, context->gpr[29], context->gpr[30], services, 0x8048838Cu);
    guest_store_u32(
        memory, context->gpr[29] + 0x00000004u, context->gpr[30], services, 0x80488390u);

label_80488394:
    context->gpr[28] = context->gpr[28] + 0x00000001u;
    context->gpr[31] = context->gpr[31] + 0x00000008u;
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[28]),
        static_cast<std::int32_t>(0x00000020u));

label_804883A0:
    if (cr_bit(context, 0u)) {
        goto label_80488368;
    }
    context->gpr[11] = context->gpr[1] + 0x00000020u;
    context->lr = 0x804883ACu;
    native_restgpr_80517548(context, memory, services, 0x8051757Cu);

label_804883AC:
    context->gpr[0] =
        guest_load_u32(memory, context->gpr[1] + 0x00000024u, services, 0x804883ACu);
    context->lr = context->gpr[0];
    context->gpr[1] = context->gpr[1] + 0x00000020u;
}

inline void native_read_next_char_utf16_800072B4(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    context->gpr[5] = context->gpr[3];
    context->gpr[3] =
        guest_load_u32(memory, context->gpr[3], services, guest_pc + 4u);
    context->gpr[4] =
        guest_load_u32(memory, context->gpr[5], services, guest_pc + 8u);
    context->gpr[3] =
        guest_load_u16(memory, context->gpr[3], services, guest_pc + 0x0Cu);
    context->gpr[0] = context->gpr[4] + 2u;
    guest_store_u32(
        memory, context->gpr[5], context->gpr[0], services, guest_pc + 0x14u);
}

inline void native_resfont_get_char_width_80007D2C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t old_sp = context->gpr[1];
    const std::uint32_t frame = old_sp - 0x10u;
    guest_store_u32(memory, frame, old_sp, services, guest_pc);
    context->gpr[1] = frame;
    context->gpr[0] = context->lr;
    guest_store_u32(memory, frame + 0x14u, context->gpr[0], services, guest_pc + 8u);

    context->gpr[12] =
        guest_load_u32(memory, context->gpr[3], services, guest_pc + 0x0Cu);
    context->gpr[12] =
        guest_load_u32(memory, context->gpr[12] + 0x4Cu, services, guest_pc + 0x10u);
    context->ctr = context->gpr[12];
    context->lr = 0x80007D48u;
    static std::uint32_t cached_target_80007D44 = 0u;
    static NativeGameFunction cached_function_80007D44 = nullptr;
    call_guest_cached(
        services,
        context->ctr & 0xFFFFFFFCu,
        &cached_target_80007D44,
        &cached_function_80007D44,
        context,
        memory,
        0x80007D44u);

    context->gpr[0] = std::rotl(context->gpr[3], 8) & 0x000000FFu;
    guest_store_u8(
        memory,
        frame + 0x08u,
        static_cast<std::uint8_t>(context->gpr[0]),
        services,
        guest_pc + 0x20u);
    context->gpr[0] = std::rotl(context->gpr[3], 16) & 0x000000FFu;
    guest_store_u8(
        memory,
        frame + 0x09u,
        static_cast<std::uint8_t>(context->gpr[0]),
        services,
        guest_pc + 0x28u);
    context->gpr[0] = std::rotl(context->gpr[3], 24) & 0x000000FFu;
    context->gpr[3] = context->gpr[0];
    guest_store_u8(
        memory,
        frame + 0x0Au,
        static_cast<std::uint8_t>(context->gpr[0]),
        services,
        guest_pc + 0x34u);
    context->gpr[3] =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::int8_t>(context->gpr[3])));
    context->gpr[0] =
        guest_load_u32(memory, frame + 0x14u, services, guest_pc + 0x3Cu);
    context->lr = context->gpr[0];
    context->gpr[1] = frame + 0x10u;
}

inline void native_vtable_flag_dispatch_80261254(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    context->gpr[0] =
        guest_load_u16(memory, context->gpr[3] + 0x08u, services, guest_pc);
    context->gpr[0] &= 0x00000001u;
    compare_unsigned(context, 0u, context->gpr[0], 0x00000001u);
    if (cr_bit(context, 2u)) {
        return;
    }
    context->gpr[12] =
        guest_load_u32(memory, context->gpr[3], services, guest_pc + 0x10u);
    context->gpr[12] =
        guest_load_u32(memory, context->gpr[12] + 0x14u, services, guest_pc + 0x14u);
    context->ctr = context->gpr[12];
    static std::uint32_t cached_target_80261270 = 0u;
    static NativeGameFunction cached_function_80261270 = nullptr;
    call_guest_cached(
        services,
        context->ctr & 0xFFFFFFFCu,
        &cached_target_80261270,
        &cached_function_80261270,
        context,
        memory,
        0x80261270u);
}

inline void native_stride6_offset8_8042E100(
    PpcContext* context,
    GuestMemoryV1*,
    const NativeServicesV1*,
    std::uint32_t) {
    const std::int64_t result =
        static_cast<std::int64_t>(static_cast<std::int32_t>(context->gpr[4])) *
        static_cast<std::int32_t>(6);
    context->gpr[0] = static_cast<std::uint32_t>(result);
    context->gpr[3] = context->gpr[3] + context->gpr[0] + 8u;
}

inline void native_vec_zero(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::byte* dst = resolve_guest(memory, context->gpr[3], 12, services, guest_pc);
    native_store_guest_u32_fast(dst, 0, 0);
    native_store_guest_u32_fast(dst, 4, 0);
    native_store_guest_u32_fast(dst, 8, 0);
    guest_notify_write(memory, context->gpr[3], 12u);
}

// stfs converts the FPR encoding to its architectural single-store bit
// slice, independently of FPSCR RN/NI. It is not frsp or a rounded arithmetic
// conversion: signaling NaNs retain their payload/quiet bit and no FP status
// is published. The tiny-value arm follows hardware-tested Gekko/Broadway
// conversion behavior also used by Dolphin and WiiCompiled.
GALAXY_ALWAYS_INLINE std::uint32_t native_stored_single_bits(
    std::uint64_t bits,
    std::uint32_t /*fpscr*/) {
    const auto exponent = static_cast<std::uint32_t>((bits >> 52u) & 0x7FFu);
    if (exponent >= 874u && exponent <= 896u) {
        const std::uint64_t significand =
            0x0010000000000000ull | (bits & 0x000FFFFFFFFFFFFFull);
        return (static_cast<std::uint32_t>(bits >> 32u) & 0x80000000u) |
            static_cast<std::uint32_t>(significand >> (926u - exponent));
    }
    return (static_cast<std::uint32_t>(bits >> 32u) & 0xC0000000u) |
        (static_cast<std::uint32_t>(bits >> 29u) & 0x3FFFFFFFu);
}

inline void native_vec_set_from_fprs(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::byte* dst = resolve_guest(memory, context->gpr[3], 12, services, guest_pc);
    native_store_guest_u32_fast(
        dst, 0, native_stored_single_bits(context->fpr_bits[1], context->fpscr));
    native_store_guest_u32_fast(
        dst, 4, native_stored_single_bits(context->fpr_bits[2], context->fpscr));
    native_store_guest_u32_fast(
        dst, 8, native_stored_single_bits(context->fpr_bits[3], context->fpscr));
    guest_notify_write(memory, context->gpr[3], 12u);
}

inline void native_vec_add(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t dst_addr = context->gpr[3];
    const std::uint32_t src_addr = context->gpr[4];
    const std::uint32_t x0 = guest_load_u32(memory, dst_addr + 0u, services, guest_pc);
    const std::uint32_t y0 = guest_load_u32(memory, dst_addr + 4u, services, guest_pc);
    const std::uint32_t z0 = guest_load_u32(memory, dst_addr + 8u, services, guest_pc);
    const std::uint32_t x1 = guest_load_u32(memory, src_addr + 0u, services, guest_pc);
    const std::uint32_t y1 = guest_load_u32(memory, src_addr + 4u, services, guest_pc);
    const std::uint32_t z1 = guest_load_u32(memory, src_addr + 8u, services, guest_pc);

    const PpcFloatResult x =
        ppc_f32_binary(PpcFloatBinaryOperation::Add, x0, x1, context->fpscr);
    ppc_commit_scalar_result(context, 0u, x, true, false, services, guest_pc);
    guest_store_u32(
        memory,
        dst_addr + 0u,
        native_stored_single_bits(context->fpr_bits[0], context->fpscr),
        services,
        guest_pc);

    const PpcFloatResult y =
        ppc_f32_binary(PpcFloatBinaryOperation::Add, y0, y1, context->fpscr);
    ppc_commit_scalar_result(context, 0u, y, true, false, services, guest_pc);
    guest_store_u32(
        memory,
        dst_addr + 4u,
        native_stored_single_bits(context->fpr_bits[0], context->fpscr),
        services,
        guest_pc);

    const PpcFloatResult z =
        ppc_f32_binary(PpcFloatBinaryOperation::Add, z0, z1, context->fpscr);
    ppc_commit_scalar_result(context, 1u, z, true, false, services, guest_pc);
    guest_store_u32(
        memory,
        dst_addr + 8u,
        native_stored_single_bits(context->fpr_bits[1], context->fpscr),
        services,
        guest_pc);
}

GALAXY_ALWAYS_INLINE void load_fpr_single(
    PpcContext* context,
    std::uint32_t index,
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
GALAXY_ALWAYS_INLINE void store_fpr_single(
    const PpcContext* context,
    std::uint32_t index,
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
GALAXY_ALWAYS_INLINE std::uint32_t require_single_precision_bits(
    std::uint64_t bits,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
inline void require_paired_single_mode(
    const PpcContext* context,
    bool d_form,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
inline void psq_load(
    PpcContext* context,
    std::uint32_t target,
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t gqr_index,
    bool one_element,
    bool d_form,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
inline void psq_store(
    const PpcContext* context,
    std::uint32_t source,
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t gqr_index,
    bool one_element,
    bool d_form,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);

inline void native_vec_add_8001CF80(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    psq_load(context, 3u, memory, context->gpr[3], 0u, false, true, services, 0x8001CF80u);
    psq_load(context, 2u, memory, context->gpr[4], 0u, false, true, services, 0x8001CF84u);
    psq_load(
        context,
        1u,
        memory,
        context->gpr[3] + 0x08u,
        0u,
        true,
        true,
        services,
        0x8001CF88u);
    psq_load(
        context,
        0u,
        memory,
        context->gpr[4] + 0x08u,
        0u,
        true,
        true,
        services,
        0x8001CF8Cu);

    require_paired_single_mode(context, false, services, 0x8001CF90u);
    {
        const std::uint32_t left_ps0 =
            require_single_precision_bits(context->fpr_bits[3], services, 0x8001CF90u);
        const std::uint32_t left_ps1 =
            require_single_precision_bits(context->ps1_bits[3], services, 0x8001CF90u);
        const std::uint32_t right_ps0 =
            require_single_precision_bits(context->fpr_bits[2], services, 0x8001CF90u);
        const std::uint32_t right_ps1 =
            require_single_precision_bits(context->ps1_bits[2], services, 0x8001CF90u);
        const PpcFloatResult result_ps0 =
            ppc_f32_binary(PpcFloatBinaryOperation::Add, left_ps0, right_ps0, context->fpscr);
        const PpcFloatResult result_ps1 =
            ppc_f32_binary(PpcFloatBinaryOperation::Add, left_ps1, right_ps1, context->fpscr);
        ppc_commit_paired_result(
            context, 2u, result_ps0, result_ps1, false, false, services, 0x8001CF90u);
    }
    {
        const std::uint32_t left_ps0 =
            require_single_precision_bits(context->fpr_bits[1], services, 0x8001CF94u);
        const std::uint32_t left_ps1 =
            require_single_precision_bits(context->ps1_bits[1], services, 0x8001CF94u);
        const std::uint32_t right_ps0 =
            require_single_precision_bits(context->fpr_bits[0], services, 0x8001CF94u);
        const std::uint32_t right_ps1 =
            require_single_precision_bits(context->ps1_bits[0], services, 0x8001CF94u);
        const PpcFloatResult result_ps0 =
            ppc_f32_binary(PpcFloatBinaryOperation::Add, left_ps0, right_ps0, context->fpscr);
        const PpcFloatResult result_ps1 =
            ppc_f32_binary(PpcFloatBinaryOperation::Add, left_ps1, right_ps1, context->fpscr);
        ppc_commit_paired_result(
            context, 0u, result_ps0, result_ps1, false, false, services, 0x8001CF94u);
    }
    psq_store(context, 2u, memory, context->gpr[3], 0u, false, true, services, 0x8001CF98u);
    psq_store(
        context,
        0u,
        memory,
        context->gpr[3] + 0x08u,
        0u,
        true,
        true,
        services,
        0x8001CF9Cu);
}

inline void native_vec_scale_8001FD6C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    load_fpr_single(context, 3u, memory, context->gpr[3], services, 0x8001FD6Cu);
    load_fpr_single(context, 2u, memory, context->gpr[3] + 0x04u, services, 0x8001FD70u);
    load_fpr_single(context, 0u, memory, context->gpr[3] + 0x08u, services, 0x8001FD74u);
    {
        const PpcFloatResult result =
            ppc_f64_binary_to_f32(
                PpcFloatBinaryOperation::Multiply,
                context->fpr_bits[3],
                context->fpr_bits[1],
                context->fpscr);
        ppc_commit_scalar_result(context, 3u, result, true, false, services, 0x8001FD78u);
    }
    {
        const PpcFloatResult result =
            ppc_f64_binary_to_f32(
                PpcFloatBinaryOperation::Multiply,
                context->fpr_bits[2],
                context->fpr_bits[1],
                context->fpscr);
        ppc_commit_scalar_result(context, 2u, result, true, false, services, 0x8001FD7Cu);
    }
    {
        const PpcFloatResult result =
            ppc_f64_binary_to_f32(
                PpcFloatBinaryOperation::Multiply,
                context->fpr_bits[0],
                context->fpr_bits[1],
                context->fpscr);
        ppc_commit_scalar_result(context, 0u, result, true, false, services, 0x8001FD80u);
    }
    store_fpr_single(context, 3u, memory, context->gpr[3], services, 0x8001FD84u);
    store_fpr_single(context, 2u, memory, context->gpr[3] + 0x04u, services, 0x8001FD88u);
    store_fpr_single(context, 0u, memory, context->gpr[3] + 0x08u, services, 0x8001FD8Cu);
}

inline void native_ring_vec_fetch(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t self_addr = context->gpr[3];
    const std::uint32_t out_addr = context->gpr[4];
    const std::uint32_t index = context->gpr[5];
    const std::uint32_t count =
        guest_load_u32(memory, self_addr + 0x628u, services, guest_pc);
    if (static_cast<std::int32_t>(index) >= static_cast<std::int32_t>(count)) {
        guest_store_u32(memory, out_addr + 0u, 0, services, guest_pc);
        guest_store_u32(memory, out_addr + 4u, 0, services, guest_pc);
        guest_store_u32(memory, out_addr + 8u, 0, services, guest_pc);
        context->gpr[3] = 0;
        return;
    }

    const std::uint32_t head =
        guest_load_u32(memory, self_addr + 0x624u, services, guest_pc);
    // Broadway subtraction wraps to 32 bits before its signed comparison.
    std::uint32_t relative = head - index;
    if (static_cast<std::int32_t>(relative) < 0) {
        relative += 0x80u;
    }
    const std::uint32_t src_addr =
        self_addr + 0x24u + relative * 12u;
    guest_store_u32(
        memory,
        out_addr + 0u,
        guest_load_u32(memory, src_addr + 0u, services, guest_pc),
        services,
        guest_pc);
    guest_store_u32(
        memory,
        out_addr + 4u,
        guest_load_u32(memory, src_addr + 4u, services, guest_pc),
        services,
        guest_pc);
    guest_store_u32(
        memory,
        out_addr + 8u,
        guest_load_u32(memory, src_addr + 8u, services, guest_pc),
        services,
        guest_pc);
    context->gpr[3] = 1;
}

GALAXY_ALWAYS_INLINE void load_fpr_single(
    PpcContext* context,
    std::uint32_t index,
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
GALAXY_ALWAYS_INLINE std::uint32_t require_single_precision_bits(
    std::uint64_t bits,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
inline void require_paired_single_mode(
    const PpcContext* context,
    bool requires_quantized_load_store,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
inline void psq_load(
    PpcContext* context,
    std::uint32_t target,
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t gqr_index,
    bool one_element,
    bool d_form,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
inline void psq_store(
    const PpcContext* context,
    std::uint32_t source,
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t gqr_index,
    bool one_element,
    bool d_form,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);

inline void native_psvec_cross_product(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);

    psq_load(context, 1u, memory, context->gpr[4], 0u, false, true, services, 0x804B6CB8u);
    load_fpr_single(
        context, 2u, memory, context->gpr[3] + 0x00000008u, services, 0x804B6CBCu);
    psq_load(context, 0u, memory, context->gpr[3], 0u, false, true, services, 0x804B6CC0u);

    require_paired_single_mode(context, false, services, 0x804B6CC4u);
    context->fpr_bits[6] = context->ps1_bits[1];
    context->ps1_bits[6] = context->fpr_bits[1];

    load_fpr_single(
        context, 3u, memory, context->gpr[4] + 0x00000008u, services, 0x804B6CC8u);

    require_paired_single_mode(context, false, services, 0x804B6CCCu);
    {
        const PpcFloatResult lane0 = ppc_f32_binary(
            PpcFloatBinaryOperation::Multiply,
            require_single_precision_bits(context->fpr_bits[1], services, 0x804B6CCCu),
            require_single_precision_bits(context->fpr_bits[2], services, 0x804B6CCCu),
            context->fpscr);
        const PpcFloatResult lane1 = ppc_f32_binary(
            PpcFloatBinaryOperation::Multiply,
            require_single_precision_bits(context->ps1_bits[1], services, 0x804B6CCCu),
            require_single_precision_bits(context->ps1_bits[2], services, 0x804B6CCCu),
            context->fpscr);
        ppc_commit_paired_result(
            context, 4u, lane0, lane1, false, false, services, 0x804B6CCCu);
    }

    require_paired_single_mode(context, false, services, 0x804B6CD0u);
    {
        const std::uint32_t scalar =
            require_single_precision_bits(context->fpr_bits[0], services, 0x804B6CD0u);
        const PpcFloatResult lane0 = ppc_f32_binary(
            PpcFloatBinaryOperation::Multiply,
            require_single_precision_bits(context->fpr_bits[1], services, 0x804B6CD0u),
            scalar,
            context->fpscr);
        const PpcFloatResult lane1 = ppc_f32_binary(
            PpcFloatBinaryOperation::Multiply,
            require_single_precision_bits(context->ps1_bits[1], services, 0x804B6CD0u),
            scalar,
            context->fpscr);
        ppc_commit_paired_result(
            context, 7u, lane0, lane1, false, false, services, 0x804B6CD0u);
    }

    require_paired_single_mode(context, false, services, 0x804B6CD4u);
    {
        const PpcFloatResult lane0 = ppc_f32_ternary(
            PpcFloatTernaryOperation::MultiplySubtract,
            require_single_precision_bits(context->fpr_bits[0], services, 0x804B6CD4u),
            require_single_precision_bits(context->fpr_bits[3], services, 0x804B6CD4u),
            require_single_precision_bits(context->fpr_bits[4], services, 0x804B6CD4u),
            context->fpscr);
        const PpcFloatResult lane1 = ppc_f32_ternary(
            PpcFloatTernaryOperation::MultiplySubtract,
            require_single_precision_bits(context->ps1_bits[0], services, 0x804B6CD4u),
            require_single_precision_bits(context->ps1_bits[3], services, 0x804B6CD4u),
            require_single_precision_bits(context->ps1_bits[4], services, 0x804B6CD4u),
            context->fpscr);
        ppc_commit_paired_result(
            context, 5u, lane0, lane1, false, false, services, 0x804B6CD4u);
    }

    require_paired_single_mode(context, false, services, 0x804B6CD8u);
    {
        const PpcFloatResult lane0 = ppc_f32_ternary(
            PpcFloatTernaryOperation::MultiplySubtract,
            require_single_precision_bits(context->fpr_bits[0], services, 0x804B6CD8u),
            require_single_precision_bits(context->fpr_bits[6], services, 0x804B6CD8u),
            require_single_precision_bits(context->fpr_bits[7], services, 0x804B6CD8u),
            context->fpscr);
        const PpcFloatResult lane1 = ppc_f32_ternary(
            PpcFloatTernaryOperation::MultiplySubtract,
            require_single_precision_bits(context->ps1_bits[0], services, 0x804B6CD8u),
            require_single_precision_bits(context->ps1_bits[6], services, 0x804B6CD8u),
            require_single_precision_bits(context->ps1_bits[7], services, 0x804B6CD8u),
            context->fpscr);
        ppc_commit_paired_result(
            context, 8u, lane0, lane1, false, false, services, 0x804B6CD8u);
    }

    require_paired_single_mode(context, false, services, 0x804B6CDCu);
    context->fpr_bits[9] = context->ps1_bits[5];
    context->ps1_bits[9] = context->ps1_bits[5];

    require_paired_single_mode(context, false, services, 0x804B6CE0u);
    context->fpr_bits[10] = context->fpr_bits[5];
    context->ps1_bits[10] = context->ps1_bits[8];

    psq_store(context, 9u, memory, context->gpr[5], 0u, true, true, services, 0x804B6CE4u);

    require_paired_single_mode(context, false, services, 0x804B6CE8u);
    context->fpr_bits[10] ^= 0x8000000000000000ull;
    context->ps1_bits[10] ^= 0x8000000000000000ull;

    psq_store(
        context,
        10u,
        memory,
        context->gpr[5] + 0x00000004u,
        0u,
        false,
        true,
        services,
        0x804B6CECu);
}

inline bool native_fast_host_float_helpers_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_NATIVE_FAST_HOST_FLOAT_HELPERS") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

GALAXY_ALWAYS_INLINE void load_fpr_single(
    PpcContext* context,
    std::uint32_t index,
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
GALAXY_ALWAYS_INLINE std::uint32_t require_single_precision_bits(
    std::uint64_t bits,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
inline void require_paired_single_mode(
    const PpcContext* context,
    bool requires_quantized_load_store,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
inline void psq_load(
    PpcContext* context,
    std::uint32_t target,
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t gqr_index,
    bool one_element,
    bool d_form,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
inline void psq_store(
    const PpcContext* context,
    std::uint32_t source,
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t gqr_index,
    bool one_element,
    bool d_form,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);
GALAXY_ALWAYS_INLINE std::uint64_t widen_f32_bits(std::uint32_t bits);
inline std::uint32_t narrow_paired_single_ftz(std::uint64_t bits);

inline void native_psmtx_identity_804B5EDC(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);

    load_fpr_single(
        context, 0u, memory, context->gpr[2] + 0x000024B4u, services, 0x804B5EDCu);
    load_fpr_single(
        context, 1u, memory, context->gpr[2] + 0x000024B0u, services, 0x804B5EE0u);
    psq_store(
        context, 0u, memory, context->gpr[3] + 0x08u, 0u, false, true, services, 0x804B5EE4u);
    require_paired_single_mode(context, false, services, 0x804B5EE8u);
    context->fpr_bits[2] = context->ps1_bits[1];
    context->ps1_bits[2] = context->fpr_bits[0];
    require_paired_single_mode(context, false, services, 0x804B5EECu);
    context->fpr_bits[1] = context->fpr_bits[0];
    psq_store(
        context, 0u, memory, context->gpr[3] + 0x18u, 0u, false, true, services, 0x804B5EF0u);
    psq_store(
        context, 0u, memory, context->gpr[3] + 0x20u, 0u, false, true, services, 0x804B5EF4u);
    psq_store(
        context, 1u, memory, context->gpr[3] + 0x10u, 0u, false, true, services, 0x804B5EF8u);
    psq_store(
        context, 2u, memory, context->gpr[3] + 0x00u, 0u, false, true, services, 0x804B5EFCu);
    psq_store(
        context, 2u, memory, context->gpr[3] + 0x28u, 0u, false, true, services, 0x804B5F00u);
}

inline void native_psmtx_copy_804B5F08(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);

    for (std::uint32_t pair = 0; pair < 6u; ++pair) {
        const std::uint32_t offset = pair * 8u;
        psq_load(
            context, pair, memory, context->gpr[3] + offset, 0u, false, true, services,
            0x804B5F08u + pair * 8u);
        psq_store(
            context, pair, memory, context->gpr[4] + offset, 0u, false, true, services,
            0x804B5F0Cu + pair * 8u);
    }
}

inline void native_psmtx_concat_804B5F3C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);

    if (native_fast_host_float_helpers_enabled() && context->gqr[0] == 0u) {
        require_paired_single_mode(context, false, services, 0x804B5F3Cu);

        const std::uint32_t old_sp = context->gpr[1];
        const std::uint32_t frame = old_sp + 0xFFFFFFC0u;
        const std::uint64_t saved_f14 = context->fpr_bits[14];
        const std::uint64_t saved_f15 = context->fpr_bits[15];
        const std::uint64_t saved_f31 = context->fpr_bits[31];

        guest_store_u32(memory, frame, old_sp, services, 0x804B5F3Cu);
        context->gpr[1] = frame;
        guest_store_u64(memory, frame + 0x08u, saved_f14, services, 0x804B5F44u);
        guest_store_u64(memory, frame + 0x10u, saved_f15, services, 0x804B5F54u);
        guest_store_u64(memory, frame + 0x28u, saved_f31, services, 0x804B5F5Cu);

        const std::byte* lhs =
            resolve_guest(memory, context->gpr[3], 48, services, 0x804B5F40u);
        const std::byte* rhs =
            resolve_guest(memory, context->gpr[4], 48, services, 0x804B5F48u);
        std::byte* dst =
            resolve_guest(memory, context->gpr[5], 48, services, 0x804B5FB8u);

        float a[12]{};
        float b[12]{};
        for (std::uint32_t i = 0; i < 12u; ++i) {
            const std::uint32_t offset = i * 4u;
            a[i] = native_load_guest_f32_fast(lhs, offset);
            b[i] = native_load_guest_f32_fast(rhs, offset);
        }

        const auto madd = [](float multiplicand, float scalar, float addend) {
            return std::fma(multiplicand, scalar, addend);
        };
        const auto multiply = [](float multiplicand, float scalar) {
            return multiplicand * scalar;
        };
        const auto affine_dot3 = [&](std::uint32_t row, std::uint32_t column) {
            const float start = multiply(b[column], a[row * 4u]);
            const float with_y = madd(b[4u + column], a[row * 4u + 1u], start);
            return madd(b[8u + column], a[row * 4u + 2u], with_y);
        };
        const auto affine_dot4 = [&](std::uint32_t row) {
            const float start = multiply(b[3u], a[row * 4u]);
            const float with_y = madd(b[7u], a[row * 4u + 1u], start);
            const float with_z = madd(b[11u], a[row * 4u + 2u], with_y);
            return madd(1.0F, a[row * 4u + 3u], with_z);
        };

        float output[12]{};
        for (std::uint32_t row = 0; row < 3u; ++row) {
            output[row * 4u + 0u] = affine_dot3(row, 0u);
            output[row * 4u + 1u] = affine_dot3(row, 1u);
            output[row * 4u + 2u] = affine_dot3(row, 2u);
            output[row * 4u + 3u] = affine_dot4(row);
        }

        const auto widen_output = [](float value) {
            return widen_f32_bits(std::bit_cast<std::uint32_t>(value));
        };
        const auto store_output = [&](std::uint32_t index, std::uint32_t pc) {
            const std::uint32_t bits =
                narrow_paired_single_ftz(widen_output(output[index]));
            native_store_guest_u32_fast(dst, index * 4u, bits);
            static_cast<void>(pc);
        };
        for (std::uint32_t i = 0; i < 12u; ++i) {
            store_output(i, 0x804B5FB8u + i * 4u);
        }
        guest_notify_write(memory, context->gpr[5], 48u);

        context->gpr[6] = 0x8069E148u;
        context->fpr_bits[12] = widen_output(output[0]);
        context->ps1_bits[12] = widen_output(output[1]);
        context->fpr_bits[13] = widen_output(output[2]);
        context->ps1_bits[13] = widen_output(output[3]);
        context->fpr_bits[14] = saved_f14;
        context->ps1_bits[14] = widen_output(output[5]);
        context->fpr_bits[15] = saved_f15;
        context->ps1_bits[15] = widen_output(output[7]);
        context->fpr_bits[2] = widen_output(output[8]);
        context->ps1_bits[2] = widen_output(output[9]);
        context->fpr_bits[0] = widen_output(output[10]);
        context->ps1_bits[0] = widen_output(output[11]);
        context->fpr_bits[31] = saved_f31;
        context->ps1_bits[31] = widen_f32_bits(std::bit_cast<std::uint32_t>(1.0F));
        context->gpr[1] = old_sp;
        return;
    }

    const auto f32 = [&](std::uint64_t bits, std::uint32_t pc) {
        return require_single_precision_bits(bits, services, pc);
    };
    const auto paired_mul_s0 = [&](
        std::uint32_t target,
        std::uint32_t left,
        std::uint32_t scalar,
        std::uint32_t pc) {
        require_paired_single_mode(context, false, services, pc);
        const std::uint32_t scalar_bits = f32(context->fpr_bits[scalar], pc);
        const PpcFloatResult lane0 = ppc_f32_binary(
            PpcFloatBinaryOperation::Multiply,
            f32(context->fpr_bits[left], pc),
            scalar_bits,
            context->fpscr);
        const PpcFloatResult lane1 = ppc_f32_binary(
            PpcFloatBinaryOperation::Multiply,
            f32(context->ps1_bits[left], pc),
            scalar_bits,
            context->fpscr);
        ppc_commit_paired_result(
            context, target, lane0, lane1, false, false, services, pc);
    };
    const auto paired_madd_s0 = [&](
        std::uint32_t target,
        std::uint32_t multiplicand,
        std::uint32_t scalar,
        std::uint32_t addend,
        std::uint32_t pc) {
        require_paired_single_mode(context, false, services, pc);
        const std::uint32_t scalar_bits = f32(context->fpr_bits[scalar], pc);
        const PpcFloatResult lane0 = ppc_f32_ternary(
            PpcFloatTernaryOperation::MultiplyAdd,
            f32(context->fpr_bits[multiplicand], pc),
            scalar_bits,
            f32(context->fpr_bits[addend], pc),
            context->fpscr);
        const PpcFloatResult lane1 = ppc_f32_ternary(
            PpcFloatTernaryOperation::MultiplyAdd,
            f32(context->ps1_bits[multiplicand], pc),
            scalar_bits,
            f32(context->ps1_bits[addend], pc),
            context->fpscr);
        ppc_commit_paired_result(
            context, target, lane0, lane1, false, false, services, pc);
    };
    const auto paired_madd_s1 = [&](
        std::uint32_t target,
        std::uint32_t multiplicand,
        std::uint32_t scalar,
        std::uint32_t addend,
        std::uint32_t pc) {
        require_paired_single_mode(context, false, services, pc);
        const std::uint32_t scalar_bits = f32(context->ps1_bits[scalar], pc);
        const PpcFloatResult lane0 = ppc_f32_ternary(
            PpcFloatTernaryOperation::MultiplyAdd,
            f32(context->fpr_bits[multiplicand], pc),
            scalar_bits,
            f32(context->fpr_bits[addend], pc),
            context->fpscr);
        const PpcFloatResult lane1 = ppc_f32_ternary(
            PpcFloatTernaryOperation::MultiplyAdd,
            f32(context->ps1_bits[multiplicand], pc),
            scalar_bits,
            f32(context->ps1_bits[addend], pc),
            context->fpscr);
        ppc_commit_paired_result(
            context, target, lane0, lane1, false, false, services, pc);
    };

    const std::uint32_t frame = context->gpr[1] + 0xFFFFFFC0u;
    guest_store_u32(memory, frame, context->gpr[1], services, 0x804B5F3Cu);
    context->gpr[1] = frame;
    psq_load(
        context, 0u, memory, context->gpr[3],
        0u, false, true, services, 0x804B5F40u);
    guest_store_u64(memory, frame + 0x08u, context->fpr_bits[14], services, 0x804B5F44u);
    psq_load(
        context, 6u, memory, context->gpr[4],
        0u, false, true, services, 0x804B5F48u);
    context->gpr[6] = 0x806A0000u;
    psq_load(
        context, 7u, memory, context->gpr[4] + 0x08u,
        0u, false, true, services, 0x804B5F50u);
    guest_store_u64(memory, frame + 0x10u, context->fpr_bits[15], services, 0x804B5F54u);
    context->gpr[6] += 0xFFFFE148u;
    guest_store_u64(memory, frame + 0x28u, context->fpr_bits[31], services, 0x804B5F5Cu);
    psq_load(
        context, 8u, memory, context->gpr[4] + 0x10u,
        0u, false, true, services, 0x804B5F60u);
    paired_mul_s0(12u, 6u, 0u, 0x804B5F64u);
    psq_load(
        context, 2u, memory, context->gpr[3] + 0x10u,
        0u, false, true, services, 0x804B5F68u);
    paired_mul_s0(13u, 7u, 0u, 0x804B5F6Cu);
    psq_load(
        context, 31u, memory, context->gpr[6],
        0u, false, true, services, 0x804B5F70u);
    paired_mul_s0(14u, 6u, 2u, 0x804B5F74u);
    psq_load(
        context, 9u, memory, context->gpr[4] + 0x18u,
        0u, false, true, services, 0x804B5F78u);
    paired_mul_s0(15u, 7u, 2u, 0x804B5F7Cu);
    psq_load(
        context, 1u, memory, context->gpr[3] + 0x08u,
        0u, false, true, services, 0x804B5F80u);
    paired_madd_s1(12u, 8u, 0u, 12u, 0x804B5F84u);
    psq_load(
        context, 3u, memory, context->gpr[3] + 0x18u,
        0u, false, true, services, 0x804B5F88u);
    paired_madd_s1(14u, 8u, 2u, 14u, 0x804B5F8Cu);
    psq_load(
        context, 10u, memory, context->gpr[4] + 0x20u,
        0u, false, true, services, 0x804B5F90u);
    paired_madd_s1(13u, 9u, 0u, 13u, 0x804B5F94u);
    psq_load(
        context, 11u, memory, context->gpr[4] + 0x28u,
        0u, false, true, services, 0x804B5F98u);
    paired_madd_s1(15u, 9u, 2u, 15u, 0x804B5F9Cu);
    psq_load(
        context, 4u, memory, context->gpr[3] + 0x20u,
        0u, false, true, services, 0x804B5FA0u);
    psq_load(
        context, 5u, memory, context->gpr[3] + 0x28u,
        0u, false, true, services, 0x804B5FA4u);
    paired_madd_s0(12u, 10u, 1u, 12u, 0x804B5FA8u);
    paired_madd_s0(13u, 11u, 1u, 13u, 0x804B5FACu);
    paired_madd_s0(14u, 10u, 3u, 14u, 0x804B5FB0u);
    paired_madd_s0(15u, 11u, 3u, 15u, 0x804B5FB4u);

    psq_store(
        context, 12u, memory, context->gpr[5],
        0u, false, true, services, 0x804B5FB8u);
    paired_mul_s0(2u, 6u, 4u, 0x804B5FBCu);
    paired_madd_s1(13u, 31u, 1u, 13u, 0x804B5FC0u);
    paired_mul_s0(0u, 7u, 4u, 0x804B5FC4u);
    psq_store(
        context, 14u, memory, context->gpr[5] + 0x10u,
        0u, false, true, services, 0x804B5FC8u);
    paired_madd_s1(15u, 31u, 3u, 15u, 0x804B5FCCu);
    psq_store(
        context, 13u, memory, context->gpr[5] + 0x08u,
        0u, false, true, services, 0x804B5FD0u);
    paired_madd_s1(2u, 8u, 4u, 2u, 0x804B5FD4u);
    paired_madd_s1(0u, 9u, 4u, 0u, 0x804B5FD8u);
    paired_madd_s0(2u, 10u, 5u, 2u, 0x804B5FDCu);
    context->fpr_bits[14] =
        guest_load_u64(memory, context->gpr[1] + 0x08u, services, 0x804B5FE0u);
    psq_store(
        context, 15u, memory, context->gpr[5] + 0x18u,
        0u, false, true, services, 0x804B5FE4u);
    paired_madd_s0(0u, 11u, 5u, 0u, 0x804B5FE8u);
    psq_store(
        context, 2u, memory, context->gpr[5] + 0x20u,
        0u, false, true, services, 0x804B5FECu);
    paired_madd_s1(0u, 31u, 5u, 0u, 0x804B5FF0u);
    context->fpr_bits[15] =
        guest_load_u64(memory, context->gpr[1] + 0x10u, services, 0x804B5FF4u);
    psq_store(
        context, 0u, memory, context->gpr[5] + 0x28u,
        0u, false, true, services, 0x804B5FF8u);
    context->fpr_bits[31] =
        guest_load_u64(memory, context->gpr[1] + 0x28u, services, 0x804B5FFCu);
    context->gpr[1] += 0x40u;
}

inline void native_psvec_normalize_804B6BCC(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);

    psq_load(
        context,
        2u,
        memory,
        context->gpr[3] + 0x00000000u,
        0u,
        false,
        true,
        services,
        0x804B6BCCu);
    psq_load(
        context,
        3u,
        memory,
        context->gpr[3] + 0x00000008u,
        0u,
        true,
        true,
        services,
        0x804B6BD0u);

    const auto f32 = [&](std::uint64_t bits, std::uint32_t pc) {
        return require_single_precision_bits(bits, services, pc);
    };
    const auto paired_mul = [&](
        std::uint32_t target,
        std::uint32_t left,
        std::uint32_t right,
        std::uint32_t pc) {
        require_paired_single_mode(context, false, services, pc);
        const PpcFloatResult lane0 = ppc_f32_binary(
            PpcFloatBinaryOperation::Multiply,
            f32(context->fpr_bits[left], pc),
            f32(context->fpr_bits[right], pc),
            context->fpscr);
        const PpcFloatResult lane1 = ppc_f32_binary(
            PpcFloatBinaryOperation::Multiply,
            f32(context->ps1_bits[left], pc),
            f32(context->ps1_bits[right], pc),
            context->fpscr);
        ppc_commit_paired_result(
            context, target, lane0, lane1, false, false, services, pc);
    };
    const auto paired_muls0 = [&](
        std::uint32_t target,
        std::uint32_t left,
        std::uint32_t right,
        std::uint32_t pc) {
        require_paired_single_mode(context, false, services, pc);
        const std::uint32_t scalar = f32(context->fpr_bits[right], pc);
        const PpcFloatResult lane0 = ppc_f32_binary(
            PpcFloatBinaryOperation::Multiply,
            f32(context->fpr_bits[left], pc),
            scalar,
            context->fpscr);
        const PpcFloatResult lane1 = ppc_f32_binary(
            PpcFloatBinaryOperation::Multiply,
            f32(context->ps1_bits[left], pc),
            scalar,
            context->fpscr);
        ppc_commit_paired_result(
            context, target, lane0, lane1, false, false, services, pc);
    };

    paired_mul(5u, 2u, 2u, 0x804B6BD4u);
    load_fpr_single(
        context,
        0u,
        memory,
        context->gpr[2] + 0x000024E8u,
        services,
        0x804B6BD8u);
    load_fpr_single(
        context,
        1u,
        memory,
        context->gpr[2] + 0x000024ECu,
        services,
        0x804B6BDCu);

    require_paired_single_mode(context, false, services, 0x804B6BE0u);
    {
        const PpcFloatResult lane0 = ppc_f32_ternary(
            PpcFloatTernaryOperation::MultiplyAdd,
            f32(context->fpr_bits[3], 0x804B6BE0u),
            f32(context->fpr_bits[3], 0x804B6BE0u),
            f32(context->fpr_bits[5], 0x804B6BE0u),
            context->fpscr);
        const PpcFloatResult lane1 = ppc_f32_ternary(
            PpcFloatTernaryOperation::MultiplyAdd,
            f32(context->ps1_bits[3], 0x804B6BE0u),
            f32(context->ps1_bits[3], 0x804B6BE0u),
            f32(context->ps1_bits[5], 0x804B6BE0u),
            context->fpscr);
        ppc_commit_paired_result(
            context, 4u, lane0, lane1, false, false, services, 0x804B6BE0u);
    }
    require_paired_single_mode(context, false, services, 0x804B6BE4u);
    {
        const PpcFloatResult lane0 = ppc_f32_binary(
            PpcFloatBinaryOperation::Add,
            f32(context->fpr_bits[4], 0x804B6BE4u),
            f32(context->ps1_bits[5], 0x804B6BE4u),
            context->fpscr);
        const PpcFloatResult lane1 =
            ppc_f32_passthrough(f32(context->ps1_bits[3], 0x804B6BE4u));
        ppc_commit_paired_result(
            context, 4u, lane0, lane1, false, false, services, 0x804B6BE4u);
    }
    ppc_commit_scalar_result(
        context,
        5u,
        ppc_f64_reciprocal_sqrt_estimate(context->fpr_bits[4], context->fpscr),
        false,
        false,
        services,
        0x804B6BE8u);
    ppc_commit_scalar_result(
        context,
        6u,
        ppc_f64_binary_to_f32(
            PpcFloatBinaryOperation::Multiply,
            context->fpr_bits[5],
            context->fpr_bits[5],
            context->fpscr),
        true,
        false,
        services,
        0x804B6BECu);
    ppc_commit_scalar_result(
        context,
        0u,
        ppc_f64_binary_to_f32(
            PpcFloatBinaryOperation::Multiply,
            context->fpr_bits[5],
            context->fpr_bits[0],
            context->fpscr),
        true,
        false,
        services,
        0x804B6BF0u);
    ppc_commit_scalar_result(
        context,
        6u,
        ppc_f32_ternary(
            PpcFloatTernaryOperation::NegativeMultiplySubtract,
            f32(context->fpr_bits[6], 0x804B6BF4u),
            f32(context->fpr_bits[4], 0x804B6BF4u),
            f32(context->fpr_bits[1], 0x804B6BF4u),
            context->fpscr),
        true,
        false,
        services,
        0x804B6BF4u);
    ppc_commit_scalar_result(
        context,
        5u,
        ppc_f64_binary_to_f32(
            PpcFloatBinaryOperation::Multiply,
            context->fpr_bits[6],
            context->fpr_bits[0],
            context->fpscr),
        true,
        false,
        services,
        0x804B6BF8u);

    paired_muls0(2u, 2u, 5u, 0x804B6BFCu);
    paired_muls0(3u, 3u, 5u, 0x804B6C00u);
    psq_store(
        context,
        2u,
        memory,
        context->gpr[4] + 0x00000000u,
        0u,
        false,
        true,
        services,
        0x804B6C04u);
    psq_store(
        context,
        3u,
        memory,
        context->gpr[4] + 0x00000008u,
        0u,
        true,
        true,
        services,
        0x804B6C08u);
}

inline void native_vec_normalize_in_place_803E4D24(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);
    context->gpr[4] = context->gpr[3] | context->gpr[3];
    context->pc = 0x804B6BCCu;
    native_psvec_normalize_804B6BCC(
        context, memory, services, 0x804B6BCCu);
}

inline void native_vec3_abs_le_threshold_803E595C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    load_fpr_single(
        context, 0u, memory, context->gpr[3] + 0x00u, services, guest_pc);
    compare_f64(context, 0u, context->fpr_bits[0], context->fpr_bits[1], true);
    if (cr_bit(context, 1u)) {
        context->gpr[3] = 0u;
        return;
    }

    context->fpr_bits[2] = context->fpr_bits[1] ^ 0x8000000000000000ull;
    compare_f64(context, 0u, context->fpr_bits[0], context->fpr_bits[2], true);
    if (cr_bit(context, 0u)) {
        context->gpr[3] = 0u;
        return;
    }

    load_fpr_single(
        context, 0u, memory, context->gpr[3] + 0x04u, services, 0x803E5984u);
    compare_f64(context, 0u, context->fpr_bits[0], context->fpr_bits[1], true);
    if (cr_bit(context, 1u)) {
        context->gpr[3] = 0u;
        return;
    }

    compare_f64(context, 0u, context->fpr_bits[0], context->fpr_bits[2], true);
    if (cr_bit(context, 0u)) {
        context->gpr[3] = 0u;
        return;
    }

    load_fpr_single(
        context, 0u, memory, context->gpr[3] + 0x08u, services, 0x803E59A8u);
    compare_f64(context, 0u, context->fpr_bits[0], context->fpr_bits[1], true);
    if (cr_bit(context, 1u)) {
        context->gpr[3] = 0u;
        return;
    }

    compare_f64(context, 0u, context->fpr_bits[0], context->fpr_bits[2], true);
    context->gpr[0] = context->cr;
    context->gpr[0] = std::rotl(context->gpr[0], 1) & 0x00000001u;
    context->gpr[0] = static_cast<std::uint32_t>(std::countl_zero(context->gpr[0]));
    context->gpr[3] = std::rotl(context->gpr[0], 27) & 0x07FFFFFFu;
}

inline void native_gx_set_chan_color_value_804BD28C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t channel_index,
    std::uint32_t color) {
    const std::uint32_t channel = (channel_index << 1u) & 0xFFFFFFFEu;
    const std::uint32_t first_reg =
        (((channel + 0xE0u) << 24u) & 0xFF000000u) |
        (std::rotl(color, 8) & 0x000000FFu) |
        (std::rotl(color, 12) & 0x000FF000u);
    const std::uint32_t second_reg =
        (((channel + 0xE1u) << 24u) & 0xFF000000u) |
        (std::rotl(color, 24) & 0x000000FFu) |
        (std::rotl(color, 28) & 0x000FF000u);

    const std::byte fifo_bytes[20]{
        std::byte{0x61},
        static_cast<std::byte>((first_reg >> 24u) & 0xFFu),
        static_cast<std::byte>((first_reg >> 16u) & 0xFFu),
        static_cast<std::byte>((first_reg >> 8u) & 0xFFu),
        static_cast<std::byte>(first_reg & 0xFFu),
        std::byte{0x61},
        static_cast<std::byte>((second_reg >> 24u) & 0xFFu),
        static_cast<std::byte>((second_reg >> 16u) & 0xFFu),
        static_cast<std::byte>((second_reg >> 8u) & 0xFFu),
        static_cast<std::byte>(second_reg & 0xFFu),
        std::byte{0x61},
        static_cast<std::byte>((second_reg >> 24u) & 0xFFu),
        static_cast<std::byte>((second_reg >> 16u) & 0xFFu),
        static_cast<std::byte>((second_reg >> 8u) & 0xFFu),
        static_cast<std::byte>(second_reg & 0xFFu),
        std::byte{0x61},
        static_cast<std::byte>((second_reg >> 24u) & 0xFFu),
        static_cast<std::byte>((second_reg >> 16u) & 0xFFu),
        static_cast<std::byte>((second_reg >> 8u) & 0xFFu),
        static_cast<std::byte>(second_reg & 0xFFu),
    };
    if (memory == nullptr || memory->write_device == nullptr ||
        !memory->write_device(memory->user, 0xCC008000u,
                              static_cast<std::uint32_t>(sizeof(fifo_bytes)),
                              fifo_bytes)) {
        guest_execution_fault(
            services, guest_pc, "GX channel color native FIFO service is unavailable");
    }

    const std::uint32_t gx =
        guest_load_u32(memory, context->gpr[2] + 0x2500u, services, guest_pc);
    guest_store_u16(memory, gx + 0x02u, 0u, services, guest_pc);

    context->gpr[0] = 0u;
    context->gpr[3] = gx;
    context->gpr[4] = 0xCC010000u;
    context->gpr[5] = 0x61u;
    context->gpr[6] = second_reg;
    context->gpr[7] = first_reg;
    context->gpr[8] = color;
}

inline void native_gx_set_chan_color_804BD28C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t channel_index = context->gpr[3];
    const std::uint32_t color =
        guest_load_u32(memory, context->gpr[4], services, guest_pc);
    native_gx_set_chan_color_value_804BD28C(
        context, memory, services, guest_pc, channel_index, color);
}

inline std::uint8_t native_jpa_color_multi(
    std::uint32_t color,
    std::uint32_t scale_plus_one) {
    return static_cast<std::uint8_t>((color * scale_plus_one) >> 8u);
}

inline void native_jpa_regist_child_prm_env_8044432C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t frame = context->gpr[1] + 0xFFFFFFD0u;
    guest_store_u32(memory, frame, context->gpr[1], services, guest_pc);
    context->gpr[1] = frame;
    context->gpr[0] = context->lr;
    guest_store_u32(memory, frame + 0x34u, context->gpr[0], services, 0x80444334u);
    context->gpr[11] = frame + 0x30u;
    context->lr = 0x80444340u;
    guest_store_u32(memory, context->gpr[11] + 0xFFFFFFE8u, context->gpr[26], services, 0x8051752Cu);
    guest_store_u32(memory, context->gpr[11] + 0xFFFFFFECu, context->gpr[27], services, 0x80517530u);
    guest_store_u32(memory, context->gpr[11] + 0xFFFFFFF0u, context->gpr[28], services, 0x80517534u);
    guest_store_u32(memory, context->gpr[11] + 0xFFFFFFF4u, context->gpr[29], services, 0x80517538u);
    guest_store_u32(memory, context->gpr[11] + 0xFFFFFFF8u, context->gpr[30], services, 0x8051753Cu);
    guest_store_u32(memory, context->gpr[11] + 0xFFFFFFFCu, context->gpr[31], services, 0x80517540u);

    const std::uint32_t child_shape =
        guest_load_u32(memory, context->gpr[3], services, 0x80444340u);
    const std::uint32_t work = context->gpr[4];
    context->gpr[28] = child_shape;
    context->gpr[3] = 1u;

    const std::byte* work_color =
        resolve_guest(memory, work + 0x8Cu, 0x0Bu, services, 0x80444348u);
    const std::byte* child_color =
        resolve_guest(memory, child_shape + 0xB8u, 0x07u, services, 0x8044434Cu);
    const auto read_work_color = [&](std::uint32_t offset) -> std::uint32_t {
        return std::to_integer<std::uint8_t>(work_color[offset - 0x8Cu]);
    };
    const auto read_child_color = [&](std::uint32_t offset) -> std::uint32_t {
        return std::to_integer<std::uint8_t>(child_color[offset - 0xB8u]);
    };

    const std::uint32_t work_prm_a = read_work_color(0x93u);
    const std::uint32_t child_prm_a = read_child_color(0xBBu);
    const std::uint32_t work_prm_a_base = read_work_color(0x8Fu);
    const std::uint32_t child_prm_b = read_child_color(0xBAu);
    const std::uint32_t child_prm_r = read_child_color(0xB8u);
    const std::uint32_t work_prm_a_scale = read_work_color(0x96u);
    const std::uint32_t child_prm_g = read_child_color(0xB9u);
    const std::uint32_t work_prm_r = read_work_color(0x8Cu);
    const std::uint32_t work_prm_b = read_work_color(0x8Eu);
    const std::uint32_t child_env_b = read_child_color(0xBEu);
    const std::uint32_t child_env_r = read_child_color(0xBCu);
    const std::uint32_t child_env_g = read_child_color(0xBDu);
    const std::uint32_t work_prm_g = read_work_color(0x8Du);
    const std::uint32_t work_env_b = read_work_color(0x92u);
    const std::uint32_t work_env_r = read_work_color(0x90u);
    const std::uint32_t work_env_g = read_work_color(0x91u);

    const std::uint8_t prm_a_base =
        native_jpa_color_multi(work_prm_a_base, child_prm_a + 1u);
    const std::uint8_t prm_r =
        native_jpa_color_multi(work_prm_r, child_prm_r + 1u);
    const std::uint8_t prm_b =
        native_jpa_color_multi(work_prm_b, child_prm_b + 1u);
    const std::uint8_t prm_g =
        native_jpa_color_multi(work_prm_g, child_prm_g + 1u);
    const std::uint8_t prm_a =
        native_jpa_color_multi(prm_a_base, work_prm_a_scale + 1u);
    const std::uint8_t env_g =
        native_jpa_color_multi(work_env_g, child_env_g + 1u);
    const std::uint8_t env_r =
        native_jpa_color_multi(work_env_r, child_env_r + 1u);
    const std::uint8_t env_b =
        native_jpa_color_multi(work_env_b, child_env_b + 1u);

    context->gpr[26] = work_prm_g;
    context->gpr[27] = work_prm_b;
    context->gpr[28] = work_env_r;
    context->gpr[29] = work_env_g;
    context->gpr[30] = work_env_b;
    context->gpr[31] = work_prm_a;
    context->gpr[9] = prm_a_base;
    context->gpr[10] = prm_b;
    context->gpr[12] = prm_r;

    const std::uint32_t prm_color =
        (static_cast<std::uint32_t>(prm_r) << 24u) |
        (static_cast<std::uint32_t>(prm_g) << 16u) |
        (static_cast<std::uint32_t>(prm_b) << 8u) |
        static_cast<std::uint32_t>(prm_a);
    const std::uint32_t env_with_work_alpha =
        (static_cast<std::uint32_t>(env_r) << 24u) |
        (static_cast<std::uint32_t>(env_g) << 16u) |
        (static_cast<std::uint32_t>(env_b) << 8u) |
        (work_prm_a & 0xFFu);

    context->gpr[4] = frame + 0x0Cu;
    guest_store_u32(memory, frame + 0x0Cu, prm_color, services, 0x804443D8u);
    guest_store_u32(memory, frame + 0x10u, env_with_work_alpha, services, 0x80444410u);
    guest_store_u32(memory, frame + 0x14u, prm_color, services, 0x804443CCu);

    context->gpr[3] = 1u;
    context->gpr[4] = frame + 0x0Cu;
    context->lr = 0x80444420u;
    context->pc = 0x804BD28Cu;

    const std::uint32_t env_color =
        (static_cast<std::uint32_t>(env_r) << 24u) |
        (static_cast<std::uint32_t>(env_g) << 16u) |
        (static_cast<std::uint32_t>(env_b) << 8u) |
        (work_prm_a & 0xFFu);
    context->gpr[7] = env_r;
    context->gpr[0] = context->gpr[31] & 0xFFu;
    context->gpr[6] = env_g;
    context->gpr[4] = frame + 0x08u;
    context->gpr[5] = env_b;
    context->gpr[3] = 2u;
    guest_store_u32(memory, frame + 0x08u, env_color, services, 0x80444438u);
    context->lr = 0x8044444Cu;
    context->pc = 0x804BD28Cu;
    std::byte fifo_bytes[40]{};
    std::uint32_t final_first_reg = 0u;
    std::uint32_t final_second_reg = 0u;
    const auto append_chan_color =
        [&fifo_bytes, &final_first_reg, &final_second_reg](
            std::size_t offset,
            std::uint32_t channel_index,
            std::uint32_t color) {
            const std::uint32_t channel =
                (channel_index << 1u) & 0xFFFFFFFEu;
            const std::uint32_t first_reg =
                (((channel + 0xE0u) << 24u) & 0xFF000000u) |
                (std::rotl(color, 8) & 0x000000FFu) |
                (std::rotl(color, 12) & 0x000FF000u);
            const std::uint32_t second_reg =
                (((channel + 0xE1u) << 24u) & 0xFF000000u) |
                (std::rotl(color, 24) & 0x000000FFu) |
                (std::rotl(color, 28) & 0x000FF000u);
            const auto write_word =
                [&fifo_bytes](std::size_t packet_offset, std::uint32_t word) {
                    fifo_bytes[packet_offset] = std::byte{0x61};
                    fifo_bytes[packet_offset + 1u] =
                        static_cast<std::byte>((word >> 24u) & 0xFFu);
                    fifo_bytes[packet_offset + 2u] =
                        static_cast<std::byte>((word >> 16u) & 0xFFu);
                    fifo_bytes[packet_offset + 3u] =
                        static_cast<std::byte>((word >> 8u) & 0xFFu);
                    fifo_bytes[packet_offset + 4u] =
                        static_cast<std::byte>(word & 0xFFu);
                };
            write_word(offset, first_reg);
            write_word(offset + 5u, second_reg);
            write_word(offset + 10u, second_reg);
            write_word(offset + 15u, second_reg);
            final_first_reg = first_reg;
            final_second_reg = second_reg;
        };
    append_chan_color(0u, 1u, prm_color);
    append_chan_color(20u, 2u, env_color);
    if (memory == nullptr || memory->write_device == nullptr ||
        !memory->write_device(memory->user, 0xCC008000u,
                              static_cast<std::uint32_t>(sizeof(fifo_bytes)),
                              fifo_bytes)) {
        guest_execution_fault(
            services,
            0x804BD28Cu,
            "GX channel color native FIFO service is unavailable");
    }
    const std::uint32_t gx =
        guest_load_u32(memory, context->gpr[2] + 0x2500u, services, 0x804BD28Cu);
    guest_store_u16(memory, gx + 0x02u, 0u, services, 0x804BD28Cu);
    context->gpr[0] = 0u;
    context->gpr[3] = gx;
    context->gpr[4] = 0xCC010000u;
    context->gpr[5] = 0x61u;
    context->gpr[6] = final_second_reg;
    context->gpr[7] = final_first_reg;
    context->gpr[8] = env_color;

    context->gpr[11] = context->gpr[1] + 0x30u;
    context->lr = 0x80444454u;
    context->pc = 0x80517578u;
    context->gpr[26] =
        guest_load_u32(memory, context->gpr[11] + 0xFFFFFFE8u, services, 0x80517578u);
    context->gpr[27] =
        guest_load_u32(memory, context->gpr[11] + 0xFFFFFFECu, services, 0x8051757Cu);
    context->gpr[28] =
        guest_load_u32(memory, context->gpr[11] + 0xFFFFFFF0u, services, 0x80517580u);
    context->gpr[29] =
        guest_load_u32(memory, context->gpr[11] + 0xFFFFFFF4u, services, 0x80517584u);
    context->gpr[30] =
        guest_load_u32(memory, context->gpr[11] + 0xFFFFFFF8u, services, 0x80517588u);
    context->gpr[31] =
        guest_load_u32(memory, context->gpr[11] + 0xFFFFFFFCu, services, 0x8051758Cu);
    context->gpr[0] =
        guest_load_u32(memory, context->gpr[1] + 0x34u, services, 0x80444454u);
    context->lr = context->gpr[0];
    context->gpr[1] = context->gpr[1] + 0x30u;
}

inline void native_gx_send_zero_primitive_804BA7FC(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t gx =
        guest_load_u32(memory, context->gpr[2] + 0x2500u, services, guest_pc);
    const std::uint16_t width =
        guest_load_u16(memory, gx + 0x04u, services, 0x804BA80Cu);
    const std::uint16_t height =
        guest_load_u16(memory, gx + 0x06u, services, 0x804BA810u);
    const std::uint32_t vertex_bytes =
        static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height);
    const std::uint32_t padded_vertex_bytes =
        (vertex_bytes + 3u) & ~std::uint32_t{3u};

    const auto write_fifo = [&](const std::byte* bytes, std::uint32_t size) {
        if (memory == nullptr || memory->write_device == nullptr ||
            !memory->write_device(memory->user, 0xCC008000u, size, bytes)) {
            guest_execution_fault(
                services, guest_pc, "GX zero primitive native FIFO service is unavailable");
        }
    };

    const std::byte header[3]{
        std::byte{0x98},
        static_cast<std::byte>((width >> 8u) & 0xFFu),
        static_cast<std::byte>(width & 0xFFu),
    };
    write_fifo(header, static_cast<std::uint32_t>(sizeof(header)));

    static constexpr std::byte kZeros[32]{};
    std::uint32_t remaining = padded_vertex_bytes;
    while (remaining != 0u) {
        const std::uint32_t chunk =
            remaining < static_cast<std::uint32_t>(sizeof(kZeros))
                ? remaining
                : static_cast<std::uint32_t>(sizeof(kZeros));
        write_fifo(kZeros, chunk);
        remaining -= chunk;
    }

    std::uint32_t first_loop_bytes = 0u;
    if (vertex_bytes == 0u) {
        const std::uint32_t field =
            0x2u | (((context->xer & 0x80000000u) != 0u) ? 0x1u : 0u);
        context->cr = (context->cr & 0x0FFFFFFFu) | (field << 28u);
        context->gpr[4] = height;
        context->gpr[5] = width;
    } else {
        const std::uint32_t rounded = vertex_bytes + 3u;
        context->gpr[5] = vertex_bytes + 0xFFFFFFE0u;
        context->gpr[4] = 0u;
        if ((rounded >> 2u) > 8u && rounded >= 3u &&
            vertex_bytes <= rounded && context->gpr[5] > 0u) {
            first_loop_bytes = ((context->gpr[5] + 0x1Fu) >> 5u) << 5u;
        }
        compare_unsigned(context, 0u, first_loop_bytes, vertex_bytes);
        context->ctr = 0u;
    }

    guest_store_u16(memory, gx + 0x02u, 1u, services, 0x804BA8CCu);
    context->gpr[0] = 1u;
    context->gpr[3] = 0xCC010000u;
    context->gpr[6] = gx;
    context->gpr[7] = first_loop_bytes;
    context->gpr[8] = vertex_bytes;
}

inline void native_gx_begin_804BA6B0(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    // This helper contains a nested translated call. Materialize the exact
    // RMGE01 GXBegin frame before that call instead of retaining architectural
    // state in C++ locals: a guest interrupt may unwind the native C++ stack
    // and later resume at 0x804BA6EC through OSLoadContext/RFI.
    const std::uint32_t frame = context->gpr[1] + 0xFFFFFFE0u;
    guest_store_u32(memory, frame, context->gpr[1], services, guest_pc);
    context->gpr[1] = frame;
    context->gpr[0] = context->lr;
    guest_store_u32(
        memory, frame + 0x24u, context->gpr[0], services, 0x804BA6B8u);
    guest_store_u32(
        memory, frame + 0x1Cu, context->gpr[31], services, 0x804BA6BCu);
    context->gpr[31] =
        guest_load_u32(memory, context->gpr[2] + 0x2500u, services, 0x804BA6C0u);
    guest_store_u32(
        memory, frame + 0x18u, context->gpr[30], services, 0x804BA6C4u);
    context->gpr[30] = context->gpr[5] | context->gpr[5];
    guest_store_u32(
        memory, frame + 0x14u, context->gpr[29], services, 0x804BA6CCu);
    context->gpr[29] = context->gpr[4] | context->gpr[4];
    guest_store_u32(
        memory, frame + 0x10u, context->gpr[28], services, 0x804BA6D4u);
    context->gpr[28] = context->gpr[3] | context->gpr[3];

    if (guest_load_u32(
            memory, context->gpr[31] + 0x5FCu, services, 0x804BA6DCu) != 0u) {
        context->lr = 0x804BA6ECu;
        context->pc = 0x804BA438u;
        static std::uint32_t cached_target_804BA6E8 = 0u;
        static NativeGameFunction cached_function_804BA6E8 = nullptr;
        call_guest_cached(
            services,
            0x804BA438u,
            &cached_target_804BA6E8,
            &cached_function_804BA6E8,
            context,
            memory,
            0x804BA6E8u);
        call_return_checkpoint(
            services, 0x804BA6E8u, 0x804BA6ECu, context, memory);
    }

    if (guest_load_u32(
            memory, context->gpr[31], services, 0x804BA6ECu) == 0u) {
        context->lr = 0x804BA6ECu;
        native_gx_send_zero_primitive_804BA7FC(
            context, memory, services, 0x804BA7FCu);
    }

    const std::uint32_t wgpipe_base = 0xCC010000u;
    guest_store_u8(
        memory,
        wgpipe_base + 0xFFFF8000u,
        static_cast<std::uint8_t>(context->gpr[29] | context->gpr[28]),
        services,
        0x804BA7D4u);
    guest_store_u16(
        memory,
        wgpipe_base + 0xFFFF8000u,
        static_cast<std::uint16_t>(context->gpr[30]),
        services,
        0x804BA7D8u);

    context->gpr[0] =
        guest_load_u32(memory, frame + 0x24u, services, 0x804BA7DCu);
    context->gpr[31] =
        guest_load_u32(memory, frame + 0x1Cu, services, 0x804BA7E0u);
    context->gpr[30] =
        guest_load_u32(memory, frame + 0x18u, services, 0x804BA7E4u);
    context->gpr[29] =
        guest_load_u32(memory, frame + 0x14u, services, 0x804BA7E8u);
    context->gpr[28] =
        guest_load_u32(memory, frame + 0x10u, services, 0x804BA7ECu);
    context->gpr[3] = wgpipe_base;
    context->lr = context->gpr[0];
    context->gpr[1] = frame + 0x20u;
}

inline void native_gx_set_array_804BE59C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    context->gpr[6] = 0xCC010000u;
    context->gpr[0] = 0x10u;
    guest_store_u8(
        memory,
        context->gpr[6] + 0xFFFF8000u,
        static_cast<std::uint8_t>(context->gpr[0]),
        services,
        0x804BE5A4u);
    context->gpr[5] = 0x1005u;
    context->gpr[4] =
        guest_load_u32(memory, context->gpr[2] + 0x2500u, services, 0x804BE5ACu);
    context->gpr[0] = 1u;
    guest_store_u32(memory, context->gpr[6] + 0xFFFF8000u, context->gpr[5], services, 0x804BE5B4u);
    guest_store_u32(memory, context->gpr[6] + 0xFFFF8000u, context->gpr[3], services, 0x804BE5B8u);
    guest_store_u16(
        memory,
        context->gpr[4] + 0x02u,
        static_cast<std::uint16_t>(context->gpr[0]),
        services,
        0x804BE5BCu);
}

inline void native_gx_load_pos_mtx_imm_804BE1D8(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);
    if (context->gqr[0] == 0u) {
        require_paired_single_mode(context, true, services, 0x804BE1D8u);

        const std::byte* matrix =
            resolve_guest(memory, context->gpr[3], 48, services, 0x804BE1F4u);
        const std::uint32_t wgpipe = 0xCC008000u;
        context->gpr[5] = 0xCC010000u;
        context->gpr[0] = 0x10u;
        context->gpr[4] = std::rotl(context->gpr[4], 2) & 0xFFFFFFFCu;
        context->gpr[0] = context->gpr[4] | 0x000B0000u;
        context->gpr[4] = wgpipe;

        const auto load_lane = [&](std::uint32_t offset) {
            const std::uint32_t bits = native_load_guest_u32_fast(matrix, offset);
            return widen_f32_bits(bits);
        };
        std::byte fifo_bytes[53]{std::byte{0x10}};
        const auto emit_be32 = [&](
            std::uint32_t offset,
            std::uint32_t value) {
            fifo_bytes[offset + 0u] =
                static_cast<std::byte>((value >> 24u) & 0xFFu);
            fifo_bytes[offset + 1u] =
                static_cast<std::byte>((value >> 16u) & 0xFFu);
            fifo_bytes[offset + 2u] =
                static_cast<std::byte>((value >> 8u) & 0xFFu);
            fifo_bytes[offset + 3u] = static_cast<std::byte>(value & 0xFFu);
        };
        emit_be32(1u, context->gpr[0]);

        const auto store_lane = [&](std::uint32_t offset, std::uint32_t fifo_offset) {
            const std::uint32_t bits =
                narrow_paired_single_ftz(load_lane(offset));
            emit_be32(fifo_offset, bits);
            return widen_f32_bits(bits);
        };

        context->fpr_bits[5] = store_lane(0x00u, 5u);
        context->ps1_bits[5] = store_lane(0x04u, 9u);
        context->fpr_bits[4] = store_lane(0x08u, 13u);
        context->ps1_bits[4] = store_lane(0x0Cu, 17u);
        context->fpr_bits[3] = store_lane(0x10u, 21u);
        context->ps1_bits[3] = store_lane(0x14u, 25u);
        context->fpr_bits[2] = store_lane(0x18u, 29u);
        context->ps1_bits[2] = store_lane(0x1Cu, 33u);
        context->fpr_bits[1] = store_lane(0x20u, 37u);
        context->ps1_bits[1] = store_lane(0x24u, 41u);
        context->fpr_bits[0] = store_lane(0x28u, 45u);
        context->ps1_bits[0] = store_lane(0x2Cu, 49u);
        if (memory == nullptr || memory->write_device == nullptr ||
            !memory->write_device(
                memory->user, wgpipe,
                static_cast<std::uint32_t>(sizeof(fifo_bytes)), fifo_bytes)) {
            guest_execution_fault(
                services, 0x804BE20Cu,
                "GX position matrix native FIFO service is unavailable");
        }
        return;
    }

    context->gpr[5] = 0xCC010000u;
    context->gpr[0] = 0x10u;
    context->gpr[4] = std::rotl(context->gpr[4], 2) & 0xFFFFFFFCu;
    guest_store_u8(
        memory, context->gpr[5] + 0xFFFF8000u,
        static_cast<std::uint8_t>(context->gpr[0]), services, 0x804BE1E4u);
    context->gpr[0] = context->gpr[4] | 0x000B0000u;
    guest_store_u32(
        memory, context->gpr[5] + 0xFFFF8000u,
        context->gpr[0], services, 0x804BE1ECu);
    context->gpr[4] = context->gpr[5] + 0xFFFF8000u;

    psq_load(
        context, 5u, memory, context->gpr[3] + 0x00u,
        0u, false, true, services, 0x804BE1F4u);
    psq_load(
        context, 4u, memory, context->gpr[3] + 0x08u,
        0u, false, true, services, 0x804BE1F8u);
    psq_load(
        context, 3u, memory, context->gpr[3] + 0x10u,
        0u, false, true, services, 0x804BE1FCu);
    psq_load(
        context, 2u, memory, context->gpr[3] + 0x18u,
        0u, false, true, services, 0x804BE200u);
    psq_load(
        context, 1u, memory, context->gpr[3] + 0x20u,
        0u, false, true, services, 0x804BE204u);
    psq_load(
        context, 0u, memory, context->gpr[3] + 0x28u,
        0u, false, true, services, 0x804BE208u);

    psq_store(
        context, 5u, memory, context->gpr[4],
        0u, false, true, services, 0x804BE20Cu);
    psq_store(
        context, 4u, memory, context->gpr[4],
        0u, false, true, services, 0x804BE210u);
    psq_store(
        context, 3u, memory, context->gpr[4],
        0u, false, true, services, 0x804BE214u);
    psq_store(
        context, 2u, memory, context->gpr[4],
        0u, false, true, services, 0x804BE218u);
    psq_store(
        context, 1u, memory, context->gpr[4],
        0u, false, true, services, 0x804BE21Cu);
    psq_store(
        context, 0u, memory, context->gpr[4],
        0u, false, true, services, 0x804BE220u);
}

inline void native_gx_begin_804BDEA8(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t frame = context->gpr[1] + 0xFFFFFFE0u;
    guest_store_u32(memory, frame, context->gpr[1], services, guest_pc);
    context->gpr[1] = frame;
    context->gpr[0] = context->lr;
    guest_store_u32(memory, frame + 0x24u, context->gpr[0], services, 0x804BDEB0u);
    guest_store_u32(memory, frame + 0x1Cu, context->gpr[31], services, 0x804BDEB4u);
    context->gpr[31] =
        guest_load_u32(memory, context->gpr[2] + 0x2500u, services, 0x804BDEB8u);
    guest_store_u32(memory, frame + 0x18u, context->gpr[30], services, 0x804BDEBCu);
    context->gpr[30] = context->gpr[4] | context->gpr[4];
    guest_store_u32(memory, frame + 0x14u, context->gpr[29], services, 0x804BDEC4u);
    context->gpr[29] = context->gpr[3] | context->gpr[3];

    context->gpr[0] =
        guest_load_u32(memory, context->gpr[31] + 0x5FCu, services, 0x804BDECCu);
    compare_signed(context, 0u, static_cast<std::int32_t>(context->gpr[0]), 0);
    if (!cr_bit(context, 2u)) {
        context->lr = 0x804BDEDCu;
        context->pc = 0x804BA438u;
        static std::uint32_t cached_target_804BDED8 = 0u;
        static NativeGameFunction cached_function_804BDED8 = nullptr;
        call_guest_cached(
            services,
            0x804BA438u,
            &cached_target_804BDED8,
            &cached_function_804BDED8,
            context,
            memory,
            0x804BDED8u);
    }

    context->gpr[0] =
        guest_load_u32(memory, context->gpr[31], services, 0x804BDEDCu);
    compare_signed(context, 0u, static_cast<std::int32_t>(context->gpr[0]), 0);
    if (cr_bit(context, 2u)) {
        context->lr = 0x804BDEECu;
        native_gx_send_zero_primitive_804BA7FC(
            context, memory, services, 0x804BA7FCu);
    }

    context->gpr[3] = 0xCC010000u;
    context->gpr[0] = 0x40u;
    guest_store_u8(
        memory, context->gpr[3] + 0xFFFF8000u,
        static_cast<std::uint8_t>(context->gpr[0]), services, 0x804BDEF4u);
    guest_store_u32(
        memory, context->gpr[3] + 0xFFFF8000u,
        context->gpr[29], services, 0x804BDEF8u);
    guest_store_u32(
        memory, context->gpr[3] + 0xFFFF8000u,
        context->gpr[30], services, 0x804BDEFCu);

    context->gpr[0] =
        guest_load_u32(memory, frame + 0x24u, services, 0x804BDF00u);
    context->gpr[31] =
        guest_load_u32(memory, frame + 0x1Cu, services, 0x804BDF04u);
    context->gpr[30] =
        guest_load_u32(memory, frame + 0x18u, services, 0x804BDF08u);
    context->gpr[29] =
        guest_load_u32(memory, frame + 0x14u, services, 0x804BDF0Cu);
    context->lr = context->gpr[0];
    context->gpr[1] = frame + 0x20u;
}

inline void native_name_hash(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    std::uint32_t hash = 0;
    std::uint32_t address = context->gpr[3];
    for (;;) {
        const std::uint8_t byte =
            guest_load_u8(memory, address, services, guest_pc);
        const auto signed_byte =
            static_cast<std::int32_t>(static_cast<std::int8_t>(byte));
        if (signed_byte == 0) {
            context->gpr[3] = hash;
            return;
        }
        hash = hash * 31u + static_cast<std::uint32_t>(signed_byte);
        ++address;
    }
}

inline void native_strstr(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t haystack = context->gpr[3];
    const std::uint32_t needle = context->gpr[4];
    if (needle == 0u) {
        return;
    }

    const std::uint8_t first =
        guest_load_u8(memory, needle, services, guest_pc);
    if (first == 0u) {
        context->gpr[3] = haystack;
        return;
    }

    for (std::uint32_t candidate = haystack;; ++candidate) {
        const std::uint8_t hay =
            guest_load_u8(memory, candidate, services, guest_pc);
        if (hay == 0u) {
            context->gpr[3] = 0;
            return;
        }
        if (hay != first) {
            continue;
        }
        std::uint32_t h = candidate + 1u;
        std::uint32_t n = needle + 1u;
        for (;;) {
            const std::uint8_t hch =
                guest_load_u8(memory, h, services, guest_pc);
            const std::uint8_t nch =
                guest_load_u8(memory, n, services, guest_pc);
            if (hch != nch) {
                if (nch == 0u) {
                    context->gpr[3] = candidate;
                    return;
                }
                break;
            }
            if (hch == 0u) {
                context->gpr[3] = candidate;
                return;
            }
            ++h;
            ++n;
        }
    }
}

// Translates PowerPC cache-management instructions (dcbi/dcbf/dcbst and
// friends) whose real purpose is CPU<->GPU memory visibility on GameCube's
// non-coherent architecture. On this runtime the only cross-thread party is
// the render thread, which already synchronizes at coarser frame/FIFO
// boundaries -- this fence's job is only to make the current thread's writes
// visible to a thread that later acquires, which is exactly what
// acq_rel provides. seq_cst additionally guarantees a single total order
// across *every* seq_cst operation in the whole program, a guarantee nothing
// here depends on. On x86 acquire/release ordering is free (already implied
// by the hardware's strong memory model); seq_cst is not -- it lowers to a
// real serializing instruction (mfence-class). Guest code calls this once
// per 32-byte cache line in tight range loops (measured: hundreds of
// thousands of times per second), so the difference is not academic.
inline void full_memory_fence() {
    std::atomic_thread_fence(std::memory_order_acq_rel);
}

inline double fpr_as_double(const PpcContext* context, std::uint32_t index) {
    return std::bit_cast<double>(context->fpr_bits[index]);
}

inline double ps1_as_double(const PpcContext* context, std::uint32_t index) {
    return std::bit_cast<double>(context->ps1_bits[index]);
}

inline void set_fpr_double(PpcContext* context, std::uint32_t index, double value) {
    context->fpr_bits[index] = std::bit_cast<std::uint64_t>(value);
}

inline bool paired_singles_enabled(const PpcContext* context) {
    constexpr std::uint32_t kHid2Pse = 0x20000000u;
    return (context->hid2 & kHid2Pse) != 0;
}

GALAXY_NOINLINE inline std::uint64_t widen_f32_bits_special(std::uint32_t bits) {
    const std::uint64_t sign = static_cast<std::uint64_t>(bits >> 31) << 63;
    const std::uint32_t exponent = (bits >> 23) & 0xFFu;
    const std::uint32_t fraction = bits & 0x007FFFFFu;
    if (exponent == 0xFFu) {
        return sign | 0x7FF0000000000000ull |
               (static_cast<std::uint64_t>(fraction) << 29);
    }
    if (exponent != 0) {
        // Normal binary32 values widen exactly: shift exponent/fraction
        // together, then add the exponent bias. The fraction cannot carry
        // into that addition. Keep zeros, subnormals and NaN payloads below
        // on their integer paths, independent of the host FP environment.
        return sign | ((static_cast<std::uint64_t>(bits & 0x7FFFFFFFu) << 29) +
                       0x3800000000000000ull);
    }
    if (fraction == 0) {
        return sign;
    }

    const std::uint32_t leading =
        31u - static_cast<std::uint32_t>(std::countl_zero(fraction));
    const std::uint64_t wide_exponent =
        static_cast<std::uint64_t>(static_cast<std::int32_t>(leading) - 149 + 1023)
        << 52;
    const std::uint64_t wide_fraction =
        static_cast<std::uint64_t>(fraction ^ (1u << leading)) << (52u - leading);
    return sign | wide_exponent | wide_fraction;
}

// Normal binary32 values and signed zero widen with two integer operations;
// NaN, infinity and subnormals take the out-of-line complete conversion.
GALAXY_ALWAYS_INLINE std::uint64_t widen_f32_bits(std::uint32_t bits) {
    const std::uint32_t magnitude = bits & 0x7FFFFFFFu;
    const std::uint64_t sign = static_cast<std::uint64_t>(bits >> 31) << 63;
    if (magnitude - 0x00800000u < 0x7F000000u) [[likely]] {
        return sign | ((static_cast<std::uint64_t>(magnitude) << 29) +
                       0x3800000000000000ull);
    }
    if (magnitude == 0u) {
        return sign;
    }
    return widen_f32_bits_special(bits);
}

GALAXY_ALWAYS_INLINE void load_fpr_single(
    PpcContext* context,
    std::uint32_t index,
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t bits = guest_load_u32(memory, address, services, guest_pc);
    const std::uint64_t widened = widen_f32_bits(bits);
    context->fpr_bits[index] = widened;
    if (paired_singles_enabled(context)) {
        context->ps1_bits[index] = widened;
    }
}

inline std::uint32_t narrow_f64_to_f32_bits(
    std::uint64_t bits,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t exponent = static_cast<std::uint32_t>((bits >> 52) & 0x7FFu);
    const std::uint64_t magnitude = bits & 0x7FFFFFFFFFFFFFFFull;
    std::uint32_t output = 0;
    if (exponent > 896u || magnitude == 0) {
        output = static_cast<std::uint32_t>((bits >> 32) & 0xC0000000ull) |
                 static_cast<std::uint32_t>((bits >> 29) & 0x3FFFFFFFull);
    } else if (exponent >= 874u) {
        const std::uint32_t shift = 926u - exponent;
        const std::uint64_t significand =
            0x0010000000000000ull | (bits & 0x000FFFFFFFFFFFFFull);
        output = static_cast<std::uint32_t>(bits >> 32) & 0x80000000u;
        output |= static_cast<std::uint32_t>(significand >> shift) & 0x007FFFFFu;
    } else {
        guest_execution_fault(
            services,
            guest_pc,
            "undefined PowerPC single-precision store conversion");
    }
    return output;
}

GALAXY_NOINLINE inline std::uint32_t require_single_precision_bits_slow(
    std::uint64_t bits,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t narrowed =
        narrow_f64_to_f32_bits(bits, services, guest_pc);
    if (widen_f32_bits(narrowed) != bits) {
        guest_execution_fault(
            services,
            guest_pc,
            "single-precision instruction received a non-single operand");
    }
    return narrowed;
}

// Keep the proven normal/zero path at the caller; share conversion and faults.
GALAXY_ALWAYS_INLINE std::uint32_t require_single_precision_bits(
    std::uint64_t bits,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint64_t magnitude = bits & 0x7FFFFFFFFFFFFFFFull;
    const std::uint32_t sign = static_cast<std::uint32_t>(bits >> 32u) & 0x80000000u;
    // A widened normal binary32 has exponent 897..1150 and its low 29
    // fraction bits clear. This proves the round trip directly with integers,
    // without rounding or changing the host FP environment. Exceptional and
    // non-single encodings retain the original conversion/fault checks below.
    if (magnitude >= 0x3810000000000000ull &&
        magnitude < 0x47F0000000000000ull &&
        (magnitude & 0x1FFFFFFFull) == 0u) {
        return sign | static_cast<std::uint32_t>(
            (magnitude - 0x3800000000000000ull) >> 29u);
    }
    if (magnitude == 0u) {
        return sign;
    }
    return require_single_precision_bits_slow(bits, services, guest_pc);
}

GALAXY_ALWAYS_INLINE void store_fpr_single(
    const PpcContext* context,
    std::uint32_t index,
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t output =
        native_stored_single_bits(context->fpr_bits[index], context->fpscr);
    guest_store_u32(memory, address, output, services, guest_pc);
}

inline void native_psmtx_trans_apply_804B63D8(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);

    const auto load_pair = [&](
        std::uint32_t index,
        std::uint32_t address,
        std::uint32_t pc) {
        context->fpr_bits[index] =
            widen_f32_bits(guest_load_u32(memory, address, services, pc));
        context->ps1_bits[index] =
            widen_f32_bits(guest_load_u32(memory, address + 4u, services, pc));
    };
    const auto store_pair = [&](
        std::uint32_t index,
        std::uint32_t address,
        std::uint32_t pc) {
        psq_store(context, index, memory, address, 0u, false, true, services, pc);
    };
    const auto frsp = [&](std::uint32_t index, std::uint32_t pc) {
        ppc_commit_scalar_result(
            context,
            index,
            ppc_round_f64_to_f32(context->fpr_bits[index], context->fpscr),
            false,
            false,
            services,
            pc);
    };
    const auto ps_sum1_with_scalar = [&](
        std::uint32_t index,
        std::uint32_t scalar_index,
        std::uint32_t pc) {
        require_paired_single_mode(context, false, services, pc);
        const PpcFloatResult lane0 = ppc_f32_passthrough(
            require_single_precision_bits(context->fpr_bits[index], services, pc));
        const PpcFloatResult lane1 = ppc_f32_binary(
            PpcFloatBinaryOperation::Add,
            require_single_precision_bits(
                context->fpr_bits[scalar_index], services, pc),
            require_single_precision_bits(context->ps1_bits[index], services, pc),
            context->fpscr);
        ppc_commit_paired_result(
            context, index, lane0, lane1, true, false, services, pc);
    };

    const std::uint32_t src = context->gpr[3];
    const std::uint32_t dst = context->gpr[4];
    load_pair(4u, src + 0x00u, 0x804B63D8u);
    frsp(1u, 0x804B63DCu);
    load_pair(5u, src + 0x08u, 0x804B63E0u);
    frsp(2u, 0x804B63E4u);
    load_pair(7u, src + 0x18u, 0x804B63E8u);
    frsp(3u, 0x804B63ECu);
    load_pair(8u, src + 0x28u, 0x804B63F0u);
    store_pair(4u, dst + 0x00u, 0x804B63F4u);
    ps_sum1_with_scalar(5u, 1u, 0x804B63F8u);
    load_pair(6u, src + 0x10u, 0x804B63FCu);
    store_pair(5u, dst + 0x08u, 0x804B6400u);
    ps_sum1_with_scalar(7u, 2u, 0x804B6404u);
    load_pair(9u, src + 0x20u, 0x804B6408u);
    store_pair(6u, dst + 0x10u, 0x804B640Cu);
    ps_sum1_with_scalar(8u, 3u, 0x804B6410u);
    store_pair(7u, dst + 0x18u, 0x804B6414u);
    store_pair(9u, dst + 0x20u, 0x804B6418u);
    store_pair(8u, dst + 0x28u, 0x804B641Cu);
}

inline void native_psmtx_scale_804B6424(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);

    const auto store_pair = [&](
        std::uint32_t index,
        std::uint32_t address,
        std::uint32_t pc) {
        psq_store(context, index, memory, address, 0u, false, true, services, pc);
    };

    const std::uint32_t dst = context->gpr[3];
    load_fpr_single(
        context, 0u, memory, context->gpr[2] + 0x000024B4u, services, 0x804B6424u);
    store_fpr_single(context, 1u, memory, dst + 0x00u, services, 0x804B6428u);
    store_pair(0u, dst + 0x04u, 0x804B642Cu);
    store_pair(0u, dst + 0x0Cu, 0x804B6430u);
    store_fpr_single(context, 2u, memory, dst + 0x14u, services, 0x804B6434u);
    store_pair(0u, dst + 0x18u, 0x804B6438u);
    store_pair(0u, dst + 0x20u, 0x804B643Cu);
    store_fpr_single(context, 3u, memory, dst + 0x28u, services, 0x804B6440u);
    store_fpr_single(context, 0u, memory, dst + 0x2Cu, services, 0x804B6444u);
}

inline void native_jpa_alpha_callback_804465A0(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);

    const std::uint32_t frame = context->gpr[1] + 0xFFFFFFF0u;
    guest_store_u32(memory, frame, context->gpr[1], services, 0x804465A0u);
    context->gpr[1] = frame;

    load_fpr_single(
        context, 2u, memory, context->gpr[2] + 0x00001FD4u, services, 0x804465A4u);
    load_fpr_single(
        context, 1u, memory, context->gpr[4] + 0x00000084u, services, 0x804465A8u);
    load_fpr_single(
        context, 0u, memory, context->gpr[2] + 0x00001FD0u, services, 0x804465ACu);

    ppc_commit_scalar_result(
        context,
        1u,
        ppc_f64_binary_to_f32(
            PpcFloatBinaryOperation::Subtract,
            context->fpr_bits[2],
            context->fpr_bits[1],
            context->fpscr),
        true,
        false,
        services,
        0x804465B0u);
    ppc_commit_scalar_result(
        context,
        0u,
        ppc_f64_binary_to_f32(
            PpcFloatBinaryOperation::Multiply,
            context->fpr_bits[0],
            context->fpr_bits[1],
            context->fpscr),
        true,
        false,
        services,
        0x804465B4u);

    psq_store(
        context,
        0u,
        memory,
        context->gpr[1] + 0x00000008u,
        2u,
        true,
        true,
        services,
        0x804465B8u);
    context->gpr[0] =
        guest_load_u8(memory, context->gpr[1] + 0x00000008u, services, 0x804465BCu);
    guest_store_u8(
        memory,
        context->gpr[4] + 0x00000096u,
        static_cast<std::uint8_t>(context->gpr[0]),
        services,
        0x804465C0u);
    context->gpr[1] = context->gpr[1] + 0x00000010u;
}

inline void native_jpa_scale_callback_804465CC(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);

    load_fpr_single(
        context, 2u, memory, context->gpr[2] + 0x00001FD4u, services, 0x804465CCu);
    load_fpr_single(
        context, 0u, memory, context->gpr[4] + 0x00000084u, services, 0x804465D0u);
    load_fpr_single(
        context, 1u, memory, context->gpr[4] + 0x00000068u, services, 0x804465D4u);
    ppc_commit_scalar_result(
        context,
        2u,
        ppc_f64_binary_to_f32(
            PpcFloatBinaryOperation::Subtract,
            context->fpr_bits[2],
            context->fpr_bits[0],
            context->fpscr),
        true,
        false,
        services,
        0x804465D8u);
    load_fpr_single(
        context, 0u, memory, context->gpr[4] + 0x0000006Cu, services, 0x804465DCu);
    ppc_commit_scalar_result(
        context,
        1u,
        ppc_f64_binary_to_f32(
            PpcFloatBinaryOperation::Multiply,
            context->fpr_bits[1],
            context->fpr_bits[2],
            context->fpscr),
        true,
        false,
        services,
        0x804465E0u);
    ppc_commit_scalar_result(
        context,
        0u,
        ppc_f64_binary_to_f32(
            PpcFloatBinaryOperation::Multiply,
            context->fpr_bits[0],
            context->fpr_bits[2],
            context->fpscr),
        true,
        false,
        services,
        0x804465E4u);
    store_fpr_single(
        context, 1u, memory, context->gpr[4] + 0x00000060u, services, 0x804465E8u);
    store_fpr_single(
        context, 0u, memory, context->gpr[4] + 0x00000064u, services, 0x804465ECu);
}

inline void native_mtx_scale_803A387C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);
    // Preserve the DOL's interleaved register effects and stores. In particular,
    // the final multiply may trap after the first store: publishing only after
    // the whole helper returned lost that completed write to both consumers.
    const auto load_single = [&](std::uint32_t index, std::uint32_t offset, std::uint32_t pc) {
        ensure_fpu_available(services, pc, context, memory);
        load_fpr_single(context, index, memory, context->gpr[3] + offset, services, pc);
    };
    const auto multiply = [&](
        std::uint32_t target,
        std::uint64_t left,
        std::uint64_t right,
        std::uint32_t pc) {
        ensure_fpu_available(services, pc, context, memory);
        const PpcFloatResult result =
            ppc_f64_binary_to_f32(PpcFloatBinaryOperation::Multiply, left, right, context->fpscr);
        ppc_commit_scalar_result(context, target, result, true, false, services, pc);
    };
    const auto store_single = [&](std::uint32_t index, std::uint32_t offset, std::uint32_t pc) {
        ensure_fpu_available(services, pc, context, memory);
        store_fpr_single(context, index, memory, context->gpr[3] + offset, services, pc);
    };

    load_single(3u, 0x00u, 0x803A387Cu);
    load_single(0u, 0x10u, 0x803A3880u);
    multiply(10u, context->fpr_bits[3], context->fpr_bits[1], 0x803A3884u);
    load_single(4u, 0x20u, 0x803A3888u);
    multiply(9u, context->fpr_bits[0], context->fpr_bits[1], 0x803A388Cu);
    load_single(3u, 0x04u, 0x803A3890u);
    multiply(8u, context->fpr_bits[4], context->fpr_bits[1], 0x803A3894u);
    load_single(0u, 0x14u, 0x803A3898u);
    multiply(7u, context->fpr_bits[3], context->fpr_bits[2], 0x803A389Cu);
    load_single(5u, 0x24u, 0x803A38A0u);
    multiply(6u, context->fpr_bits[0], context->fpr_bits[2], 0x803A38A4u);
    load_single(4u, 0x08u, 0x803A38A8u);
    load_single(0u, 0x28u, 0x803A38ACu);
    multiply(5u, context->fpr_bits[5], context->fpr_bits[2], 0x803A38B0u);
    load_single(3u, 0x18u, 0x803A38B4u);
    multiply(4u, context->fpr_bits[4], context->fpr_bits[1], 0x803A38B8u);
    multiply(0u, context->fpr_bits[0], context->fpr_bits[1], 0x803A38BCu);
    store_single(10u, 0x00u, 0x803A38C0u);
    multiply(2u, context->fpr_bits[3], context->fpr_bits[1], 0x803A38C4u);
    store_single(9u, 0x10u, 0x803A38C8u);
    store_single(8u, 0x20u, 0x803A38CCu);
    store_single(7u, 0x04u, 0x803A38D0u);
    store_single(6u, 0x14u, 0x803A38D4u);
    store_single(5u, 0x24u, 0x803A38D8u);
    store_single(4u, 0x08u, 0x803A38DCu);
    store_single(2u, 0x18u, 0x803A38E0u);
    store_single(0u, 0x28u, 0x803A38E4u);
}

inline std::uint32_t narrow_paired_single_ftz(std::uint64_t bits) {
    const std::uint32_t exponent = static_cast<std::uint32_t>((bits >> 52) & 0x7FFu);
    const std::uint64_t magnitude = bits & 0x7FFFFFFFFFFFFFFFull;
    if (exponent > 896u || magnitude == 0) {
        return static_cast<std::uint32_t>((bits >> 32) & 0xC0000000ull) |
               static_cast<std::uint32_t>((bits >> 29) & 0x3FFFFFFFull);
    }
    return static_cast<std::uint32_t>(bits >> 32) & 0x80000000u;
}

inline std::int32_t sign_extend_gqr_scale(std::uint32_t scale) {
    return (scale & 0x20u) != 0 ? static_cast<std::int32_t>(scale) - 64
                                : static_cast<std::int32_t>(scale);
}

inline std::uint32_t psq_load_type(std::uint32_t gqr) {
    return (gqr >> 16) & 0x7u;
}

inline std::int32_t psq_load_scale(std::uint32_t gqr) {
    return sign_extend_gqr_scale((gqr >> 24) & 0x3Fu);
}

inline std::uint32_t psq_store_type(std::uint32_t gqr) {
    return gqr & 0x7u;
}

inline std::int32_t psq_store_scale(std::uint32_t gqr) {
    return sign_extend_gqr_scale((gqr >> 8) & 0x3Fu);
}

inline void require_paired_single_mode(
    const PpcContext* context,
    bool requires_quantized_load_store,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    constexpr std::uint32_t kHid2Lsqe = 0x80000000u;
    constexpr std::uint32_t kHid2Pse = 0x20000000u;
    const std::uint32_t required =
        kHid2Pse | (requires_quantized_load_store ? kHid2Lsqe : 0u);
    if ((context->hid2 & required) != required) {
        guest_execution_fault(
            services,
            guest_pc,
            "paired-single instruction executed while HID2 mode is disabled");
    }
}

inline std::uint32_t psq_element_size(
    std::uint32_t type,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    switch (type) {
    case 0:
        return 4;
    case 4:
    case 6:
        return 1;
    case 5:
    case 7:
        return 2;
    default:
        guest_execution_fault(services, guest_pc, "reserved paired-single quantization type");
    }
}

// Exact 2^exponent for the 6-bit signed GQR scale range (-32..31) and its
// negation (-31..32). Scaling a finite double by a power of two is the same
// operation std::ldexp performs, without the library call.
inline double psq_power_of_two(std::int32_t exponent) {
    return std::bit_cast<double>(
        static_cast<std::uint64_t>(1023 + exponent) << 52);
}

inline std::uint64_t psq_load_integer_bits(std::int32_t value, std::int32_t scale) {
    const double scaled = static_cast<double>(value) * psq_power_of_two(-scale);
    return std::bit_cast<std::uint64_t>(scaled);
}

inline std::uint64_t psq_load_element(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t type,
    std::int32_t scale,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    switch (type) {
    case 0:
        return widen_f32_bits(guest_load_u32(memory, address, services, guest_pc));
    case 4:
        return psq_load_integer_bits(
            guest_load_u8(memory, address, services, guest_pc), scale);
    case 5:
        return psq_load_integer_bits(
            guest_load_u16(memory, address, services, guest_pc), scale);
    case 6: {
        const std::uint32_t value =
            guest_load_u8(memory, address, services, guest_pc);
        const std::int32_t signed_value =
            (value & 0x80u) != 0 ? static_cast<std::int32_t>(value) - 0x100
                                 : static_cast<std::int32_t>(value);
        return psq_load_integer_bits(signed_value, scale);
    }
    case 7: {
        const std::uint32_t value =
            guest_load_u16(memory, address, services, guest_pc);
        const std::int32_t signed_value =
            (value & 0x8000u) != 0 ? static_cast<std::int32_t>(value) - 0x10000
                                   : static_cast<std::int32_t>(value);
        return psq_load_integer_bits(signed_value, scale);
    }
    default:
        guest_execution_fault(services, guest_pc, "reserved paired-single load type");
    }
}

inline bool f64_is_nan(std::uint64_t bits);

inline std::uint32_t psq_quantize_unsigned(
    std::uint64_t bits,
    std::int32_t scale,
    std::uint32_t maximum) {
    if (f64_is_nan(bits)) {
        return maximum;
    }
    const bool negative = (bits >> 63) != 0;
    const std::uint64_t magnitude = bits & 0x7FFFFFFFFFFFFFFFull;
    if (magnitude == 0 || negative) {
        return 0;
    }
    if ((bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull) {
        return maximum;
    }
    const double scaled = std::bit_cast<double>(bits) * psq_power_of_two(scale);
    if (scaled >= static_cast<double>(maximum)) {
        return maximum;
    }
    if (scaled <= 0.0) {
        return 0;
    }
    return static_cast<std::uint32_t>(scaled);
}

inline std::int32_t psq_quantize_signed(
    std::uint64_t bits,
    std::int32_t scale,
    std::int32_t minimum,
    std::int32_t maximum) {
    if (f64_is_nan(bits)) {
        return maximum;
    }
    const bool negative = (bits >> 63) != 0;
    const std::uint64_t magnitude = bits & 0x7FFFFFFFFFFFFFFFull;
    if (magnitude == 0) {
        return 0;
    }
    if ((bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull) {
        return negative ? minimum : maximum;
    }
    const double scaled = std::bit_cast<double>(bits) * psq_power_of_two(scale);
    if (scaled >= static_cast<double>(maximum)) {
        return maximum;
    }
    if (scaled <= static_cast<double>(minimum)) {
        return minimum;
    }
    return static_cast<std::int32_t>(scaled);
}

inline void psq_store_element(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint64_t bits,
    std::uint32_t type,
    std::int32_t scale,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    switch (type) {
    case 0:
        guest_store_u32(
            memory, address, narrow_paired_single_ftz(bits), services, guest_pc);
        return;
    case 4:
        guest_store_u8(
            memory,
            address,
            static_cast<std::uint8_t>(psq_quantize_unsigned(bits, scale, 0xFFu)),
            services,
            guest_pc);
        return;
    case 5:
        guest_store_u16(
            memory,
            address,
            static_cast<std::uint16_t>(psq_quantize_unsigned(bits, scale, 0xFFFFu)),
            services,
            guest_pc);
        return;
    case 6:
        guest_store_u8(
            memory,
            address,
            static_cast<std::uint8_t>(
                psq_quantize_signed(bits, scale, -128, 127)),
            services,
            guest_pc);
        return;
    case 7:
        guest_store_u16(
            memory,
            address,
            static_cast<std::uint16_t>(
                psq_quantize_signed(bits, scale, -32768, 32767)),
            services,
            guest_pc);
        return;
    default:
        guest_execution_fault(services, guest_pc, "reserved paired-single store type");
    }
}

inline void psq_load(
    PpcContext* context,
    std::uint32_t target,
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t gqr_index,
    bool one_element,
    bool d_form,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    require_paired_single_mode(context, d_form, services, guest_pc);
    const std::uint32_t gqr = context->gqr[gqr_index];
    const std::uint32_t type = psq_load_type(gqr);
    // The host's complete fast-region capability is also what scalar accesses
    // trust. Do not repeat fixed MEM1/MEM2 alias/size classification before it;
    // the capability checks the actual span and includes locked-cache RAM.
    // A partial mapping retains the ordered scalar path, including the first
    // lane's effect before a fault.
    // Keep the existing bit conversion and lane assignment order.
    if (type == 0u && !trace_fileloader_stack_enabled()) {
        if (const std::byte* direct = resolve_guest_fast(
                memory, address, one_element ? 4u : 8u); direct != nullptr) {
            context->fpr_bits[target] = widen_f32_bits(
                guest_flat_load_bytes<std::uint32_t>(direct, 0u));
            context->ps1_bits[target] = one_element
                ? 0x3FF0000000000000ull
                : widen_f32_bits(guest_flat_load_bytes<std::uint32_t>(direct, 4u));
            return;
        }
    }
    // Type0 checked RAM needs neither quantized scale nor element-size dispatch.
    // Reserved-type faults still occur before the first scalar effect.
    const std::int32_t scale = psq_load_scale(gqr);
    const std::uint32_t element_size = psq_element_size(type, services, guest_pc);
    context->fpr_bits[target] =
        psq_load_element(memory, address, type, scale, services, guest_pc);
    context->ps1_bits[target] =
        one_element
            ? 0x3FF0000000000000ull
            : psq_load_element(
                  memory,
                  address + element_size,
                  type,
                  scale,
                  services,
                  guest_pc);
}

inline void psq_store(
    const PpcContext* context,
    std::uint32_t source,
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t gqr_index,
    bool one_element,
    bool d_form,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    require_paired_single_mode(context, d_form, services, guest_pc);
    const std::uint32_t gqr = context->gqr[gqr_index];
    const std::uint32_t type = psq_store_type(gqr);
    if (type == 0u && !trace_fileloader_temp_writes_enabled() &&
        !trace_u32_store_enabled()) {
        if (std::byte* direct = resolve_guest_fast(
                memory, address, one_element ? 4u : 8u); direct != nullptr) {
            const std::uint32_t first = byte_swap_u32(
                narrow_paired_single_ftz(context->fpr_bits[source]));
            std::memcpy(direct, &first, sizeof(first));
            // Runtime RAM has a callback even when the dirty tracker fully
            // owns a write and suppresses that callback. In that case both
            // lanes can use the existing same-page publication path. Prove
            // complete coverage; a partial tracker or an observable callback
            // must retain lane-zero notification before evaluating lane one.
            bool pair_observer_suppressed = false;
            std::uint32_t shared_page = 0u;
            if (!one_element && memory->notify_write != nullptr &&
                memory->dirty_page_words != nullptr) {
                pair_observer_suppressed = guest_tracker_single_page(
                    address, 8u, memory->dirty_tracked_base,
                    memory->dirty_tracked_size, memory->dirty_page_shift,
                    memory->dirty_page_word_count, shared_page);
            }
            if (one_element ||
                (memory->notify_write != nullptr && !pair_observer_suppressed)) {
                // An observer may inspect RAM or affect the next lane's mapping
                // and value. Notify lane zero before evaluating/storing lane one.
                guest_notify_write(memory, address, 4u);
                if (!one_element) {
                    psq_store_element(
                        memory, address + 4u, context->ps1_bits[source],
                        0u, 0, services, guest_pc);
                }
                return;
            }
            const std::uint32_t second = byte_swap_u32(
                narrow_paired_single_ftz(context->ps1_bits[source]));
            std::memcpy(direct + 4u, &second, sizeof(second));
            if (pair_observer_suppressed) {
                guest_notify_write_pair_admitted(memory, address, shared_page);
                return;
            }
            // Both halves share a tracker page unless the pair straddles one.
            // Publish them with a single lookup in the common case; the helper
            // declines (having changed nothing) on a straddling pair and the
            // two scalar notifications below then run exactly as before.
            if (guest_notify_write_pair_same_page(memory, address)) {
                return;
            }
            guest_notify_write(memory, address, 4u);
            guest_notify_write(memory, address + 4u, 4u);
            return;
        }
    }
    const std::int32_t scale = psq_store_scale(gqr);
    const std::uint32_t element_size = psq_element_size(type, services, guest_pc);
    psq_store_element(
        memory,
        address,
        context->fpr_bits[source],
        type,
        scale,
        services,
        guest_pc);
    if (!one_element) {
        psq_store_element(
            memory,
            address + element_size,
            context->ps1_bits[source],
            type,
            scale,
            services,
            guest_pc);
    }
}

inline void set_xer_ca(PpcContext* context, bool carry) {
    constexpr std::uint32_t kCarry = 0x20000000u;
    context->xer = carry ? context->xer | kCarry : context->xer & ~kCarry;
}

inline bool xer_ca(const PpcContext* context) {
    return (context->xer & 0x20000000u) != 0;
}

inline std::uint32_t arithmetic_shift_right(std::uint32_t value, std::uint32_t shift) {
    // shift is in [0,31]: sraw handles larger counts before calling, and
    // srawi carries a five-bit immediate. C++20 signed right shift performs
    // the required sign extension and rounds negative values downward.
    return static_cast<std::uint32_t>(std::bit_cast<std::int32_t>(value) >> shift);
}

// divw edge-case behavior on Broadway/Gekko with OE=0 (overflow exceptions
// disabled, which is the case for virtually all compiled game code):
//
//   divide by zero:    result = (dividend < 0) ? 0xFFFFFFFF : 0x00000000
//   INT_MIN / -1:      result = 0xFFFFFFFF  (INT_MIN sign bit is 1 → -1)
//   XER.OV is NOT set when OE=0; host UB is avoided by checking first.
//
// Source: Dolphin Interpreter_Integer.cpp, IBM PowerPC Book I §4.3.1.
inline std::uint32_t divide_signed_word(
    std::uint32_t dividend_bits,
    std::uint32_t divisor_bits,
    const NativeServicesV1* /*services*/,
    std::uint32_t /*guest_pc*/) {
    const std::int32_t dividend = static_cast<std::int32_t>(dividend_bits);
    const std::int32_t divisor = static_cast<std::int32_t>(divisor_bits);
    if (divisor == 0 || (dividend == static_cast<std::int32_t>(0x80000000u) &&
                         divisor == -1)) {
        // Hardware returns the sign-extended sign bit of the dividend.
        return (dividend_bits & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0x00000000u;
    }
    return static_cast<std::uint32_t>(dividend / divisor);
}

// divwu edge-case behavior on Broadway/Gekko with OE=0:
//   divide by zero: result = 0x00000000
inline std::uint32_t divide_unsigned_word(
    std::uint32_t dividend,
    std::uint32_t divisor,
    const NativeServicesV1* /*services*/,
    std::uint32_t /*guest_pc*/) {
    // Keep the host operation defined even when an optimizing compiler
    // specializes an inlined call with a constant zero divisor. Selecting 1
    // for that hardware edge case avoids MSVC C4723 without changing the
    // Broadway result (zero).
    const std::uint32_t safe_divisor = divisor == 0u ? 1u : divisor;
    const std::uint32_t quotient = dividend / safe_divisor;
    return divisor == 0u ? 0x00000000u : quotient;
}

// Force-inlined like `compare_signed` below: emitted at ~4,752 sites, so a
// non-inlined call here lands on the simulation thread for every record-form
// integer instruction.
GALAXY_ALWAYS_INLINE void update_cr0(PpcContext* context, std::uint32_t value) {
    // One read-modify-write of `context->cr` instead of the previous
    // write-then-second-RMW (`set_cr_field` inlined on top of the explicit
    // update). Every recorded integer instruction (`andi.`, `add.`, `mr.`,
    // `cmpwi`, the `stwu` chain, ...) runs this, so the second load/store of
    // `cr` was pure repetition on the simulation thread's critical path.
    //
    // The field is selected with two masks and no data-dependent branch:
    //   nonzero  = -(value != 0)          -> all ones exactly when value != 0
    //   negative = -(value >= 0x80000000) -> all ones exactly when LT
    //   field = (negative & 8) | (nonzero & ~negative & 4) | (~nonzero & 2)
    // i.e. LT=8 when the sign bit is set, GT=4 when it is clear and the value is
    // non-zero, EQ=2 when the value is zero. That is exactly the
    // (`value < 0 ? 8 : value > 0 ? 4 : 2`) mapping the ternary produced.
    //
    // Verified against that ternary over 4,013 values (4,000 pseudo-random plus
    // the sign/zero/adjacent boundaries). An earlier attempt here used
    // `(value & 0x80000000u) >> 27` for LT and omitted the EQ term entirely; it
    // returned 0 for value == 0 and 20 for negative values, so do not
    // "simplify" this back to a single sign mask.
    const std::uint32_t nonzero = static_cast<std::uint32_t>(
        -(static_cast<std::int32_t>(value != 0u)));
    const std::uint32_t negative = static_cast<std::uint32_t>(
        -(static_cast<std::int32_t>(value >= 0x80000000u)));
    std::uint32_t field = (negative & 0x8u) |
        (nonzero & ~negative & 0x4u) | (~nonzero & 0x2u);
    if ((context->xer & 0x80000000u) != 0) {
        field |= 0x1u;
    }
    context->cr = (context->cr & 0x0FFFFFFFu) | (field << 28);
}

// Force-inlined like `compare_signed`/`compare_unsigned` below: `cr_bit` is
// emitted at ~83,709 sites, `update_cr0` at ~4,752 and `set_cr_bit` at ~2,206
// module-wide, so a plain `inline` that a non-Release configuration declines to
// inline puts a real call on the simulation thread's critical path for every
// record-form integer instruction and every conditional branch.
GALAXY_ALWAYS_INLINE void set_cr_field(
    PpcContext* context,
    std::uint32_t index,
    std::uint32_t field) {
    const std::uint32_t shift = (7u - index) * 4u;
    const std::uint32_t mask = 0xFu << shift;
    context->cr = (context->cr & ~mask) | ((field & 0xFu) << shift);
}

inline void update_cr1_from_fpscr(PpcContext* context) {
    set_cr_field(context, 1, context->fpscr >> 28);
}

inline void write_cr_fields(PpcContext* context, std::uint32_t value, std::uint32_t mask) {
    for (std::uint32_t field = 0; field < 8; ++field) {
        const std::uint32_t field_mask = 1u << (7u - field);
        if ((mask & field_mask) != 0) {
            const std::uint32_t shift = (7u - field) * 4u;
            set_cr_field(context, field, value >> shift);
        }
    }
}

inline bool f64_is_nan(std::uint64_t bits) {
    return (bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull &&
           (bits & 0x000FFFFFFFFFFFFFull) != 0;
}

inline bool f64_is_signaling_nan(std::uint64_t bits) {
    return f64_is_nan(bits) && (bits & 0x0008000000000000ull) == 0;
}

inline void refresh_fpscr_summaries(PpcContext* context) {
    constexpr std::uint32_t kFex = 0x40000000u;
    constexpr std::uint32_t kVx = 0x20000000u;
    constexpr std::uint32_t kOx = 0x10000000u;
    constexpr std::uint32_t kUx = 0x08000000u;
    constexpr std::uint32_t kZx = 0x04000000u;
    constexpr std::uint32_t kXx = 0x02000000u;
    constexpr std::uint32_t kInvalidDetailMask = 0x01F80700u;
    constexpr std::uint32_t kVe = 0x00000080u;
    constexpr std::uint32_t kOe = 0x00000040u;
    constexpr std::uint32_t kUe = 0x00000020u;
    constexpr std::uint32_t kZe = 0x00000010u;
    constexpr std::uint32_t kXe = 0x00000008u;
    if ((context->fpscr & kInvalidDetailMask) != 0) {
        context->fpscr |= kVx;
    } else {
        context->fpscr &= ~kVx;
    }
    const bool enabled =
        (((context->fpscr & kVx) != 0) && (context->fpscr & kVe) != 0) ||
        (((context->fpscr & kOx) != 0) && (context->fpscr & kOe) != 0) ||
        (((context->fpscr & kUx) != 0) && (context->fpscr & kUe) != 0) ||
        (((context->fpscr & kZx) != 0) && (context->fpscr & kZe) != 0) ||
        (((context->fpscr & kXx) != 0) && (context->fpscr & kXe) != 0);
    context->fpscr =
        enabled ? context->fpscr | kFex : context->fpscr & ~kFex;
}

inline void set_fpscr_bit(PpcContext* context, std::uint32_t bit_index) {
    if (bit_index == 1 || bit_index == 2) {
        return;
    }
    context->fpscr |= 0x80000000u >> bit_index;
    refresh_fpscr_summaries(context);
}

inline void clear_fpscr_bit(PpcContext* context, std::uint32_t bit_index) {
    if (bit_index == 1 || bit_index == 2) {
        return;
    }
    context->fpscr &= ~(0x80000000u >> bit_index);
    refresh_fpscr_summaries(context);
}

inline void write_fpscr_fields(
    PpcContext* context,
    std::uint32_t value,
    std::uint32_t field_mask) {
    constexpr std::uint32_t kFex = 0x40000000u;
    constexpr std::uint32_t kVx = 0x20000000u;
    std::uint32_t write_mask = 0;
    for (std::uint32_t field = 0; field < 8; ++field) {
        if ((field_mask & (1u << (7u - field))) != 0) {
            write_mask |= 0xF0000000u >> (field * 4u);
        }
    }
    write_mask &= ~(kFex | kVx);
    context->fpscr =
        (context->fpscr & ~write_mask) | (value & write_mask);
    refresh_fpscr_summaries(context);
}

inline void write_fpscr_field_immediate(
    PpcContext* context,
    std::uint32_t field,
    std::uint32_t immediate) {
    const std::uint32_t shift = (7u - field) * 4u;
    write_fpscr_fields(
        context,
        (immediate & 0xFu) << shift,
        1u << (7u - field));
}

inline void record_invalid_fp_exception(PpcContext* context, std::uint32_t detail) {
    constexpr std::uint32_t kFx = 0x80000000u;
    if ((detail & ~context->fpscr) != 0) {
        context->fpscr |= kFx;
    }
    context->fpscr |= detail;
    refresh_fpscr_summaries(context);
}

GALAXY_NOINLINE inline void compare_f64_slow(
    PpcContext* context,
    std::uint32_t field_index,
    std::uint64_t left,
    std::uint64_t right,
    bool ordered) {
    constexpr std::uint32_t kVxSnan = 0x01000000u;
    constexpr std::uint32_t kVxVc = 0x00080000u;
    constexpr std::uint32_t kVe = 0x00000080u;
    const bool left_nan = f64_is_nan(left);
    const bool right_nan = f64_is_nan(right);
    const bool signaling = f64_is_signaling_nan(left) || f64_is_signaling_nan(right);
    std::uint32_t result = 0;
    if (left_nan || right_nan) {
        result = 0x1u;
        if (signaling) {
            record_invalid_fp_exception(context, kVxSnan);
        }
        if (ordered && (!signaling || (context->fpscr & kVe) == 0)) {
            record_invalid_fp_exception(context, kVxVc);
        }
    } else {
        const std::uint64_t left_magnitude = left & 0x7FFFFFFFFFFFFFFFull;
        const std::uint64_t right_magnitude = right & 0x7FFFFFFFFFFFFFFFull;
        if (left_magnitude == 0 && right_magnitude == 0) {
            result = 0x2u;
        } else {
            const bool left_negative = (left >> 63) != 0;
            const bool right_negative = (right >> 63) != 0;
            bool less = false;
            bool greater = false;
            if (left_negative != right_negative) {
                less = left_negative;
                greater = !left_negative;
            } else if (left == right) {
                result = 0x2u;
            } else if (left_negative) {
                less = left > right;
                greater = left < right;
            } else {
                less = left < right;
                greater = left > right;
            }
            if (result == 0) {
                result = less ? 0x8u : (greater ? 0x4u : 0x2u);
            }
        }
    }
    context->fpscr = (context->fpscr & ~0x0000F000u) | (result << 12);
    set_cr_field(context, field_index, result);
}

// Two orderable operands (no NaN) classify with three host compares; NaN
// handling and FPSCR exception recording stay out of line.
GALAXY_ALWAYS_INLINE void compare_f64(
    PpcContext* context,
    std::uint32_t field_index,
    std::uint64_t left,
    std::uint64_t right,
    bool ordered) {
    if ((left & 0x7FFFFFFFFFFFFFFFull) <= 0x7FF0000000000000ull &&
        (right & 0x7FFFFFFFFFFFFFFFull) <= 0x7FF0000000000000ull) [[likely]] {
        const double left_value = std::bit_cast<double>(left);
        const double right_value = std::bit_cast<double>(right);
        const std::uint32_t ordered_result =
            left_value < right_value ? 0x8u
            : (left_value > right_value ? 0x4u : 0x2u);
        context->fpscr = (context->fpscr & ~0x0000F000u) | (ordered_result << 12);
        set_cr_field(context, field_index, ordered_result);
        return;
    }
    compare_f64_slow(context, field_index, left, right, ordered);
}

// Integer comparison. `cmpi`/`cmpli` are ~4% of all guest instructions
// (opcode census) and the generated code immediately feeds the result to
// `cr_bit` in the following branch, so this is one of the hottest helpers in
// the program. The old form wrote the CR field inside `set_cr_field` and then
// read `context->cr` back through `cr_bit` in the branch, i.e. two dependent
// round trips through the 4 MB PpcContext for one guest instruction.
//
// The field is selected branchlessly: `-(left < right)` and `-(left > right)`
// are all-ones masks, and the PowerPC CR field is exactly LT=8, GT=4, EQ=2, so
// `(lt & 8) | (~lt & gt & 4) | (~lt & ~gt & 2)` is the same value as the
// three-way ternary chain without a data-dependent branch. EQ is forced when
// neither LT nor GT holds, which matches the ternary exactly.
GALAXY_ALWAYS_INLINE void compare_signed(
    PpcContext* context,
    std::uint32_t field_index,
    std::int32_t left,
    std::int32_t right) {
    const std::uint32_t less = static_cast<std::uint32_t>(
        -(static_cast<std::int32_t>(left < right)));
    const std::uint32_t greater = static_cast<std::uint32_t>(
        -(static_cast<std::int32_t>(left > right)));
    std::uint32_t field = (less & 0x8u) | (~less & greater & 0x4u) |
        (~less & ~greater & 0x2u);
    if ((context->xer & 0x80000000u) != 0) {
        field |= 0x1u;
    }
    set_cr_field(context, field_index, field);
}

GALAXY_ALWAYS_INLINE void compare_unsigned(
    PpcContext* context,
    std::uint32_t field_index,
    std::uint32_t left,
    std::uint32_t right) {
    // `!(left < right)` and `left > right` are the same predicate for unsigned
    // operands, so one carry-free compare yields both masks (and `~(left !=
    // right)` is the EQ mask) instead of two independent comparisons that
    // cannot be combined.
    const std::uint32_t not_less = static_cast<std::uint32_t>(
        -(static_cast<std::int32_t>(left >= right)));
    const std::uint32_t unequal = static_cast<std::uint32_t>(
        -(static_cast<std::int32_t>(left != right)));
    std::uint32_t field = (~not_less & 0x8u) |
        (not_less & unequal & 0x4u) | (~unequal & 0x2u);
    if ((context->xer & 0x80000000u) != 0) {
        field |= 0x1u;
    }
    set_cr_field(context, field_index, field);
}

GALAXY_ALWAYS_INLINE bool cr_bit(const PpcContext* context, std::uint32_t index) {
    return ((context->cr >> (31u - index)) & 1u) != 0;
}

GALAXY_ALWAYS_INLINE void set_cr_bit(PpcContext* context, std::uint32_t index, bool value) {
    const std::uint32_t mask = 1u << (31u - index);
    context->cr = value ? context->cr | mask : context->cr & ~mask;
}

inline std::uint64_t read_time_base(
    const PpcContext* context,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    if (services == nullptr || services->time_base_ticks == nullptr) {
        guest_execution_fault(services, guest_pc, "native time-base service is unavailable");
    }
    return services->time_base_ticks(services->user) + context->time_base_offset;
}

inline void write_time_base(
    PpcContext* context,
    std::uint64_t value,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    if (services == nullptr || services->time_base_ticks == nullptr) {
        guest_execution_fault(services, guest_pc, "native time-base service is unavailable");
    }
    context->time_base_offset = value - services->time_base_ticks(services->user);
}

inline std::uint64_t raw_time_base_ticks(
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    if (services == nullptr || services->time_base_ticks == nullptr) {
        guest_execution_fault(
            services, guest_pc, "native time-base service is unavailable");
    }
    return services->time_base_ticks(services->user);
}

inline std::uint64_t decrementer_elapsed_ticks(
    const PpcContext* context,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint64_t now_ticks = raw_time_base_ticks(services, guest_pc);
    if (now_ticks < context->decrementer_start_ticks) {
        guest_execution_fault(
            services, guest_pc, "native decrementer timeline moved backwards");
    }
    return now_ticks - context->decrementer_start_ticks;
}

inline std::uint32_t read_decrementer(
    const PpcContext* context,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint64_t elapsed =
        decrementer_elapsed_ticks(context, services, guest_pc);
    return context->decrementer_start_value - static_cast<std::uint32_t>(elapsed);
}

inline void write_decrementer_and_notify(
    PpcContext* context,
    std::uint32_t value,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t resume_pc) {
    if (services == nullptr || services->time_base_ticks == nullptr ||
        services->decrementer_written == nullptr) {
        guest_execution_fault(
            services,
            guest_pc,
            "native decrementer-write service is unavailable");
    }

    // Sample once so the old value, replacement value, and native deadline
    // share the exact architectural write instant.
    const std::uint64_t write_ticks =
        services->time_base_ticks(services->user);
    if (write_ticks < context->decrementer_start_ticks) {
        guest_execution_fault(
            services, guest_pc, "native decrementer timeline moved backwards");
    }
    const std::uint32_t previous_value =
        context->decrementer_start_value -
        static_cast<std::uint32_t>(
            write_ticks - context->decrementer_start_ticks);
    context->decrementer_start_ticks = write_ticks;
    context->decrementer_start_value = value;

    // The host callback may synchronously arbitrate and deliver an interrupt.
    // Publish the architectural continuation before crossing that boundary so
    // every competing exception saves the instruction after mtspr DEC.
    context->pc = resume_pc;
    services->decrementer_written(
        services->user,
        guest_pc,
        resume_pc,
        write_ticks,
        previous_value,
        context,
        memory);
}

inline bool trace_locked_cache_dma_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_LOCKED_CACHE_DMA") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

inline std::uint32_t locked_cache_dma_block_count(
    std::uint32_t dma_u,
    std::uint32_t dma_l) {
    std::uint32_t blocks =
        ((dma_u & 0x0000001Fu) << 2) | ((dma_l >> 2) & 0x00000003u);
    return blocks == 0 ? 128u : blocks;
}

inline void transfer_locked_cache_dma(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t dma_u = context->spr[922];
    const std::uint32_t dma_l = context->spr[923];
    constexpr std::uint32_t kDmaLoadDirection = 0x00000001u;
    constexpr std::uint32_t kDmaTransferStart = 0x00000002u;
    if ((dma_l & kDmaTransferStart) == 0) {
        return;
    }

    const std::uint32_t blocks = locked_cache_dma_block_count(dma_u, dma_l);
    const std::uint32_t size = blocks * 32u;
    const bool load_to_locked_cache = (dma_l & kDmaLoadDirection) != 0;
    const std::uint32_t external_address = dma_u & 0x1FFFFFE0u;
    const std::uint32_t locked_cache_address = dma_l & 0xFFFFFFE0u;
    const std::uint32_t destination =
        load_to_locked_cache ? locked_cache_address : external_address;
    const std::uint32_t source =
        load_to_locked_cache ? external_address : locked_cache_address;

    std::byte* destination_bytes =
        resolve_guest(memory, destination, size, services, guest_pc);
    const std::byte* source_bytes =
        resolve_guest(memory, source, size, services, guest_pc);
    std::memmove(destination_bytes, source_bytes, size);
    guest_notify_write(memory, destination, size);

    if (trace_locked_cache_dma_enabled() && services != nullptr &&
        services->log != nullptr) {
        const std::uint32_t sample_size = std::min(size, 64u);
        std::uint32_t sample_hash = 2166136261u;
        std::uint8_t sample_min = 0xFFu;
        std::uint8_t sample_max = 0u;
        for (std::uint32_t index = 0; index < sample_size; ++index) {
            const std::uint8_t value =
                std::to_integer<std::uint8_t>(destination_bytes[index]);
            sample_hash ^= value;
            sample_hash *= 16777619u;
            sample_min = std::min(sample_min, value);
            sample_max = std::max(sample_max, value);
        }

        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "[locked-cache-dma] %s blocks=%u bytes=%u src=0x%08X dst=0x%08X pc=0x%08X sample%u=%08X/%02X-%02X",
            load_to_locked_cache ? "load" : "store",
            blocks,
            size,
            source,
            destination,
            guest_pc,
            sample_size,
            sample_hash,
            sample_min,
            sample_max);
        services->log(services->user, LogLevelV1::Warning, message);
    }
}

inline std::uint32_t read_spr(
    const PpcContext* context,
    std::uint32_t spr,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    switch (spr) {
        case 1:
            return context->xer;
        case 8:
            return context->lr;
        case 9:
            return context->ctr;
        case 22:
            return read_decrementer(context, services, guest_pc);
        case 268:
            return static_cast<std::uint32_t>(read_time_base(context, services, guest_pc));
        case 269:
            return static_cast<std::uint32_t>(
                read_time_base(context, services, guest_pc) >> 32);
        case 920:
            return context->hid2;
        default:
            if (spr >= 912 && spr <= 919) {
                return context->gqr[spr - 912];
            }
            if (spr < 1024) {
                return context->spr[spr];
            }
            guest_execution_fault(services, guest_pc, "special-purpose register is out of range");
    }
}

inline void write_spr(
    PpcContext* context,
    std::uint32_t spr,
    std::uint32_t value,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    switch (spr) {
        case 1:
            context->xer = value;
            return;
        case 8:
            context->lr = value;
            return;
        case 9:
            context->ctr = value;
            return;
        case 22:
            guest_execution_fault(
                services,
                guest_pc,
                "DEC writes require the statically emitted architectural deadline callback");
        case 284: {
            const std::uint64_t current = read_time_base(context, services, guest_pc);
            write_time_base(
                context, (current & 0xFFFFFFFF00000000ull) | value, services, guest_pc);
            return;
        }
        case 285: {
            const std::uint64_t current = read_time_base(context, services, guest_pc);
            write_time_base(
                context,
                (static_cast<std::uint64_t>(value) << 32) | (current & 0xFFFFFFFFull),
                services,
                guest_pc);
            return;
        }
        case 920:
            context->hid2 = value;
            return;
        default:
            if (spr >= 912 && spr <= 919) {
                context->gqr[spr - 912] = value;
                return;
            }
            if (spr < 1024) {
                context->spr[spr] = value;
                return;
            }
            guest_execution_fault(services, guest_pc, "special-purpose register is out of range");
    }
}

inline void write_spr(
    PpcContext* context,
    std::uint32_t spr,
    std::uint32_t value,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    switch (spr) {
        case 922:
            context->spr[922] = value;
            return;
        case 923:
            context->spr[923] = value;
            transfer_locked_cache_dma(context, memory, services, guest_pc);
            return;
        default:
            write_spr(context, spr, value, services, guest_pc);
            return;
    }
}

inline void dispatch_system_call(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t instruction,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (services == nullptr || services->system_call == nullptr) {
        guest_execution_fault(services, guest_pc, "native system-call service is unavailable");
    }
    services->system_call(services->user, guest_pc, instruction, context, memory);
}

inline void dispatch_program_trap(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t instruction,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (services == nullptr || services->program_trap == nullptr) {
        guest_execution_fault(services, guest_pc, "native program-trap service is unavailable");
    }
    services->program_trap(services->user, guest_pc, instruction, context, memory);
}

GALAXY_NOINLINE inline void ensure_fpu_available_slow(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (context == nullptr) {
        guest_execution_fault(
            services, guest_pc, "floating-point instruction has no PPC context");
    }
    if ((context->msr & kMsrFloatingPointAvailable) != 0u) {
        return;
    }
    if (services == nullptr || services->fpu_unavailable == nullptr) {
        guest_execution_fault(
            services, guest_pc, "native FPU-unavailable exception service is unavailable");
    }
    // The service normally returns into this live AOT guard. If exception-7's
    // exact RFI exposes a pending asynchronous exception first, it may instead
    // unwind this frame; the statically emitted exact alias then owns the
    // eventual retry at guest_pc.
    services->fpu_unavailable(services->user, guest_pc, context, memory);
    if ((context->msr & kMsrFloatingPointAvailable) == 0u ||
        context->pc != guest_pc) {
        guest_execution_fault(
            services,
            guest_pc,
            "native FPU-unavailable exception service returned without exact restart state");
    }
}

// MSR[FP] is almost always set: keep only that test at each translated
// floating-point site and leave the exception machinery out of line.
GALAXY_ALWAYS_INLINE void ensure_fpu_available(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (context != nullptr &&
        (context->msr & kMsrFloatingPointAvailable) != 0u) [[likely]] {
        return;
    }
    ensure_fpu_available_slow(services, guest_pc, context, memory);
}

inline bool trap_word_immediate_condition(
    std::uint32_t trap_options,
    std::uint32_t left,
    std::uint32_t right) {
    const std::int32_t signed_left = std::bit_cast<std::int32_t>(left);
    const std::int32_t signed_right = std::bit_cast<std::int32_t>(right);
    return ((trap_options & 0x10u) != 0 && signed_left < signed_right) ||
           ((trap_options & 0x08u) != 0 && signed_left > signed_right) ||
           ((trap_options & 0x04u) != 0 && left == right) ||
           ((trap_options & 0x02u) != 0 && left < right) ||
           ((trap_options & 0x01u) != 0 && left > right);
}

inline void trap_word_immediate(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t instruction,
    std::uint32_t trap_options,
    std::uint32_t left,
    std::uint32_t right,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (trap_word_immediate_condition(trap_options, left, right)) {
        dispatch_program_trap(services, guest_pc, instruction, context, memory);
    }
}

inline void dispatch_return_from_interrupt(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (services == nullptr || services->return_from_interrupt == nullptr) {
        guest_execution_fault(
            services, guest_pc, "native return-from-interrupt service is unavailable");
    }
    services->return_from_interrupt(services->user, guest_pc, context, memory);
}

inline void cache_prefetch_hint(GuestMemoryV1* memory, std::uint32_t address) {
    (void)memory;
    (void)address;
}

inline void call_guest(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t caller_pc) {
    if (services == nullptr || services->call_guest == nullptr) {
        if (services != nullptr && services->fatal != nullptr) {
            services->fatal(services->user, caller_pc, "native guest-call target is unavailable");
        }
        std::abort();
    }
    services->call_guest(services->user, guest_address, context, memory);
}

inline bool trace_resource_helper_callsite(std::uint32_t caller_pc) {
    switch (caller_pc) {
    case 0x80414104u:
    case 0x80414110u:
    case 0x80414124u:
    case 0x80414148u:
    case 0x80414158u:
    case 0x804141A4u:
    case 0x804141B0u:
    case 0x804141C8u:
    case 0x804141E4u:
    case 0x804141FCu:
    case 0x80414268u:
    case 0x80414298u:
    case 0x804142ACu:
    case 0x80414350u:
    case 0x8041436Cu:
    case 0x804143ECu:
    case 0x804143F8u:
    case 0x80414410u:
    case 0x8041449Cu:
    case 0x80414514u:
    case 0x80414568u:
    case 0x8041458Cu:
    case 0x804145C4u:
    case 0x804145E0u:
    case 0x804145F0u:
    case 0x80414660u:
    case 0x80414688u:
    case 0x804146A0u:
    case 0x804146D0u:
    case 0x80492678u:
        return true;
    default:
        return false;
    }
}

inline void trace_resource_helper_call(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t guest_address,
    std::uint32_t caller_pc,
    const char* kind) {
    if (!trace_scenario_opening_state_enabled() ||
        services == nullptr || services->log == nullptr ||
        context == nullptr || !trace_resource_helper_callsite(caller_pc)) {
        return;
    }
    const auto read_u32_or_zero = [memory](std::uint32_t address) {
        std::uint32_t value = 0;
        if (memory != nullptr) {
            trace_movie_read_u32_fast(memory, address, value);
        }
        return value;
    };
    const std::uint32_t obj = context->gpr[3];
    const std::uint32_t h28 = obj == 0u ? 0u : read_u32_or_zero(obj + 0x28u);
    const std::uint32_t h2c = obj == 0u ? 0u : read_u32_or_zero(obj + 0x2Cu);
    const std::uint32_t h30 = obj == 0u ? 0u : read_u32_or_zero(obj + 0x30u);
    const std::uint32_t h48 = obj == 0u ? 0u : read_u32_or_zero(obj + 0x48u);
    const std::uint32_t h50 = obj == 0u ? 0u : read_u32_or_zero(obj + 0x50u);
    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "[resource-helper-call] kind=%s caller=0x%08X target=0x%08X lr=0x%08X"
        " r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X r9=0x%08X"
        " h28=0x%08X h2c=0x%08X h30=0x%08X h48=0x%08X h50=0x%08X",
        kind,
        caller_pc,
        guest_address & 0xFFFFFFFCu,
        context->lr,
        context->gpr[3],
        context->gpr[4],
        context->gpr[5],
        context->gpr[6],
        context->gpr[7],
        context->gpr[8],
        context->gpr[9],
        h28,
        h2c,
        h30,
        h48,
        h50);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline void trace_resource_helper_return(
    const NativeServicesV1* services,
    const PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t guest_address,
    std::uint32_t caller_pc,
    const char* kind) {
    if (!trace_scenario_opening_state_enabled() ||
        services == nullptr || services->log == nullptr ||
        context == nullptr || !trace_resource_helper_callsite(caller_pc)) {
        return;
    }
    const auto read_u32_or_zero = [memory](std::uint32_t address) {
        std::uint32_t value = 0;
        if (memory != nullptr) {
            trace_movie_read_u32_fast(memory, address, value);
        }
        return value;
    };
    const std::uint32_t obj = context->gpr[29] != 0u ? context->gpr[29] : context->gpr[3];
    const std::uint32_t h28 = obj == 0u ? 0u : read_u32_or_zero(obj + 0x28u);
    const std::uint32_t h2c = obj == 0u ? 0u : read_u32_or_zero(obj + 0x2Cu);
    const std::uint32_t h30 = obj == 0u ? 0u : read_u32_or_zero(obj + 0x30u);
    const std::uint32_t h48 = obj == 0u ? 0u : read_u32_or_zero(obj + 0x48u);
    const std::uint32_t h50 = obj == 0u ? 0u : read_u32_or_zero(obj + 0x50u);
    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "[resource-helper-return] kind=%s caller=0x%08X target=0x%08X lr=0x%08X result=0x%08X"
        " r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X r9=0x%08X"
        " obj=0x%08X h28=0x%08X h2c=0x%08X h30=0x%08X h48=0x%08X h50=0x%08X",
        kind,
        caller_pc,
        guest_address & 0xFFFFFFFCu,
        context->lr,
        context->gpr[3],
        context->gpr[3],
        context->gpr[4],
        context->gpr[5],
        context->gpr[6],
        context->gpr[7],
        context->gpr[8],
        context->gpr[9],
        obj,
        h28,
        h2c,
        h30,
        h48,
        h50);
    services->log(services->user, LogLevelV1::Warning, message);
}

inline void call_guest_resolved(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    NativeGameFunction function,
    PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t caller_pc) {
    if (services == nullptr || services->call_guest_resolved == nullptr || function == nullptr) {
        if (services != nullptr && services->fatal != nullptr) {
            services->fatal(services->user, caller_pc, "native resolved guest-call target is unavailable");
        }
        std::abort();
    }
    if (trace_scenario_opening_state_enabled() &&
        services->log != nullptr &&
        guest_address == 0x804140D4u && caller_pc == 0x80492678u) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "[resource-validate-call] caller=0x%08X file=0x%08X data=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X record=0x%08X item=0x%08X status=%u token=%u pending=%u",
            caller_pc,
            context == nullptr ? 0u : context->gpr[3],
            context == nullptr ? 0u : context->gpr[4],
            context == nullptr ? 0u : context->gpr[5],
            context == nullptr ? 0u : context->gpr[6],
            context == nullptr ? 0u : context->gpr[7],
            context == nullptr ? 0u : context->gpr[8],
            context == nullptr ? 0u : context->gpr[30],
            context == nullptr ? 0u : context->gpr[31],
            (context == nullptr || memory == nullptr || context->gpr[31] == 0u)
                ? 0u
                : trace_save_read_u32_or_zero(memory, context->gpr[31] + 0x4Cu),
            (context == nullptr || memory == nullptr || context->gpr[31] == 0u)
                ? 0u
                : trace_save_read_u32_or_zero(memory, context->gpr[31] + 0x58u),
            (context == nullptr || memory == nullptr || context->gpr[31] == 0u)
                ? 0u
                : trace_save_read_u32_or_zero(memory, context->gpr[31] + 0x5Au));
        services->log(services->user, LogLevelV1::Warning, message);
    }
    trace_resource_helper_call(
        services, context, memory, guest_address, caller_pc, "resolved");
    services->call_guest_resolved(services->user, guest_address, function, context, memory);
    trace_resource_helper_return(
        services, context, memory, guest_address, caller_pc, "resolved");
    if (trace_scenario_opening_state_enabled() &&
        services->log != nullptr &&
        guest_address == 0x804140D4u && caller_pc == 0x80492678u) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "[resource-validate-return] caller=0x%08X result=0x%08X record=0x%08X item=0x%08X status=%u token=%u pending=%u",
            caller_pc,
            context == nullptr ? 0u : context->gpr[3],
            context == nullptr ? 0u : context->gpr[30],
            context == nullptr ? 0u : context->gpr[31],
            (context == nullptr || memory == nullptr || context->gpr[31] == 0u)
                ? 0u
                : trace_save_read_u32_or_zero(memory, context->gpr[31] + 0x4Cu),
            (context == nullptr || memory == nullptr || context->gpr[31] == 0u)
                ? 0u
                : trace_save_read_u32_or_zero(memory, context->gpr[31] + 0x58u),
            (context == nullptr || memory == nullptr || context->gpr[31] == 0u)
                ? 0u
                : trace_save_read_u32_or_zero(memory, context->gpr[31] + 0x5Au));
        services->log(services->user, LogLevelV1::Warning, message);
    }
}

inline bool profile_generated_direct_calls_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_PROFILE_GENERATED_DIRECT_CALLS") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

inline bool profile_generated_direct_edges_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_PROFILE_GENERATED_DIRECT_EDGES") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

// A full-module direct-edge profile must never turn every generated call in a
// long boot route into timed atomic work.  The runtime publishes the same
// short, explicit VI window used by the main-frame trace so a profile can be
// both attributable and bounded.  The environment flag remains separate from
// the window: the runtime still needs it at shutdown to decide whether to dump
// the collected records.
GALAXY_ALWAYS_INLINE bool profile_generated_direct_edges_active(
    const NativeServicesV1* services) {
    // Check the runtime-published window first.  The generated-call helpers
    // execute on every resolved edge, including the long pre-window boot
    // route.  Avoid even consulting the process-wide profile predicate until
    // the runtime has opened its short explicit window; this keeps a bounded
    // diagnostic from adding a second branch/predicate load to every earlier
    // native call.  Both operands are pure gates, so the order changes only
    // host diagnostic overhead, never guest-visible call behavior.
    return main_frame_trace_window_active(services) &&
           profile_generated_direct_edges_enabled();
}

inline constexpr bool is_route_marker_generated_direct_entry(
    std::uint32_t guest_address) {
    switch (guest_address) {
    case 0x802AFD80u: // MarioActor::movement
    case 0x802B0D0Cu: // MarioActor::control
    case 0x802B0E18u: // MarioActor::control2
    case 0x802B0FE4u: // MarioActor::controlMain
    case 0x802B2A28u: // MarioActor::calcAnimInMovement
    case 0x802B321Cu: // MarioActor::calcAndSetBaseMtx
    case 0x802CAC98u: // Mario::calcAnimIfRunning
    case 0x802CAF9Cu: // Mario::calcAnimBlend
    case 0x8038D2E4u: // THPPlayer::open
    case 0x8038D34Cu: // THPPlayer::close
    case 0x8038D868u: // THPPlayer::calc
        return true;
    default:
        return false;
    }
}

// Relative input anchoring needs only the three Mario control entry points.
// Keep this predicate separate from the broader trace-only route set so an
// input script cannot force animation or THP calls through the host boundary.
inline constexpr bool is_mario_control_generated_direct_entry(
    std::uint32_t guest_address) {
    switch (guest_address) {
    case 0x802B0D0Cu: // MarioActor::control
    case 0x802B0E18u: // MarioActor::control2
    case 0x802B0FE4u: // MarioActor::controlMain
        return true;
    default:
        return false;
    }
}

static_assert(is_route_marker_generated_direct_entry(0x802AFD80u));
static_assert(is_route_marker_generated_direct_entry(0x802B0D0Cu));
static_assert(!is_route_marker_generated_direct_entry(0x802AFD84u));
static_assert(is_mario_control_generated_direct_entry(0x802B0D0Cu));
static_assert(!is_mario_control_generated_direct_entry(0x802AFD80u));

inline constexpr std::size_t kProfileMaxProbes = 64u;
inline std::atomic<std::uint64_t>& direct_edge_profile_dropped() {
    static std::atomic<std::uint64_t> count{0};
    return count;
}
inline std::atomic<std::uint64_t>& jpa_profile_dropped() {
    static std::atomic<std::uint64_t> count{0};
    return count;
}

struct DirectEdgeProfileSlot {
    std::atomic<std::uint64_t> key{0};
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> cycles{0};
};

inline std::array<DirectEdgeProfileSlot, 32768>& direct_edge_profile_slots() {
    static std::array<DirectEdgeProfileSlot, 32768> slots{};
    return slots;
}

inline std::uint64_t direct_edge_profile_ticks() {
#if defined(_MSC_VER)
    return __rdtsc();
#else
    return 0;
#endif
}

inline std::uint64_t direct_edge_profile_hash(std::uint64_t key) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ULL;
    key ^= key >> 33;
    return key;
}

inline void dump_direct_edge_profile() {
    struct Snapshot {
        std::uint64_t key;
        std::uint64_t calls;
        std::uint64_t cycles;
    };
    std::vector<Snapshot> snapshots;
    snapshots.reserve(1024);
    for (const DirectEdgeProfileSlot& slot : direct_edge_profile_slots()) {
        const std::uint64_t key = slot.key.load(std::memory_order_relaxed);
        if (key == 0) {
            continue;
        }
        const std::uint64_t calls =
            slot.calls.load(std::memory_order_relaxed);
        const std::uint64_t cycles =
            slot.cycles.load(std::memory_order_relaxed);
        if (calls != 0) {
            snapshots.push_back(Snapshot{key, calls, cycles});
        }
    }
    std::sort(
        snapshots.begin(),
        snapshots.end(),
        [](const Snapshot& left, const Snapshot& right) {
            return left.cycles > right.cycles;
        });

    std::fprintf(
        stderr,
        "[direct-edge-profile] edges=%zu max-probes=%zu omitted-events=%llu",
        snapshots.size(), kProfileMaxProbes,
        static_cast<unsigned long long>(direct_edge_profile_dropped().load(
            std::memory_order_relaxed)));
    const std::size_t limit = std::min<std::size_t>(snapshots.size(), 128);
    for (std::size_t index = 0; index < limit; ++index) {
        const Snapshot& snapshot = snapshots[index];
        const std::uint32_t caller =
            static_cast<std::uint32_t>(snapshot.key >> 32);
        const std::uint32_t callee =
            static_cast<std::uint32_t>(snapshot.key);
        const std::uint64_t average =
            snapshot.calls != 0 ? snapshot.cycles / snapshot.calls : 0;
        std::fprintf(
            stderr,
            " %08X->%08X=%llu/%llu/%llu",
            static_cast<unsigned>(caller),
            static_cast<unsigned>(callee),
            static_cast<unsigned long long>(snapshot.cycles),
            static_cast<unsigned long long>(snapshot.calls),
            static_cast<unsigned long long>(average));
    }
    std::fprintf(stderr, "\n");
}

inline void ensure_direct_edge_profile_registered() {
    static std::atomic_bool registered{false};
    if (registered.load(std::memory_order_acquire)) return;
    bool expected = false;
    if (registered.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        std::atexit(dump_direct_edge_profile);
    }
}

inline void record_direct_edge_profile_local(
    std::uint32_t caller_pc,
    std::uint32_t callee_pc,
    std::uint64_t cycles) {
    ensure_direct_edge_profile_registered();
    const std::uint64_t key =
        (static_cast<std::uint64_t>(caller_pc) << 32) |
        static_cast<std::uint64_t>(callee_pc);
    if (key == 0u) {
        direct_edge_profile_dropped().fetch_add(1, std::memory_order_relaxed);
        return;
    }
    auto& slots = direct_edge_profile_slots();
    const std::uint64_t hash = direct_edge_profile_hash(key);
    for (std::size_t probe = 0; probe < std::min(slots.size(), kProfileMaxProbes); ++probe) {
        DirectEdgeProfileSlot& slot =
            slots[(hash + probe) & (slots.size() - 1u)];
        std::uint64_t observed =
            slot.key.load(std::memory_order_acquire);
        if (observed == 0 && slot.key.compare_exchange_strong(
                observed, key, std::memory_order_acq_rel)) {
            observed = key;
        }
        // A losing insert may have found this same key. Add to zero-initialized
        // counters instead of overwriting a concurrently published increment.
        if (observed == key) {
            slot.calls.fetch_add(1, std::memory_order_relaxed);
            slot.cycles.fetch_add(cycles, std::memory_order_relaxed);
            return;
        }
    }
    direct_edge_profile_dropped().fetch_add(1, std::memory_order_relaxed);
    static std::atomic_bool overflow_reported{false};
    bool expected = false;
    if (overflow_reported.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        std::fprintf(stderr, "[direct-edge-profile] probe limit reached; events omitted\n");
    }
}

// This diagnostic callback intentionally has no local fallback.  A generated
// module and the runtime must agree on the ABI so that records land in the
// runtime-owned table which is later dumped.  Silently retaining records in a
// DLL-local table produced the misleading `edges=0` reports that prompted this
// service; missing support is an installation/runtime contract failure.
inline void report_direct_edge_profile(
    const NativeServicesV1* services,
    std::uint32_t caller_pc,
    std::uint32_t callee_pc,
    std::uint64_t cycles) {
    if (services == nullptr || services->record_direct_edge_profile == nullptr) {
        if (services != nullptr && services->fatal != nullptr) {
            services->fatal(
                services->user,
                caller_pc,
                "native direct-edge profile sink is unavailable");
        }
        std::abort();
    }
    services->record_direct_edge_profile(
        services->user, caller_pc, callee_pc, cycles);
}

inline bool profile_jpa_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_PROFILE_JPA") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

struct JpaProfileCounters {
    std::atomic<std::uint64_t> draw_calls{0};
    std::atomic<std::uint64_t> draw_flag_skips{0};
    std::atomic<std::uint64_t> draw_fast_hits{0};
    std::atomic<std::uint64_t> draw_fast_misses{0};
    std::atomic<std::uint64_t> emitter_calls{0};
    std::atomic<std::uint64_t> emitter_nonnull_callbacks{0};
    std::atomic<std::uint64_t> child_list_calls{0};
    std::atomic<std::uint64_t> child_callbacks{0};
};

struct JpaTargetProfileSlot {
    std::atomic<std::uint64_t> key{0};
    std::atomic<std::uint64_t> calls{0};
};

inline JpaProfileCounters& jpa_profile_counters() {
    static JpaProfileCounters counters{};
    return counters;
}

inline std::array<JpaTargetProfileSlot, 4096>& jpa_target_profile_slots() {
    static std::array<JpaTargetProfileSlot, 4096> slots{};
    return slots;
}

inline std::uint64_t jpa_profile_hash(std::uint64_t key) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ULL;
    key ^= key >> 33;
    return key;
}

inline void dump_jpa_profile() {
    struct Snapshot {
        std::uint64_t key;
        std::uint64_t calls;
    };

    const JpaProfileCounters& counters = jpa_profile_counters();
    const std::uint64_t draw_calls =
        counters.draw_calls.load(std::memory_order_relaxed);
    const std::uint64_t emitter_calls =
        counters.emitter_calls.load(std::memory_order_relaxed);
    const std::uint64_t child_calls =
        counters.child_list_calls.load(std::memory_order_relaxed);
    if (draw_calls == 0 && emitter_calls == 0 && child_calls == 0) {
        return;
    }

    std::vector<Snapshot> snapshots;
    snapshots.reserve(128);
    for (const JpaTargetProfileSlot& slot : jpa_target_profile_slots()) {
        const std::uint64_t key = slot.key.load(std::memory_order_relaxed);
        const std::uint64_t calls = slot.calls.load(std::memory_order_relaxed);
        if (key != 0 && calls != 0) {
            snapshots.push_back(Snapshot{key, calls});
        }
    }
    std::sort(
        snapshots.begin(),
        snapshots.end(),
        [](const Snapshot& left, const Snapshot& right) {
            return left.calls > right.calls;
        });

    std::fprintf(
        stderr,
        "[jpa-profile] draw=%llu flag-skip=%llu fast-hit=%llu "
        "fast-miss=%llu emitter=%llu emitter-callback=%llu "
        "child-list=%llu child-callback=%llu",
        static_cast<unsigned long long>(draw_calls),
        static_cast<unsigned long long>(
            counters.draw_flag_skips.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            counters.draw_fast_hits.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            counters.draw_fast_misses.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(emitter_calls),
        static_cast<unsigned long long>(
            counters.emitter_nonnull_callbacks.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(child_calls),
        static_cast<unsigned long long>(
            counters.child_callbacks.load(std::memory_order_relaxed)));
    std::fprintf(stderr, " max-probes=%zu omitted-target-events=%llu",
        kProfileMaxProbes, static_cast<unsigned long long>(
            jpa_profile_dropped().load(std::memory_order_relaxed)));
    const std::size_t limit = std::min<std::size_t>(snapshots.size(), 32);
    for (std::size_t index = 0; index < limit; ++index) {
        const std::uint32_t kind =
            static_cast<std::uint32_t>(snapshots[index].key >> 32);
        const std::uint32_t target =
            static_cast<std::uint32_t>(snapshots[index].key);
        std::fprintf(
            stderr,
            " kind%u:%08X=%llu",
            static_cast<unsigned>(kind),
            static_cast<unsigned>(target),
            static_cast<unsigned long long>(snapshots[index].calls));
    }
    std::fprintf(stderr, "\n");
}

inline void ensure_jpa_profile_registered() {
    if (!profile_jpa_enabled()) {
        return;
    }
    static std::atomic_bool registered{false};
    if (registered.load(std::memory_order_acquire)) return;
    bool expected = false;
    if (registered.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        std::atexit(dump_jpa_profile);
    }
}

inline void record_jpa_counter(std::atomic<std::uint64_t>& counter) {
    if (!profile_jpa_enabled()) {
        return;
    }
    ensure_jpa_profile_registered();
    counter.fetch_add(1, std::memory_order_relaxed);
}

inline void record_jpa_target(std::uint32_t kind, std::uint32_t target) {
    if (!profile_jpa_enabled()) {
        return;
    }
    ensure_jpa_profile_registered();
    const std::uint64_t key =
        (static_cast<std::uint64_t>(kind) << 32) |
        static_cast<std::uint64_t>(target & 0xFFFFFFFCu);
    if (key == 0u) {
        jpa_profile_dropped().fetch_add(1, std::memory_order_relaxed);
        return;
    }
    auto& slots = jpa_target_profile_slots();
    const std::uint64_t hash = jpa_profile_hash(key);
    for (std::size_t probe = 0; probe < std::min(slots.size(), kProfileMaxProbes); ++probe) {
        JpaTargetProfileSlot& slot =
            slots[(hash + probe) & (slots.size() - 1u)];
        std::uint64_t observed =
            slot.key.load(std::memory_order_acquire);
        if (observed == 0 && slot.key.compare_exchange_strong(
                observed, key, std::memory_order_acq_rel)) {
            observed = key;
        }
        // A losing insert may have found this same key. Add to zero-initialized
        // counters instead of overwriting a concurrently published increment.
        if (observed == key) {
            slot.calls.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    jpa_profile_dropped().fetch_add(1, std::memory_order_relaxed);
    static std::atomic_bool overflow_reported{false};
    bool expected = false;
    if (overflow_reported.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        std::fprintf(stderr, "[jpa-profile] target probe limit reached; events omitted\n");
    }
}

inline void call_guest_direct_resolved(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    NativeGameFunction function,
    PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t pc) {
    const bool observe_route_entry =
        services != nullptr &&
        (((services->runtime_flags & kNativeServiceFlagTraceRouteMarkers) !=
              0u &&
          is_route_marker_generated_direct_entry(guest_address)) ||
         ((services->runtime_flags &
           kNativeServiceFlagRelativeMarioInputAnchor) != 0u &&
          is_mario_control_generated_direct_entry(guest_address)));
    if (profile_generated_direct_calls_enabled() || function == nullptr ||
        observe_route_entry) {
        call_guest_resolved(services, guest_address, function, context, memory, pc);
        return;
    }
    context->pc = guest_address;
    if (!profile_generated_direct_edges_active(services)) {
        function(context, memory, services);
        return;
    }
    const std::uint64_t start_cycles = direct_edge_profile_ticks();
    try {
        function(context, memory, services);
    } catch (...) {
        report_direct_edge_profile(
            services,
            pc, guest_address, direct_edge_profile_ticks() - start_cycles);
        throw;
    }
    report_direct_edge_profile(
        services,
        pc, guest_address, direct_edge_profile_ticks() - start_cycles);
}

inline bool trace_function_async_causality_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_FUNCTION_ASYNC_CAUSALITY") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

inline void function_async_trace_callback(
    const NativeServicesV1* services,
    std::uint32_t marker,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (!trace_function_async_causality_enabled()) {
        return;
    }
    if (services == nullptr || services->branch_checkpoint == nullptr) {
        guest_execution_fault(
            services,
            marker,
            "FunctionAsync causality callback is unavailable");
    }
    services->branch_checkpoint(services->user, marker, context, memory);
}

inline void trace_function_async_gameplay_pointer_consumer(
    const NativeServicesV1* services,
    PpcContext* context,
    GuestMemoryV1* memory) {
    function_async_trace_callback(
        services, kFunctionAsyncTracePointerConsumer, context, memory);
}

GALAXY_NOINLINE inline void call_guest_cached_diagnostic(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    NativeGameFunction* cached_function,
    PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t caller_pc) {
    if (services == nullptr || services->call_guest_cached == nullptr ||
        cached_address == nullptr || cached_function == nullptr) {
        if (services != nullptr && services->fatal != nullptr) {
            services->fatal(services->user, caller_pc, "native cached guest-call service is unavailable");
        }
        std::abort();
    }
    if (trace_save_sequence_function_entries_enabled() &&
        services->log != nullptr &&
        (caller_pc == 0x8016FB14u || caller_pc == 0x8016FB7Cu)) {
        static std::atomic<std::uint32_t> count{0};
        const std::uint32_t index = count.fetch_add(1);
        if (index < 64u || (index % 120u) == 0u) {
            char message[320]{};
            const std::uint32_t spine = context == nullptr ? 0u : context->gpr[4];
            const std::uint32_t curr =
                (memory == nullptr || spine == 0u)
                    ? 0u
                    : trace_save_read_u32_or_zero(memory, spine + 0x04u);
            const std::uint32_t next =
                (memory == nullptr || spine == 0u)
                    ? 0u
                    : trace_save_read_u32_or_zero(memory, spine + 0x08u);
            const std::uint32_t step =
                (memory == nullptr || spine == 0u)
                    ? 0u
                    : trace_save_read_u32_or_zero(memory, spine + 0x0Cu);
            std::snprintf(
                message,
                sizeof(message),
                "[save-nerve-dispatch] caller=0x%08X target=0x%08X obj=0x%08X spine=0x%08X curr=0x%08X next=0x%08X step=%u cached=0x%08X",
                caller_pc,
                guest_address & 0xFFFFFFFCu,
                context == nullptr ? 0u : context->gpr[3],
                spine,
                curr,
                next,
                step,
                cached_address == nullptr ? 0u : *cached_address);
            services->log(services->user, LogLevelV1::Warning, message);
        }
    }
    if (trace_scenario_opening_state_enabled() &&
        services->log != nullptr &&
        (caller_pc == 0x8016FB14u || caller_pc == 0x8016FB7Cu)) {
        static std::atomic<std::uint32_t> count{0};
        const std::uint32_t index = count.fetch_add(1);
        if (index < 512u || (index % 60u) == 0u) {
            char message[512]{};
            const std::uint32_t r13 = context == nullptr ? 0u : context->gpr[13];
            const std::uint32_t spine = context == nullptr ? 0u : context->gpr[4];
            const std::uint32_t curr =
                (memory == nullptr || spine == 0u)
                    ? 0u
                    : trace_save_read_u32_or_zero(memory, spine + 0x04u);
            const std::uint32_t next =
                (memory == nullptr || spine == 0u)
                    ? 0u
                    : trace_save_read_u32_or_zero(memory, spine + 0x08u);
            const std::uint32_t step =
                (memory == nullptr || spine == 0u)
                    ? 0u
                    : trace_save_read_u32_or_zero(memory, spine + 0x0Cu);
            std::snprintf(
                message,
                sizeof(message),
                "[scenario-nerve-dispatch] caller=0x%08X target=0x%08X obj=0x%08X spine=0x%08X curr=0x%08X(%s) next=0x%08X(%s) step=%u cached=0x%08X",
                caller_pc,
                guest_address & 0xFFFFFFFCu,
                context == nullptr ? 0u : context->gpr[3],
                spine,
                curr,
                trace_scenario_nerve_label(r13, curr),
                next,
                trace_scenario_nerve_label(r13, next),
                step,
                cached_address == nullptr ? 0u : *cached_address);
            services->log(services->user, LogLevelV1::Warning, message);
        }
    }
    if (trace_scenario_opening_state_enabled() &&
        services->log != nullptr &&
        caller_pc == 0x8049DB10u) {
        static std::atomic<std::uint32_t> count{0};
        const std::uint32_t index = count.fetch_add(1);
        if (index < 512u || (index % 120u) == 0u) {
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "[resource-loader-dispatch] caller=0x%08X target=0x%08X group=0x%08X sub=%u cached=0x%08X",
                caller_pc,
                guest_address & 0xFFFFFFFCu,
                context == nullptr ? 0u : context->gpr[3],
                context == nullptr ? 0u : context->gpr[4],
                cached_address == nullptr ? 0u : *cached_address);
            services->log(services->user, LogLevelV1::Warning, message);
        }
    }
    // Every generated cached call must cross the runtime-owned service
    // boundary.  That boundary is where native input, DMA, and asynchronous
    // exception work are serviced and where a cached target is rejected while
    // an intercept is required.  A former direct-call shortcut skipped that
    // work after a cache hit; it could miss more than one hardware AI DMA
    // deadline.  Keep the cache as a lookup cache only, never as permission to
    // bypass the scheduling boundary.
    // The translator lowers every resolved PPC call through this cacheable
    // boundary, including calls whose target is statically known at install
    // time.  Profile those generated call edges here instead of only in the
    // unused direct-resolved helper: the caller PC is still exact and the
    // runtime-owned sink receives the fully normalized target after dispatch.
    // This remains behind the main-frame window so it cannot become an
    // always-on atomic timing cost.
    if (!profile_generated_direct_edges_active(services)) {
        trace_resource_helper_call(
            services, context, memory, guest_address, caller_pc, "cached");
        services->call_guest_cached(
            services->user, guest_address, cached_address, cached_function, context, memory);
        trace_resource_helper_return(
            services, context, memory, guest_address, caller_pc, "cached");
        return;
    }

    const std::uint64_t start_cycles = direct_edge_profile_ticks();
    try {
        trace_resource_helper_call(
            services, context, memory, guest_address, caller_pc, "cached");
        services->call_guest_cached(
            services->user, guest_address, cached_address, cached_function, context, memory);
        trace_resource_helper_return(
            services, context, memory, guest_address, caller_pc, "cached");
    } catch (...) {
        report_direct_edge_profile(
            services,
            caller_pc,
            guest_address & 0xFFFFFFFCu,
            direct_edge_profile_ticks() - start_cycles);
        throw;
    }
    report_direct_edge_profile(
        services,
        caller_pc,
        guest_address & 0xFFFFFFFCu,
        direct_edge_profile_ticks() - start_cycles);
    if (trace_scenario_opening_state_enabled() &&
        services->log != nullptr &&
        caller_pc == 0x8049DB10u) {
        static std::atomic<std::uint32_t> count{0};
        const std::uint32_t index = count.fetch_add(1);
        if (index < 512u || (index % 120u) == 0u) {
            const std::uint32_t item = context == nullptr ? 0u : context->gpr[3];
            std::uint32_t status = 0;
            std::uint32_t item_vtable = 0;
            if (memory != nullptr && item != 0u) {
                status = trace_save_read_u32_or_zero(memory, item + 0x4Cu);
                item_vtable = trace_save_read_u32_or_zero(memory, item);
            }
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "[resource-loader-return] caller=0x%08X item=0x%08X itemVt=0x%08X status=%u cached=0x%08X",
                caller_pc,
                item,
                item_vtable,
                status,
                cached_address == nullptr ? 0u : *cached_address);
            services->log(services->user, LogLevelV1::Warning, message);
        }
    }
}

[[noreturn]] GALAXY_NOINLINE inline void call_guest_cached_unavailable(
    const NativeServicesV1* services,
    std::uint32_t caller_pc) {
    if (services != nullptr && services->fatal != nullptr) {
        services->fatal(services->user, caller_pc,
            "native cached guest-call service is unavailable");
    }
    std::abort();
}

GALAXY_ALWAYS_INLINE bool cached_call_trace_enabled() {
    // These two selectors were already process-constant environment statics.
    // Cache only trace policy; the runtime profiling window must remain live.
    static const bool enabled = trace_save_sequence_function_entries_enabled() ||
        trace_scenario_opening_state_enabled();
    return enabled;
}

GALAXY_ALWAYS_INLINE void call_guest_cached(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    NativeGameFunction* cached_function,
    PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t caller_pc) {
    if (services == nullptr || services->call_guest_cached == nullptr ||
        cached_address == nullptr || cached_function == nullptr) GALAXY_UNLIKELY {
        call_guest_cached_unavailable(services, caller_pc);
    }
    // Keep the existing window-first profile predicate: it must not initialize
    // the profiler's environment singleton outside the runtime-owned window.
    if (cached_call_trace_enabled() ||
        profile_generated_direct_edges_active(services)) GALAXY_UNLIKELY {
        call_guest_cached_diagnostic(services, guest_address, cached_address,
            cached_function, context, memory, caller_pc);
        return;
    }
    // A cached target is only a lookup shortcut. Input, DMA, exceptions and
    // interceptor ownership still belong to this mandatory runtime boundary.
    services->call_guest_cached(services->user, guest_address, cached_address,
        cached_function, context, memory);
}

inline void native_jpa_direction_callback_803A3BB4(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    NativeGameFunction* cached_function,
    PpcContext* context,
    GuestMemoryV1* memory) {
    context->lr = 0x803A3BB8u;
    switch (guest_address & 0xFFFFFFFCu) {
        case 0x803A35DCu:
            native_jpa_vec_copy_803A35DC(
                context, memory, services, 0x803A35DCu);
            return;
        case 0x803A35E8u:
            native_jpa_vec_copy_803A35E8(
                context, memory, services, 0x803A35E8u);
            return;
        default:
            call_guest_cached(
                services,
                guest_address,
                cached_address,
                cached_function,
                context,
                memory,
                0x803A3BB4u);
            return;
    }
}

inline void native_jpa_projection_callback_803A3CA8(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    NativeGameFunction* cached_function,
    PpcContext* context,
    GuestMemoryV1* memory) {
    context->lr = 0x803A3CACu;
    switch (guest_address & 0xFFFFFFFCu) {
        case 0x803A387Cu:
            native_mtx_scale_803A387C(
                context, memory, services, 0x803A387Cu);
            return;
        default:
            call_guest_cached(
                services,
                guest_address,
                cached_address,
                cached_function,
                context,
                memory,
                0x803A3CA8u);
            return;
    }
}

inline void native_jpa_draw_callback_803A3CE4(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    NativeGameFunction* cached_function,
    PpcContext* context,
    GuestMemoryV1* memory) {
    context->lr = 0x803A3CE8u;
    switch (guest_address & 0xFFFFFFFCu) {
        case 0x803A33ACu:
            native_noop_803A33AC(
                context, memory, services, 0x803A33ACu);
            return;
        default:
            call_guest_cached(
                services,
                guest_address,
                cached_address,
                cached_function,
                context,
                memory,
                0x803A3CE4u);
            return;
    }
}

inline bool native_jpa_draw_particle_fast_path(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t frame,
    std::uint32_t resource,
    std::uint32_t work) {
    constexpr std::uint32_t kJPaDrawTable = 0x805C7220u;
    struct Vec3 {
        float x;
        float y;
        float z;
    };

    const auto table_callback = [&](std::uint32_t table_offset,
                                    std::uint32_t index,
                                    std::uint32_t pc) {
        return guest_load_u32(
            memory,
            kJPaDrawTable + table_offset +
                (std::rotl(index, 2) & 0xFFFFFFFCu),
            services,
            pc);
    };

    const std::uint32_t direction_target = table_callback(
        0x54u,
        guest_load_u32(memory, resource + 0x200u, services, 0x803A3B9Cu),
        0x803A3BACu) &
        0xFFFFFFFCu;
    const std::uint32_t projection_target = table_callback(
        0x7Cu,
        guest_load_u32(memory, resource + 0x208u, services, 0x803A3C94u),
        0x803A3CA0u) &
        0xFFFFFFFCu;
    const std::uint32_t draw_target = table_callback(
        0x48u,
        guest_load_u32(memory, resource + 0x210u, services, 0x803A3CD0u),
        0x803A3CDCu) &
        0xFFFFFFFCu;

    if ((direction_target != 0x803A35DCu &&
         direction_target != 0x803A35E8u) ||
        projection_target != 0x803A387Cu ||
        draw_target != 0x803A33ACu) {
        record_jpa_counter(jpa_profile_counters().draw_fast_misses);
        record_jpa_target(1u, direction_target);
        record_jpa_target(2u, projection_target);
        record_jpa_target(3u, draw_target);
        return false;
    }
    record_jpa_counter(jpa_profile_counters().draw_fast_hits);

    const auto load_vec = [&](std::uint32_t address, std::uint32_t pc) {
        const std::byte* base = resolve_guest(memory, address, 12u, services, pc);
        return Vec3{
            native_load_guest_f32_fast(base, 0u),
            native_load_guest_f32_fast(base, 4u),
            native_load_guest_f32_fast(base, 8u),
        };
    };
    const auto store_vec = [&](std::uint32_t address, const Vec3& value,
                               std::uint32_t pc) {
        std::byte* base = resolve_guest(memory, address, 12u, services, pc);
        native_store_guest_f32_fast(base, 0u, value.x);
        native_store_guest_f32_fast(base, 4u, value.y);
        native_store_guest_f32_fast(base, 8u, value.z);
        guest_notify_write(memory, address, 12u);
    };
    const auto load_f32 = [&](std::uint32_t address, std::uint32_t pc) {
        return native_load_guest_f32_fast(
            resolve_guest(memory, address, 4u, services, pc), 0u);
    };
    const auto abs_le_threshold = [](const Vec3& value, float threshold) {
        return std::fabs(value.x) <= threshold &&
               std::fabs(value.y) <= threshold &&
               std::fabs(value.z) <= threshold;
    };
    const auto normalize = [](const Vec3& value) {
        const float length_sq =
            std::fma(value.x, value.x,
                     std::fma(value.y, value.y, value.z * value.z));
        const float inv_length = 1.0F / std::sqrt(length_sq);
        return Vec3{
            value.x * inv_length,
            value.y * inv_length,
            value.z * inv_length,
        };
    };
    const auto cross = [](const Vec3& lhs, const Vec3& rhs) {
        return Vec3{
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x,
        };
    };

    context->gpr[12] = direction_target;
    context->ctr = direction_target;
    context->lr = 0x803A3BB8u;
    const std::uint32_t direction_source =
        direction_target == 0x803A35DCu ? work + 0x24u : work + 0x0Cu;
    Vec3 direction = load_vec(direction_source, direction_target);
    store_vec(frame + 0x14u, direction, 0x803A35DCu);

    const float threshold =
        load_f32(context->gpr[2] + 0x1830u, 0x803A3BB8u);
    if (abs_le_threshold(direction, threshold)) {
        context->gpr[3] = 1u;
        return true;
    }
    direction = normalize(direction);
    store_vec(frame + 0x14u, direction, 0x803A3704u);

    Vec3 basis_z = load_vec(work + 0x54u, 0x803A3708u);
    Vec3 basis_x = cross(basis_z, direction);
    store_vec(frame + 0x08u, basis_x, 0x803A373Cu);
    if (abs_le_threshold(basis_x, threshold)) {
        context->gpr[3] = 1u;
        return true;
    }
    basis_x = normalize(basis_x);
    store_vec(frame + 0x08u, basis_x, 0x803A37A0u);

    basis_z = normalize(cross(direction, basis_x));
    store_vec(work + 0x54u, basis_z, 0x803A37FCu);

    const float scale_xz =
        load_f32(resource + 0x144u, 0x803A38B8u) *
        load_f32(work + 0x60u, 0x803A38BCu);
    const float scale_y =
        load_f32(resource + 0x148u, 0x803A38CCu) *
        load_f32(work + 0x64u, 0x803A38D0u);

    float local[12]{
        basis_z.x * scale_xz,
        direction.x * scale_y,
        basis_x.x * scale_xz,
        load_f32(work + 0x00u, 0x803A38FCu),
        basis_z.y * scale_xz,
        direction.y * scale_y,
        basis_x.y * scale_xz,
        load_f32(work + 0x04u, 0x803A3910u),
        basis_z.z * scale_xz,
        direction.z * scale_y,
        basis_x.z * scale_xz,
        load_f32(work + 0x08u, 0x803A3924u),
    };

    const std::byte* parent =
        resolve_guest(memory, resource + 0x184u, 48u, services, 0x804B5F40u);
    float base[12]{};
    for (std::uint32_t i = 0; i < 12u; ++i) {
        base[i] = native_load_guest_f32_fast(parent, i * 4u);
    }

    const auto affine_dot3 = [&](std::uint32_t row, std::uint32_t column) {
        return std::fma(
            local[8u + column],
            base[row * 4u + 2u],
            std::fma(
                local[4u + column],
                base[row * 4u + 1u],
                local[column] * base[row * 4u]));
    };
    const auto affine_dot4 = [&](std::uint32_t row) {
        return std::fma(
            1.0F,
            base[row * 4u + 3u],
            std::fma(
                local[11u],
                base[row * 4u + 2u],
                std::fma(
                    local[7u],
                    base[row * 4u + 1u],
                    local[3u] * base[row * 4u])));
    };

    float output[12]{};
    for (std::uint32_t row = 0; row < 3u; ++row) {
        output[row * 4u + 0u] = affine_dot3(row, 0u);
        output[row * 4u + 1u] = affine_dot3(row, 1u);
        output[row * 4u + 2u] = affine_dot3(row, 2u);
        output[row * 4u + 3u] = affine_dot4(row);
    }

    std::byte* matrix =
        resolve_guest(memory, frame + 0x20u, 48u, services, 0x804B5FB8u);
    for (std::uint32_t i = 0; i < 12u; ++i) {
        native_store_guest_f32_fast(matrix, i * 4u, output[i]);
    }
    guest_notify_write(memory, frame + 0x20u, 48u);

    context->gpr[12] = projection_target;
    context->ctr = projection_target;
    context->lr = 0x803A3CACu;
    context->gpr[3] = frame + 0x20u;
    context->gpr[4] = 0u;
    native_gx_load_pos_mtx_imm_804BE1D8(
        context, memory, services, 0x804BE1D8u);

    context->gpr[12] = draw_target;
    context->ctr = draw_target;
    context->lr = 0x803A3CE8u;

    context->gpr[3] = table_callback(
        0x40u,
        guest_load_u32(memory, resource + 0x20Cu, services, 0x803A3CE8u),
        0x803A3CF8u);
    context->gpr[4] = 0x20u;
    context->lr = 0x803A3D00u;
    native_gx_begin_804BDEA8(
        context, memory, services, 0x804BDEA8u);
    call_return_checkpoint(
        services, 0x803A3CFCu, 0x803A3D00u, context, memory);
    return true;
}

inline void native_jpa_draw_particle_803A3B6C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_cast<void>(guest_pc);

    constexpr std::uint32_t kJPaDrawTable = 0x805C7220u;
    const std::uint32_t old_sp = context->gpr[1];
    const std::uint32_t frame = old_sp + 0xFFFFFFA0u;
    guest_store_u32(memory, frame, old_sp, services, 0x803A3B6Cu);
    context->gpr[1] = frame;
    context->gpr[0] = context->lr;
    guest_store_u32(memory, frame + 0x64u, context->gpr[0], services, 0x803A3B74u);
    context->gpr[11] = frame + 0x60u;
    context->lr = 0x803A3B80u;
    native_savegpr_805174FC(context, memory, services, 0x80517538u);

    const std::uint32_t resource = context->gpr[3];
    const std::uint32_t work = context->gpr[4];
    context->gpr[31] = kJPaDrawTable;
    context->gpr[29] = resource;
    context->gpr[30] = work;
    record_jpa_counter(jpa_profile_counters().draw_calls);

    const std::uint32_t flags =
        guest_load_u32(memory, work + 0x7Cu, services, 0x803A3B80u);
    if ((flags & 0x00000008u) == 0u) {
        const bool fast_path_handled =
            native_fast_host_float_helpers_enabled() &&
            native_jpa_draw_particle_fast_path(
                context, memory, services, frame, resource, work);
        if (!fast_path_handled) {
        const auto table_callback = [&](std::uint32_t table_offset,
                                        std::uint32_t index,
                                        std::uint32_t pc) {
            return guest_load_u32(
                memory,
                kJPaDrawTable + table_offset +
                    (std::rotl(index, 2) & 0xFFFFFFFCu),
                services,
                pc);
        };

        context->gpr[6] = kJPaDrawTable + 0x54u;
        context->gpr[5] = frame + 0x14u;
        context->gpr[12] = table_callback(
            0x54u,
            guest_load_u32(memory, resource + 0x200u, services, 0x803A3B9Cu),
            0x803A3BACu);
        context->ctr = context->gpr[12];
        static std::uint32_t cached_target_803A3BB4 = 0u;
        static NativeGameFunction cached_function_803A3BB4 = nullptr;
        native_jpa_direction_callback_803A3BB4(
            services,
            context->ctr & 0xFFFFFFFCu,
            &cached_target_803A3BB4,
            &cached_function_803A3BB4,
            context,
            memory);

        load_fpr_single(
            context, 1u, memory, context->gpr[2] + 0x1830u, services, 0x803A3BB8u);
        context->gpr[3] = frame + 0x14u;
        native_vec3_abs_le_threshold_803E595C(
            context, memory, services, 0x803E595Cu);

        if (static_cast<std::int32_t>(context->gpr[3]) == 0) {
            context->gpr[3] = frame + 0x14u;
            native_vec_normalize_in_place_803E4D24(
                context, memory, services, 0x803E4D24u);

            context->gpr[3] = work + 0x54u;
            context->gpr[4] = frame + 0x14u;
            context->gpr[5] = frame + 0x08u;
            native_psvec_cross_product(context, memory, services, 0x804B6CB8u);

            load_fpr_single(
                context, 1u, memory, context->gpr[2] + 0x1830u, services, 0x803A3BE4u);
            context->gpr[3] = frame + 0x08u;
            native_vec3_abs_le_threshold_803E595C(
                context, memory, services, 0x803E595Cu);

            if (static_cast<std::int32_t>(context->gpr[3]) == 0) {
                context->gpr[3] = frame + 0x08u;
                native_vec_normalize_in_place_803E4D24(
                    context, memory, services, 0x803E4D24u);

                context->gpr[3] = frame + 0x14u;
                context->gpr[4] = frame + 0x08u;
                context->gpr[5] = work + 0x54u;
                native_psvec_cross_product(context, memory, services, 0x804B6CB8u);

                context->gpr[3] = work + 0x54u;
                native_vec_normalize_in_place_803E4D24(
                    context, memory, services, 0x803E4D24u);

                context->gpr[4] = kJPaDrawTable + 0x7Cu;
                context->gpr[3] = frame + 0x20u;
                load_fpr_single(
                    context, 3u, memory, resource + 0x144u, services, 0x803A3C18u);
                load_fpr_single(
                    context, 1u, memory, work + 0x60u, services, 0x803A3C20u);
                load_fpr_single(
                    context, 2u, memory, resource + 0x148u, services, 0x803A3C28u);
                load_fpr_single(
                    context, 0u, memory, work + 0x64u, services, 0x803A3C2Cu);

                PpcFloatResult result = ppc_f64_binary_to_f32(
                    PpcFloatBinaryOperation::Multiply,
                    context->fpr_bits[3],
                    context->fpr_bits[1],
                    context->fpscr);
                ppc_commit_scalar_result(
                    context, 1u, result, true, false, services, 0x803A3C30u);
                result = ppc_f64_binary_to_f32(
                    PpcFloatBinaryOperation::Multiply,
                    context->fpr_bits[2],
                    context->fpr_bits[0],
                    context->fpscr);
                ppc_commit_scalar_result(
                    context, 2u, result, true, false, services, 0x803A3C3Cu);

                const auto copy_f32 = [&](std::uint32_t dst,
                                          std::uint32_t src,
                                          std::uint32_t pc) {
                    guest_store_u32(
                        memory,
                        dst,
                        guest_load_u32(memory, src, services, pc),
                        services,
                        pc);
                };
                copy_f32(frame + 0x20u, work + 0x54u, 0x803A3C44u);
                copy_f32(frame + 0x24u, frame + 0x14u, 0x803A3C4Cu);
                copy_f32(frame + 0x28u, frame + 0x08u, 0x803A3C54u);
                copy_f32(frame + 0x2Cu, work + 0x00u, 0x803A3C64u);
                copy_f32(frame + 0x30u, work + 0x58u, 0x803A3C6Cu);
                copy_f32(frame + 0x34u, frame + 0x18u, 0x803A3C70u);
                copy_f32(frame + 0x38u, frame + 0x0Cu, 0x803A3C74u);
                copy_f32(frame + 0x3Cu, work + 0x04u, 0x803A3C7Cu);
                copy_f32(frame + 0x40u, work + 0x5Cu, 0x803A3C84u);
                copy_f32(frame + 0x44u, frame + 0x1Cu, 0x803A3C88u);
                copy_f32(frame + 0x48u, frame + 0x10u, 0x803A3C8Cu);
                copy_f32(frame + 0x4Cu, work + 0x08u, 0x803A3C94u);

                context->gpr[12] = table_callback(
                    0x7Cu,
                    guest_load_u32(memory, resource + 0x208u, services, 0x803A3C98u),
                    0x803A3CA0u);
                context->ctr = context->gpr[12];
                static std::uint32_t cached_target_803A3CA8 = 0u;
                static NativeGameFunction cached_function_803A3CA8 = nullptr;
                native_jpa_projection_callback_803A3CA8(
                    services,
                    context->ctr & 0xFFFFFFFCu,
                    &cached_target_803A3CA8,
                    &cached_function_803A3CA8,
                    context,
                    memory);

                context->gpr[4] = frame + 0x20u;
                context->gpr[3] = resource + 0x184u;
                context->gpr[5] = context->gpr[4];
                native_psmtx_concat_804B5F3C(
                    context, memory, services, 0x804B5F3Cu);

                context->gpr[3] = frame + 0x20u;
                context->gpr[4] = 0u;
                native_gx_load_pos_mtx_imm_804BE1D8(
                    context, memory, services, 0x804BE1D8u);

                context->gpr[5] = kJPaDrawTable + 0x48u;
                context->gpr[3] = resource;
                context->gpr[4] = frame + 0x20u;
                context->gpr[12] = table_callback(
                    0x48u,
                    guest_load_u32(memory, resource + 0x210u, services, 0x803A3CC8u),
                    0x803A3CDCu);
                context->ctr = context->gpr[12];
                static std::uint32_t cached_target_803A3CE4 = 0u;
                static NativeGameFunction cached_function_803A3CE4 = nullptr;
                native_jpa_draw_callback_803A3CE4(
                    services,
                    context->ctr & 0xFFFFFFFCu,
                    &cached_target_803A3CE4,
                    &cached_function_803A3CE4,
                    context,
                    memory);

                context->gpr[3] = table_callback(
                    0x40u,
                    guest_load_u32(memory, resource + 0x20Cu, services, 0x803A3CE8u),
                    0x803A3CF8u);
                context->gpr[4] = 0x20u;
                context->lr = 0x803A3D00u;
                native_gx_begin_804BDEA8(
                    context, memory, services, 0x804BDEA8u);
                call_return_checkpoint(
                    services, 0x803A3CFCu, 0x803A3D00u, context, memory);
            }
        }
        }
    } else {
        record_jpa_counter(jpa_profile_counters().draw_flag_skips);
    }

    context->gpr[11] = context->gpr[1] + 0x60u;
    context->lr = 0x803A3D08u;
    native_restgpr_80517548(context, memory, services, 0x80517584u);
    context->gpr[0] =
        guest_load_u32(memory, context->gpr[1] + 0x64u, services, 0x803A3D08u);
    context->lr = context->gpr[0];
    context->gpr[1] = context->gpr[1] + 0x60u;
}

// Tight, bounded, hardware-instruction-style loops (cache range management --
// DCInvalidateRange/DCFlushRange/DCStoreRange -- and similar counted
// dcbi/dcbf/dcbst back-edge loops) checkpoint on every single iteration even
// though each iteration is one trivial hardware instruction on real silicon.
// Measured: a single such loop can dominate 90%+ of all full-callback
// escalations even though the existing checkpoint_next_slow watermark is
// already throttling overall. That watermark is a global cadence, so it gets
// pushed past "due" again quickly by other unrelated checkpoint traffic
// in between a tight loop's own iterations; it cannot, by itself, recognize
// "this exact backward branch is repeating." This adds that recognition as a
// second, independent, purely additive throttle: while the same guest_pc
// checkpoints back-to-back many times in a row, only escalate to the full
// callback every kRepeatPcSlowInterval-th hit instead of every hit. It never
// suppresses a checkpoint when a real event is pending, and it decays
// immediately once guest_pc changes (leaving the loop), so ordinary
// non-looping control flow is completely unaffected.
// C++17 inline variables: guaranteed one instance program-wide (unlike a
// function-local static in a header-defined __forceinline function on MSVC
// without an explicit `inline` linkage specifier, which is not guaranteed to
// be deduplicated across translation units). Used for both the repeat-PC
// throttle and the diagnostic breakdown of why escalation happened.
inline std::uint32_t g_checkpoint_repeat_pc = 0u;
inline std::uint32_t g_checkpoint_repeat_streak = 0u;
inline std::uint64_t g_checkpoint_escalate_event_pending = 0u;
inline std::uint64_t g_checkpoint_escalate_trace = 0u;
inline std::uint64_t g_checkpoint_escalate_watermark = 0u;
inline std::uint64_t g_checkpoint_escalate_watermark_throttled = 0u;

// Complete adaptive-checkpoint policy. `counted_checkpoint` is the counter
// value when the inline fast path below already advanced it (0: not yet).
GALAXY_NOINLINE inline void branch_checkpoint_taken_slow(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t resume_pc,
    PpcContext* context,
    GuestMemoryV1* memory,
    std::uint64_t counted_checkpoint) {
    if (services == nullptr || services->branch_checkpoint == nullptr) {
        guest_execution_fault(
            services, guest_pc, "native branch-checkpoint service is unavailable");
    }
    constexpr std::uint32_t kRepeatPcSlowInterval = 256u;
    if (services->checkpoint_counter != nullptr &&
        services->checkpoint_next_slow != nullptr) {
        const std::uint64_t checkpoint = counted_checkpoint != 0u
            ? counted_checkpoint
            : ++(*services->checkpoint_counter);
        const bool event_pending =
            services->pending_event_mask != nullptr &&
            atomic_load_u32(services->pending_event_mask) != 0u;
        const bool trace_native_input_checkpoint =
            (services->runtime_flags & kNativeServiceFlagTraceWpadReadCopies) != 0u &&
            is_native_input_trace_call_return_pc(guest_pc);
        bool skip = checkpoint < *services->checkpoint_next_slow &&
            !event_pending && !trace_native_input_checkpoint;
        if (!skip && !event_pending && !trace_native_input_checkpoint) {
            // This site would otherwise escalate purely from watermark
            // expiry (no real event, no trace). Check the repeat-PC streak
            // before paying the full callback cost.
            //
            // This is now the only place the streak is maintained, so it is a
            // real count: the fast path in branch_checkpoint_taken no longer
            // zeroes it. The decay is `guest_pc != repeat_pc`, i.e. it resets
            // exactly when escalation moves to different code, which is the
            // documented intent.
            ++g_checkpoint_escalate_watermark;
            if (guest_pc == g_checkpoint_repeat_pc) {
                ++g_checkpoint_repeat_streak;
            } else {
                g_checkpoint_repeat_pc = guest_pc;
                g_checkpoint_repeat_streak = 1u;
            }
            if (g_checkpoint_repeat_streak > 1u &&
                (g_checkpoint_repeat_streak % kRepeatPcSlowInterval) != 1u) {
                skip = true;
                ++g_checkpoint_escalate_watermark_throttled;
            }
        } else {
            g_checkpoint_repeat_pc = 0u;
            g_checkpoint_repeat_streak = 0u;
            if (event_pending) {
                ++g_checkpoint_escalate_event_pending;
            } else if (trace_native_input_checkpoint) {
                ++g_checkpoint_escalate_trace;
            }
        }
        if (skip) {
            return;
        }
    }
    trace_strlen_stall_checkpoint(services, guest_pc, context, memory);
    context->pc = resume_pc;
    services->branch_checkpoint(services->user, guest_pc, context, memory);
}

// Hot path of every translated backward branch and call return: advance the
// counter and, unless it reached the published watermark, an event is pending
// or WPAD copy tracing is on, leave after a handful of instructions. The
// escalation bookkeeping (identical to the slow function) stays out of line.
GALAXY_ALWAYS_INLINE void branch_checkpoint_taken(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t resume_pc,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (services != nullptr && services->branch_checkpoint != nullptr &&
        services->checkpoint_counter != nullptr &&
        services->checkpoint_next_slow != nullptr &&
        services->pending_event_mask != nullptr &&
        (services->runtime_flags & kNativeServiceFlagTraceWpadReadCopies) == 0u)
        [[likely]] {
        const std::uint64_t checkpoint = ++(*services->checkpoint_counter);
        if (checkpoint < *services->checkpoint_next_slow &&
            atomic_load_u32(services->pending_event_mask) == 0u) [[likely]] {
            // Non-escalating path: no guest_pc was consumed and no escalation
            // decision was made, so the repeat-PC streak has nothing to record.
            //
            // It used to be cleared here, which made the streak always exactly
            // zero when branch_checkpoint_taken_slow read it. The throttle below
            // requires `streak > 1`, so it could never fire: every watermark
            // escalation paid the full host callback, and
            // g_checkpoint_escalate_watermark_throttled stayed 0. Clearing it
            // here is also what the decay was *meant* to be -- but the decay
            // belongs where the state is read, because the slow function can be
            // reached without passing through this fast path at all (the whole
            // `[[likely]]` guard can be false, and call_return_checkpoint's
            // general path reaches branch_checkpoint_taken directly).
            //
            // Leaving it untouched keeps two global stores off the hottest path
            // in the process: this is force-inlined at every translated backward
            // branch and every translated call return.
            return;
        }
        branch_checkpoint_taken_slow(
            services, guest_pc, resume_pc, context, memory, checkpoint);
        return;
    }
    branch_checkpoint_taken_slow(
        services, guest_pc, resume_pc, context, memory, 0u);
}

GALAXY_ALWAYS_INLINE void branch_checkpoint(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    PpcContext* context,
    GuestMemoryV1* memory) {
    branch_checkpoint_taken(
        services, guest_pc, guest_pc, context, memory);
}

// mtmsr is an architectural interrupt-recognition boundary.  This statically
// emitted checkpoint deliberately bypasses the adaptive branch gate so a DEC
// request latched while MSR[EE] was clear is taken before the next translated
// instruction.  It is not a polling hook and performs no runtime decoding.
GALAXY_ALWAYS_INLINE void architectural_interrupt_checkpoint(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t resume_pc,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (context == nullptr || services == nullptr ||
        services->branch_checkpoint == nullptr) {
        guest_execution_fault(
            services,
            guest_pc,
            "native architectural-interrupt checkpoint service is unavailable");
    }
    context->pc = resume_pc;
    // mtmsr remains context-synchronizing in generated code, but no async
    // exception is architecturally recognizable while the newly written EE
    // bit is clear. Avoid a needless hot host callback for OSDisableInterrupts.
    if ((context->msr & kMsrExternalInterruptEnable) == 0u) {
        return;
    }
    const bool has_counter = services->checkpoint_counter != nullptr;
    const bool has_threshold = services->checkpoint_next_slow != nullptr;
    if (has_counter != has_threshold) {
        guest_execution_fault(
            services,
            guest_pc,
            "native checkpoint gate published an incomplete pointer pair");
    }
    if (has_counter) {
        ++(*services->checkpoint_counter);
    }
    services->branch_checkpoint(services->user, guest_pc, context, memory);
}

GALAXY_NOINLINE inline void call_return_checkpoint_general(
    const NativeServicesV1* services,
    std::uint32_t call_pc,
    std::uint32_t return_pc,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (services == nullptr || services->branch_checkpoint == nullptr ||
        services->pending_event_mask == nullptr) {
        guest_execution_fault(
            services,
            call_pc,
            "native call-return checkpoint services are unavailable");
    }
    const bool event_pending =
        atomic_load_u32(services->pending_event_mask) != 0u;
    const bool trace_native_input_checkpoint =
        (services->runtime_flags & kNativeServiceFlagTraceWpadReadCopies) != 0u &&
        is_native_input_trace_call_return_pc(call_pc);
    const bool adaptive_call_return_checkpoint =
        (services->runtime_flags &
         kNativeServiceFlagAdaptiveCallReturnCheckpoints) != 0u &&
        services->checkpoint_counter != nullptr &&
        services->checkpoint_next_slow != nullptr;
    if (!event_pending && !trace_native_input_checkpoint &&
        !adaptive_call_return_checkpoint) {
        return;
    }
    branch_checkpoint_taken(
        services, call_pc, return_pc, context, memory);
}

// An adaptive call return must always enter the counted branch policy. That
// policy already observes pending events; a preliminary peek cannot suppress
// counting or deliver anything, so it only duplicates the atomic load. Keep
// trace and malformed service tables on the complete policy. An empty ordinary
// nonadaptive return performs the same validated observation inline.
GALAXY_ALWAYS_INLINE void call_return_checkpoint(
    const NativeServicesV1* services,
    std::uint32_t call_pc,
    std::uint32_t return_pc,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (services != nullptr && services->branch_checkpoint != nullptr &&
        services->pending_event_mask != nullptr &&
        (services->runtime_flags & kNativeServiceFlagTraceWpadReadCopies) == 0u) {
        if ((services->runtime_flags &
             kNativeServiceFlagAdaptiveCallReturnCheckpoints) != 0u) {
            if (services->checkpoint_counter != nullptr &&
                services->checkpoint_next_slow != nullptr) [[likely]] {
                branch_checkpoint_taken(
                    services, call_pc, return_pc, context, memory);
                return;
            }
        } else if (atomic_load_u32(services->pending_event_mask) == 0u) [[likely]] {
            return;
        }
    }
    call_return_checkpoint_general(
        services, call_pc, return_pc, context, memory);
}

GALAXY_ALWAYS_INLINE void native_input_trace_copy_pre_checkpoint(
    const NativeServicesV1* services,
    std::uint32_t call_pc,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (services == nullptr ||
        (services->runtime_flags & kNativeServiceFlagTraceWpadReadCopies) == 0u ||
        !is_wpad_read_status_copy_call_pc(call_pc)) {
        return;
    }
    if (services->branch_checkpoint == nullptr) {
        guest_execution_fault(
            services,
            call_pc,
            "native input-copy pre-checkpoint service is unavailable");
    }
    // Do not route through branch_checkpoint_taken: this observation must not
    // advance the adaptive checkpoint counter or overwrite context->pc before
    // the actual linked guest call executes.
    services->branch_checkpoint(
        services->user,
        native_input_trace_copy_pre_checkpoint_pc(call_pc),
        context,
        memory);
}

inline void native_name_obj_dispatch_core_802617B4(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services) {
    const std::uint32_t frame = context->gpr[1] - 0x20u;
    guest_store_u32(memory, frame, context->gpr[1], services, 0x802617B4u);
    context->gpr[1] = frame;
    context->gpr[0] = context->lr;
    guest_store_u32(memory, frame + 0x24u, context->gpr[0], services, 0x802617BCu);
    context->gpr[11] = frame + 0x20u;
    context->lr = 0x802617C8u;
    native_savegpr_805174FC(context, memory, services, 0x80517538u);

    const std::uint32_t base = context->gpr[3];
    const std::uint32_t record_table =
        guest_load_u32(memory, base, services, 0x802617CCu);
    const std::uint32_t record =
        record_table + context->gpr[4] * 0x14u;
    context->gpr[29] = base;
    context->gpr[31] = record;

    if (guest_load_u32(memory, record + 0x08u, services, 0x802617D8u) != 0u) {
        const std::uint32_t first =
            guest_load_u32(memory, record + 0x0Cu, services, 0x802617E4u);
        if (first != 0u) {
            context->gpr[3] = first;
            context->gpr[12] =
                guest_load_u32(memory, first, services, 0x802617F0u);
            context->gpr[12] =
                guest_load_u32(memory, context->gpr[12] + 0x08u, services, 0x802617F4u);
            context->ctr = context->gpr[12];
            context->lr = 0x80261800u;
            static std::uint32_t cached_target_802617FC = 0u;
            static NativeGameFunction cached_function_802617FC = nullptr;
            call_guest_cached(
                services,
                context->ctr & 0xFFFFFFFCu,
                &cached_target_802617FC,
                &cached_function_802617FC,
                context,
                memory,
                0x802617FCu);
        }

        std::uint32_t cursor =
            guest_load_u32(memory, record, services, 0x80261800u);
        for (;;) {
            const std::uint32_t start =
                guest_load_u32(memory, record, services, 0x80261828u);
            const std::uint32_t count =
                guest_load_u32(memory, record + 0x08u, services, 0x80261824u);
            const std::uint32_t end = start + ((count << 2u) & 0xFFFFFFFCu);
            if (cursor == end) {
                break;
            }
            context->gpr[30] = cursor;
            branch_checkpoint_taken(
                services, 0x80261838u, 0x80261808u, context, memory);
            context->gpr[3] =
                guest_load_u32(memory, base + 0x08u, services, 0x80261808u);
            context->gpr[4] =
                guest_load_u32(memory, cursor, services, 0x8026180Cu);
            context->gpr[12] =
                guest_load_u32(memory, context->gpr[3], services, 0x80261810u);
            context->gpr[12] =
                guest_load_u32(memory, context->gpr[12] + 0x08u, services, 0x80261814u);
            context->ctr = context->gpr[12];
            context->lr = 0x80261820u;
            static std::uint32_t cached_target_8026181C = 0u;
            static NativeGameFunction cached_function_8026181C = nullptr;
            call_guest_cached(
                services,
                context->ctr & 0xFFFFFFFCu,
                &cached_target_8026181C,
                &cached_function_8026181C,
                context,
                memory,
                0x8026181Cu);
            cursor += 4u;
        }
    }

    context->gpr[11] = frame + 0x20u;
    context->lr = 0x80261844u;
    native_restgpr_80517548(context, memory, services, 0x80517584u);
    context->gpr[0] =
        guest_load_u32(memory, frame + 0x24u, services, 0x80261844u);
    context->lr = context->gpr[0];
    context->gpr[1] = frame + 0x20u;
}

inline void native_name_obj_dispatch_offset(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t offset) {
    context->gpr[3] =
        guest_load_u32(memory, context->gpr[3] + offset, services, guest_pc);
    native_name_obj_dispatch_core_802617B4(context, memory, services);
}

inline void native_name_obj_dispatch_8026C0A0(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    native_name_obj_dispatch_offset(context, memory, services, guest_pc, 0x08u);
}

inline void native_name_obj_dispatch_8026C0A8(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    native_name_obj_dispatch_offset(context, memory, services, guest_pc, 0x0Cu);
}

inline void native_name_obj_dispatch_8026C0E4(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    native_name_obj_dispatch_offset(context, memory, services, guest_pc, 0x10u);
}

inline void native_jas_dsp_set_bus_connect_80495C3C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    constexpr std::array<std::uint16_t, 12> kConnectTable{
        0x0000u, 0x0D00u, 0x0D60u, 0x0DC0u,
        0x0E20u, 0x0E80u, 0x0EE0u, 0x0CA0u,
        0x0F40u, 0x0FA0u, 0x0B00u, 0x09A0u,
    };

    const std::uint32_t channel = context->gpr[3];
    const std::uint32_t bus = context->gpr[4];
    const std::uint32_t connect_index = context->gpr[5];
    if (bus >= 6u) {
        char message[96]{};
        std::snprintf(
            message,
            sizeof(message),
            "JASDsp::TChannel::setBusConnect bus index %u is out of range",
            static_cast<unsigned int>(bus));
        guest_execution_fault(
            services, guest_pc, message);
    }
    if (connect_index >= kConnectTable.size() && connect_index != 0xFFu) {
        char message[104]{};
        std::snprintf(
            message,
            sizeof(message),
            "JASDsp::TChannel::setBusConnect route index 0x%02X is out of range",
            static_cast<unsigned int>(connect_index));
        guest_execution_fault(
            services, guest_pc, message);
    }

    context->gpr[6] = 0x8054D2B0u;
    context->gpr[0] = (bus << 3u) & 0x000007F8u;
    context->gpr[4] = (connect_index << 1u) & 0x000001FEu;
    context->gpr[3] = channel + context->gpr[0];
    context->gpr[0] = connect_index == 0xFFu ? 0u : kConnectTable[connect_index];
    guest_store_u16(
        memory,
        context->gpr[3] + 0x10u,
        static_cast<std::uint16_t>(context->gpr[0]),
        services,
        0x80495C54u);
}

inline void native_jpa_actor_check_80448D54(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    const std::uint32_t actor = context->gpr[3];
    context->gpr[4] = guest_load_u32(memory, actor + 0xE8u, services, 0x80448D54u);
    context->gpr[5] = static_cast<std::uint32_t>(static_cast<std::int32_t>(
        static_cast<std::int16_t>(
            guest_load_u16(memory, actor + 0x104u, services, 0x80448D58u))));
    context->gpr[4] =
        guest_load_u32(memory, context->gpr[4] + 0x2Cu, services, 0x80448D5Cu);
    context->gpr[4] =
        guest_load_u32(memory, context->gpr[4], services, 0x80448D60u);
    context->gpr[0] = static_cast<std::uint32_t>(static_cast<std::int32_t>(
        static_cast<std::int16_t>(
            guest_load_u16(memory, context->gpr[4] + 0x70u, services, 0x80448D64u))));
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[5]),
        static_cast<std::int32_t>(context->gpr[0]));
    if (!cr_bit(context, 0u)) {
        context->gpr[3] = 1u;
        return;
    }

    context->gpr[0] =
        guest_load_u32(memory, actor + 0xF4u, services, 0x80448D78u);
    context->gpr[0] &= 0x00000002u;
    update_cr0(context, context->gpr[0]);
    if (cr_bit(context, 2u)) {
        context->gpr[0] = context->gpr[5] + 1u;
        guest_store_u16(
            memory,
            actor + 0x104u,
            static_cast<std::uint16_t>(context->gpr[0]),
            services,
            0x80448D88u);
    }
    context->gpr[3] = 0u;
}

inline void native_jpa_actor_check_80448D94(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    const std::uint32_t actor = context->gpr[3];
    context->gpr[0] =
        guest_load_u32(memory, actor + 0xF4u, services, 0x80448D94u);
    context->gpr[0] &= 0x00000100u;
    update_cr0(context, context->gpr[0]);
    if (!cr_bit(context, 2u)) {
        context->gpr[3] = 1u;
        return;
    }

    context->gpr[4] =
        guest_load_u32(memory, actor + 0x24u, services, 0x80448DA8u);
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[4]),
        static_cast<std::int32_t>(0));
    if (cr_bit(context, 2u)) {
        context->gpr[3] = 0u;
        return;
    }

    if (cr_bit(context, 0u)) {
        context->gpr[5] =
            guest_load_u32(memory, actor + 0xF4u, services, 0x80448DC0u);
        context->gpr[4] =
            guest_load_u32(memory, actor + 0xD0u, services, 0x80448DC4u);
        context->gpr[0] =
            guest_load_u32(memory, actor + 0xDCu, services, 0x80448DC8u);
        context->gpr[5] |= 0x00000008u;
        guest_store_u32(memory, actor + 0xF4u, context->gpr[5], services, 0x80448DD0u);
        context->gpr[0] = context->gpr[4] + context->gpr[0];
        context->gpr[0] = static_cast<std::uint32_t>(std::countl_zero(context->gpr[0]));
        context->gpr[3] = std::rotl(context->gpr[0], 27) & 0x07FFFFFFu;
        return;
    }

    context->gpr[0] =
        guest_load_u32(memory, actor + 0x100u, services, 0x80448DE4u);
    compare_unsigned(context, 0u, context->gpr[0], context->gpr[4]);
    if (cr_bit(context, 0u)) {
        context->gpr[3] = 0u;
        return;
    }

    context->gpr[0] =
        guest_load_u32(memory, actor + 0xF4u, services, 0x80448DF0u);
    context->gpr[4] = context->gpr[0] | 0x00000008u;
    context->gpr[0] = context->gpr[4] & 0x00000040u;
    update_cr0(context, context->gpr[0]);
    guest_store_u32(memory, actor + 0xF4u, context->gpr[4], services, 0x80448DFCu);
    if (!cr_bit(context, 2u)) {
        context->gpr[3] = 0u;
        return;
    }

    context->gpr[4] =
        guest_load_u32(memory, actor + 0xD0u, services, 0x80448E0Cu);
    context->gpr[0] =
        guest_load_u32(memory, actor + 0xDCu, services, 0x80448E10u);
    context->gpr[0] = context->gpr[4] + context->gpr[0];
    context->gpr[0] = static_cast<std::uint32_t>(std::countl_zero(context->gpr[0]));
    context->gpr[3] = std::rotl(context->gpr[0], 27) & 0x07FFFFFFu;
}

inline void native_jpa_list_remove_80443D78(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    context->gpr[5] =
        guest_load_u32(memory, context->gpr[4] + 0x04u, services, 0x80443D78u);
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[5]),
        static_cast<std::int32_t>(0));
    if (!cr_bit(context, 2u)) {
        context->gpr[6] =
            guest_load_u32(memory, context->gpr[4], services, 0x80443D84u);
        compare_signed(
            context,
            0u,
            static_cast<std::int32_t>(context->gpr[6]),
            static_cast<std::int32_t>(0));
        if (!cr_bit(context, 2u)) {
            guest_store_u32(
                memory,
                context->gpr[6] + 0x04u,
                context->gpr[5],
                services,
                0x80443D90u);
            context->gpr[0] =
                guest_load_u32(memory, context->gpr[4], services, 0x80443D94u);
            context->gpr[5] =
                guest_load_u32(memory, context->gpr[4] + 0x04u, services, 0x80443D98u);
            guest_store_u32(memory, context->gpr[5], context->gpr[0], services, 0x80443D9Cu);
            context->gpr[5] =
                guest_load_u32(memory, context->gpr[3] + 0x08u, services, 0x80443DA0u);
            context->gpr[0] = context->gpr[5] + 0xFFFFFFFFu;
            guest_store_u32(memory, context->gpr[3] + 0x08u, context->gpr[0], services, 0x80443DA8u);
            context->gpr[3] = context->gpr[4] | context->gpr[4];
            return;
        }
    }

    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[5]),
        static_cast<std::int32_t>(0));
    if (!cr_bit(context, 2u)) {
        context->gpr[0] = 0u;
        guest_store_u32(memory, context->gpr[5], context->gpr[0], services, 0x80443DBCu);
        context->gpr[5] =
            guest_load_u32(memory, context->gpr[3] + 0x08u, services, 0x80443DC0u);
        context->gpr[6] =
            guest_load_u32(memory, context->gpr[4] + 0x04u, services, 0x80443DC4u);
        context->gpr[0] = context->gpr[5] + 0xFFFFFFFFu;
        guest_store_u32(memory, context->gpr[3], context->gpr[6], services, 0x80443DCCu);
        guest_store_u32(memory, context->gpr[3] + 0x08u, context->gpr[0], services, 0x80443DD0u);
        context->gpr[3] = context->gpr[4] | context->gpr[4];
        return;
    }

    context->gpr[5] =
        guest_load_u32(memory, context->gpr[4], services, 0x80443DD8u);
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[5]),
        static_cast<std::int32_t>(0));
    if (!cr_bit(context, 2u)) {
        context->gpr[0] = 0u;
        guest_store_u32(memory, context->gpr[5] + 0x04u, context->gpr[0], services, 0x80443DE8u);
        context->gpr[5] =
            guest_load_u32(memory, context->gpr[3] + 0x08u, services, 0x80443DECu);
        context->gpr[6] =
            guest_load_u32(memory, context->gpr[4], services, 0x80443DF0u);
        context->gpr[0] = context->gpr[5] + 0xFFFFFFFFu;
        guest_store_u32(memory, context->gpr[3] + 0x04u, context->gpr[6], services, 0x80443DF8u);
        guest_store_u32(memory, context->gpr[3] + 0x08u, context->gpr[0], services, 0x80443DFCu);
        context->gpr[3] = context->gpr[4] | context->gpr[4];
        return;
    }

    context->gpr[5] =
        guest_load_u32(memory, context->gpr[3] + 0x08u, services, 0x80443E04u);
    context->gpr[6] = 0u;
    guest_store_u32(memory, context->gpr[3] + 0x04u, context->gpr[6], services, 0x80443E0Cu);
    context->gpr[0] = context->gpr[5] + 0xFFFFFFFFu;
    guest_store_u32(memory, context->gpr[3], context->gpr[6], services, 0x80443E14u);
    guest_store_u32(memory, context->gpr[3] + 0x08u, context->gpr[0], services, 0x80443E18u);
    context->gpr[3] = context->gpr[4] | context->gpr[4];
}

inline void native_jpa_list_append_80443E24(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    context->gpr[0] = guest_load_u32(memory, context->gpr[3], services, 0x80443E24u);
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[0]),
        static_cast<std::int32_t>(0));
    if (!cr_bit(context, 2u)) {
        context->gpr[0] = 0u;
        guest_store_u32(memory, context->gpr[4], context->gpr[0], services, 0x80443E34u);
        context->gpr[0] = guest_load_u32(memory, context->gpr[3], services, 0x80443E38u);
        guest_store_u32(memory, context->gpr[4] + 0x04u, context->gpr[0], services, 0x80443E3Cu);
        context->gpr[5] = guest_load_u32(memory, context->gpr[3], services, 0x80443E40u);
        guest_store_u32(memory, context->gpr[5], context->gpr[4], services, 0x80443E44u);
        guest_store_u32(memory, context->gpr[3], context->gpr[4], services, 0x80443E48u);
    } else {
        guest_store_u32(memory, context->gpr[3] + 0x04u, context->gpr[4], services, 0x80443E50u);
        context->gpr[0] = 0u;
        guest_store_u32(memory, context->gpr[3], context->gpr[4], services, 0x80443E58u);
        guest_store_u32(memory, context->gpr[4], context->gpr[0], services, 0x80443E5Cu);
        guest_store_u32(memory, context->gpr[4] + 0x04u, context->gpr[0], services, 0x80443E60u);
    }
    context->gpr[4] =
        guest_load_u32(memory, context->gpr[3] + 0x08u, services, 0x80443E64u);
    context->gpr[0] = context->gpr[4] + 1u;
    guest_store_u32(memory, context->gpr[3] + 0x08u, context->gpr[0], services, 0x80443E6Cu);
}

inline void native_jpa_actor_flags_mask_80447264(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    context->gpr[3] =
        guest_load_u32(memory, context->gpr[3] + 0xE8u, services, 0x80447264u);
    context->gpr[3] =
        guest_load_u32(memory, context->gpr[3] + 0x2Cu, services, 0x80447268u);
    context->gpr[3] =
        guest_load_u32(memory, context->gpr[3], services, 0x8044726Cu);
    context->gpr[0] =
        guest_load_u32(memory, context->gpr[3] + 0x08u, services, 0x80447270u);
    context->gpr[3] = context->gpr[4] & context->gpr[0];
}

inline void native_jpa_random_i16_8044727C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t) {
    context->gpr[4] = 0x00190000u;
    context->gpr[5] =
        guest_load_u32(memory, context->gpr[3] + 0xC4u, services, 0x80447280u);
    context->gpr[0] = context->gpr[4] + 0x660Du;
    const std::int64_t product =
        static_cast<std::int64_t>(static_cast<std::int32_t>(context->gpr[5])) *
        static_cast<std::int32_t>(context->gpr[0]);
    context->gpr[4] = static_cast<std::uint32_t>(product);
    context->gpr[4] = context->gpr[4] + 0x3C6F0000u;
    context->gpr[0] = context->gpr[4] + 0xFFFFF35Fu;
    guest_store_u32(memory, context->gpr[3] + 0xC4u, context->gpr[0], services, 0x80447294u);
    context->gpr[0] = std::rotl(context->gpr[0], 16) & 0x0000FFFFu;
    context->gpr[3] = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(static_cast<std::int16_t>(context->gpr[0])));
}

inline void native_actor_list_apply_80448710(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    if (context->pc != 0x80448710u) [[unlikely]] {
        guest_execution_fault(
            services, context->pc, "invalid interior function entry");
    }

    const std::uint32_t frame = context->gpr[1] + 0xFFFFFFE0u;
    guest_store_u32(memory, frame, context->gpr[1], services, guest_pc);
    context->gpr[1] = frame;
    context->gpr[0] = context->lr;
    guest_store_u32(memory, frame + 0x24u, context->gpr[0], services, 0x80448718u);
    context->gpr[11] = frame + 0x20u;
    context->lr = 0x80448724u;
    native_savegpr_805174FC(context, memory, services, 0x80517538u);

    context->gpr[0] = context->gpr[4] * 0x0Cu;
    context->gpr[4] =
        guest_load_u32(memory, context->gpr[3], services, 0x80448728u);
    context->gpr[29] = context->gpr[3];
    context->gpr[30] =
        guest_load_u32(memory, context->gpr[4] + context->gpr[0], services, 0x80448730u);

    static std::uint32_t cached_target_8044874C = 0u;
    static NativeGameFunction cached_function_8044874C = nullptr;
    static std::uint32_t cached_target_8044876C = 0u;
    static NativeGameFunction cached_function_8044876C = nullptr;

    while (static_cast<std::int32_t>(context->gpr[30]) != 0) {
        branch_checkpoint_taken(
            services, 0x80448774u, 0x80448738u, context, memory);
        context->gpr[31] =
            guest_load_u32(memory, context->gpr[30], services, 0x80448738u);
        context->gpr[30] =
            guest_load_u32(memory, context->gpr[30] + 0x0Cu, services, 0x8044873Cu);
        context->gpr[3] =
            guest_load_u32(memory, context->gpr[31] + 0xE8u, services, 0x80448740u);
        context->gpr[5] = context->gpr[31];
        context->gpr[4] =
            guest_load_u32(memory, context->gpr[29] + 0x20u, services, 0x80448748u);
        context->lr = 0x80448750u;
        call_guest_cached(
            services,
            0x80442AB8u,
            &cached_target_8044874C,
            &cached_function_8044874C,
            context,
            memory,
            0x8044874Cu);

        compare_signed(
            context,
            0u,
            static_cast<std::int32_t>(context->gpr[3]),
            static_cast<std::int32_t>(0));
        if (cr_bit(context, 2u)) {
            continue;
        }

        context->gpr[0] =
            guest_load_u32(memory, context->gpr[31] + 0xF4u, services, 0x80448758u);
        context->gpr[0] &= 0x00000200u;
        update_cr0(context, context->gpr[0]);
        if (!cr_bit(context, 2u)) {
            continue;
        }

        context->gpr[3] = context->gpr[29];
        context->gpr[4] = context->gpr[31];
        context->lr = 0x80448770u;
        call_guest_cached(
            services,
            0x80448984u,
            &cached_target_8044876C,
            &cached_function_8044876C,
            context,
            memory,
            0x8044876Cu);
    }

    context->gpr[11] = frame + 0x20u;
    context->lr = 0x80448780u;
    native_restgpr_80517548(context, memory, services, 0x80517584u);
    context->gpr[0] =
        guest_load_u32(memory, frame + 0x24u, services, 0x80448780u);
    context->lr = context->gpr[0];
    context->gpr[1] = frame + 0x20u;
}

inline void native_dsp_running_check_80496A60(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t flag_address = context->gpr[13] + 0xFFFFDF58u;
    std::uint32_t source_value =
        guest_load_u8(memory, flag_address, services, 0x80496A60u);

    if (source_value == 0u) {
        branch_checkpoint(services, guest_pc, context, memory);
        source_value =
            guest_load_u8(memory, context->gpr[13] + 0xFFFFDF58u, services, 0x80496A60u);
    }

    context->gpr[0] = 0x00000001u - source_value;
    set_xer_ca(context, 0x00000001u >= source_value);
    context->gpr[0] = static_cast<std::uint32_t>(std::countl_zero(context->gpr[0]));
    context->gpr[3] = std::rotl(context->gpr[0], 27) & 0x07FFFFFFu;
}

GALAXY_ALWAYS_INLINE void native_ptmf_scall_805173E0(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    context->gpr[0] = guest_load_u32(memory, context->gpr[12], services, guest_pc);
    context->gpr[11] = guest_load_u32(memory, context->gpr[12] + 4u, services, 0x805173E4u);
    context->gpr[12] = guest_load_u32(memory, context->gpr[12] + 8u, services, 0x805173E8u);
    context->gpr[3] = context->gpr[3] + context->gpr[0];
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[11]),
        static_cast<std::int32_t>(0));
    if (!cr_bit(context, 0u)) {
        context->gpr[12] =
            guest_load_u32(memory, context->gpr[3] + context->gpr[12], services, 0x805173F8u);
        context->gpr[12] =
            guest_load_u32(memory, context->gpr[12] + context->gpr[11], services, 0x805173FCu);
    }
    context->ctr = context->gpr[12];

    const std::uint32_t target = context->ctr & 0xFFFFFFFCu;
    constexpr std::size_t cache_slot_count = 256u;
    static std::array<std::uint32_t, cache_slot_count> cached_targets_80517404{};
    static std::array<NativeGameFunction, cache_slot_count> cached_functions_80517404{};
    const std::size_t cache_index =
        ((target >> 2u) ^ (target >> 6u) ^ (target >> 11u)) &
        (cache_slot_count - 1u);
    call_guest_cached(
        services,
        target,
        &cached_targets_80517404[cache_index],
        &cached_functions_80517404[cache_index],
        context,
        memory,
        0x80517404u);
}

inline void native_movement_group_dispatch_80261454(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    context->gpr[3] =
        guest_load_u32(memory, context->gpr[3] + 0x10u, services, guest_pc);
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[3]),
        static_cast<std::int32_t>(0));
    if (cr_bit(context, 2u)) {
        return;
    }
    context->gpr[12] =
        guest_load_u32(memory, context->gpr[3], services, guest_pc + 0x0Cu);
    context->gpr[12] =
        guest_load_u32(memory, context->gpr[12] + 0x08u, services, guest_pc + 0x10u);
    context->ctr = context->gpr[12];
    static std::uint32_t cached_target_8026146C = 0u;
    static NativeGameFunction cached_function_8026146C = nullptr;
    call_guest_cached(
        services,
        context->ctr & 0xFFFFFFFCu,
        &cached_target_8026146C,
        &cached_function_8026146C,
        context,
        memory,
        0x8026146Cu);
}

inline void native_movement_group_dispatch_80261494(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    context->gpr[3] =
        guest_load_u32(memory, context->gpr[3] + 0x18u, services, guest_pc);
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(context->gpr[3]),
        static_cast<std::int32_t>(0));
    if (cr_bit(context, 2u)) {
        return;
    }
    context->gpr[12] =
        guest_load_u32(memory, context->gpr[3], services, guest_pc + 0x0Cu);
    context->gpr[12] =
        guest_load_u32(memory, context->gpr[12] + 0x08u, services, guest_pc + 0x10u);
    context->ctr = context->gpr[12];
    static std::uint32_t cached_target_802614AC = 0u;
    static NativeGameFunction cached_function_802614AC = nullptr;
    call_guest_cached(
        services,
        context->ctr & 0xFFFFFFFCu,
        &cached_target_802614AC,
        &cached_function_802614AC,
        context,
        memory,
        0x802614ACu);
}

GALAXY_ALWAYS_INLINE void native_ptmf_wrapper_80261B40(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t frame = context->gpr[1] - 0x10u;
    guest_store_u32(memory, frame, context->gpr[1], services, guest_pc);
    context->gpr[1] = frame;
    context->gpr[0] = context->lr;
    context->gpr[5] = context->gpr[3];
    context->gpr[3] = context->gpr[4];
    guest_store_u32(memory, frame + 0x14u, context->gpr[0], services, guest_pc + 0x10u);
    context->gpr[12] = context->gpr[5] + 0x04u;
    context->lr = 0x80261B5Cu;
    native_ptmf_scall_805173E0(context, memory, services, 0x805173E0u);
    context->gpr[0] =
        guest_load_u32(memory, frame + 0x14u, services, guest_pc + 0x20u);
    context->lr = context->gpr[0];
    context->gpr[1] = frame + 0x10u;
}

GALAXY_ALWAYS_INLINE void native_ptmf_wrapper_80261B70(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t frame = context->gpr[1] - 0x10u;
    guest_store_u32(memory, frame, context->gpr[1], services, guest_pc);
    context->gpr[1] = frame;
    context->gpr[0] = context->lr;
    context->gpr[5] = context->gpr[3];
    context->gpr[3] = context->gpr[4];
    guest_store_u32(memory, frame + 0x14u, context->gpr[0], services, guest_pc + 0x10u);
    context->gpr[12] = context->gpr[5] + 0x04u;
    context->lr = 0x80261B8Cu;
    native_ptmf_scall_805173E0(context, memory, services, 0x805173E0u);
    context->gpr[0] =
        guest_load_u32(memory, frame + 0x14u, services, guest_pc + 0x20u);
    context->lr = context->gpr[0];
    context->gpr[1] = frame + 0x10u;
}

GALAXY_ALWAYS_INLINE void native_ptmf_wrapper_800C9078(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t frame = context->gpr[1] - 0x10u;
    guest_store_u32(memory, frame, context->gpr[1], services, guest_pc);
    context->gpr[1] = frame;
    context->gpr[0] = context->lr;
    context->gpr[4] = context->gpr[3];
    context->gpr[3] =
        guest_load_u32(memory, context->gpr[3] + 0x04u, services, guest_pc + 0x0Cu);
    guest_store_u32(memory, frame + 0x14u, context->gpr[0], services, guest_pc + 0x10u);
    context->gpr[12] = context->gpr[4] + 0x08u;
    context->lr = 0x800C9094u;
    native_ptmf_scall_805173E0(context, memory, services, 0x805173E0u);
    context->gpr[0] =
        guest_load_u32(memory, frame + 0x14u, services, guest_pc + 0x20u);
    context->lr = context->gpr[0];
    context->gpr[1] = frame + 0x10u;
}

GALAXY_ALWAYS_INLINE void native_ptmf_wrapper_800C9AAC(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t frame = context->gpr[1] - 0x10u;
    guest_store_u32(memory, frame, context->gpr[1], services, guest_pc);
    context->gpr[1] = frame;
    context->gpr[0] = context->lr;
    context->gpr[4] = context->gpr[3];
    context->gpr[3] =
        guest_load_u32(memory, context->gpr[3] + 0x04u, services, guest_pc + 0x0Cu);
    guest_store_u32(memory, frame + 0x14u, context->gpr[0], services, guest_pc + 0x10u);
    context->gpr[12] = context->gpr[4] + 0x08u;
    context->lr = 0x800C9AC8u;
    native_ptmf_scall_805173E0(context, memory, services, 0x805173E0u);
    context->gpr[0] =
        guest_load_u32(memory, frame + 0x14u, services, guest_pc + 0x20u);
    context->lr = context->gpr[0];
    context->gpr[1] = frame + 0x10u;
}

inline void native_jpa_emitter_callback_8044592C(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    record_jpa_counter(jpa_profile_counters().emitter_calls);
    const std::byte* owner_slot =
        resolve_guest(memory, context->gpr[3], 4, services, guest_pc);
    const std::uint32_t callback_owner =
        native_load_guest_u32_fast(owner_slot, 0u);
    const std::uint32_t particle = context->gpr[4];
    const std::byte* owner =
        resolve_guest(memory, callback_owner + 0xF0u, 4, services, 0x80445934u);
    const std::uint32_t callback =
        native_load_guest_u32_fast(owner, 0u);

    context->gpr[6] = callback_owner;
    context->gpr[5] = particle;
    context->gpr[3] = callback;
    const std::uint32_t cr0 =
        callback == 0u ? 0x2u : ((callback & 0x80000000u) != 0u ? 0x8u : 0x4u);
    set_cr_field(
        context,
        0u,
        cr0 | (((context->xer & 0x80000000u) != 0u) ? 0x1u : 0u));
    if (callback == 0u) {
        return;
    }
    record_jpa_counter(jpa_profile_counters().emitter_nonnull_callbacks);

    const std::byte* callback_object =
        resolve_guest(memory, callback, 4, services, 0x80445940u);
    const std::uint32_t vtable =
        native_load_guest_u32_fast(callback_object, 0u);
    const std::byte* vtable_slot =
        resolve_guest(memory, vtable + 0x10u, 4, services, 0x80445948u);
    const std::uint32_t target =
        native_load_guest_u32_fast(vtable_slot, 0u);
    record_jpa_target(4u, target);
    context->gpr[12] = target;
    context->gpr[4] = callback_owner;
    context->ctr = target;

    static std::uint32_t cached_target_80445950 = 0u;
    static NativeGameFunction cached_function_80445950 = nullptr;
    call_guest_cached(
        services,
        target & 0xFFFFFFFCu,
        &cached_target_80445950,
        &cached_function_80445950,
        context,
        memory,
        0x80445950u);
}

inline void native_jpa_calc_child_func_list_80443904(
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    record_jpa_counter(jpa_profile_counters().child_list_calls);
    const std::uint32_t frame = context->gpr[1] + 0xFFFFFFE0u;
    guest_store_u32(memory, frame, context->gpr[1], services, guest_pc);
    context->gpr[1] = frame;
    context->gpr[0] = context->lr;
    guest_store_u32(memory, context->gpr[1] + 0x24u, context->gpr[0], services, 0x8044390Cu);
    context->gpr[11] = context->gpr[1] + 0x20u;
    context->lr = 0x80443918u;
    guest_store_u32(memory, context->gpr[11] + 0xFFFFFFECu, context->gpr[27], services, 0x80517530u);
    guest_store_u32(memory, context->gpr[11] + 0xFFFFFFF0u, context->gpr[28], services, 0x80517534u);
    guest_store_u32(memory, context->gpr[11] + 0xFFFFFFF4u, context->gpr[29], services, 0x80517538u);
    guest_store_u32(memory, context->gpr[11] + 0xFFFFFFF8u, context->gpr[30], services, 0x8051753Cu);
    guest_store_u32(memory, context->gpr[11] + 0xFFFFFFFCu, context->gpr[31], services, 0x80517540u);

    const std::uint32_t func_list =
        guest_load_u32(memory, context->gpr[3] + 0x14u, services, 0x80443918u);
    context->gpr[0] = func_list;
    context->gpr[27] = context->gpr[3];
    context->gpr[28] = context->gpr[4];
    context->gpr[29] = context->gpr[5];
    compare_signed(
        context,
        0u,
        static_cast<std::int32_t>(func_list),
        static_cast<std::int32_t>(0));
    if (!cr_bit(context, 2u)) {
        context->gpr[3] =
            guest_load_u8(memory, context->gpr[27] + 0x46u, services, 0x80443930u);
        context->gpr[30] = context->gpr[3] + 0xFFFFFFFFu;
        context->gpr[31] = std::rotl(context->gpr[30], 2) & 0xFFFFFFFCu;

        static std::uint32_t cached_target_80443954 = 0u;
        static NativeGameFunction cached_function_80443954 = nullptr;
        for (;;) {
            compare_signed(
                context,
                0u,
                static_cast<std::int32_t>(context->gpr[30]),
                static_cast<std::int32_t>(0));
            if (cr_bit(context, 0u)) {
                break;
            }

            branch_checkpoint_taken(
                services, 0x80443964u, 0x80443940u, context, memory);
            context->gpr[5] =
                guest_load_u32(memory, context->gpr[27] + 0x14u, services, 0x80443940u);
            context->gpr[3] = context->gpr[28];
            context->gpr[4] = context->gpr[29];
            context->gpr[12] =
                guest_load_u32(memory, context->gpr[5] + context->gpr[31], services, 0x8044394Cu);
            context->ctr = context->gpr[12];
            context->lr = 0x80443958u;
            record_jpa_counter(jpa_profile_counters().child_callbacks);
            record_jpa_target(5u, context->ctr);
            switch (context->ctr & 0xFFFFFFFCu) {
            case 0x804465A0u:
                native_jpa_alpha_callback_804465A0(
                    context, memory, services, 0x804465A0u);
                break;
            case 0x804465CCu:
                native_jpa_scale_callback_804465CC(
                    context, memory, services, 0x804465CCu);
                break;
            default:
                call_guest_cached(
                    services,
                    context->ctr & 0xFFFFFFFCu,
                    &cached_target_80443954,
                    &cached_function_80443954,
                    context,
                    memory,
                    0x80443954u);
                break;
            }
            context->gpr[30] = context->gpr[30] + 0xFFFFFFFFu;
            context->gpr[31] = context->gpr[31] + 0xFFFFFFFCu;
        }
    }

    context->gpr[11] = context->gpr[1] + 0x20u;
    context->lr = 0x80443970u;
    context->gpr[27] =
        guest_load_u32(memory, context->gpr[11] + 0xFFFFFFECu, services, 0x8051757Cu);
    context->gpr[28] =
        guest_load_u32(memory, context->gpr[11] + 0xFFFFFFF0u, services, 0x80517580u);
    context->gpr[29] =
        guest_load_u32(memory, context->gpr[11] + 0xFFFFFFF4u, services, 0x80517584u);
    context->gpr[30] =
        guest_load_u32(memory, context->gpr[11] + 0xFFFFFFF8u, services, 0x80517588u);
    context->gpr[31] =
        guest_load_u32(memory, context->gpr[11] + 0xFFFFFFFCu, services, 0x8051758Cu);
    context->gpr[0] =
        guest_load_u32(memory, context->gpr[1] + 0x24u, services, 0x80443970u);
    context->lr = context->gpr[0];
    context->gpr[1] = context->gpr[1] + 0x20u;
}

inline void native_jpa_draw_particle_list_callback(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    NativeGameFunction* cached_function,
    PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t caller_pc) {
    const std::uint32_t target = guest_address & 0xFFFFFFFCu;
    switch (target) {
    case 0x803A3B6Cu:
        native_jpa_draw_particle_803A3B6C(
            context, memory, services, 0x803A3B6Cu);
        return;
    default:
        call_guest_cached(
            services,
            target,
            cached_address,
            cached_function,
            context,
            memory,
            caller_pc);
        return;
    }
}

inline void native_jpa_draw_particle_list_callback_804433BC(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    NativeGameFunction* cached_function,
    PpcContext* context,
    GuestMemoryV1* memory) {
    native_jpa_draw_particle_list_callback(
        services,
        guest_address,
        cached_address,
        cached_function,
        context,
        memory,
        0x804433BCu);
}

inline void native_jpa_draw_particle_list_callback_80443420(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    NativeGameFunction* cached_function,
    PpcContext* context,
    GuestMemoryV1* memory) {
    native_jpa_draw_particle_list_callback(
        services,
        guest_address,
        cached_address,
        cached_function,
        context,
        memory,
        0x80443420u);
}

struct GameModuleManifestV1 {
    std::uint32_t struct_size{sizeof(GameModuleManifestV1)};
    std::uint32_t abi_version{kNativeAbiVersion};
    char game_id[7]{};
    char main_dol_sha1[41]{};
    std::uint32_t guest_entry_point{};
    std::uint32_t translated_function_count{};
    std::uint64_t translated_instruction_count{};
};

// Manifest for an install-time static RSO sidecar. The runtime verifies this
// identity before it will route a loader-produced MEM2 PC to the native body.
// It is not a runtime RSO loader: neither RSO relocation records nor PPC bytes
// cross this API boundary.
struct NativeRsoModuleManifestV1 {
    std::uint32_t struct_size{sizeof(NativeRsoModuleManifestV1)};
    std::uint32_t abi_version{kNativeRsoModuleAbiVersion};
    std::uint32_t native_abi_version{kNativeAbiVersion};
    char game_id[7]{};
    char main_dol_sha1[41]{};
    char rso_sha1[41]{};
    std::uint32_t section_count{};
    std::uint32_t code_section{};
    std::uint32_t code_file_offset{};
    std::uint32_t code_size{};
    std::uint32_t entry_count{};
    std::uint32_t symbolic_immediate_count{};
};

using ModuleManifestFn = const GameModuleManifestV1* (*)();
using ModuleInitFn = bool (*)(const NativeServicesV1* services);
using ModuleEntryFn = void (*)(PpcContext* context, GuestMemoryV1* memory);
using ModuleLookupFn = NativeGameFunction (*)(std::uint32_t guest_address);
using NativeRsoModuleManifestFn = const NativeRsoModuleManifestV1* (*)();
using NativeRsoTryCallFn = bool (*) (
    std::uint32_t guest_address,
    std::uint32_t rso_base,
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services);
using NativeRsoResumeFn = bool (*) (
    std::uint32_t guest_address,
    std::uint32_t rso_base,
    PpcContext* context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services);

static_assert(std::is_standard_layout_v<PpcContext>);
static_assert(std::is_standard_layout_v<NativeServicesV1>);
static_assert(std::is_standard_layout_v<GameModuleManifestV1>);
static_assert(std::is_standard_layout_v<NativeRsoModuleManifestV1>);

}  // namespace galaxy

extern "C" {

GALAXY_MODULE_EXPORT const galaxy::GameModuleManifestV1* galaxy_module_manifest();
GALAXY_MODULE_EXPORT bool galaxy_module_init(const galaxy::NativeServicesV1* services);
GALAXY_MODULE_EXPORT void galaxy_module_entry(
    galaxy::PpcContext* context,
    galaxy::GuestMemoryV1* memory);
GALAXY_MODULE_EXPORT galaxy::NativeGameFunction galaxy_lookup_function(
    std::uint32_t guest_address);

}

// Shared guarded math after the complete ABI and guest helper declarations.
#include "galaxy/ppc_float_native_inline.h"
