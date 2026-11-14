// SPDX-License-Identifier: GPL-3.0-only
// One stage of a mod's program as DXBC (shader model 5.0) for the Direct3D 11 renderer, with the HLSL compiler every
// Windows has in its system folder (d3dcompiler_47.dll): the one fxc runs, so a mod's programs are compiled as
// compile_programs.py dxbc compiles the game's (/O3, debug data and reflection stripped, the bound-table form).
#include "galaxy/render/d3d11/d3d11_context.h"
#include "galaxy/render/mod_post.h"
#include "galaxy/render/mod_shaders.h"

#include "galaxy/windows_api.h"

#include <d3dcompiler.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace galaxy::render {
namespace {

namespace fs = std::filesystem;

// What compile_programs.py's DXBC_TABLE defines on both stages: 16 maps to a table (d3d11::kBoundTableSlots).
static_assert(d3d11::kBoundTableSlots == 16u);

using StripShader = HRESULT(WINAPI*)(LPCVOID, SIZE_T, UINT, ID3DBlob**);

// The compiler, loaded once from the system folder only (never one beside the game or in the working folder).
struct Compiler {
    HMODULE module = nullptr;
    pD3DCompile compile = nullptr;
    StripShader strip = nullptr;
    DWORD error = 0u;
    std::uint32_t revision = 0u;  // its file version, folded
};

// The file version of a loaded module: its VS_FIXEDFILEINFO, found in its version resource by its signature.
std::uint64_t file_version(HMODULE module) {
    HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));  // VS_VERSION_INFO, RT_VERSION
    HGLOBAL loaded = found != nullptr ? LoadResource(module, found) : nullptr;
    const auto* bytes = loaded != nullptr ? static_cast<const std::uint8_t*>(LockResource(loaded)) : nullptr;
    const DWORD size = found != nullptr ? SizeofResource(module, found) : 0u;
    if (bytes == nullptr) {
        return 0u;
    }
    for (DWORD at = 0u; at + sizeof(VS_FIXEDFILEINFO) <= size; at += 4u) {
        VS_FIXEDFILEINFO info{};
        std::memcpy(&info, bytes + at, sizeof(info));
        if (info.dwSignature == 0xFEEF04BDu) {
            return std::uint64_t{info.dwFileVersionMS} << 32u | info.dwFileVersionLS;
        }
    }
    return 0u;
}

const Compiler& compiler() {
    static const Compiler loaded = [] {
        Compiler c;
        c.module = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (c.module == nullptr) {
            c.error = GetLastError();
            return c;
        }
        c.compile = reinterpret_cast<pD3DCompile>(reinterpret_cast<void*>(GetProcAddress(c.module, "D3DCompile")));
        c.strip = reinterpret_cast<StripShader>(reinterpret_cast<void*>(GetProcAddress(c.module, "D3DStripShader")));
        if (c.compile == nullptr || c.strip == nullptr) {
            c.error = GetLastError();
            c.compile = nullptr;
            c.strip = nullptr;
            return c;
        }
        // FNV-1a over the eight bytes of the version: a new compiler builds the mods' programs again.
        const std::uint64_t version = file_version(c.module);
        std::uint32_t hash = 2166136261u;
        for (unsigned i = 0; i < 8u; ++i) {
            hash = (hash ^ static_cast<std::uint32_t>((version >> (8u * i)) & 0xFFu)) * 16777619u;
        }
        c.revision = hash;
        return c;
    }();
    return loaded;
}

// The headers a program includes, from `folder` and below only (the cook writes them there): the including file's
// folder first, as fxc looks.
class FolderIncludes final : public ID3DInclude {
public:
    explicit FolderIncludes(fs::path folder) : root_(fs::weakly_canonical(folder)) {}

    HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* bytes) override {
        const fs::path wanted(reinterpret_cast<const char8_t*>(name));
        if (wanted.empty() || wanted.is_absolute() || wanted.has_root_name()) {
            return E_FAIL;
        }
        fs::path from = root_;
        for (const Opened& opened : opened_) {
            if (opened.text.data() == parent) {
                from = opened.folder;
            }
        }
        for (const fs::path& base : {from, root_}) {
            std::error_code error;
            const fs::path file = fs::weakly_canonical(base / wanted, error);
            if (error || !inside(file)) {
                continue;
            }
            std::ifstream in(file, std::ios::binary);
            if (!in) {
                continue;
            }
            auto opened = std::make_unique<Opened>();
            opened->text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            opened->folder = file.parent_path();
            *data = opened->text.data();
            *bytes = static_cast<UINT>(opened->text.size());
            opened_.push_back(std::move(*opened));
            // The vector moved the string: the pointer handed out is the one stored now.
            *data = opened_.back().text.data();
            return S_OK;
        }
        return E_FAIL;
    }
    HRESULT __stdcall Close(LPCVOID) override { return S_OK; }

private:
    struct Opened {
        std::string text;
        fs::path folder;
    };
    [[nodiscard]] bool inside(const fs::path& file) const {
        const fs::path relative = file.lexically_relative(root_);
        return !relative.empty() && *relative.begin() != "..";
    }
    fs::path root_;
    std::vector<Opened> opened_;
};

// The compiler's report, its first 4096 characters.
std::string said(ID3DBlob* messages) {
    if (messages == nullptr || messages->GetBufferSize() == 0u) {
        return {};
    }
    std::string text(static_cast<const char*>(messages->GetBufferPointer()), messages->GetBufferSize());
    while (!text.empty() && (text.back() == '\0' || text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text.substr(0u, 4096u);
}

std::vector<std::uint8_t> compile_stage(const std::string& source, const fs::path& includes, bool pixel,
                                        const std::vector<std::string>& defines, std::string& problem) {
    const Compiler& c = compiler();
    if (c.compile == nullptr) {
        problem = "the system's shader compiler (d3dcompiler_47.dll) could not be loaded (error " + std::to_string(c.error) + ")";
        return {};
    }
    std::vector<D3D_SHADER_MACRO> macros;
    for (const std::string& define : defines) {
        const std::size_t equals = define.find('=');
        macros.push_back({define.c_str(), equals == std::string::npos ? "1" : define.c_str() + equals + 1u});
    }
    // The names point into `defines`; a NAME=VALUE one is cut at its '=' in a copy.
    std::vector<std::string> names;
    names.reserve(defines.size());
    for (std::size_t i = 0; i < defines.size(); ++i) {
        const std::size_t equals = defines[i].find('=');
        if (equals != std::string::npos) {
            names.push_back(defines[i].substr(0u, equals));
            macros[i].Name = names.back().c_str();
        }
    }
    macros.push_back({nullptr, nullptr});
    FolderIncludes headers(includes);
    // The name the compiler's messages give the text (its includes are found by FolderIncludes, not by it).
    const std::string name = pixel ? "program_ps.hlsl" : "program_vs.hlsl";
    ID3DBlob* code = nullptr;
    ID3DBlob* messages = nullptr;
    const HRESULT result = c.compile(source.data(), source.size(), name.c_str(), macros.data(), &headers,
                                     pixel ? "program_ps" : "program_vs", pixel ? "ps_5_0" : "vs_5_0",
                                     D3DCOMPILE_OPTIMIZATION_LEVEL3, 0u, &code, &messages);
    std::vector<std::uint8_t> bytes;
    if (SUCCEEDED(result) && code != nullptr) {
        ID3DBlob* stripped = nullptr;
        if (SUCCEEDED(c.strip(code->GetBufferPointer(), code->GetBufferSize(),
                              D3DCOMPILER_STRIP_REFLECTION_DATA | D3DCOMPILER_STRIP_DEBUG_INFO, &stripped)) &&
            stripped != nullptr) {
            const auto* at = static_cast<const std::uint8_t*>(stripped->GetBufferPointer());
            bytes.assign(at, at + stripped->GetBufferSize());
            stripped->Release();
        } else {
            problem = "a program compiled but could not be stripped";
        }
    } else {
        problem = "a program didn't compile";
        if (const std::string text = said(messages); !text.empty()) {
            problem += ":\n" + text;
        }
    }
    if (code != nullptr) {
        code->Release();
    }
    if (messages != nullptr) {
        messages->Release();
    }
    return bytes;
}

}  // namespace

std::uint32_t mod_shader_compiler_revision() noexcept {
    return compiler().revision;
}

bool mod_shader_has_bound_form() noexcept {
    return true;
}

std::string mod_shader_compiler_missing(const std::filesystem::path&) {
    const Compiler& c = compiler();
    if (c.compile == nullptr) {
        return "d3dcompiler_47.dll (D3DCompile, D3DStripShader) did not load from the system folder (error " +
               std::to_string(c.error) + ")";
    }
    return {};
}

std::vector<std::uint8_t> compile_mod_shader_stage(const std::string& source, const std::filesystem::path& includes, bool pixel,
                                                   const std::vector<const char*>& defines, std::string& problem, bool) {
    // Both stages declare the tables (program.hlsli), as the game's own DXBC programs.
    std::vector<std::string> all(defines.begin(), defines.end());
    all.emplace_back("NEBULA_BOUND_TABLES");
    all.emplace_back("NEBULA_TABLE_SLOTS=16");
    return compile_stage(source, includes, pixel, all, problem);
}

std::vector<std::uint8_t> compile_mod_post_stage(const std::string& source, const std::filesystem::path& work, bool pixel,
                                                 std::string& problem) {
    // The prelude declares its own bindings (mod_post.h); the entries are program_vs and program_ps.
    return compile_stage(source, work, pixel, {}, problem);
}

}  // namespace galaxy::render
