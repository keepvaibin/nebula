// SPDX-License-Identifier: GPL-3.0-only
// The Vulkan driver key (device_quirks_vk.h), which names the Vulkan pipeline cache file: one file per device, driver
// build and pipeline cache UUID.
#include "galaxy/render/vk/device_quirks_vk.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>

namespace {

using galaxy::render::vk::DeviceIdentity;
using galaxy::render::vk::DriverKind;
using galaxy::render::vk::driver_key;
using galaxy::render::vk::quirks_for;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

DeviceIdentity identity(std::uint32_t vendor, std::uint32_t device, std::uint32_t driver, std::uint32_t uuid_seed) {
    DeviceIdentity id;
    id.vendor_id = vendor;
    id.device_id = device;
    id.raw_driver_version = driver;
    for (std::size_t i = 0u; i < id.pipeline_cache_uuid.size(); ++i) {
        id.pipeline_cache_uuid[i] = static_cast<std::uint8_t>(uuid_seed + static_cast<std::uint32_t>(i));
    }
    return id;
}

bool the_key_holds_every_part() {
    const std::string key = driver_key(identity(0x10DEu, 0x2684u, 0x1234u, 1u));
    return expect(key == "10de-00002684-00001234-0102030405060708090a0b0c0d0e0f10", "vendor, device, driver build and cache UUID");
}

bool different_devices_and_drivers_have_different_keys() {
    bool ok = true;
    std::set<std::string> keys;
    for (const std::uint32_t vendor : {0x10DEu, 0x5143u}) {
        for (const std::uint32_t device : {0x1u, 0x2u}) {
            for (const std::uint32_t driver : {10u, 11u}) {
                for (const std::uint32_t seed : {1u, 2u}) {
                    keys.insert(driver_key(identity(vendor, device, driver, seed)));
                }
            }
        }
    }
    ok &= expect(keys.size() == 16u, "each vendor, device, driver build and UUID has its own key");
    ok &= expect(driver_key(identity(1u, 2u, 3u, 4u)) == driver_key(identity(1u, 2u, 3u, 4u)), "the same device has the same key");
    return ok;
}

bool the_key_is_safe_in_a_file_name() {
    const std::string key = driver_key(identity(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xF0u));
    return expect(key.find_first_not_of("0123456789abcdef-") == std::string::npos, "lower-case hex and dashes only");
}

}  // namespace

// The workaround table (quirks_for) by driver and version: each mobile driver's, and none on desktop drivers.
bool quirks_follow_the_driver() {
    const auto with = [](DriverKind kind, std::uint32_t major, std::uint32_t minor, std::uint32_t patch) {
        DeviceIdentity id;
        id.driver = kind;
        id.driver_version = {major, minor, patch};
        return quirks_for(id);
    };
    bool ok = true;
    const auto old_adreno = with(DriverKind::QualcommProprietary, 512u, 700u, 0u);
    ok &= expect(old_adreno.clear_with_draw && old_adreno.coherent_readback && old_adreno.depth_d24 &&
                     !old_adreno.clear_by_command,
                 "Qualcomm before 512.762.12: drawn clears, coherent readback, D24S8");
    const auto new_adreno = with(DriverKind::QualcommProprietary, 512u, 762u, 12u);
    ok &= expect(!new_adreno.clear_with_draw && new_adreno.depth_d24, "Qualcomm 512.762.12: vkCmdClearAttachments again");
    const auto mali = with(DriverKind::ArmProprietary, 47u, 0u, 0u);
    ok &= expect(mali.coherent_readback && !mali.clear_with_draw && !mali.depth_d24, "ARM: coherent readback only");
    const auto powervr = with(DriverKind::ImaginationProprietary, 1u, 0u, 0u);
    ok &= expect(powervr.clear_by_command && !powervr.coherent_readback, "Imagination: clears by command");
    for (const DriverKind desktop : {DriverKind::NvidiaProprietary, DriverKind::AmdProprietary, DriverKind::IntelProprietary,
                                     DriverKind::MesaTurnip, DriverKind::SwiftShader}) {
        const auto q = with(desktop, 1u, 0u, 0u);
        ok &= expect(!q.clear_with_draw && !q.coherent_readback && !q.clear_by_command && !q.depth_d24,
                     "no workaround on another driver");
    }
    return ok;
}

// NVIDIA's own driver outside Windows is not asked for present wait (frozen presents reported on its Linux versions): the
// table says so there, for every version, and on Windows, and for every other driver, it does not.
bool nvidia_on_linux_has_no_present_wait() {
    const auto with = [](DriverKind kind, std::uint32_t major, bool windows) {
        DeviceIdentity id;
        id.driver = kind;
        id.driver_version = {major, 0u, 0u};
        return quirks_for(id, windows);
    };
    bool ok = true;
    for (const std::uint32_t version : {535u, 570u, 580u, 590u, 595u, 610u}) {
        ok &= expect(with(DriverKind::NvidiaProprietary, version, false).no_present_wait, "NVIDIA on Linux, any version: no present wait");
        ok &= expect(!with(DriverKind::NvidiaProprietary, version, true).no_present_wait, "NVIDIA on Windows keeps present wait");
    }
    for (const DriverKind other : {DriverKind::AmdProprietary, DriverKind::MesaOther, DriverKind::IntelProprietary,
                                   DriverKind::QualcommProprietary, DriverKind::MoltenVK, DriverKind::SwiftShader}) {
        ok &= expect(!with(other, 590u, false).no_present_wait && !with(other, 590u, true).no_present_wait,
                     "every other driver keeps present wait, on either system");
    }
    ok &= expect(galaxy::render::vk::describe(with(DriverKind::NvidiaProprietary, 590u, false)).find("no-present-wait") != std::string::npos,
                 "the log names the workaround");
    return ok;
}

// VMA is told the version both the instance and the device have (P-058): 1.1 before, whatever they were.
bool the_allocator_gets_the_version_in_use() {
    using galaxy::render::vk::allocator_api_version;
    bool ok = true;
    ok &= expect(allocator_api_version(VK_API_VERSION_1_3, VK_MAKE_API_VERSION(0, 1, 3, 280)) == VK_API_VERSION_1_3,
                 "a 1.3 instance and device: 1.3");
    ok &= expect(allocator_api_version(VK_API_VERSION_1_2, VK_MAKE_API_VERSION(0, 1, 3, 250)) == VK_API_VERSION_1_2,
                 "a 1.2 instance on a 1.3 device: 1.2");
    ok &= expect(allocator_api_version(VK_API_VERSION_1_3, VK_MAKE_API_VERSION(0, 1, 1, 128)) == VK_API_VERSION_1_1,
                 "a 1.1 device: 1.1");
    ok &= expect(allocator_api_version(VK_API_VERSION_1_3, VK_MAKE_API_VERSION(0, 1, 0, 61)) == VK_API_VERSION_1_0,
                 "a 1.0 device: 1.0");
    ok &= expect(allocator_api_version(VK_MAKE_API_VERSION(0, 1, 4, 0), VK_MAKE_API_VERSION(0, 1, 4, 309)) ==
                     VK_API_VERSION_1_3,
                 "1.4 on both: 1.3, the newest asked for");
    return ok;
}

int main() {
    bool ok = true;
    ok &= the_allocator_gets_the_version_in_use();
    ok &= quirks_follow_the_driver();
    ok &= nvidia_on_linux_has_no_present_wait();
    ok &= the_key_holds_every_part();
    ok &= different_devices_and_drivers_have_different_keys();
    ok &= the_key_is_safe_in_a_file_name();
    if (!ok) {
        return 1;
    }
    std::cout << "vulkan driver key tests passed\n";
    return 0;
}
