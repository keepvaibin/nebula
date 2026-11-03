// The utility shaders' bytecode, generated at build (tools/dev/utility_shaders.cpp).

#include "galaxy/render/utility_shaders.h"

#include "utility_shaders_dxbc.h"

namespace galaxy::render {

UtilityShaderBytes utility_shader(UtilityShader shader) noexcept {
    switch (shader) {
#define X(name, source, target, entry) \
    case UtilityShader::name:          \
        return {g_utility_##name, sizeof(g_utility_##name)};
#include "utility_shader_table.inc"
#undef X
    }
    return {};
}

}  // namespace galaxy::render
