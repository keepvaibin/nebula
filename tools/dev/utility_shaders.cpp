// Build tool: compiles the renderer's utility shaders (runtime/src/render/utility_shader_table.inc) into a header of
// bytecode arrays, with the flags the renderer used at run time: the same compiler, so the same bytes.
// utility_shaders <output header>

#include "galaxy/render/curved_surfaces.h"
#include "galaxy/render/interp_eval.h"
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
    if (argc != 2) {
        return 2;
    }
    std::FILE* out = std::fopen(argv[1], "wb");
    if (out == nullptr) {
        return 2;
    }
    bool ok = true;
#define X(name, source, target, entry) ok = emit(out, #name, source, target, entry) && ok;
#include "../../runtime/src/render/utility_shader_table.inc"
#undef X
    std::fclose(out);
    if (!ok) {
        std::remove(argv[1]);
    }
    return ok ? 0 : 1;
}
