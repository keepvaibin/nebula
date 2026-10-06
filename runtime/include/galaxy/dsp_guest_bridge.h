#pragma once

#include "galaxy/dsp_context.h"
#include "galaxy/native_api.h"

#include <cstdlib>
#include <cstdint>

namespace galaxy {

struct DspGuestMemoryBridge {
    GuestMemoryV1* memory{};
    const NativeServicesV1* services{};
    std::uint32_t guest_pc{};
    void* user{};
    void (*request_interrupt)(void* user){};
    void (*accelerator_exception)(void* user, DspAcceleratorException exception){};
};

[[noreturn]] inline void dsp_guest_bridge_fatal(
    const DspGuestMemoryBridge* bridge,
    const char* message) {
    if (bridge != nullptr &&
        bridge->services != nullptr &&
        bridge->services->fatal != nullptr) {
        bridge->services->fatal(
            bridge->services->user,
            bridge->guest_pc,
            message);
    }
    std::abort();
}

inline DspGuestMemoryBridge& dsp_guest_bridge_from_user(void* user) {
    if (user == nullptr) {
        dsp_guest_bridge_fatal(nullptr, "DSP guest-memory bridge is not connected");
    }
    return *static_cast<DspGuestMemoryBridge*>(user);
}

inline bool dsp_guest_bridge_validate_span(
    void* user,
    std::uint32_t address,
    std::uint32_t size) {
    if (user == nullptr) {
        return false;
    }
    auto& bridge = *static_cast<DspGuestMemoryBridge*>(user);
    if (bridge.memory == nullptr || !dsp_u32_span_is_valid(address, size)) {
        return false;
    }
    if (size == 0u ||
        resolve_guest_fast(bridge.memory, address, size) != nullptr) {
        return true;
    }
    if (bridge.memory->regions == nullptr) {
        return false;
    }
    const std::uint64_t request_end =
        static_cast<std::uint64_t>(address) + size;
    for (std::uint32_t index = 0; index < bridge.memory->region_count; ++index) {
        const GuestMemoryRegionV1& region = bridge.memory->regions[index];
        const std::uint64_t region_end =
            static_cast<std::uint64_t>(region.guest_base) + region.size;
        if (address >= region.guest_base && request_end <= region_end &&
            region.host_base != nullptr) {
            return true;
        }
    }
    return false;
}

inline bool dsp_guest_bridge_read_byte(
    void* user,
    std::uint32_t address,
    std::uint8_t* value) {
    if (value == nullptr ||
        !dsp_guest_bridge_validate_span(user, address, 1u)) {
        return false;
    }
    auto& bridge = *static_cast<DspGuestMemoryBridge*>(user);
    *value = guest_load_u8(
        bridge.memory,
        address,
        bridge.services,
        bridge.guest_pc);
    return true;
}

inline bool dsp_guest_bridge_write_byte(
    void* user,
    std::uint32_t address,
    std::uint8_t value) {
    if (!dsp_guest_bridge_validate_span(user, address, 1u)) {
        return false;
    }
    auto& bridge = *static_cast<DspGuestMemoryBridge*>(user);
    guest_store_u8(
        bridge.memory,
        address,
        value,
        bridge.services,
        bridge.guest_pc);
    return true;
}

inline void dsp_guest_bridge_request_interrupt(void* user) {
    auto& bridge = dsp_guest_bridge_from_user(user);
    if (bridge.request_interrupt == nullptr) {
        dsp_guest_bridge_fatal(&bridge, "DSP interrupt requested without host handler");
    }
    bridge.request_interrupt(bridge.user);
}

inline void dsp_guest_bridge_accelerator_exception(
    void* user,
    DspAcceleratorException exception) {
    auto& bridge = dsp_guest_bridge_from_user(user);
    if (bridge.accelerator_exception == nullptr) {
        dsp_guest_bridge_fatal(&bridge, "DSP accelerator exception without host handler");
    }
    bridge.accelerator_exception(bridge.user, exception);
}

inline void dsp_attach_guest_memory_bridge(
    DspContext& context,
    DspGuestMemoryBridge& bridge) {
    context.hardware.request_interrupt = dsp_guest_bridge_request_interrupt;
    context.hardware.validate_external_span = dsp_guest_bridge_validate_span;
    context.hardware.external_read_byte = dsp_guest_bridge_read_byte;
    context.hardware.external_write_byte = dsp_guest_bridge_write_byte;
    // Without a host-supplied ARAM router, both buses use the same flat guest
    // memory.
    context.hardware.validate_aram_span = dsp_guest_bridge_validate_span;
    context.hardware.aram_read_byte = dsp_guest_bridge_read_byte;
    context.hardware.aram_write_byte = dsp_guest_bridge_write_byte;
    context.hardware.accelerator_exception = dsp_guest_bridge_accelerator_exception;
    context.hardware_user = &bridge;
}

}  // namespace galaxy
