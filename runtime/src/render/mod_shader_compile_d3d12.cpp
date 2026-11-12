// SPDX-License-Identifier: GPL-3.0-only
// One stage of a mod's program as DXIL, with Microsoft's compiler where the game's installer puts it (dxc\ beside the game).
#include "galaxy/render/mod_shaders.h"

#include "galaxy/windows_api.h"
#include "galaxy/windows_paths.h"

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

std::string mod_shader_compiler_missing(const std::filesystem::path& cache_folder) {
    namespace fs = std::filesystem;
    const fs::path folder = fs::path(windows::executable_path()).parent_path() / "dxc";
    // What the installer puts there (installer InstallEngine.cs): the compiler and the two libraries it loads.
    std::string stamp;
    for (const char* name : {"dxc.exe", "dxcompiler.dll", "dxil.dll"}) {
        std::error_code error;
        const fs::path file = folder / name;
        const std::uintmax_t size = fs::file_size(file, error);
        if (error) {
            return std::string("dxc\\") + name + " is not beside the game (" + error.message() + ")";
        }
        const fs::file_time_type time = fs::last_write_time(file, error);
        stamp += std::string(name) + " " + std::to_string(size) + " " +
                 std::to_string(error ? 0 : static_cast<long long>(time.time_since_epoch().count())) + "\n";
    }
    const fs::path cache = cache_folder / "compiler.checked";
    {
        std::ifstream in(cache, std::ios::binary);
        const std::string kept((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (kept == stamp) {
            return {};
        }
    }
    const fs::path compiler = folder / "dxc.exe";
    std::wstring command = L"\"" + compiler.wstring() + L"\" --version";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(compiler.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &process)) {
        return "dxc\\dxc.exe did not start (error " + std::to_string(GetLastError()) + ")";
    }
    constexpr DWORD kVersionMs = 30000u;
    const bool ended = WaitForSingleObject(process.hProcess, kVersionMs) == WAIT_OBJECT_0;
    DWORD exit_code = 1;
    if (ended) {
        GetExitCodeProcess(process.hProcess, &exit_code);
    } else {
        TerminateProcess(process.hProcess, 1u);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!ended) {
        return "dxc\\dxc.exe --version did not end in 30 s";
    }
    if (exit_code != 0u) {
        return "dxc\\dxc.exe --version ended with " + std::to_string(exit_code);
    }
    std::error_code error;
    fs::create_directories(cache_folder, error);
    std::ofstream out(cache, std::ios::binary | std::ios::trunc);
    out.write(stamp.data(), static_cast<std::streamsize>(stamp.size()));
    return {};
}

std::vector<std::uint8_t> compile_mod_shader_stage(const std::string& source, const std::filesystem::path& includes, bool pixel,
                                                   const std::vector<const char*>& defines, std::string& problem, bool) {
    namespace fs = std::filesystem;
    const fs::path compiler = fs::path(windows::executable_path()).parent_path() / "dxc" / "dxc.exe";
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
    bool timed_out = false;
    if (CreateProcessW(compiler.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        // A compiler that hangs ends here, not the conversion with it.
        constexpr DWORD kCompileMs = 120000u;
        if (WaitForSingleObject(process.hProcess, kCompileMs) == WAIT_OBJECT_0) {
            GetExitCodeProcess(process.hProcess, &exit_code);
        } else {
            timed_out = true;
            TerminateProcess(process.hProcess, 1u);
            WaitForSingleObject(process.hProcess, 5000u);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    std::vector<std::uint8_t> bytes;
    if (exit_code == 0u && !timed_out) {
        std::ifstream in(output, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    } else {
        problem = timed_out ? "a program took too long to compile" : "a program didn't compile";
    }
    fs::remove(input, error);
    fs::remove(output, error);
    return bytes;
}

}  // namespace galaxy::render
