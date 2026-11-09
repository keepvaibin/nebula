// SPDX-License-Identifier: GPL-3.0-only
// One stage of a mod's program as DXIL, with Microsoft's compiler where the game's installer puts it (dxc\ beside the game).
#include "galaxy/render/mod_shaders.h"

#include "galaxy/windows_api.h"

#include <atomic>
#include <fstream>
#include <iterator>
#include <string>

namespace galaxy::render {

std::uint32_t mod_shader_compiler_revision() noexcept {
    return 0u;
}

bool mod_shader_has_bound_form() noexcept {
    return false;
}

std::vector<std::uint8_t> compile_mod_shader_stage(const std::string& source, const std::filesystem::path& includes, bool pixel,
                                                   const std::vector<const char*>& defines, std::string& problem, bool) {
    namespace fs = std::filesystem;
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const fs::path compiler = fs::path(exe).parent_path() / "dxc" / "dxc.exe";
    std::error_code error;
    if (!fs::is_regular_file(compiler, error)) {
        problem = "the shader compiler isn't installed";
        return {};
    }
    static std::atomic<unsigned> next{0u};
    const fs::path stem = includes / ("build" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(next.fetch_add(1u)));
    fs::path input = stem;
    input += ".hlsl";
    fs::path output = stem;
    output += ".dxil";
    {
        std::ofstream out(input, std::ios::binary);
        out.write(source.data(), static_cast<std::streamsize>(source.size()));
    }
    std::wstring command = L"\"" + compiler.wstring() + L"\" -nologo -T " + (pixel ? L"ps_6_0 -E program_ps" : L"vs_6_0 -E program_vs") +
                           L" -O3 -WX -Qstrip_debug -Qstrip_reflect";
    for (const char* define : defines) {
        command += L" -D";
        for (const char* c = define; *c != 0; ++c) command += static_cast<wchar_t>(*c);
        command += L"=1";
    }
    command += L" -I \"" + includes.wstring() + L"\" -Fo \"" + output.wstring() + L"\" \"" + input.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    DWORD exit_code = 1;
    if (CreateProcessW(compiler.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        WaitForSingleObject(process.hProcess, INFINITE);
        GetExitCodeProcess(process.hProcess, &exit_code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    std::vector<std::uint8_t> bytes;
    if (exit_code == 0u) {
        std::ifstream in(output, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    } else {
        problem = "a program didn't compile";
    }
    fs::remove(input, error);
    fs::remove(output, error);
    return bytes;
}

}  // namespace galaxy::render
