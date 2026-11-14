// SPDX-License-Identifier: GPL-3.0-only
// HLSL to SPIR-V through glslang (shader_compile_vk.h).
#include "galaxy/render/vk/shader_compile_vk.h"
#include "../materials/nebula_limits.hlsli"

// The release libraries are built with the HLSL front end; this exposes its switch for semantic decorations.
#define ENABLE_HLSL 1
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <spirv-tools/libspirv.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <set>

namespace galaxy::render::vk {

namespace {

constexpr RegisterBinding kDrawBindings[] = {
    {"b0", 0, 0},
    {"b1", 0, 1},
    {"b2", 0, 2},
    {"t16", 0, 3},
    {"t17", 0, 4},
    {"t18", 0, 5},
    {"b3", 0, 0, true},
    // Set 1 the images and set 2 the samplers, as one array each over every texture and sampler (space1) or as a bound
    // table (program.hlsli).
    {"t0, space1", 1, 0},
    {"t0, space2", 1, 0},  // the same images as Texture3D (program.hlsli gVolumes)
    {"s0, space1", 2, 0},
    {"t0", 1, 0},
    {"s0", 2, 0},
};

void initialize_glslang() {
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
}

// SPIR-V's words for the location rewrites.
constexpr std::uint32_t kOpName = 5;
constexpr std::uint32_t kOpExtension = 10;
constexpr std::uint32_t kOpVariable = 59;
constexpr std::uint32_t kOpDecorate = 71;
constexpr std::uint32_t kOpDecorateId = 332;
constexpr std::uint32_t kOpDecorateString = 5632;
constexpr std::uint32_t kOpMemberDecorateString = 5633;
constexpr std::uint32_t kDecorationLocation = 30;
constexpr std::uint32_t kDecorationIndex = 32;
constexpr std::uint32_t kDecorationHlslCounterBuffer = 5634;
constexpr std::uint32_t kDecorationUserSemantic = 5635;
constexpr std::uint32_t kDecorationUserType = 5636;
constexpr std::uint32_t kStorageInput = 1;
constexpr std::uint32_t kStorageOutput = 3;

std::string spirv_string(const std::vector<std::uint32_t>& spirv, std::size_t first, std::size_t end) {
    std::string text;
    for (std::size_t at = first; at < end; ++at) {
        for (unsigned byte = 0; byte < 4u; ++byte) {
            const char c = static_cast<char>((spirv[at] >> (byte * 8u)) & 0xffu);
            if (c == '\0') {
                return text;
            }
            text += c;
        }
    }
    return text;
}

// A semantic's location, as D3D12 matches stages by semantic: vertex attributes
// (POSITION 0, NORMAL 1, TANGENT 2, BINORMAL 3, COLOR0-1 4-5, BLENDINDICES0 6, POSITION1 7, BLENDINDICES1 8, then
// TEXCOORDn at 9 + n, as many as the program names);
// between stages COLOR0-1 at 0-1 and TEXCOORDn at 2 + n (the program's coordinates, then the lit and motion varyings
// its text numbers after them); pixel outputs
// SV_TARGETn at n. `element`: the variable's element of an
// array declared with this semantic, which takes the following indices.
std::optional<std::uint32_t> semantic_location(std::string semantic, std::uint32_t element, bool vertex_input,
                                               bool pixel_output) {
    for (char& c : semantic) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    std::size_t digits = semantic.size();
    while (digits > 0u && std::isdigit(static_cast<unsigned char>(semantic[digits - 1u])) != 0) {
        --digits;
    }
    const std::string name = semantic.substr(0, digits);
    const std::uint32_t index =
        (digits < semantic.size() ? static_cast<std::uint32_t>(std::stoul(semantic.substr(digits))) : 0u) + element;
    if (pixel_output) {
        return name == "SV_TARGET" && index < 8u ? std::optional<std::uint32_t>(index) : std::nullopt;
    }
    if (vertex_input) {
        if (name == "POSITION" && index == 0u) return 0u;
        if (name == "NORMAL" && index == 0u) return 1u;
        if (name == "TANGENT" && index == 0u) return 2u;
        if (name == "BINORMAL" && index == 0u) return 3u;
        if (name == "COLOR" && index < 2u) return 4u + index;
        if (name == "BLENDINDICES" && index == 0u) return 6u;
        if (name == "POSITION" && index == 1u) return 7u;
        if (name == "BEND" && index == 0u) return 7u;
        if (name == "BLENDINDICES" && index == 1u) return 8u;
        if (name == "TEXCOORD") return 9u + index;
        return std::nullopt;
    }
    if (name == "COLOR" && index < 2u) return index;
    if (name == "TEXCOORD") return 2u + index;
    return std::nullopt;
}

// glslang numbers stage inputs and outputs in the order it meets them, so two shaders of a pipeline (or a vertex
// shader and the vertex layout) can disagree when one leaves a variable unused. Every non-built-in input and output
// moves to its semantic's location (glslang's SPV_GOOGLE semantic decorations, then removed with their extensions).
bool assign_semantic_locations(std::vector<std::uint32_t>& spirv, ShaderStage stage, std::string& error) {
    std::map<std::uint32_t, std::uint32_t> storage;   // variable -> storage class (inputs and outputs)
    std::map<std::uint32_t, std::string> semantics;   // variable -> semantic
    std::map<std::uint32_t, std::uint32_t> elements;  // variable -> element, for an array glslang split ("v.uv[3]")
    for (std::size_t at = 5; at < spirv.size();) {
        const std::uint32_t count = spirv[at] >> 16u;
        const std::uint32_t op = spirv[at] & 0xffffu;
        if (count == 0u || at + count > spirv.size()) {
            error = "malformed SPIR-V";
            return false;
        }
        if (op == kOpVariable && count >= 4u && (spirv[at + 3] == kStorageInput || spirv[at + 3] == kStorageOutput)) {
            storage[spirv[at + 2]] = spirv[at + 3];
        } else if (op == kOpDecorateString && count >= 4u && spirv[at + 2] == kDecorationUserSemantic) {
            semantics[spirv[at + 1]] = spirv_string(spirv, at + 3u, at + count);
        } else if (op == kOpName && count >= 3u) {
            const std::string name = spirv_string(spirv, at + 2u, at + count);
            const std::size_t open = name.rfind('[');
            if (!name.empty() && name.back() == ']' && open != std::string::npos && open + 2u < name.size() &&
                std::all_of(name.begin() + static_cast<std::ptrdiff_t>(open + 1u), name.end() - 1,
                            [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
                elements[spirv[at + 1]] =
                    static_cast<std::uint32_t>(std::stoul(name.substr(open + 1u, name.size() - open - 2u)));
            }
        }
        at += count;
    }
    std::vector<std::uint32_t> out(spirv.begin(), spirv.begin() + 5);
    out.reserve(spirv.size());
    for (std::size_t at = 5; at < spirv.size();) {
        const std::uint32_t count = spirv[at] >> 16u;
        const std::uint32_t op = spirv[at] & 0xffffu;
        const auto keep = [&] {
            out.insert(out.end(), spirv.begin() + static_cast<std::ptrdiff_t>(at),
                       spirv.begin() + static_cast<std::ptrdiff_t>(at + count));
        };
        if (op == kOpExtension && spirv_string(spirv, at + 1u, at + count).starts_with("SPV_GOOGLE_")) {
            // dropped with the decorations below
        } else if ((op == kOpDecorateString || op == kOpMemberDecorateString) &&
                   (spirv[at + 2u + (op == kOpMemberDecorateString ? 1u : 0u)] == kDecorationUserSemantic ||
                    spirv[at + 2u + (op == kOpMemberDecorateString ? 1u : 0u)] == kDecorationUserType)) {
        } else if (op == kOpDecorateId && count >= 3u && spirv[at + 2] == kDecorationHlslCounterBuffer) {
        } else if (op == kOpDecorate && count == 4u && spirv[at + 2] == kDecorationLocation &&
                   storage.contains(spirv[at + 1])) {
            const std::uint32_t variable = spirv[at + 1];
            const auto semantic = semantics.find(variable);
            if (semantic == semantics.end()) {
                error = "stage variable %" + std::to_string(variable) + " has no semantic";
                return false;
            }
            const bool input = storage[variable] == kStorageInput;
            const auto element = elements.find(variable);
            const auto location =
                semantic_location(semantic->second, element != elements.end() ? element->second : 0u,
                                  stage == ShaderStage::Vertex && input, stage == ShaderStage::Fragment && !input);
            if (!location) {
                error = "no location for semantic " + semantic->second;
                return false;
            }
            keep();
            out.back() = *location;
        } else {
            keep();
        }
        at += count;
    }
    spirv = std::move(out);
    return true;
}

// Drops debug names and source text (they mean nothing to a driver, SPIR-V 2.13). Adreno's driver matches stage
// variables by name, so a vertex input named like one of the outputs fails its compile (naga issue 1331): glslang names
// a split HLSL array input "v.uv[0]" and the output "@entryPointOutput.uv[0]", and Qualcomm 512.842 refuses every
// vertex shader that reads a texture coordinate.
void strip_debug_names(std::vector<std::uint32_t>& spirv) {
    constexpr std::uint32_t kOpSourceContinued = 2;
    constexpr std::uint32_t kOpSource = 3;
    constexpr std::uint32_t kOpSourceExtension = 4;
    constexpr std::uint32_t kOpMemberName = 6;
    constexpr std::uint32_t kOpString = 7;
    constexpr std::uint32_t kOpLine = 8;
    constexpr std::uint32_t kOpNoLine = 317;
    constexpr std::uint32_t kOpModuleProcessed = 330;
    std::vector<std::uint32_t> out(spirv.begin(), spirv.begin() + 5);
    out.reserve(spirv.size());
    for (std::size_t at = 5; at < spirv.size();) {
        const std::uint32_t count = std::max(spirv[at] >> 16u, 1u);
        const std::uint32_t op = spirv[at] & 0xffffu;
        const bool debug = op == kOpSourceContinued || op == kOpSource || op == kOpSourceExtension || op == kOpName ||
            op == kOpMemberName || op == kOpString || op == kOpLine || op == kOpNoLine || op == kOpModuleProcessed;
        if (!debug) {
            out.insert(out.end(), spirv.begin() + static_cast<std::ptrdiff_t>(at),
                       spirv.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(at + count, spirv.size())));
        }
        at += count;
    }
    spirv = std::move(out);
}

// The bindless form's texture and sampler arrays (program.hlsli: gTextures[] and gVolumes[] in descriptor set 1,
// gSamplers[] in set 2, kDrawBindings) as OpTypeRuntimeArray. glslang's HLSL front end never marks an unsized array as
// variably indexed (its GLSL one does), so at link it takes the size its constant indices imply: 1 for arrays indexed only
// through draw_tables, and every draw then indexes a one-element OpTypeArray past its end (out of bounds in SPIR-V 2.16:
// drivers may clamp or fold the index). An array of images or samplers in set 1 or 2 of length 1 is made run-time sized,
// with the capability and extension glslang adds for an unsized array that survives linking (RuntimeDescriptorArray,
// SPV_EXT_descriptor_indexing: core only in SPIR-V 1.5). The bound-table form (fixed tables of kBoundTableSlots) and
// modules with no such array are left as they are. The index stays dynamically uniform (one draw_tables entry per draw),
// so no NonUniform decoration is needed.
void make_bindless_arrays_runtime(std::vector<std::uint32_t>& spirv) {
    constexpr std::uint32_t kOpExtension = 10;
    constexpr std::uint32_t kOpCapability = 17;
    constexpr std::uint32_t kOpTypeImage = 25;
    constexpr std::uint32_t kOpTypeSampler = 26;
    constexpr std::uint32_t kOpTypeSampledImage = 27;
    constexpr std::uint32_t kOpTypeArray = 28;
    constexpr std::uint32_t kOpTypeRuntimeArray = 29;
    constexpr std::uint32_t kOpTypePointer = 32;
    constexpr std::uint32_t kOpConstant = 43;
    constexpr std::uint32_t kOpVariable = 59;
    constexpr std::uint32_t kOpDecorate = 71;
    constexpr std::uint32_t kDecorationDescriptorSet = 34;
    constexpr std::uint32_t kStorageUniformConstant = 0;
    constexpr std::uint32_t kCapabilityRuntimeDescriptorArray = 5302;
    constexpr std::uint32_t kImageSet = 1;    // kDrawBindings: t0 space1 and t0 space2
    constexpr std::uint32_t kSamplerSet = 2;  // kDrawBindings: s0 space1
    constexpr char kExtension[] = "SPV_EXT_descriptor_indexing";
    // The extension's name as literal string words (nul-terminated, padded with zeros).
    std::vector<std::uint32_t> extension_words((sizeof(kExtension) + 3u) / 4u, 0u);
    std::memcpy(extension_words.data(), kExtension, sizeof(kExtension));

    std::set<std::uint32_t> resource_types;
    std::map<std::uint32_t, std::size_t> array_at;      // OpTypeArray result -> its word
    std::map<std::uint32_t, std::uint32_t> pointee;     // UniformConstant pointer type -> pointee
    std::map<std::uint32_t, std::uint32_t> constant;    // 32-bit constant -> value
    std::map<std::uint32_t, std::uint32_t> set_of;      // variable -> descriptor set
    std::vector<std::pair<std::uint32_t, std::uint32_t>> variables;  // (pointer type, variable), UniformConstant
    bool has_capability = false;
    bool has_extension = false;
    for (std::size_t at = 5; at < spirv.size();) {
        const std::uint32_t count = spirv[at] >> 16u;
        const std::uint32_t op = spirv[at] & 0xffffu;
        if (count == 0u || at + count > spirv.size()) {
            return;  // malformed: the validator reports it
        }
        const std::uint32_t* words = spirv.data() + at;
        if (op == kOpCapability && count >= 2u && words[1] == kCapabilityRuntimeDescriptorArray) {
            has_capability = true;
        } else if (op == kOpExtension && count - 1u == extension_words.size() &&
                   std::equal(extension_words.begin(), extension_words.end(), words + 1)) {
            has_extension = true;
        } else if ((op == kOpTypeImage || op == kOpTypeSampler || op == kOpTypeSampledImage) && count >= 2u) {
            resource_types.insert(words[1]);
        } else if (op == kOpTypeArray && count == 4u) {
            array_at[words[1]] = at;
        } else if (op == kOpTypePointer && count == 4u && words[2] == kStorageUniformConstant) {
            pointee[words[1]] = words[3];
        } else if (op == kOpConstant && count == 4u) {
            constant[words[2]] = words[3];
        } else if (op == kOpVariable && count >= 4u && words[3] == kStorageUniformConstant) {
            variables.emplace_back(words[1], words[2]);
        } else if (op == kOpDecorate && count >= 4u && words[2] == kDecorationDescriptorSet) {
            set_of[words[1]] = words[3];
        }
        at += count;
    }
    std::set<std::size_t> rewrite;  // the words of the OpTypeArray instructions to make run-time sized
    for (const auto& [pointer, variable] : variables) {
        const auto set = set_of.find(variable);
        const auto type = pointee.find(pointer);
        if (set == set_of.end() || (set->second != kImageSet && set->second != kSamplerSet) || type == pointee.end()) {
            continue;
        }
        const auto array = array_at.find(type->second);
        if (array == array_at.end()) {
            continue;
        }
        const std::uint32_t* words = spirv.data() + array->second;
        const auto length = constant.find(words[3]);
        if (resource_types.count(words[2]) != 0u && length != constant.end() && length->second == 1u) {
            rewrite.insert(array->second);
        }
    }
    if (rewrite.empty()) {
        return;
    }
    std::vector<std::uint32_t> out(spirv.begin(), spirv.begin() + 5);
    out.reserve(spirv.size() + 2u + extension_words.size());
    // Logical layout (SPIR-V 2.4): every OpCapability, then every OpExtension, then the rest.
    bool capability_placed = has_capability;
    bool extension_placed = has_extension;
    for (std::size_t at = 5; at < spirv.size();) {
        const std::uint32_t count = spirv[at] >> 16u;
        const std::uint32_t op = spirv[at] & 0xffffu;
        if (!capability_placed && op != kOpCapability) {
            out.push_back((2u << 16u) | kOpCapability);
            out.push_back(kCapabilityRuntimeDescriptorArray);
            capability_placed = true;
        }
        if (!extension_placed && op != kOpCapability && op != kOpExtension) {
            out.push_back((static_cast<std::uint32_t>(extension_words.size() + 1u) << 16u) | kOpExtension);
            out.insert(out.end(), extension_words.begin(), extension_words.end());
            extension_placed = true;
        }
        if (rewrite.count(at) != 0u) {
            out.push_back((3u << 16u) | kOpTypeRuntimeArray);
            out.push_back(spirv[at + 1u]);  // result
            out.push_back(spirv[at + 2u]);  // element type
        } else {
            out.insert(out.end(), spirv.begin() + static_cast<std::ptrdiff_t>(at),
                       spirv.begin() + static_cast<std::ptrdiff_t>(at + count));
        }
        at += count;
    }
    spirv = std::move(out);
}

EShLanguage language(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex: return EShLangVertex;
    case ShaderStage::Geometry: return EShLangGeometry;
    case ShaderStage::Fragment: return EShLangFragment;
    case ShaderStage::Compute: return EShLangCompute;
    }
    return EShLangVertex;
}

}  // namespace

std::span<const RegisterBinding> draw_bindings() noexcept {
    return kDrawBindings;
}

std::string annotate(std::string_view hlsl, std::span<const RegisterBinding> bindings) {
    // Each resource declaration names its register as ": register(<reg>)"; the attribute goes at the start of that
    // declaration (after the previous ';', '{', '}' or line break and any blanks). Several declarations may share a
    // line.
    std::string out;
    out.reserve(hlsl.size() + 256u);
    std::size_t copied = 0;  // hlsl[0, copied) is in `out`
    for (std::size_t reg_at = hlsl.find("register("); reg_at != std::string_view::npos;
         reg_at = hlsl.find("register(", reg_at + 9u)) {
        const std::size_t open = reg_at + 9u;
        const std::size_t close = hlsl.find(')', open);
        if (close == std::string_view::npos) {
            break;
        }
        // Normalise the spacing around a comma ("t0,space1" -> "t0, space1").
        std::string reg;
        for (const char c : hlsl.substr(open, close - open)) {
            if (c == ' ' || c == '\t') {
                continue;
            }
            reg += c;
            if (c == ',') {
                reg += ' ';
            }
        }
        const RegisterBinding* mapped = nullptr;
        for (const RegisterBinding& binding : bindings) {
            if (reg == binding.reg) {
                mapped = &binding;
                break;
            }
        }
        if (mapped == nullptr) {
            continue;
        }
        std::size_t start = hlsl.find_last_of(";{}\n", reg_at);
        start = start == std::string_view::npos ? 0u : start + 1u;
        start = hlsl.find_first_not_of(" \t\r", start);
        out.append(hlsl.substr(copied, start - copied));
        if (mapped->push_constant) {
            // A push-constant block takes no binding: the register goes too, or glslang decorates one.
            const std::size_t colon = hlsl.rfind(':', reg_at);
            const std::size_t cut = colon != std::string_view::npos && colon >= start ? colon : reg_at;
            out += "[[vk::push_constant]] ";
            out.append(hlsl.substr(start, cut - start));
            copied = close + 1u;
            continue;
        }
        out += "[[vk::binding(" + std::to_string(mapped->binding) + ", " + std::to_string(mapped->set) + ")]] ";
        copied = start;
    }
    out.append(hlsl.substr(copied));
    return out;
}

std::vector<std::uint32_t> compile_hlsl(std::string_view hlsl, ShaderStage stage, const char* entry,
                                        std::initializer_list<const char*> defines, std::string& error,
                                        CompileOptions compile_options) {
    initialize_glslang();
    const EShLanguage lang = language(stage);
    glslang::TShader shader(lang);
    const std::string source(hlsl);
    const char* strings[] = {source.c_str()};
    const int lengths[] = {static_cast<int>(source.size())};
    shader.setStringsWithLengths(strings, lengths, 1);
    shader.setEntryPoint(entry);
    shader.setSourceEntryPoint(entry);
    // Constant-buffer matrices are row-major, as the D3D12 compile's D3DCOMPILE_PACK_MATRIX_ROW_MAJOR: the shared code
    // writes them row by row and the shaders multiply mul(matrix, vector).
    std::string preamble = "#define NEBULA_VULKAN 1\n#pragma pack_matrix(row_major)\n";
    if (compile_options.bound_tables) {
        // A device without descriptor indexing: each draw binds its own table of maps (program.hlsli); else the arrays are
        // runtime-sized (OpTypeRuntimeArray, capability RuntimeDescriptorArray).
        preamble += "#define NEBULA_BOUND_TABLES 1\n#define NEBULA_TABLE_SLOTS " + std::to_string(kBoundTableSlots) + "\n";
    }
    for (const char* define : defines) {
        const char* equals = std::strchr(define, '=');
        if (equals == nullptr) {
            preamble += std::string("#define ") + define + "\n";
        } else {
            preamble += "#define " + std::string(define, equals) + " " + std::string(equals + 1) + "\n";
        }
    }
    shader.setPreamble(preamble.c_str());
    shader.setEnvInput(glslang::EShSourceHlsl, lang, glslang::EShClientVulkan, 100);
    // Vulkan 1.0's environment, SPIR-V 1.0: the one every Vulkan device takes (a 1.0 device accepts nothing later), so the
    // modules built ahead of time serve them all. Storage buffers come out as Uniform + BufferBlock rather than the
    // StorageBuffer class; the operations are the same.
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    // Semantics on stage variables (assign_semantic_locations), removed again before the module is used; a compute
    // shader has none, and would keep the extension.
    if (stage != ShaderStage::Compute) {
        shader.setEnvTargetHlslFunctionality1();
    }
    // Every resource carries an explicit [[vk::binding]] (annotate()); stage inputs and outputs take provisional
    // locations, replaced by their semantics' after compiling.
    shader.setAutoMapLocations(true);
    // SV_Position.w is w in a pixel shader, as on D3D12; SPIR-V's FragCoord.w is 1/w (fog reads it).
    shader.setDxPositionW(true);
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl |
                                                   EShMsgHlslOffsets | EShMsgHlslLegalization);
    if (!shader.parse(GetDefaultResources(), 100, false, messages)) {
        error = shader.getInfoLog();
        return {};
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages) || !program.mapIO()) {
        error = program.getInfoLog();
        return {};
    }
    // The optimizer pass list legalizes HLSL's SPIR-V for Vulkan (it is required, not only a speed-up).
    std::vector<std::uint32_t> spirv;
    glslang::SpvOptions options;
    options.disableOptimizer = false;
    spv::SpvBuildLogger logger;
    const glslang::TIntermediate& intermediate = *program.getIntermediate(lang);
    glslang::GlslangToSpv(intermediate, spirv, &logger, &options);
    if (spirv.empty()) {
        error = "glslang produced no SPIR-V";
        return {};
    }
    // A compute shader has no stage inputs or outputs but built-ins.
    if (stage != ShaderStage::Compute && !assign_semantic_locations(spirv, stage, error)) {
        return {};
    }
    if (!compile_options.bound_tables) {
        make_bindless_arrays_runtime(spirv);
    }
    if (!compile_options.keep_names) {
        strip_debug_names(spirv);
    }
    std::string messages_out = logger.getAllMessages();
    if (compile_options.validate) {
        // The module as a Vulkan 1.0 device takes it (later versions accept every 1.0 module).
        spv_context context = spvContextCreate(SPV_ENV_VULKAN_1_0);
        spv_diagnostic diagnostic = nullptr;
        if (spvValidateBinary(context, spirv.data(), spirv.size(), &diagnostic) != SPV_SUCCESS) {
            messages_out += diagnostic != nullptr && diagnostic->error != nullptr ? diagnostic->error
                                                                                  : "SPIR-V validation failed";
        }
        spvDiagnosticDestroy(diagnostic);
        spvContextDestroy(context);
        if (!messages_out.empty()) {
            error = std::move(messages_out);
            return {};
        }
    }
    return spirv;
}

}  // namespace galaxy::render::vk
