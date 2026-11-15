// SPDX-License-Identifier: GPL-3.0-only
// The pipeline cache files' headers (pipeline_cache_file.h): a good file is taken; a wrong revision, vendor, device, driver, UUID,
// size, hash or magic, a truncated file and a longer one are refused.
#include "render/core/pipeline_cache_file.h"
#include "render/core/pipeline_workers.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace galaxy::render::core;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++g_failures;
    }
}

std::vector<char> sample_data() {
    std::vector<char> data(1000u);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<char>((i * 31u + 7u) & 0xFFu);
    }
    return data;
}

VkCacheIdentity vk_identity() {
    VkCacheIdentity identity;
    identity.compiler_revision = 12u;
    identity.vendor_id = 0x10DEu;
    identity.device_id = 0x2684u;
    identity.driver_version = 0x8C1234u;
    for (std::size_t i = 0; i < kCacheUuidSize; ++i) {
        identity.uuid[i] = static_cast<std::uint8_t>(0xA0u + i);
    }
    return identity;
}

D3d12CacheIdentity d3d12_identity() {
    D3d12CacheIdentity identity;
    identity.known = true;
    identity.vendor_id = 0x1002u;
    identity.device_id = 0x744Cu;
    identity.revision = 0xC8u;
    identity.driver_version = 0x0000001F00170042ull;
    return identity;
}

void flip(std::vector<char>& file, std::size_t at) {
    file[at] = static_cast<char>(file[at] ^ 0x5A);
}

void vulkan_tests() {
    const std::vector<char> data = sample_data();
    const VkCacheIdentity identity = vk_identity();
    const std::vector<char> file = wrap_vk_cache(identity, data.data(), data.size());
    check(file.size() == sizeof(VkCacheHeader) + data.size(), "vk: the file is the header and the data");
    check(unwrap_vk_cache(identity, file) == data, "vk: a good file is taken");

    VkCacheIdentity other = identity;
    other.compiler_revision += 1u;
    check(unwrap_vk_cache(other, file).empty(), "vk: another compiler revision is refused");
    other = identity;
    other.vendor_id ^= 1u;
    check(unwrap_vk_cache(other, file).empty(), "vk: another vendor is refused");
    other = identity;
    other.device_id ^= 1u;
    check(unwrap_vk_cache(other, file).empty(), "vk: another device is refused");
    other = identity;
    other.driver_version += 1u;
    check(unwrap_vk_cache(other, file).empty(), "vk: another driver is refused");
    for (std::size_t i = 0; i < kCacheUuidSize; ++i) {
        other = identity;
        other.uuid[i] = static_cast<std::uint8_t>(other.uuid[i] ^ 1u);
        check(unwrap_vk_cache(other, file).empty(), "vk: another pipeline cache UUID (any byte) is refused");
    }

    // A header edited in the file: each field on its own.
    const auto field = [&](std::size_t offset, std::size_t bytes, const char* what) {
        for (std::size_t i = 0; i < bytes; ++i) {
            std::vector<char> bad = file;
            flip(bad, offset + i);
            check(unwrap_vk_cache(identity, bad).empty(), what);
        }
    };
    field(offsetof(VkCacheHeader, magic), sizeof(VkCacheHeader::magic), "vk: a wrong magic is refused");
    field(offsetof(VkCacheHeader, compiler_revision), 4u, "vk: a wrong revision in the file is refused");
    field(offsetof(VkCacheHeader, vendor_id), 4u, "vk: a wrong vendor in the file is refused");
    field(offsetof(VkCacheHeader, device_id), 4u, "vk: a wrong device in the file is refused");
    field(offsetof(VkCacheHeader, driver_version), 4u, "vk: a wrong driver in the file is refused");
    field(offsetof(VkCacheHeader, uuid), kCacheUuidSize, "vk: a wrong UUID in the file is refused");
    field(offsetof(VkCacheHeader, size), 8u, "vk: a wrong size in the header is refused");
    field(offsetof(VkCacheHeader, hash), 8u, "vk: a wrong hash in the header is refused");

    // The data changed under an intact header.
    for (const std::size_t at : {sizeof(VkCacheHeader), sizeof(VkCacheHeader) + data.size() / 2u, file.size() - 1u}) {
        std::vector<char> bad = file;
        flip(bad, at);
        check(unwrap_vk_cache(identity, bad).empty(), "vk: changed data (hash) is refused");
    }

    // Truncated files.
    for (const std::size_t keep : {std::size_t{0}, std::size_t{1}, sizeof(VkCacheHeader) - 1u, sizeof(VkCacheHeader),
                                   sizeof(VkCacheHeader) + 1u, file.size() / 2u, file.size() - 1u}) {
        const std::vector<char> cut(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(keep));
        check(unwrap_vk_cache(identity, cut).empty(), "vk: a truncated file is refused");
    }
    std::vector<char> longer = file;
    longer.push_back('x');
    check(unwrap_vk_cache(identity, longer).empty(), "vk: a longer file (size) is refused");
    check(unwrap_vk_cache(identity, {}).empty(), "vk: an empty file is refused");
}

void d3d12_tests() {
    const std::vector<char> data = sample_data();
    const D3d12CacheIdentity identity = d3d12_identity();
    const std::vector<char> file = wrap_d3d12_cache(identity, data.data(), data.size());
    check(file.size() == sizeof(D3d12CacheHeader) + data.size(), "d3d12: the file is the header and the data");
    check(unwrap_d3d12_cache(identity, file) == data, "d3d12: a good file is taken");

    D3d12CacheIdentity other = identity;
    other.vendor_id ^= 1u;
    check(unwrap_d3d12_cache(other, file).empty(), "d3d12: another vendor is refused");
    other = identity;
    other.device_id ^= 1u;
    check(unwrap_d3d12_cache(other, file).empty(), "d3d12: another device is refused");
    other = identity;
    other.revision += 1u;
    check(unwrap_d3d12_cache(other, file).empty(), "d3d12: another adapter revision is refused");
    other = identity;
    other.driver_version += 1u;
    check(unwrap_d3d12_cache(other, file).empty(), "d3d12: another driver is refused");
    other = identity;
    other.known = false;
    check(unwrap_d3d12_cache(other, file).empty(), "d3d12: an unidentified adapter does not take an identified one's file");

    const auto field = [&](std::size_t offset, std::size_t bytes, const char* what) {
        for (std::size_t i = 0; i < bytes; ++i) {
            std::vector<char> bad = file;
            flip(bad, offset + i);
            check(unwrap_d3d12_cache(identity, bad).empty(), what);
        }
    };
    field(offsetof(D3d12CacheHeader, magic), sizeof(D3d12CacheHeader::magic), "d3d12: a wrong magic (the format's revision) is refused");
    field(offsetof(D3d12CacheHeader, vendor_id), 4u, "d3d12: a wrong vendor in the file is refused");
    field(offsetof(D3d12CacheHeader, device_id), 4u, "d3d12: a wrong device in the file is refused");
    field(offsetof(D3d12CacheHeader, revision), 4u, "d3d12: a wrong adapter revision in the file is refused");
    field(offsetof(D3d12CacheHeader, driver_version), 8u, "d3d12: a wrong driver in the file is refused");
    field(offsetof(D3d12CacheHeader, size), 8u, "d3d12: a wrong size in the header is refused");
    field(offsetof(D3d12CacheHeader, hash), 8u, "d3d12: a wrong hash in the header is refused");

    for (const std::size_t at : {sizeof(D3d12CacheHeader), sizeof(D3d12CacheHeader) + data.size() / 2u, file.size() - 1u}) {
        std::vector<char> bad = file;
        flip(bad, at);
        check(unwrap_d3d12_cache(identity, bad).empty(), "d3d12: changed data (hash) is refused");
    }
    for (const std::size_t keep : {std::size_t{0}, std::size_t{1}, sizeof(D3d12CacheHeader) - 1u, sizeof(D3d12CacheHeader),
                                   sizeof(D3d12CacheHeader) + 1u, file.size() / 2u, file.size() - 1u}) {
        const std::vector<char> cut(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(keep));
        check(unwrap_d3d12_cache(identity, cut).empty(), "d3d12: a truncated file is refused");
    }
    std::vector<char> longer = file;
    longer.push_back('x');
    check(unwrap_d3d12_cache(identity, longer).empty(), "d3d12: a longer file (size) is refused");
    check(unwrap_d3d12_cache(identity, {}).empty(), "d3d12: an empty file is refused");

    // A Vulkan file is never a Direct3D 12 one, and the other way round (different magic).
    const VkCacheIdentity vk = vk_identity();
    check(unwrap_d3d12_cache(identity, wrap_vk_cache(vk, data.data(), data.size())).empty(), "a Vulkan file is refused by d3d12");
    check(unwrap_vk_cache(vk, wrap_d3d12_cache(identity, data.data(), data.size())).empty(), "a d3d12 file is refused by Vulkan");

    // The file's name follows the identity: two adapters do not share a file.
    const std::string name = d3d12_cache_file_name(identity);
    check(name == "d3d12_pipelines_1002_744c_c8_0000001f00170042.bin", "d3d12: the file name carries the identity");
    D3d12CacheIdentity changed = identity;
    changed.device_id += 1u;
    check(d3d12_cache_file_name(changed) != name, "d3d12: another device is another file");
    changed = identity;
    changed.driver_version += 1u;
    check(d3d12_cache_file_name(changed) != name, "d3d12: another driver is another file");
    check(d3d12_cache_file_name(D3d12CacheIdentity{}) == "d3d12_pipelines_unknown.bin", "d3d12: an unidentified adapter's file");
}

void hash_tests() {
    const std::vector<char> data = sample_data();
    const std::uint64_t hash = cache_bytes_hash(data.data(), data.size());
    check(hash == cache_bytes_hash(data.data(), data.size()), "the hash is repeatable");
    check(hash != cache_bytes_hash(data.data(), data.size() - 1u), "the hash covers the size");
    std::vector<char> changed = data;
    changed.back() = static_cast<char>(changed.back() ^ 1);
    check(hash != cache_bytes_hash(changed.data(), changed.size()), "the hash covers the last byte");
    check(cache_bytes_hash(nullptr, 0u) == cache_bytes_hash(nullptr, 0u), "the empty hash is defined");
}

// The workers' rule (pipeline_workers.h): the game's and render thread's cores left, a quarter of the free memory at
// 64 MB a compile, at least one.
void worker_tests() {
    constexpr std::uint64_t mb = 1ull << 20u;
    check(pipeline_worker_count(32u, 0u) == 30u, "memory not known: the cores less two");
    check(pipeline_worker_count(32u, 64000u * mb) == 30u, "a desktop with memory to spare: the cores less two");
    check(pipeline_worker_count(32u, 2048u * mb) == 8u, "2 GB free: a quarter of it at 64 MB a compile");
    check(pipeline_worker_count(4u, 8192u * mb) == 2u, "four cores: two workers");
    check(pipeline_worker_count(2u, 8192u * mb) == 1u && pipeline_worker_count(1u, 0u) == 1u, "two cores or fewer: one");
    check(pipeline_worker_count(16u, 100u * mb) == 1u, "almost no memory: still one");
}

}  // namespace

int main() {
    hash_tests();
    vulkan_tests();
    d3d12_tests();
    worker_tests();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d checks failed\n", g_failures);
        return 1;
    }
    std::puts("pipeline cache file tests passed");
    return 0;
}
