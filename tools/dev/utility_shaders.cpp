// Build tool: compiles the renderer's utility shaders (runtime/src/render/utility_shader_table.inc) into a header of
// bytecode arrays, with the flags the renderer used at run time: the same compiler, so the same bytes.
// utility_shaders <output header> [sm50]: sm50 compiles them for shader model 5.0 (the Direct3D 11 renderer) and adds the
// depth clear's pixel shader (ClearDepthPs), which Direct3D 11 needs for a rectangle of depth.

#include "galaxy/render/renderer_common.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <string_view>

using namespace galaxy::render;

namespace {

bool emit(std::FILE* out, const char* name, std::string_view source, const char* target, const char* entry) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, entry, target,
                                  D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        std::fprintf(stderr, "%s (%s %s): %s\n", name, target, entry,
                     errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no error text");
        return false;
    }
    std::fprintf(out, "static const unsigned char g_utility_%s[] = {", name);
    const auto* bytes = static_cast<const unsigned char*>(code->GetBufferPointer());
    for (std::size_t i = 0; i < code->GetBufferSize(); ++i) {
        std::fprintf(out, "%s%u,", i % 32u == 0u ? "\n" : "", bytes[i]);
    }
    std::fprintf(out, "\n};\n");
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const bool sm50 = argc == 3 && std::string_view(argv[2]) == "sm50";
    if (argc != 2 && !sm50) {
        return 2;
    }
    std::FILE* out = std::fopen(argv[1], "wb");
    if (out == nullptr) {
        return 2;
    }
    // vs_5_1 and the like become vs_5_0: the shaders use no register spaces or 5.1 features.
    const auto profile = [sm50](const char* target) {
        std::string named(target);
        if (sm50 && named.size() > 4u && named.compare(named.size() - 4u, 4u, "_5_1") == 0) {
            named.back() = '0';
        }
        return named;
    };
    bool ok = true;
#define X(name, source, target, entry) ok = emit(out, #name, source, profile(target).c_str(), entry) && ok;
#include "../../runtime/src/render/utility_shader_table.inc"
#undef X
    if (sm50) {
        ok = emit(out, "ClearDepthPs", clear_depth_pixel_shader_source(), "ps_5_0", "main") && ok;
    }
    std::fclose(out);
    if (!ok) {
        std::remove(argv[1]);
    }
    return ok ? 0 : 1;
}
