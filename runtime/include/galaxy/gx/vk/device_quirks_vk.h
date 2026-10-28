// SPDX-License-Identifier: GPL-3.0-only
// Which GPU and driver the Vulkan backend runs on, and what that driver is known to get wrong (docs/VULKAN.md,
// "Driver compatibility"). The known-bug table is the part of Dolphin's DriverDetails, ANGLE's vk_renderer features and
// Dawn's Vulkan toggles that touches what Nebula uses; each entry in device_quirks_vk.cpp names its source.
// PipelineCacheVk's self-test adds what this device's driver refuses (PipelineCompat), kept per driver.
#pragma once

#include <volk.h>

#include <array>
#include <cstdint>
#include <string>

namespace galaxy::gx::vk {

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
    // Specialised TEV stages take a zero colour input A or B from a constant instead of a literal (Qualcomm compilers
    // fold the literal wrongly).
    bool tev_zero_from_constant = false;
    // EFB clears drawn instead of vkCmdClearAttachments (which races with draws on older Qualcomm drivers).
    bool clear_with_draw = false;
    // Readback buffers in host-coherent memory (cached memory reads back slower on Mali and Adreno).
    bool coherent_readback = false;
    // Readbacks copy an optimal-tiled image into a linear image first (Qualcomm's driver makes a temporary image for
    // every optimal image-to-buffer copy, slowly).
    bool linear_readback = false;
    // Render passes clear by command, not by load op (PowerVR's load-op clears are broken).
    bool clear_by_command = false;
    // No tessellation, so no curved surfaces (NVIDIA's Vulkan compiler crashes building their pipelines).
    bool no_tessellation = false;
};

[[nodiscard]] DeviceIdentity identify(VkPhysicalDevice physical, bool has_driver_properties);
[[nodiscard]] DeviceQuirks quirks_for(const DeviceIdentity& identity);
[[nodiscard]] const char* driver_kind_name(DriverKind kind) noexcept;
// "Adreno (TM) 840, Qualcomm proprietary 512.842.19 (Driver Build: ...), Vulkan 1.4"
[[nodiscard]] std::string describe(const DeviceIdentity& identity);
[[nodiscard]] std::string describe(const DeviceQuirks& quirks);
// What a pipeline-cache file is valid for: the device, its driver build and the driver's cache UUID.
[[nodiscard]] std::string driver_key(const DeviceIdentity& identity);

}  // namespace galaxy::gx::vk
