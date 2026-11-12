// SPDX-License-Identifier: GPL-3.0-only
#include "render/core/pipelines.h"

#include "render/core/pipeline_cache_file.h"
#include "galaxy/launch_args.h"
#include "galaxy/render/program_table.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/vk/device_quirks_vk.h"
#include "galaxy/render/vk/renderer_vk.h"
#include "galaxy/render/vk/shader_compile_vk.h"
#include "galaxy/render/vk/vk_context.h"

#include <nebula_model.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace galaxy::render::core {
namespace {

// The vertex inputs' locations (shader_compile_vk.cpp semantic_location): the fixed attributes, then the coordinates,
// as many as a program names.
enum Location : std::uint32_t {
    kPosition = 0u,
    kNormal = 1u,
    kTangent = 2u,
    kBinormal = 3u,
    kColor0 = 4u,
    kColor1 = 5u,
    kMatrix = 6u,
    kBefore = 7u,
    kWiden = 8u,
    kCoord0 = 9u,
};
constexpr std::uint32_t kNoLocation = ~0u;

VkFormat location_format(std::uint32_t location) noexcept {
    switch (location) {
    case kColor0:
    case kColor1: return VK_FORMAT_R8G8B8A8_UNORM;
    case kMatrix:
    case kWiden: return VK_FORMAT_R32_UINT;
    case kPosition:
    case kNormal:
    case kTangent:
    case kBinormal:
    case kBefore: return VK_FORMAT_R32G32B32_SFLOAT;
    default: return VK_FORMAT_R32G32_SFLOAT;
    }
}

VkFormat input_format(std::uint32_t format) noexcept {
    switch (format) {
    case 0: return VK_FORMAT_R32G32B32_SFLOAT;
    case 1: return VK_FORMAT_R32G32_SFLOAT;
    case 2: return VK_FORMAT_R8G8B8A8_UNORM;
    default: return VK_FORMAT_R32_UINT;
    }
}

std::uint32_t input_location(std::uint32_t semantic) noexcept {
    switch (semantic) {
    case NEBULA_INPUT_NORMAL: return kNormal;
    case NEBULA_INPUT_COLOR0: return kColor0;
    case NEBULA_INPUT_COLOR1: return kColor1;
    case NEBULA_INPUT_TANGENT: return kTangent;
    case NEBULA_INPUT_BINORMAL: return kBinormal;
    case NEBULA_INPUT_WIDEN: return kWiden;
    case NEBULA_INPUT_MATRIX: return kMatrix;
    case NEBULA_INPUT_GAIN: return kWiden;  // a uint at the widened corner's place: only sites read that one
    default: return semantic >= NEBULA_INPUT_COORD0 ? kCoord0 + (semantic - NEBULA_INPUT_COORD0) : kNoLocation;
    }
}

// The locations of a program's stage inputs, one flag for each up to the highest.
std::vector<bool> input_locations(const ProgramBytes& program) {
    constexpr std::uint32_t kOpVariable = 59u, kOpDecorate = 71u, kDecorationLocation = 30u, kStorageInput = 1u;
    const auto* spirv = static_cast<const std::uint32_t*>(program.data);
    const std::size_t words = program.size / 4u;
    std::unordered_map<std::uint32_t, std::uint32_t> locations;
    std::vector<std::uint32_t> inputs;
    for (std::size_t at = 5u; at < words && (spirv[at] >> 16u) != 0u; at += spirv[at] >> 16u) {
        const std::uint32_t op = spirv[at] & 0xFFFFu;
        const std::size_t length = spirv[at] >> 16u;
        if (length > words - at) {
            break;  // a cut instruction: nothing past it is read
        }
        if (op == kOpDecorate && length >= 4u && spirv[at + 2u] == kDecorationLocation) {
            locations[spirv[at + 1u]] = spirv[at + 3u];
        } else if (op == kOpVariable && length >= 4u && spirv[at + 3u] == kStorageInput) {
            inputs.push_back(spirv[at + 2u]);
        }
    }
    std::vector<bool> reads;
    for (const std::uint32_t input : inputs) {
        if (const auto it = locations.find(input); it != locations.end()) {
            if (it->second >= reads.size()) {
                reads.resize(std::size_t{it->second} + 1u, false);
            }
            reads[it->second] = true;
        }
    }
    return reads;
}

// A tracer pipeline's pixel module with an unread input at each location the vertex module writes and it declares
// none of (defect D10): a 32-bit float or integer scalar or vector as the output is, integers flat, listed in the fragment entry
// point. Empty when nothing is missing or a module is not as expected (the pipeline then takes the module as it is).
std::vector<std::uint32_t> with_unread_inputs(const ProgramBytes& pixel, const ProgramBytes& vertex) {
    constexpr std::uint32_t kOpEntryPoint = 15u, kOpTypeInt = 21u, kOpTypeFloat = 22u, kOpTypeVector = 23u, kOpTypePointer = 32u,
                            kOpFunction = 54u, kOpVariable = 59u, kOpDecorate = 71u, kDecorationFlat = 14u, kDecorationLocation = 30u,
                            kStorageInput = 1u, kStorageOutput = 3u, kModelFragment = 4u;
    // A varying's type: kind 1 float, 2 signed, 3 unsigned (32 bits); count its components.
    struct Shape {
        std::uint32_t kind = 0u, count = 0u;
        bool operator<(const Shape& o) const noexcept { return kind != o.kind ? kind < o.kind : count < o.count; }
    };
    struct Scan {
        std::map<std::uint32_t, Shape> shapes;             // type id -> shape (scalars and vectors)
        std::map<std::uint32_t, std::uint32_t> pointers;   // pointer type id (the storage asked) -> pointee
        std::map<std::uint32_t, std::uint32_t> variables;  // variable (the storage asked) -> its pointer type
        std::map<std::uint32_t, std::uint32_t> locations;  // decorated id -> location
        std::uint32_t bound = 0u;
    };
    const auto scan = [&](const ProgramBytes& program, std::uint32_t storage, Scan& out) {
        const auto* code = static_cast<const std::uint32_t*>(program.data);
        const std::size_t words = program.size / 4u;
        if (code == nullptr || words < 5u) {
            return false;
        }
        out.bound = code[3];
        for (std::size_t at = 5u; at < words;) {
            const std::uint32_t op = code[at] & 0xFFFFu;
            const std::size_t length = code[at] >> 16u;
            if (length == 0u || length > words - at) {
                return false;
            }
            if (op == kOpTypeFloat && length >= 3u && code[at + 2u] == 32u) {
                out.shapes[code[at + 1u]] = {1u, 1u};
            } else if (op == kOpTypeInt && length >= 4u && code[at + 2u] == 32u) {
                out.shapes[code[at + 1u]] = {code[at + 3u] != 0u ? 2u : 3u, 1u};
            } else if (op == kOpTypeVector && length >= 4u) {
                if (const auto it = out.shapes.find(code[at + 2u]); it != out.shapes.end() && it->second.count == 1u) {
                    out.shapes[code[at + 1u]] = {it->second.kind, code[at + 3u]};
                }
            } else if (op == kOpTypePointer && length >= 4u && code[at + 2u] == storage) {
                out.pointers[code[at + 1u]] = code[at + 3u];
            } else if (op == kOpVariable && length >= 4u && code[at + 3u] == storage) {
                out.variables[code[at + 2u]] = code[at + 1u];
            } else if (op == kOpDecorate && length >= 4u && code[at + 2u] == kDecorationLocation) {
                out.locations[code[at + 1u]] = code[at + 3u];
            }
            at += length;
        }
        return true;
    };
    Scan vs, ps;
    if (!scan(vertex, kStorageOutput, vs) || !scan(pixel, kStorageInput, ps)) {
        return {};
    }
    std::map<std::uint32_t, bool> declared;  // the pixel module's input locations
    for (const auto& input : ps.variables) {
        if (const auto it = ps.locations.find(input.first); it != ps.locations.end()) {
            declared[it->second] = true;
        }
    }
    std::map<std::uint32_t, Shape> missing;  // location -> shape
    for (const auto& [variable, pointer] : vs.variables) {
        const auto location = vs.locations.find(variable);
        const auto pointee = vs.pointers.find(pointer);
        if (location == vs.locations.end() || pointee == vs.pointers.end() || declared.count(location->second) != 0u) {
            continue;
        }
        if (const auto shape = vs.shapes.find(pointee->second); shape != vs.shapes.end()) {
            missing[location->second] = shape->second;
        }
    }
    if (missing.empty()) {
        return {};
    }
    // The pixel module's own types where it has them, new ones after its others.
    std::uint32_t next = ps.bound;
    std::map<Shape, std::uint32_t> type_of;
    for (const auto& [id, shape] : ps.shapes) {
        type_of.emplace(shape, id);
    }
    std::map<std::uint32_t, std::uint32_t> input_pointer_of;  // pointee -> Input pointer
    for (const auto& [pointer, pointee] : ps.pointers) {
        input_pointer_of.emplace(pointee, pointer);
    }
    std::vector<std::uint32_t> types, decorations, added;
    const auto type_for = [&](Shape shape) {
        if (const auto it = type_of.find(shape); it != type_of.end()) {
            return it->second;
        }
        const std::uint32_t id = next++;
        if (shape.count == 1u) {
            if (shape.kind == 1u) {
                types.insert(types.end(), {3u << 16u | kOpTypeFloat, id, 32u});
            } else {
                types.insert(types.end(), {4u << 16u | kOpTypeInt, id, 32u, shape.kind == 2u ? 1u : 0u});
            }
        } else {
            const std::uint32_t component = [&] {
                const Shape one{shape.kind, 1u};
                if (const auto it = type_of.find(one); it != type_of.end()) {
                    return it->second;
                }
                const std::uint32_t scalar = next++;
                if (shape.kind == 1u) {
                    types.insert(types.end(), {3u << 16u | kOpTypeFloat, scalar, 32u});
                } else {
                    types.insert(types.end(), {4u << 16u | kOpTypeInt, scalar, 32u, shape.kind == 2u ? 1u : 0u});
                }
                type_of.emplace(one, scalar);
                return scalar;
            }();
            types.insert(types.end(), {4u << 16u | kOpTypeVector, id, component, shape.count});
        }
        type_of.emplace(shape, id);
        return id;
    };
    for (const auto& [location, shape] : missing) {
        const std::uint32_t type = type_for(shape);
        std::uint32_t pointer = 0u;
        if (const auto it = input_pointer_of.find(type); it != input_pointer_of.end()) {
            pointer = it->second;
        } else {
            pointer = next++;
            types.insert(types.end(), {4u << 16u | kOpTypePointer, pointer, kStorageInput, type});
            input_pointer_of.emplace(type, pointer);
        }
        const std::uint32_t variable = next++;
        types.insert(types.end(), {4u << 16u | kOpVariable, pointer, variable, kStorageInput});
        decorations.insert(decorations.end(), {4u << 16u | kOpDecorate, variable, kDecorationLocation, location});
        if (shape.kind != 1u) {
            decorations.insert(decorations.end(), {3u << 16u | kOpDecorate, variable, kDecorationFlat});
        }
        added.push_back(variable);
    }
    // Written again: the decorations before the first type, the types and variables before the first function, the
    // fragment entry point listing the new inputs.
    const auto* code = static_cast<const std::uint32_t*>(pixel.data);
    const std::size_t words = pixel.size / 4u;
    std::vector<std::uint32_t> out(code, code + 5u);
    out[3] = next;
    bool decorated = false, typed = false, listed = false;
    for (std::size_t at = 5u; at < words;) {
        const std::uint32_t op = code[at] & 0xFFFFu;
        const std::size_t length = code[at] >> 16u;
        const bool declares = (op >= 19u && op <= 39u) || (op >= 41u && op <= 52u) || op == kOpVariable || op == 1u;
        if (!decorated && (declares || op == kOpFunction)) {
            out.insert(out.end(), decorations.begin(), decorations.end());
            decorated = true;
        }
        if (!typed && op == kOpFunction) {
            out.insert(out.end(), types.begin(), types.end());
            typed = true;
        }
        if (op == kOpEntryPoint && length >= 3u && code[at + 1u] == kModelFragment && !listed) {
            out.push_back(static_cast<std::uint32_t>(length + added.size()) << 16u | kOpEntryPoint);
            out.insert(out.end(), code + at + 1u, code + at + length);
            out.insert(out.end(), added.begin(), added.end());
            listed = true;
        } else {
            out.insert(out.end(), code + at, code + at + length);
        }
        at += length;
    }
    if (!decorated || !typed || !listed) {
        return {};
    }
    return out;
}

// The widened pixel modules, by (pixel, vertex) program: kept for the modules' life, so a module's key stays its words.
struct WidenedModules {
    std::mutex lock;
    std::map<std::pair<const void*, const void*>, std::vector<std::uint32_t>> words;
};
WidenedModules gWidened;

// `named`: with --diagnostics rt-inputs, a pair first widened here is named (a pipeline other than the prepass, whose
// pixel form should read what its vertex stage writes).
ProgramBytes widened_pixel(const ProgramBytes& pixel, const ProgramBytes& vertex, std::uint64_t pixel_id, std::uint64_t vertex_id,
                           unsigned form, unsigned aux, bool named) {
    std::lock_guard guard(gWidened.lock);
    auto [it, made] = gWidened.words.try_emplace({pixel.data, vertex.data});
    if (made) {
        it->second = with_unread_inputs(pixel, vertex);
        if (named && !it->second.empty() && ::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs)) {
            std::fprintf(stderr, "[rt-inputs] vk interface: pixel %016llx form %u (aux %u) lacks varyings vertex %016llx writes\n",
                         static_cast<unsigned long long>(pixel_id), form, aux, static_cast<unsigned long long>(vertex_id));
        }
    }
    return it->second.empty() ? pixel : ProgramBytes{it->second.data(), it->second.size() * 4u};
}

VkBlendFactor blend_factor(std::uint32_t f) noexcept {
    switch (f) {
    case NEBULA_FACTOR_ZERO: return VK_BLEND_FACTOR_ZERO;
    case NEBULA_FACTOR_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
    case NEBULA_FACTOR_INV_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case NEBULA_FACTOR_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
    case NEBULA_FACTOR_INV_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case NEBULA_FACTOR_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
    case NEBULA_FACTOR_INV_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case NEBULA_FACTOR_DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
    case NEBULA_FACTOR_INV_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    default: return VK_BLEND_FACTOR_ONE;
    }
}

// Colour factors are invalid on the alpha channel.
VkBlendFactor alpha_factor(VkBlendFactor factor) noexcept {
    switch (factor) {
    case VK_BLEND_FACTOR_SRC_COLOR: return VK_BLEND_FACTOR_SRC_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case VK_BLEND_FACTOR_DST_COLOR: return VK_BLEND_FACTOR_DST_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default: return factor;
    }
}

VkBlendFactor no_destination_alpha(VkBlendFactor factor) noexcept {
    return factor == VK_BLEND_FACTOR_DST_ALPHA             ? VK_BLEND_FACTOR_ONE
           : factor == VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA ? VK_BLEND_FACTOR_ZERO
                                                           : factor;
}

// The one device's shader modules and the driver's pipeline cache, shared by the building threads.
struct Modules {
    std::mutex lock;
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineCache cache = VK_NULL_HANDLE;
    std::filesystem::path cache_file;
    std::unordered_map<const void*, VkShaderModule> modules;
    std::uint64_t on_disk = 0u;  // the hash of the bytes the file holds: an unchanged cache is not written again
    bool file_read = false;      // the cache file has been read since the device came up (once, even when creating fails)
    std::vector<char> initial;   // its data until the cache exists
};
Modules gModules;

// Graphics pipeline library parts (VK_EXT_graphics_pipeline_library), shared by every pipeline that has the same state for that part:
// the vertex input, the pre-raster shaders (vertex program, raster state), the fragment shader (pixel program, depth and
// stencil) and the fragment output (blend, targets). A pipeline is these four linked. Kept for the device's life.
struct LibraryParts {
    std::mutex lock;
    std::unordered_map<std::uint64_t, VkPipeline> input, pre_raster, fragment, output;
    std::atomic<bool> told{false};
};
LibraryParts gParts;

// A key from the values that decide a part's compiled state (a mix of 64-bit words).
class PartKey {
public:
    void add(std::uint64_t value) noexcept {
        hash_ = (hash_ ^ value) * 0x9E3779B97F4A7C15ull;
        hash_ ^= hash_ >> 29u;
    }
    // A Vulkan handle: a pointer on 64-bit targets, a 64-bit integer on 32-bit ones.
    template <typename T>
    void handle(T value) noexcept {
        if constexpr (std::is_pointer_v<T>) {
            add(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value)));
        } else {
            add(static_cast<std::uint64_t>(value));
        }
    }
    [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
    std::uint64_t hash_ = 0xCBF29CE484222325ull;
};

// The part under `key`, made from `info` (a library create info) the first time. Two threads may both make it; one is kept.
VkPipeline library_part(std::unordered_map<std::uint64_t, VkPipeline>& parts, std::uint64_t key, VkDevice device, VkPipelineCache cache,
                        const VkGraphicsPipelineCreateInfo& info) {
    {
        std::lock_guard guard(gParts.lock);
        if (const auto it = parts.find(key); it != parts.end()) {
            return it->second;
        }
    }
    VkPipeline made = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, cache, 1u, &info, nullptr, &made) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    std::lock_guard guard(gParts.lock);
    const auto [at, inserted] = parts.emplace(key, made);
    if (!inserted) {
        vkDestroyPipeline(device, made, nullptr);  // another thread made it first; nothing has used this one
    }
    return at->second;
}

std::uint64_t bytes_hash(const std::vector<char>& bytes) noexcept {
    return cache_bytes_hash(bytes.data(), bytes.size());
}

// The cache file: a header (pipeline_cache_file.h) then the driver's cache data. The header names what the data is good for (the
// programs' compiler revision and the device's vendor, id, driver version and pipeline cache UUID); a file that differs, is
// short, or whose data does not hash to the header's is never handed to the driver, so the cache starts empty and is written
// again.
static_assert(VK_UUID_SIZE == kCacheUuidSize);

VkCacheIdentity cache_identity() noexcept {
    const VkPhysicalDeviceProperties& properties = vk::Context::get().properties();
    VkCacheIdentity identity{};
    identity.compiler_revision = vk::kCompilerRevision;
    identity.vendor_id = properties.vendorID;
    identity.device_id = properties.deviceID;
    identity.driver_version = properties.driverVersion;
    std::memcpy(identity.uuid, properties.pipelineCacheUUID, kCacheUuidSize);
    return identity;
}

// The driver data inside a cache file; empty when the file is not this device's with the same programs.
std::vector<char> unwrap_cache_file(const std::vector<char>& file) {
    return unwrap_vk_cache(cache_identity(), file);
}

// The cache is internally synchronised, so the workers keep building while a copy goes out; one writer at a time.
std::size_t write_driver_cache() noexcept {
    static std::mutex writing;
    std::lock_guard one(writing);
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineCache cache = VK_NULL_HANDLE;
    std::filesystem::path file;
    std::uint64_t on_disk = 0u;
    {
        std::lock_guard guard(gModules.lock);
        device = gModules.device;
        cache = gModules.cache;
        file = gModules.cache_file;
        on_disk = gModules.on_disk;
    }
    std::size_t size = 0u;
    if (cache == VK_NULL_HANDLE || vk::Context::get().quirks().no_cache_file ||
        vkGetPipelineCacheData(device, cache, &size, nullptr) != VK_SUCCESS || size == 0u) {
        return 0u;
    }
    std::vector<char> bytes(size);
    if (vkGetPipelineCacheData(device, cache, &size, bytes.data()) != VK_SUCCESS) {
        return 0u;
    }
    bytes.resize(size);
    const std::uint64_t hash = bytes_hash(bytes);
    if (hash == on_disk) {
        return 0u;
    }
    const std::vector<char> out = wrap_vk_cache(cache_identity(), bytes.data(), bytes.size());
    if (!replace_file(file, out.data(), out.size())) {
        return 0u;
    }
    std::lock_guard guard(gModules.lock);
    gModules.on_disk = hash;
    return bytes.size();
}

VkShaderModule module_of(VkDevice device, const ProgramBytes& program) {
    std::lock_guard guard(gModules.lock);
    if (const auto it = gModules.modules.find(program.data); it != gModules.modules.end()) {
        return it->second;
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = program.size;
    info.pCode = static_cast<const std::uint32_t*>(program.data);
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    gModules.modules.emplace(program.data, module);
    return module;
}

VkPipelineCache driver_cache(VkDevice device) {
    std::lock_guard guard(gModules.lock);
    if (gModules.cache != VK_NULL_HANDLE) {
        return gModules.cache;
    }
    gModules.device = device;
    if (!gModules.file_read) {
        gModules.file_read = true;
        // Named for what the data is valid for: the device, its driver build and the driver's cache UUID.
        gModules.cache_file = shader_cache_directory() / ("vk_pipelines-" + vk::driver_key(vk::Context::get().identity()) + ".bin");
        std::error_code ignored;
        std::filesystem::remove(shader_cache_directory() / "vk_pipelines.bin", ignored);  // the one file every GPU shared
        if (vk::Context::get().quirks().no_cache_file) {
            // Nothing is read; write_driver_cache writes nothing either.
        } else if (std::ifstream in(gModules.cache_file, std::ios::binary | std::ios::ate); in) {
            std::vector<char> file(static_cast<std::size_t>(in.tellg()));
            in.seekg(0);
            in.read(file.data(), static_cast<std::streamsize>(file.size()));
            gModules.initial = unwrap_cache_file(file);
        }
        gModules.on_disk = gModules.initial.empty() ? 0u : bytes_hash(gModules.initial);
    }
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = gModules.initial.size();
    info.pInitialData = gModules.initial.empty() ? nullptr : gModules.initial.data();
    if (vkCreatePipelineCache(device, &info, nullptr, &gModules.cache) != VK_SUCCESS) {
        info.initialDataSize = 0u;
        info.pInitialData = nullptr;
        vkCreatePipelineCache(device, &info, nullptr, &gModules.cache);
    }
    if (gModules.cache != VK_NULL_HANDLE) {
        std::vector<char>().swap(gModules.initial);
    }
    return gModules.cache;
}

}  // namespace

gpu::Pipeline* Pipelines::build(const PipelineEntry& entry, unsigned variant, unsigned aux) {
    return build_vk(entry, variant, aux, false);
}

gpu::Pipeline* Pipelines::build_fast(const PipelineEntry& entry, unsigned variant, unsigned aux) {
    return vk::Context::get().pipeline_libraries() ? build_vk(entry, variant, aux, true) : nullptr;
}

// `fast`: only the quick link of library parts, or null (the caller builds the whole pipeline); else the optimised link when
// the device has libraries, and the monolithic pipeline when that is not possible.
gpu::Pipeline* Pipelines::build_vk(const PipelineEntry& entry, unsigned variant, unsigned aux, bool fast) {
    const NebulaPipelineDesc& desc = entry.desc;
    const auto* layout = static_cast<const vk::RenderLayout*>(root_);
    const VkDevice device = device_;
    const unsigned samples_log2 = variant & kSceneVariantSamplesMask;
    const unsigned program = program_variant(desc.pixel, aux,
        (desc.state & NEBULA_STATE_PIXEL_SHADING) != 0u ? 0u : samples_log2);
    // The tracer's prepass of a motion draw goes through the vertex stage's motion form, as the draw itself will.
    const ProgramBytes vs = program_bytes(desc.vertex, aux == kAuxPrepassMotion ? unsigned{kProgramTracedMotion} : program, false);
    const ProgramBytes ps = program_bytes(desc.pixel, program, true);
    if (vs.size == 0u || ps.size == 0u) {
        std::fprintf(stderr, "[pipelines] no program %llx %llx variant %u\n", static_cast<unsigned long long>(desc.vertex),
                     static_cast<unsigned long long>(desc.pixel), program);
        return nullptr;
    }
    const unsigned bound = aux_targets(aux);  // the targets: 0-4, or the prepass's
    const bool prepass = bound == kAuxPrepass;
    const VkShaderModule vs_module = module_of(device, vs);
    // A tracer's pipeline (the prepass and the traced forms, made only with ray tracing on): its pixel stage declares
    // every varying its vertex stage writes (D10: no interface notes). Ray tracing off, every pipeline is as it was.
    const VkShaderModule ps_module =
        module_of(device, aux >= kAuxPrepass ? widened_pixel(ps, vs, desc.pixel, desc.vertex, program, aux, !prepass) : ps);
    if (vs_module == VK_NULL_HANDLE || ps_module == VK_NULL_HANDLE) {
        return nullptr;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs_module;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps_module;
    stages[1].pName = "main";

    // What the vertex program reads; each from the list if named there, else the stream of zeros. A stride belongs to a
    // pipeline here, so the description's input strides decide the bindings.
    const std::vector<bool> reads = input_locations(vs);
    std::vector<VkVertexInputAttributeDescription> attributes;
    attributes.reserve(reads.size());
    std::vector<bool> placed(reads.size(), false);
    VkVertexInputBindingDescription bindings[gpu::kStreamCount]{};
    std::uint32_t binding_count = 0u;
    const auto bind = [&](std::uint32_t slot, std::uint32_t stride) {
        for (std::uint32_t i = 0; i < binding_count; ++i) {
            if (bindings[i].binding == slot) {
                return;
            }
        }
        bindings[binding_count++] = {slot, stride, VK_VERTEX_INPUT_RATE_VERTEX};
    };
    const auto add = [&](std::uint32_t location, VkFormat format, std::uint32_t slot, std::uint32_t stride, std::uint32_t offset) {
        if (location >= reads.size() || !reads[location] || placed[location]) {
            return;
        }
        placed[location] = true;
        attributes.push_back({location, slot, format, offset});
        bind(slot, stride);
    };
    const std::uint32_t position_stride = desc.position_stride != 0u ? desc.position_stride : 12u;
    add(kPosition, VK_FORMAT_R32G32B32_SFLOAT, gpu::kStreamPosition, position_stride, 0u);
    if (desc.position_stride == 0u) {
        add(kMatrix, VK_FORMAT_R32_UINT, gpu::kStreamMatrix, 4u, 0u);
    }
    if (aux_motion_form(aux)) {
        add(kBefore, VK_FORMAT_R32G32B32_SFLOAT, gpu::kStreamBefore, position_stride, 0u);
    }
    for (const NebulaLayoutInput& input : entry.inputs) {
        const bool zero = input.stream == NEBULA_STREAM_ZERO;
        const std::uint32_t slot = zero ? std::uint32_t{gpu::kStreamZero} : std::uint32_t{input.stream};
        if (slot >= gpu::kStreamCount) {
            continue;  // no such buffer slot: the input reads zero like one a program names without a list entry
        }
        add(input_location(input.semantic), input_format(input.format), slot, zero ? 0u : input.stride, input.offset);
    }
    for (std::uint32_t location = 0; location < reads.size(); ++location) {
        add(location, location_format(location), gpu::kStreamZero, 0u, 0u);
    }
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = binding_count;
    vertex_input.pVertexBindingDescriptions = bindings;
    vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1u;
    viewport.scissorCount = 1u;

    const std::uint32_t state = desc.state;
    // The viewport flips y as D3D's does, so clockwise is front as there. Culling all has no state: the walker draws nothing.
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    switch ((state >> 15u) & 3u) {
    case NEBULA_CULL_BACK: raster.cullMode = VK_CULL_MODE_BACK_BIT; break;
    case NEBULA_CULL_FRONT: raster.cullMode = VK_CULL_MODE_FRONT_BIT; break;
    default: raster.cullMode = VK_CULL_MODE_NONE; break;
    }
    // D3D clamps fragment depth to the viewport's range and clips primitives to it (depth clears at the far plane rely on
    // the clamp).
    VkPipelineRasterizationDepthClipStateCreateInfoEXT depth_clip{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT};
    if (layout->depth_clamp && layout->depth_clip_enable) {
        raster.depthClampEnable = VK_TRUE;
        depth_clip.depthClipEnable = VK_TRUE;
        raster.pNext = &depth_clip;
    }

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(1u << samples_log2);

    // Depth is reversed: the game's less is greater.
    static constexpr VkCompareOp kDepth[8] = {
        VK_COMPARE_OP_NEVER, VK_COMPARE_OP_GREATER,   VK_COMPARE_OP_EQUAL,         VK_COMPARE_OP_GREATER_OR_EQUAL,
        VK_COMPARE_OP_LESS,  VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_ALWAYS,
    };
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    if ((state & (1u << 10u)) != 0u) {
        depth.depthTestEnable = VK_TRUE;
        depth.depthCompareOp = kDepth[(state >> 11u) & 7u];
        depth.depthWriteEnable = (state & (1u << 14u)) != 0u ? VK_TRUE : VK_FALSE;
    }
    // The prepass leaves the stencil alone: a draw's marks are made once, by the scene pass.
    if (const std::uint32_t test = (state >> 20u) & 3u; test != 0u && !prepass) {
        static constexpr VkCompareOp kStencilTest[4] = {VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_EQUAL,
                                                        VK_COMPARE_OP_NOT_EQUAL};
        static constexpr VkStencilOp kStencilOp[4] = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                                      VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_ZERO};
        depth.stencilTestEnable = VK_TRUE;
        VkStencilOpState face{};
        face.failOp = VK_STENCIL_OP_KEEP;
        face.depthFailOp = VK_STENCIL_OP_KEEP;
        face.passOp = kStencilOp[(state >> 22u) & 3u];
        face.compareOp = kStencilTest[test];
        face.compareMask = 0xFFu;
        face.writeMask = 0xFFu;
        depth.front = face;
        depth.back = face;
    }

    const bool target = (variant & kSceneVariantTarget) != 0u;
    VkPipelineColorBlendAttachmentState color{};
    color.colorWriteMask =
        ((state & NEBULA_STATE_COLOR_WRITE) != 0u ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT : 0u) |
        ((state & (NEBULA_STATE_ALPHA_WRITE | NEBULA_STATE_ALPHA_PASS)) != 0u && target ? VK_COLOR_COMPONENT_A_BIT : 0u);
    const std::uint32_t op = state & 3u;
    if (op != NEBULA_BLEND_REPLACE) {
        color.blendEnable = VK_TRUE;
        if (op == NEBULA_BLEND_SUBTRACT) {
            color.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            color.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            color.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
        } else {
            color.srcColorBlendFactor = blend_factor((state >> 2u) & 15u);
            color.dstColorBlendFactor = blend_factor((state >> 6u) & 15u);
            color.colorBlendOp = VK_BLEND_OP_ADD;
            if (!target) {
                // No destination alpha in the scene: it reads as one.
                color.srcColorBlendFactor = no_destination_alpha(color.srcColorBlendFactor);
                color.dstColorBlendFactor = no_destination_alpha(color.dstColorBlendFactor);
            }
        }
        color.srcAlphaBlendFactor = alpha_factor(color.srcColorBlendFactor);
        color.dstAlphaBlendFactor = alpha_factor(color.dstColorBlendFactor);
        color.alphaBlendOp = color.colorBlendOp;
    }
    // An added clear shell's traced form (FB b94, EVERYTHING 6.7): an exchange, premultiplied: the program gives F R and
    // F, what is behind keeps 1 - F.
    if (aux >= kAuxTraced && (aux & kAuxExchange) != 0u) {
        color.blendEnable = VK_TRUE;
        color.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        color.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color.colorBlendOp = VK_BLEND_OP_ADD;
        color.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    if (prepass) {
        color.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;  // the item target alone: the draw's item and triangle where it is nearest
        color.blendEnable = VK_FALSE;  // a blended entry's item (the second layer) replaces like any other
    }
    VkPipelineColorBlendAttachmentState targets[4]{color, {}, {}, {}};
    // Aux 4: the motion pass's second target is there and left as it is.
    if (bound == 1u) {
        targets[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
    } else if (bound == 2u || bound == 3u) {
        targets[1].blendEnable = VK_TRUE;
        targets[1].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        targets[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        targets[1].colorBlendOp = VK_BLEND_OP_MAX;
        targets[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        targets[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        targets[1].alphaBlendOp = VK_BLEND_OP_MAX;
        targets[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    }
    if (bound == 3u) {
        // The effects layer: render target 2 takes the colour as the scene target blends it, 3 the product of the
        // destination factors (nothing from the source: it keeps what the destination factor lets through). Colour only.
        constexpr VkColorComponentFlags kRgb = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
        targets[2] = color;
        targets[2].colorWriteMask = kRgb;
        targets[3] = targets[2];
        targets[3].srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        targets[3].colorBlendOp = VK_BLEND_OP_ADD;
        targets[3].dstColorBlendFactor = color.colorBlendOp == VK_BLEND_OP_ADD ? color.dstColorBlendFactor : VK_BLEND_FACTOR_ONE;
        targets[3].srcAlphaBlendFactor = alpha_factor(targets[3].srcColorBlendFactor);
        targets[3].dstAlphaBlendFactor = alpha_factor(targets[3].dstColorBlendFactor);
        targets[3].alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = prepass ? 1u : bound == 3u ? 4u : bound != 0u ? 2u : 1u;
    blend.pAttachments = targets;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2u;
    dynamic.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2u;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout->layout;
    info.renderPass = prepass      ? layout->item_pass
                      : bound != 0u ? layout->aux_pass_for(variant, bound == 4u ? 1u : std::min(bound, 3u))
                                    : layout->pass_for(variant, false);
    if (info.renderPass == VK_NULL_HANDLE) {
        // The device cannot draw this variant (the effects layer's pass is absent): the walker misses this draw.
        return nullptr;
    }

    const VkPipelineCache cache = driver_cache(device);
    if (vk::Context::get().pipeline_libraries()) {
        // The four parts (made once for each distinct state of that part), linked: quickly for a draw that waits, with the
        // link-time optimisation on a worker. Any part the driver refuses sends the pipeline down the monolithic path.
        if (!gParts.told.exchange(true)) {
            std::fprintf(stderr, "[pipelines] graphics pipeline libraries: draws link their pipeline from parts at once, workers optimise the link\n");
        }
        constexpr VkPipelineCreateFlags kPart = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;
        const auto part_info = [&](VkGraphicsPipelineLibraryCreateInfoEXT& kind, VkGraphicsPipelineLibraryFlagsEXT flags) {
            kind = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT};
            kind.flags = flags;
            VkGraphicsPipelineCreateInfo part{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            part.pNext = &kind;
            part.flags = kPart;
            part.layout = layout->layout;
            part.renderPass = info.renderPass;
            part.basePipelineIndex = -1;
            return part;
        };
        PartKey input_key;
        for (std::uint32_t i = 0; i < binding_count; ++i) {
            input_key.add(std::uint64_t{bindings[i].binding} << 32u | bindings[i].stride);
        }
        for (const VkVertexInputAttributeDescription& attribute : attributes) {
            input_key.add(std::uint64_t{attribute.location} << 40u | std::uint64_t{attribute.binding} << 32u | static_cast<std::uint32_t>(attribute.format));
            input_key.add(attribute.offset);
        }
        PartKey pre_key;
        pre_key.handle(vs_module);
        pre_key.handle(info.renderPass);
        pre_key.handle(layout->layout);
        const auto u = [](auto value) { return static_cast<std::uint64_t>(value); };
        pre_key.add(u(raster.cullMode) << 8u | u(raster.depthClampEnable) << 1u | u(depth_clip.depthClipEnable));
        PartKey fragment_key;
        fragment_key.handle(ps_module);
        fragment_key.handle(info.renderPass);
        fragment_key.handle(layout->layout);
        fragment_key.add(u(depth.depthTestEnable) | u(depth.depthWriteEnable) << 1u | u(depth.depthCompareOp) << 4u |
                         u(depth.stencilTestEnable) << 8u | u(depth.front.compareOp) << 12u | u(depth.front.passOp) << 16u |
                         u(multisample.rasterizationSamples) << 24u);
        PartKey output_key;
        output_key.handle(info.renderPass);
        output_key.add(u(blend.attachmentCount) | u(multisample.rasterizationSamples) << 8u);
        for (std::uint32_t i = 0; i < blend.attachmentCount; ++i) {
            const VkPipelineColorBlendAttachmentState& a = targets[i];
            output_key.add(u(a.blendEnable) | u(a.srcColorBlendFactor) << 4u | u(a.dstColorBlendFactor) << 9u | u(a.colorBlendOp) << 14u |
                           u(a.srcAlphaBlendFactor) << 18u | u(a.dstAlphaBlendFactor) << 23u | u(a.alphaBlendOp) << 28u |
                           u(a.colorWriteMask) << 32u);
        }
        VkGraphicsPipelineLibraryCreateInfoEXT input_kind{}, pre_kind{}, fragment_kind{}, output_kind{};
        VkGraphicsPipelineCreateInfo input_info = part_info(input_kind, VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT);
        input_info.pVertexInputState = &vertex_input;
        input_info.pInputAssemblyState = &input_assembly;
        VkGraphicsPipelineCreateInfo pre_info = part_info(pre_kind, VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT);
        pre_info.stageCount = 1u;
        pre_info.pStages = &stages[0];
        pre_info.pViewportState = &viewport;
        pre_info.pRasterizationState = &raster;
        pre_info.pDynamicState = &dynamic;
        VkGraphicsPipelineCreateInfo fragment_info = part_info(fragment_kind, VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT);
        fragment_info.stageCount = 1u;
        fragment_info.pStages = &stages[1];
        fragment_info.pMultisampleState = &multisample;
        fragment_info.pDepthStencilState = &depth;
        VkGraphicsPipelineCreateInfo output_info = part_info(output_kind, VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT);
        output_info.pMultisampleState = &multisample;
        output_info.pColorBlendState = &blend;
        const VkPipeline parts[4] = {library_part(gParts.input, input_key.value(), device, cache, input_info),
                                     library_part(gParts.pre_raster, pre_key.value(), device, cache, pre_info),
                                     library_part(gParts.fragment, fragment_key.value(), device, cache, fragment_info),
                                     library_part(gParts.output, output_key.value(), device, cache, output_info)};
        if (parts[0] != VK_NULL_HANDLE && parts[1] != VK_NULL_HANDLE && parts[2] != VK_NULL_HANDLE && parts[3] != VK_NULL_HANDLE) {
            VkPipelineLibraryCreateInfoKHR libraries{VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR};
            libraries.libraryCount = 4u;
            libraries.pLibraries = parts;
            VkGraphicsPipelineCreateInfo linked{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            linked.pNext = &libraries;
            linked.flags = fast ? 0u : VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT;
            linked.layout = layout->layout;
            linked.renderPass = info.renderPass;
            linked.basePipelineIndex = -1;
            VkPipeline whole = VK_NULL_HANDLE;
            if (vkCreateGraphicsPipelines(device, cache, 1u, &linked, nullptr, &whole) == VK_SUCCESS) {
                std::lock_guard guard(lock_);
                owned_.push_back(whole);
                return whole;
            }
        }
        if (fast) {
            return nullptr;  // the caller builds the whole pipeline (the optimised path, then the monolithic one)
        }
    }
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(device, cache, 1u, &info, nullptr, &pipeline);
    if (result != VK_SUCCESS && raster.depthClampEnable == VK_TRUE) {
        // A driver that refuses the depth-clip control still draws, unclamped.
        raster.depthClampEnable = VK_FALSE;
        raster.pNext = nullptr;
        result = vkCreateGraphicsPipelines(device, cache, 1u, &info, nullptr, &pipeline);
    }
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[pipelines] create failed %d: %llx %llx state %08x variant %u aux %u\n", static_cast<int>(result),
                     static_cast<unsigned long long>(desc.vertex), static_cast<unsigned long long>(desc.pixel), state, variant, aux);
        return nullptr;
    }
    std::lock_guard guard(lock_);
    owned_.push_back(pipeline);
    return pipeline;
}

void Pipelines::destroy_all() noexcept {
    const VkDevice device = device_;
    // A simulated loss leaves the queue alive; complete its references before destroying pipelines.
    // On a real loss this returns VK_ERROR_DEVICE_LOST and teardown can continue.
    if (device != VK_NULL_HANDLE) (void)vkDeviceWaitIdle(device);
    for (const VkPipeline pipeline : owned_) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    owned_.clear();
    {
        std::lock_guard parts(gParts.lock);
        for (auto* map : {&gParts.input, &gParts.pre_raster, &gParts.fragment, &gParts.output}) {
            for (const auto& [key, part] : *map) {
                vkDestroyPipeline(device, part, nullptr);
            }
            map->clear();
        }
        gParts.told.store(false);
    }
    write_driver_cache();
    std::lock_guard guard(gModules.lock);
    for (const auto& [data, module] : gModules.modules) {
        vkDestroyShaderModule(device, module, nullptr);
    }
    gModules.modules.clear();
    if (gModules.cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(device, gModules.cache, nullptr);
        gModules.cache = VK_NULL_HANDLE;
    }
    gModules.file_read = false;
    std::vector<char>().swap(gModules.initial);
}

std::size_t Pipelines::save_cache() noexcept {
    return write_driver_cache();
}

}  // namespace galaxy::render::core
