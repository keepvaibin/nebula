// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/mod_shaders.h"

#include <cstring>
#include <iterator>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace galaxy::render {
namespace {

constexpr std::size_t kHeaderBytes = 56u;
constexpr std::size_t kEntryBytes = 24u;

constexpr std::uint32_t kBuiltInLayouts[] = {
#include "materials/j3d/layouts.inc"
};

struct Registry {
    std::vector<std::unique_ptr<std::vector<std::uint64_t>>> files;  // words: SPIR-V and DXIL both align
    std::unordered_map<std::uint64_t, NativeShaderBytes> shaders;
    std::unordered_map<std::uint32_t, std::uint32_t> layouts;
};

Registry& registry() {
    static Registry r;
    return r;
}

std::uint64_t key_of(std::uint32_t shader, unsigned variant, bool pixel) noexcept {
    return static_cast<std::uint64_t>(shader) << 16u | static_cast<std::uint64_t>(variant) << 1u | (pixel ? 1u : 0u);
}

}  // namespace

ModShadersResult load_mod_shaders(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) {
        return ModShadersResult::Unreadable;
    }
    const std::streamoff size = in.tellg();
    if (size < static_cast<std::streamoff>(kHeaderBytes)) {
        return ModShadersResult::Unreadable;
    }
    auto words = std::make_unique<std::vector<std::uint64_t>>(static_cast<std::size_t>(size + 7) / 8u);
    in.seekg(0);
    const auto bytes = reinterpret_cast<const unsigned char*>(words->data());
    if (!in.read(reinterpret_cast<char*>(words->data()), size) || std::memcmp(bytes, "NBSHADER", 8) != 0) {
        return ModShadersResult::Unreadable;
    }
    std::uint32_t version = 0u;
    std::uint32_t count = 0u;
    std::memcpy(&version, bytes + 8, 4);
    std::memcpy(&count, bytes + 12, 4);
    const auto total = static_cast<std::uint64_t>(size);
    if (version != 2u || kHeaderBytes + std::uint64_t{count} * (kEntryBytes + 4u) > total) {
        return ModShadersResult::Unreadable;
    }
    if (std::memcmp(bytes + 16, native_shader_interface(), 40) != 0) {
        return ModShadersResult::OtherInterface;
    }
    // Nothing of a file that is wrong anywhere is used.
    std::vector<std::pair<std::uint64_t, NativeShaderBytes>> found;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> layouts;
    found.reserve(count);
    for (std::uint32_t i = 0u; i < count; ++i) {
        const unsigned char* entry = bytes + kHeaderBytes + i * kEntryBytes;
        std::uint32_t shader = 0u;
        std::uint64_t offset = 0u;
        std::uint64_t length = 0u;
        std::memcpy(&shader, entry, 4);
        std::memcpy(&offset, entry + 8, 8);
        std::memcpy(&length, entry + 16, 8);
        if (entry[4] >= kNativeVariants || entry[5] > 1u || offset > total || length > total - offset || length == 0u) {
            return ModShadersResult::Unreadable;
        }
        std::uint32_t layout = 0u;
        std::memcpy(&layout, bytes + kHeaderBytes + count * kEntryBytes + i * 4u, 4);
        layouts.emplace_back(shader, layout);
        found.emplace_back(key_of(shader, entry[4], entry[5] != 0u), NativeShaderBytes{bytes + offset, static_cast<std::size_t>(length)});
    }
    Registry& r = registry();
    // The same material in two mods is the same shader: the first stays.
    for (const auto& [key, shader] : found) {
        r.shaders.emplace(key, shader);
    }
    for (const auto& [shader, layout] : layouts) {
        r.layouts.emplace(shader, layout);
    }
    r.files.push_back(std::move(words));
    return ModShadersResult::Loaded;
}

NativeShaderBytes mod_shader_bytes(std::uint32_t shader, unsigned variant, bool pixel) noexcept {
    const Registry& r = registry();
    const auto it = r.shaders.find(key_of(shader - kNativeShaderJ3DFirst + 1u, variant, pixel));
    return it != r.shaders.end() ? it->second : NativeShaderBytes{};
}

std::uint32_t native_material_layout(std::uint32_t shader) noexcept {
    const std::uint32_t number = shader - kNativeShaderJ3DFirst;
    if (number < std::size(kBuiltInLayouts)) {
        return kBuiltInLayouts[number];
    }
    const Registry& r = registry();
    const auto it = r.layouts.find(number + 1u);
    return it != r.layouts.end() ? it->second : 0u;
}

std::uint32_t material_layout_of_source(const std::string& source) {
    static const char* const kSlots[30] = {
        "reg1", "reg2", "reg3", "kcolor0", "kcolor1", "kcolor2", "kcolor3", "mat_color0", "mat_color1", "amb_color0", "amb_color1",
        "ind_mtx0", "ind_mtx1", "ind_mtx2", "ind_mtx3", "ind_mtx4", "ind_mtx5", "fog", "fog_color", "alpha_ref", "tex_scale0",
        "tex_scale1", "tex_scale2", "tex_scale3", "tex_scale4", "tex_scale5", "tex_scale6", "tex_scale7", "ovr", "ovr_coord"};
    const std::size_t begin = source.find("struct MaterialConstants");
    const std::size_t end = source.find("};", begin);
    if (begin == std::string::npos || end == std::string::npos) {
        return 0u;
    }
    const std::string members = source.substr(begin, end - begin);
    std::uint32_t mask = 0u;
    for (std::uint32_t k = 0u; k < 30u; ++k) {
        if (members.find(std::string("float4 ") + kSlots[k] + ";") != std::string::npos) {
            mask |= 1u << k;
        }
    }
    return mask;
}

}  // namespace galaxy::render
