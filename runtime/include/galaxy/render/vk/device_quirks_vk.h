// SPDX-License-Identifier: GPL-3.0-only
// Which GPU and driver the Vulkan backend runs on, and what that driver is known to get wrong. The known-bug table is the part of known driver tables, ANGLE's vk_renderer features and
// Dawn's Vulkan toggles that touches what Nebula uses; each entry in device_quirks_vk.cpp names its source.
// PipelineCacheVk's self-test adds what this device's driver refuses (PipelineCompat), kept per driver.
#pragma once

#include <volk.h>

#include <array>
#include <cstdint>
#include <string>

namespace galaxy::render::vk {

enum class DriverKind : std::uint8_t {
    Unknown,
    QualcommProprietary,
    ArmProprietary,      // Mali, and Huawei's Maleoon (which reports ARM's driver)
    ImaginationProprietary,
    SamsungProprietary,  // Xclipse
    MesaTurnip,
    MesaPanVK,
    MesaOther,
    SwiftShader,
    NvidiaProprietary,
    AmdProprietary,
    IntelProprietary,
    MoltenVK,
    Other,
};

struct DriverVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    [[nodiscard]] bool at_least(std::uint32_t a, std::uint32_t b, std::uint32_t c) const noexcept {
        return major != a ? major > a : minor != b ? minor > b : patch >= c;
    }
};

struct DeviceIdentity {
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t api_version = 0;
    std::uint32_t raw_driver_version = 0;
    DriverKind driver = DriverKind::Unknown;
    DriverVersion driver_version;
    std::string device_name;
    std::string driver_name;  // VK_KHR_driver_properties, where the driver has it
    std::string driver_info;
    std::array<std::uint8_t, VK_UUID_SIZE> pipeline_cache_uuid{};
};

// Workarounds the table turns on. Everything Nebula needs from the device beyond these is a capability the device
// reports (features, extensions, formats): the context enables only what the device has.
struct DeviceQuirks {
    // scene target clears drawn instead of vkCmdClearAttachments (which races with draws on older Qualcomm drivers).
    bool clear_with_draw = false;
    // Readback buffers in host-coherent memory (cached memory reads back slower on Mali and Adreno).
    bool coherent_readback = false;
    // Render passes clear by command, not by load op (PowerVR's load-op clears are broken).
    bool clear_by_command = false;
    // Scene depth D24S8, not D32F with stencil (Adreno's 32-bit depth clears have no effect).
    bool depth_d24 = false;
    // No pipeline cache file is read or written (MoltenVK compiles every shader of a cache it is given as it reads it,
    // on the thread that made the cache; Metal keeps what it compiled itself).
    bool no_cache_file = false;
    // The device is not asked for VK_KHR_present_id and VK_KHR_present_wait (frames are bounded by their fences): NVIDIA's own
    // driver outside Windows. Users report programs frozen inside vkQueuePresentKHR on its Linux versions 570 to 590 (NVIDIA's
    // developer forum, threads on the 570.123 to 590.48 drivers; NVIDIA named no cause), and no version is known to be free of it
    // that a tester has run both ways. Bound it by driver version, or lift it, once a tester with such a card has (ledger).
    bool no_present_wait = false;
};

[[nodiscard]] DeviceIdentity identify(VkPhysicalDevice physical, bool has_driver_properties);
[[nodiscard]] DeviceQuirks quirks_for(const DeviceIdentity& identity);
// The same for a system given (the tests run both): `windows` is whether the program runs on Windows.
[[nodiscard]] DeviceQuirks quirks_for(const DeviceIdentity& identity, bool windows);
[[nodiscard]] const char* driver_kind_name(DriverKind kind) noexcept;
// "Adreno (TM) 840, Qualcomm proprietary 512.842.19 (Driver Build: ...), Vulkan 1.4"
[[nodiscard]] std::string describe(const DeviceIdentity& identity);
[[nodiscard]] std::string describe(const DeviceQuirks& quirks);
// What a pipeline-cache file is valid for: the device, its driver build and the driver's cache UUID.
[[nodiscard]] std::string driver_key(const DeviceIdentity& identity);
// The Vulkan version the memory allocator (VMA) is told: the lower of the instance's and the device's, major and minor
// only, from 1.0 to 1.3 (the newest Nebula asks an instance for). VMA takes newer core paths by it (1.3's
// vkGetDeviceBufferMemoryRequirements, 1.2's core buffer addresses), so it is never higher than both have.
[[nodiscard]] std::uint32_t allocator_api_version(std::uint32_t instance_version, std::uint32_t device_version) noexcept;

}  // namespace galaxy::render::vk
