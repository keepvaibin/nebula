// SPDX-License-Identifier: GPL-3.0-only
// The Vulkan driver identification and known-bug table (device_quirks_vk.h).
#include "galaxy/gx/vk/device_quirks_vk.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace galaxy::gx::vk {

namespace {

constexpr std::uint32_t kVendorQualcomm = 0x5143;
constexpr std::uint32_t kVendorArm = 0x13B5;
constexpr std::uint32_t kVendorImagination = 0x1010;
constexpr std::uint32_t kVendorSamsung = 0x144D;
constexpr std::uint32_t kVendorHuawei = 0x19E5;
constexpr std::uint32_t kVendorNvidia = 0x10DE;
constexpr std::uint32_t kVendorAmd = 0x1002;
constexpr std::uint32_t kVendorIntel = 0x8086;
constexpr std::uint32_t kVendorGoogle = 0x1AE0;

DriverKind kind_from_driver_id(VkDriverId id) {
    switch (id) {
    case VK_DRIVER_ID_QUALCOMM_PROPRIETARY: return DriverKind::QualcommProprietary;
    case VK_DRIVER_ID_ARM_PROPRIETARY: return DriverKind::ArmProprietary;
    case VK_DRIVER_ID_IMAGINATION_PROPRIETARY: return DriverKind::ImaginationProprietary;
    case VK_DRIVER_ID_SAMSUNG_PROPRIETARY: return DriverKind::SamsungProprietary;
    case VK_DRIVER_ID_MESA_TURNIP: return DriverKind::MesaTurnip;
    case VK_DRIVER_ID_MESA_PANVK: return DriverKind::MesaPanVK;
    case VK_DRIVER_ID_GOOGLE_SWIFTSHADER: return DriverKind::SwiftShader;
    case VK_DRIVER_ID_NVIDIA_PROPRIETARY: return DriverKind::NvidiaProprietary;
    case VK_DRIVER_ID_AMD_PROPRIETARY: return DriverKind::AmdProprietary;
    case VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS: return DriverKind::IntelProprietary;
    case VK_DRIVER_ID_MOLTENVK: return DriverKind::MoltenVK;
    case VK_DRIVER_ID_MESA_RADV:
    case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA:
    case VK_DRIVER_ID_MESA_LLVMPIPE:
    case VK_DRIVER_ID_MESA_V3DV:
    case VK_DRIVER_ID_MESA_VENUS:
    case VK_DRIVER_ID_MESA_DOZEN:
    case VK_DRIVER_ID_MESA_NVK:
    case VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA:
    case VK_DRIVER_ID_MESA_HONEYKRISP: return DriverKind::MesaOther;
    default: return DriverKind::Other;
    }
}

// A driver without VK_KHR_driver_properties (Vulkan 1.1 drivers before it existed) is its vendor's own.
DriverKind kind_from_vendor(std::uint32_t vendor) {
    switch (vendor) {
    case kVendorQualcomm: return DriverKind::QualcommProprietary;
    case kVendorArm:
    case kVendorHuawei: return DriverKind::ArmProprietary;
    case kVendorImagination: return DriverKind::ImaginationProprietary;
    case kVendorSamsung: return DriverKind::SamsungProprietary;
    case kVendorNvidia: return DriverKind::NvidiaProprietary;
    case kVendorAmd: return DriverKind::AmdProprietary;
    case kVendorIntel: return DriverKind::IntelProprietary;
    case kVendorGoogle: return DriverKind::SwiftShader;
    default: return DriverKind::Unknown;
    }
}

// Each vendor packs driverVersion its own way (the encodings gpuinfo.org and ANGLE decode).
DriverVersion decode(DriverKind kind, std::uint32_t raw) {
    switch (kind) {
    case DriverKind::NvidiaProprietary: return {raw >> 22u, (raw >> 14u) & 0xffu, (raw >> 6u) & 0xffu};
    case DriverKind::IntelProprietary: return {raw >> 14u, raw & 0x3fffu, 0u};
    default: return {raw >> 22u, (raw >> 12u) & 0x3ffu, raw & 0xfffu};
    }
}

// GALAXY_VK_SIMULATE_DRIVER=<kind>:<major>.<minor>.<patch> (kind: qualcomm, arm, imagination, samsung) presents this
// device as that driver to the table, so a desktop run exercises a mobile driver's workarounds.
void apply_simulation(DeviceIdentity& identity) {
    const char* value = std::getenv("GALAXY_VK_SIMULATE_DRIVER");
    if (value == nullptr || value[0] == '\0') {
        return;
    }
    static constexpr struct {
        const char* name;
        DriverKind kind;
        std::uint32_t vendor;
    } kKinds[] = {{"qualcomm", DriverKind::QualcommProprietary, kVendorQualcomm},
                  {"arm", DriverKind::ArmProprietary, kVendorArm},
                  {"imagination", DriverKind::ImaginationProprietary, kVendorImagination},
                  {"samsung", DriverKind::SamsungProprietary, kVendorSamsung}};
    for (const auto& entry : kKinds) {
        const std::size_t length = std::strlen(entry.name);
        if (std::strncmp(value, entry.name, length) == 0 && value[length] == ':') {
            unsigned major = 0, minor = 0, patch = 0;
            if (std::sscanf(value + length + 1, "%u.%u.%u", &major, &minor, &patch) >= 2) {
                identity.driver = entry.kind;
                identity.vendor_id = entry.vendor;
                identity.driver_version = {major, minor, patch};
                identity.driver_info = "simulated";
            }
            return;
        }
    }
}

}  // namespace

DeviceIdentity identify(VkPhysicalDevice physical, bool has_driver_properties) {
    DeviceIdentity identity;
    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    if (has_driver_properties) {
        properties.pNext = &driver;
    }
    vkGetPhysicalDeviceProperties2(physical, &properties);
    const VkPhysicalDeviceProperties& core = properties.properties;
    identity.vendor_id = core.vendorID;
    identity.device_id = core.deviceID;
    identity.api_version = core.apiVersion;
    identity.raw_driver_version = core.driverVersion;
    identity.device_name = core.deviceName;
    std::memcpy(identity.pipeline_cache_uuid.data(), core.pipelineCacheUUID, VK_UUID_SIZE);
    if (has_driver_properties && driver.driverID != 0) {
        identity.driver = kind_from_driver_id(driver.driverID);
        identity.driver_name = driver.driverName;
        identity.driver_info = driver.driverInfo;
    } else {
        identity.driver = kind_from_vendor(core.vendorID);
    }
    identity.driver_version = decode(identity.driver, core.driverVersion);
    apply_simulation(identity);
    return identity;
}

DeviceQuirks quirks_for(const DeviceIdentity& id) {
    DeviceQuirks quirks;
    const bool qualcomm = id.driver == DriverKind::QualcommProprietary;
    const bool arm = id.driver == DriverKind::ArmProprietary;
    // Dolphin DriverDetails BUG_BROKEN_TEV_COLOR_ZERO: Qualcomm's Android drivers from 512.700 (0x802bc000)
    // miscompile specialised TEV shaders whose colour input A or B is the constant zero.
    quirks.tev_zero_from_constant = qualcomm && id.driver_version.at_least(512, 700, 0);
    // ANGLE preferDrawClearOverVkCmdClearAttachments: vkCmdClearAttachments races with draws on Qualcomm drivers
    // before 512.762.12 (issuetracker.google.com/166809097).
    quirks.clear_with_draw = qualcomm && !id.driver_version.at_least(512, 762, 12);
    // Dolphin DriverDetails BUG_SLOW_CACHED_READBACK_MEMORY: Mali and Adreno read cached memory back slower than
    // coherent memory.
    quirks.coherent_readback = qualcomm || arm;
    // Dolphin DriverDetails BUG_SLOW_OPTIMAL_IMAGE_TO_BUFFER_COPY (VKStagingTexture): Qualcomm's driver allocates a
    // temporary image for each copy from an optimal-tiled image to a buffer; a reused linear image avoids it.
    quirks.linear_readback = qualcomm;
    // Dolphin DriverDetails BUG_BROKEN_CLEAR_LOADOP_RENDERPASS: PowerVR's render-pass load-op clears do not clear.
    quirks.clear_by_command = id.driver == DriverKind::ImaginationProprietary;
    // NVIDIA's Vulkan shader compiler (nvgpucomp64.dll, RTX 5090, Windows driver 610) crashes inside
    // vkCreateGraphicsPipelines building the curved surfaces' pipelines, whose SPIR-V validates.
    quirks.no_tessellation = id.driver == DriverKind::NvidiaProprietary;
    // Applied on every device, so not quirks: SPIR-V 1.3 (ANGLE supportsSPIRV14 and Dawn UseSpirv14: Qualcomm before
    // 512.829 crashes on SPIR-V 1.4's OpCopyLogical, Adreno 7xx too, ARM before r47 mishandles its OpSelect); no
    // maximal-reconvergence execution mode (Dawn use_spirv_reconvergence_mode, Dusklight issue 2563: Adreno 740's
    // compiler crashes); TEV byte masks one component at a time (Dolphin BUG_BROKEN_VECTOR_BITWISE_AND: Mali returns
    // the constant vector of a vector AND); D24S8 depth where the device has it (Dolphin BUG_BROKEN_D32F_CLEAR: D32F
    // clears do nothing on Adreno); viewport depth ranges never reversed (Dolphin BUG_BROKEN_REVERSED_DEPTH_RANGE).
    return quirks;
}

const char* driver_kind_name(DriverKind kind) noexcept {
    switch (kind) {
    case DriverKind::QualcommProprietary: return "Qualcomm proprietary";
    case DriverKind::ArmProprietary: return "ARM proprietary";
    case DriverKind::ImaginationProprietary: return "Imagination proprietary";
    case DriverKind::SamsungProprietary: return "Samsung proprietary";
    case DriverKind::MesaTurnip: return "Mesa Turnip";
    case DriverKind::MesaPanVK: return "Mesa PanVK";
    case DriverKind::MesaOther: return "Mesa";
    case DriverKind::SwiftShader: return "SwiftShader";
    case DriverKind::NvidiaProprietary: return "NVIDIA proprietary";
    case DriverKind::AmdProprietary: return "AMD proprietary";
    case DriverKind::IntelProprietary: return "Intel proprietary";
    case DriverKind::MoltenVK: return "MoltenVK";
    case DriverKind::Other: return "other";
    case DriverKind::Unknown: break;
    }
    return "unknown";
}

std::string describe(const DeviceIdentity& id) {
    char text[512];
    std::snprintf(text, sizeof(text), "%s (vendor 0x%04x device 0x%x), %s %u.%u.%u%s%s%s, Vulkan %u.%u.%u",
                  id.device_name.c_str(), id.vendor_id, id.device_id, driver_kind_name(id.driver),
                  id.driver_version.major, id.driver_version.minor, id.driver_version.patch,
                  id.driver_info.empty() ? "" : " (", id.driver_info.c_str(), id.driver_info.empty() ? "" : ")",
                  VK_API_VERSION_MAJOR(id.api_version), VK_API_VERSION_MINOR(id.api_version),
                  VK_API_VERSION_PATCH(id.api_version));
    return text;
}

std::string describe(const DeviceQuirks& quirks) {
    std::string text;
    const auto add = [&](bool on, const char* name) {
        if (on) {
            text += text.empty() ? "" : " ";
            text += name;
        }
    };
    add(quirks.tev_zero_from_constant, "tev-zero-from-constant");
    add(quirks.clear_with_draw, "clear-with-draw");
    add(quirks.coherent_readback, "coherent-readback");
    add(quirks.linear_readback, "linear-readback");
    add(quirks.clear_by_command, "clear-by-command");
    add(quirks.no_tessellation, "no-tessellation");
    return text.empty() ? "none" : text;
}

std::string driver_key(const DeviceIdentity& id) {
    std::string key;
    char part[64];
    std::snprintf(part, sizeof(part), "%04x-%08x-%08x-", id.vendor_id, id.device_id, id.raw_driver_version);
    key = part;
    for (const std::uint8_t byte : id.pipeline_cache_uuid) {
        std::snprintf(part, sizeof(part), "%02x", byte);
        key += part;
    }
    return key;
}

}  // namespace galaxy::gx::vk
