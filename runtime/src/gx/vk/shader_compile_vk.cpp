// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/vk/shader_compile_vk.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

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
    // A resource declaration is one line ending in ": register(<reg>)" (then ";" or " {"): the attribute goes before
    // the declaration, after the line's indentation.
    std::string out;
    out.reserve(hlsl.size() + 256u);
    std::size_t pos = 0;
    while (pos < hlsl.size()) {
        std::size_t end = hlsl.find('\n', pos);
        end = end == std::string_view::npos ? hlsl.size() : end + 1u;
        const std::string_view line = hlsl.substr(pos, end - pos);
        pos = end;
        const std::size_t reg_at = line.find("register(");
        const RegisterBinding* mapped = nullptr;
        if (reg_at != std::string_view::npos) {
            const std::size_t open = reg_at + 9u;
            const std::size_t close = line.find(')', open);
            if (close != std::string_view::npos) {
                // Normalise the spacing around a comma ("t0,space1" -> "t0, space1").
                std::string reg;
                for (const char c : line.substr(open, close - open)) {
                    if (c == ' ' || c == '\t') {
                        continue;
                    }
                    reg += c;
                    if (c == ',') {
                        reg += ' ';
                    }
                }
                for (const RegisterBinding& binding : bindings) {
                    if (reg == binding.reg) {
                        mapped = &binding;
                        break;
                    }
                }
            }
        }
        if (mapped == nullptr) {
            out += line;
            continue;
        }
        const std::size_t indent = line.find_first_not_of(" \t");
        out += line.substr(0, indent);
        if (mapped->push_constant) {
            // A push-constant block takes no binding: the register goes too, or glslang decorates one.
            const std::size_t colon = line.rfind(':', reg_at);
            const std::size_t cut = colon != std::string_view::npos && colon >= indent ? colon : reg_at;
            const std::size_t close = line.find(')', reg_at);
            out += "[[vk::push_constant]] ";
            out += line.substr(indent, cut - indent);
            out += line.substr(close + 1u);
            continue;
        }
        out += "[[vk::binding(" + std::to_string(mapped->binding) + ", " + std::to_string(mapped->set) + ")]] ";
        out += line.substr(indent);
    }
    return out;
}

std::vector<std::uint32_t> compile_hlsl(std::string_view hlsl, ShaderStage stage, const char* entry,
                                        std::initializer_list<const char*> defines, std::string& error,
                                        bool validate) {
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
    options.validate = validate;
    spv::SpvBuildLogger logger;
    glslang::GlslangToSpv(*program.getIntermediate(lang), spirv, &logger, &options);
    std::string messages_out = logger.getAllMessages();
    if (spirv.empty() || (validate && !messages_out.empty())) {
        error = messages_out.empty() ? std::string("glslang produced no SPIR-V") : std::move(messages_out);
        return {};
    }
    return spirv;
}

}  // namespace galaxy::gx::vk
