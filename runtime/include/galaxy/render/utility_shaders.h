#pragma once

// The renderer's utility shaders, compiled at build (E-19; the table: render/utility_shader_table.inc).

#include <cstddef>

namespace galaxy::render {

enum class UtilityShader {
#define X(name, source, target, entry) name,
#include "../../../src/render/utility_shader_table.inc"
#undef X
};

// A shader's bytecode, in the program for its whole life.
struct UtilityShaderBytes {
    const void* bytes = nullptr;
    std::size_t size = 0;

    [[nodiscard]] const void* GetBufferPointer() const noexcept { return bytes; }
    [[nodiscard]] std::size_t GetBufferSize() const noexcept { return size; }
    [[nodiscard]] const UtilityShaderBytes* operator->() const noexcept { return this; }
};

[[nodiscard]] UtilityShaderBytes utility_shader(UtilityShader shader) noexcept;

}  // namespace galaxy::render
