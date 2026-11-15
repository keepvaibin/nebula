// The install root's Nebula.exe: what the Start menu, the desktop, .luma files and nebula: links open. It starts the
// current version's game with the install's game and save folders, passes its own arguments on (--import FILE and so
// on) and ends. Without a whole install it opens Nebula Setup, which repairs it.
//
//   <root>\Nebula.exe [game arguments]
//   -> <root>\versions\<version>\Nebula.exe --pak <content>\game.pak --saves <data>\saves [--data <root>\data] [game arguments]
#include "text.h"

#pragma comment(lib, "user32.lib")

namespace nebula::start {

namespace {

// The program for this CPU: Nebula.exe needs AVX2; the x86-64-v2 build (SSE4.2) runs everywhere else. Starting that one
// directly spares the game a second process.
wstring GameProgram(const wstring& versionDir) {
    constexpr DWORD kAvx2 = 40;  // PF_AVX2_INSTRUCTIONS_AVAILABLE
    wstring v2 = PathCombine(versionDir, L"Nebula-x86-64-v2.exe");
    if (!IsProcessorFeaturePresent(kAvx2) && FileExists(v2)) return v2;
    return PathCombine(versionDir, L"Nebula.exe");
}

// The content folder: the install's, or one the 2.0 betas' launcher was pointed at that still holds a game.
wstring ContentFolder(const InstallLayout& layout) {
    try {
        if (FileExists(paths::SettingsFile())) {
            Json settings = LoadJson(paths::SettingsFile());
            if (auto chosen = JStr(&settings, L"content"); chosen && !chosen->empty() && FileExists(PathCombine(*chosen, L"game.pak"))) return *chosen;
        }
    } catch (const std::exception&) {
    }
    return layout.Content();
}

std::optional<wstring> CurrentVersionDir(const InstallLayout& layout) {
    try {
        Json current = LoadJson(layout.Current());
        wstring version = JStr(&current, L"version").value_or(L"");
        if (version.empty() || version.find_first_of(L"\\/:") != wstring::npos || version == L"." || version == L"..") return std::nullopt;
        wstring dir = layout.VersionDir(version);
        return FileExists(PathCombine(dir, L"Nebula.exe")) ? std::optional<wstring>(dir) : std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool Start(const wstring& program, const wstring& arguments, const wstring& directory) {
    wstring commandLine = QuoteArg(program) + (arguments.empty() ? L"" : L" " + arguments);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    // The window state this program was started with goes to the game: a shortcut's Run setting (minimized,
    // maximized), or a tool's hidden start, which the game keeps off the player's data (hidden_start.h).
    STARTUPINFOW own{};
    own.cb = sizeof(own);
    GetStartupInfoW(&own);
    if ((own.dwFlags & STARTF_USESHOWWINDOW) != 0) {
        startup.dwFlags |= STARTF_USESHOWWINDOW;
        startup.wShowWindow = own.wShowWindow;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup, &process)) return false;
    // The game takes the foreground this program was given.
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

int Main() {
    const InstallLayout layout(ModuleDirectory());
    const bool portable = paths::IsPortableRoot(layout.root);
    if (portable) paths::UsePortableRoot(layout.root);
    std::vector<wstring> passed = CommandLineArgs();

    auto versionDir = CurrentVersionDir(layout);
    if (!versionDir) {
        // Not a whole install: its Setup repairs it.
        if (FileExists(layout.SetupCopy()) && Start(layout.SetupCopy(), L"", layout.root)) return 0;
        MessageBoxW(nullptr, text::Of(L"Nebula is not installed in this folder. Run Nebula Setup to install it.").c_str(), L"Nebula", MB_OK | MB_ICONERROR);
        return 1;
    }
    try {
        CreateDirs(paths::Saves());
    } catch (const std::exception&) {
        // the game says what is wrong with the folder
    }
    std::vector<wstring> arguments = {L"--pak", PathCombine(ContentFolder(layout), L"game.pak"), L"--saves", paths::Saves()};
    if (portable) {
        arguments.push_back(L"--data");
        arguments.push_back(paths::DataRoot());
    }
    arguments.insert(arguments.end(), passed.begin(), passed.end());
    const wstring program = GameProgram(*versionDir);
    if (Start(program, JoinArgs(arguments), *versionDir)) return 0;
    const DWORD error = GetLastError();
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
                   reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    wstring message = program + L"\r\n\r\n" + (text ? wstring(text) : L"Error " + Num(static_cast<long long>(error)));
    if (text) LocalFree(text);
    MessageBoxW(nullptr, message.c_str(), L"Nebula", MB_OK | MB_ICONERROR);
    return 1;
}

}  // namespace

}  // namespace nebula::start

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return nebula::start::Main(); }
