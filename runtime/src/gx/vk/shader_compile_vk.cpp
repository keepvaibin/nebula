// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/vk/shader_compile_vk.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <spirv-tools/libspirv.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace galaxy::gx::vk {

namespace {

constexpr RegisterBinding kGxDrawBindings[] = {
    {"b0", 0, 0},
    {"b1", 0, 1},
    {"t0, space1", 0, 2},
    {"t0", 1, 0},
    {"s0", 2, 0},
};

void initialize_glslang() {
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
}

// SPIR-V's words for the dual-source rewrite.
constexpr std::uint32_t kOpVariable = 59;
constexpr std::uint32_t kOpDecorate = 71;
constexpr std::uint32_t kDecorationLocation = 30;
constexpr std::uint32_t kDecorationIndex = 32;
constexpr std::uint32_t kStorageOutput = 3;

// The output at location 1 moves to location 0, index 1. False when the module has none.
bool make_dual_source_output(std::vector<std::uint32_t>& spirv) {
    std::vector<std::uint32_t> outputs;
    for (std::size_t at = 5; at < spirv.size();) {
        const std::uint32_t count = spirv[at] >> 16u;
        if (count == 0u || at + count > spirv.size()) {
            return false;
        }
        if ((spirv[at] & 0xffffu) == kOpVariable && count >= 4u && spirv[at + 3] == kStorageOutput) {
            outputs.push_back(spirv[at + 2]);
        }
        at += count;
    }
    std::vector<std::uint32_t> out(spirv.begin(), spirv.begin() + 5);
    out.reserve(spirv.size() + 4u);
    bool moved = false;
    for (std::size_t at = 5; at < spirv.size();) {
        const std::uint32_t count = spirv[at] >> 16u;
        const bool second_target = (spirv[at] & 0xffffu) == kOpDecorate && count == 4u &&
            spirv[at + 2] == kDecorationLocation && spirv[at + 3] == 1u &&
            std::find(outputs.begin(), outputs.end(), spirv[at + 1]) != outputs.end();
        out.insert(out.end(), spirv.begin() + static_cast<std::ptrdiff_t>(at),
                   spirv.begin() + static_cast<std::ptrdiff_t>(at + count));
        if (second_target) {
            out[out.size() - 1u] = 0u;
            out.insert(out.end(), {(4u << 16u) | kOpDecorate, spirv[at + 1], kDecorationIndex, 1u});
            moved = true;
        }
        at += count;
    }
    if (moved) {
        spirv = std::move(out);
    }
    return moved;
}

EShLanguage language(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex: return EShLangVertex;
    case ShaderStage::Geometry: return EShLangGeometry;
    case ShaderStage::Fragment: return EShLangFragment;
    }
    return EShLangVertex;
}

}  // namespace

std::span<const RegisterBinding> gx_draw_bindings() noexcept {
    return kGxDrawBindings;
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
    std::string preamble = "#define NEBULA_VULKAN 1\n";
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
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    // Every resource carries an explicit [[vk::binding]] (annotate()); stage inputs and outputs take locations in
    // declaration order.
    shader.setAutoMapLocations(true);
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
    if (compile_options.dual_source && !make_dual_source_output(spirv)) {
        error = "dual-source pixel shader without SV_Target1";
        return {};
    }
    std::string messages_out = logger.getAllMessages();
    if (compile_options.validate) {
        // The module as it is used (after the dual-source rewrite), for a Vulkan 1.1 device.
        spv_context context = spvContextCreate(SPV_ENV_VULKAN_1_1);
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

}  // namespace galaxy::gx::vk
