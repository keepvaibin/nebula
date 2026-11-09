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

int main() {
    bool ok = true;
    ok &= quirks_follow_the_driver();
    ok &= the_key_holds_every_part();
    ok &= different_devices_and_drivers_have_different_keys();
    ok &= the_key_is_safe_in_a_file_name();
    if (!ok) {
        return 1;
    }
    std::cout << "vulkan driver key tests passed\n";
    return 0;
}
