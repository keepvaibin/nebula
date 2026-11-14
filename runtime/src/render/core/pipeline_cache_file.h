// SPDX-License-Identifier: GPL-3.0-only
// The pipeline caches' files, apart from the API calls (pipelines_d3d12.cpp, pipelines_vk.cpp): a header naming what the data is
// good for, then the driver's data. A file whose header differs from what this run expects, is short or long, or whose data does
// not hash to the header's is refused (an empty result): the cache then starts empty and is written again.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace galaxy::render::core {

// The hash of the cached bytes; the size is in it, so a longer file of the same start differs.
[[nodiscard]] inline std::uint64_t cache_bytes_hash(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = 0x9E3779B97F4A7C15ull ^ size;
    std::size_t i = 0u;
    for (; i + 8u <= size; i += 8u) {
        std::uint64_t word;
        std::memcpy(&word, bytes + i, 8u);
        hash = (hash ^ word) * 0xFF51AFD7ED558CCDull;
        hash ^= hash >> 32u;
    }
    for (; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 0x100000001B3ull;
    }
    return hash;
}

// ---- Vulkan: the programs' compiler revision and the device's vendor, id, driver version and pipeline cache UUID.

inline constexpr std::size_t kCacheUuidSize = 16u;

struct VkCacheIdentity {
    std::uint32_t compiler_revision = 0u;
    std::uint32_t vendor_id = 0u;
    std::uint32_t device_id = 0u;
    std::uint32_t driver_version = 0u;
    std::uint8_t uuid[kCacheUuidSize]{};
};

struct VkCacheHeader {
    char magic[8];
    std::uint32_t compiler_revision;
    std::uint32_t vendor_id;
    std::uint32_t device_id;
    std::uint32_t driver_version;
    std::uint8_t uuid[kCacheUuidSize];
    std::uint64_t size;
    std::uint64_t hash;
};
static_assert(sizeof(VkCacheHeader) == 56u, "the Vulkan cache header has no padding");
inline constexpr char kVkCacheMagic[8] = {'N', 'B', 'V', 'K', 'P', 'C', 'H', '1'};

// The header and `data`, as the file holds them.
[[nodiscard]] inline std::vector<char> wrap_vk_cache(const VkCacheIdentity& identity, const void* data, std::size_t size) {
    VkCacheHeader header{};
    std::memcpy(header.magic, kVkCacheMagic, sizeof(kVkCacheMagic));
    header.compiler_revision = identity.compiler_revision;
    header.vendor_id = identity.vendor_id;
    header.device_id = identity.device_id;
    header.driver_version = identity.driver_version;
    std::memcpy(header.uuid, identity.uuid, kCacheUuidSize);
    header.size = size;
    header.hash = cache_bytes_hash(data, size);
    std::vector<char> file(sizeof(header) + size);
    std::memcpy(file.data(), &header, sizeof(header));
    if (size != 0u) {
        std::memcpy(file.data() + sizeof(header), data, size);
    }
    return file;
}

// The driver data inside a file; empty when the file is not this device's with the same programs.
[[nodiscard]] inline std::vector<char> unwrap_vk_cache(const VkCacheIdentity& identity, const std::vector<char>& file) {
    VkCacheHeader header{};
    if (file.size() <= sizeof(header)) {
        return {};
    }
    std::memcpy(&header, file.data(), sizeof(header));
    if (std::memcmp(header.magic, kVkCacheMagic, sizeof(kVkCacheMagic)) != 0 ||
        header.compiler_revision != identity.compiler_revision || header.vendor_id != identity.vendor_id ||
        header.device_id != identity.device_id || header.driver_version != identity.driver_version ||
        std::memcmp(header.uuid, identity.uuid, kCacheUuidSize) != 0 || header.size != file.size() - sizeof(header) ||
        header.hash != cache_bytes_hash(file.data() + sizeof(header), static_cast<std::size_t>(header.size))) {
        return {};
    }
    return std::vector<char>(file.begin() + static_cast<std::ptrdiff_t>(sizeof(header)), file.end());
}

// ---- Direct3D 12: the adapter's vendor, device and revision and its user-mode driver's version. The runtime also refuses a
// library of another driver or adapter when it opens it; the header keeps a file from being handed to it at all, and the
// file's name carries the same identity so two cards do not take turns replacing one file.

struct D3d12CacheIdentity {
    bool known = false;  // the adapter could be identified; otherwise the other fields are zero
    std::uint32_t vendor_id = 0u;
    std::uint32_t device_id = 0u;
    std::uint32_t revision = 0u;
    std::uint64_t driver_version = 0u;
};

struct D3d12CacheHeader {
    char magic[8];
    std::uint32_t vendor_id;
    std::uint32_t device_id;
    std::uint32_t revision;
    std::uint32_t known;
    std::uint64_t driver_version;
    std::uint64_t size;
    std::uint64_t hash;
};
static_assert(sizeof(D3d12CacheHeader) == 48u, "the Direct3D 12 cache header has no padding");
// 3: a pipeline is named by its Direct3D description (pipelines_d3d12.cpp, PipelineName); a file of the names before
// that would keep every pipeline of theirs, never loaded again.
inline constexpr char kD3d12CacheMagic[8] = {'N', 'B', 'D', '3', 'D', 'P', 'L', '3'};

[[nodiscard]] inline std::vector<char> wrap_d3d12_cache(const D3d12CacheIdentity& identity, const void* data, std::size_t size) {
    D3d12CacheHeader header{};
    std::memcpy(header.magic, kD3d12CacheMagic, sizeof(kD3d12CacheMagic));
    header.vendor_id = identity.vendor_id;
    header.device_id = identity.device_id;
    header.revision = identity.revision;
    header.known = identity.known ? 1u : 0u;
    header.driver_version = identity.driver_version;
    header.size = size;
    header.hash = cache_bytes_hash(data, size);
    std::vector<char> file(sizeof(header) + size);
    std::memcpy(file.data(), &header, sizeof(header));
    if (size != 0u) {
        std::memcpy(file.data() + sizeof(header), data, size);
    }
    return file;
}

[[nodiscard]] inline std::vector<char> unwrap_d3d12_cache(const D3d12CacheIdentity& identity, const std::vector<char>& file) {
    D3d12CacheHeader header{};
    if (file.size() <= sizeof(header)) {
        return {};
    }
    std::memcpy(&header, file.data(), sizeof(header));
    if (std::memcmp(header.magic, kD3d12CacheMagic, sizeof(kD3d12CacheMagic)) != 0 || header.vendor_id != identity.vendor_id ||
        header.device_id != identity.device_id || header.revision != identity.revision ||
        header.known != (identity.known ? 1u : 0u) || header.driver_version != identity.driver_version ||
        header.size != file.size() - sizeof(header) ||
        header.hash != cache_bytes_hash(file.data() + sizeof(header), static_cast<std::size_t>(header.size))) {
        return {};
    }
    return std::vector<char>(file.begin() + static_cast<std::ptrdiff_t>(sizeof(header)), file.end());
}

// The file's name for an adapter.
[[nodiscard]] inline std::string d3d12_cache_file_name(const D3d12CacheIdentity& identity) {
    if (!identity.known) {
        return "d3d12_pipelines_unknown.bin";
    }
    char text[96]{};
    std::snprintf(text, sizeof(text), "d3d12_pipelines_%04x_%04x_%02x_%016llx.bin", identity.vendor_id, identity.device_id,
                  identity.revision, static_cast<unsigned long long>(identity.driver_version));
    return text;
}

}  // namespace galaxy::render::core
