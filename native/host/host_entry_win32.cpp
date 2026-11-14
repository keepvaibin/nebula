// SPDX-License-Identifier: GPL-3.0-only
// The entry point of the Windows x64 host built for AVX2 (root CMakeLists.txt, nebula_host_cpu_entry: compiled without
// AVX, linked with /ENTRY). It runs before the C runtime, any static initialiser or any code built for AVX2. A CPU with
// x86-64-v3 goes straight on to the C runtime's start. A CPU without it runs the SSE4.2 build beside this program
// (<name>-x86-64-v2.exe) with this process's command line, startup information, inherited handles, environment and
// working folder, waits for it and ends with its exit code, so a caller sees one process's lifetime and code. The child
// is in a job that ends it when this process ends (a caller ending this process ends the game); processes the child
// starts (a restart, the crash report, the cooker) leave the job and live on their own as before. Only kernel32.
#if defined(_WIN32) && defined(_M_X64)
#include "galaxy/windows_api.h"

#include "cpu_level_x86.h"

extern "C" DWORD mainCRTStartup(LPVOID);

namespace {

HANDLE gChild = nullptr;

// Ctrl+C and Ctrl+Break reach the child too (same console); the child decides. Close, log-off and shut-down end this
// process when the handler returns: it returns once the child has ended, within the system's own time limit.
BOOL WINAPI on_console_event(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT && gChild != nullptr) {
        WaitForSingleObject(gChild, INFINITE);
    }
    return TRUE;
}

void write_error(const char* text) {
    DWORD length = 0;
    while (text[length] != '\0') ++length;
    DWORD written = 0;
    const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (error != nullptr && error != INVALID_HANDLE_VALUE) WriteFile(error, text, length, &written, nullptr);
    OutputDebugStringA(text);
}

__declspec(noinline) DWORD run_baseline_build() {
    const HANDLE heap = GetProcessHeap();
    constexpr DWORD kPathChars = 32768u;
    constexpr wchar_t kSuffix[] = L"-x86-64-v2.exe";
    constexpr DWORD kSuffixChars = sizeof(kSuffix) / sizeof(kSuffix[0]);  // with the terminator
    auto* path = static_cast<wchar_t*>(HeapAlloc(heap, 0, (kPathChars + kSuffixChars) * sizeof(wchar_t)));
    if (path == nullptr) return ERROR_OUTOFMEMORY;
    DWORD length = GetModuleFileNameW(nullptr, path, kPathChars);
    if (length == 0u || length >= kPathChars) return ERROR_FILENAME_EXCED_RANGE;
    // <name>.exe -> <name>-x86-64-v2.exe
    if (length > 4u && path[length - 4u] == L'.') length -= 4u;
    for (DWORD i = 0; i < kSuffixChars; ++i) path[length + i] = kSuffix[i];

    // CreateProcessW may write to the command line: a copy.
    const wchar_t* line = GetCommandLineW();
    DWORD line_chars = 0;
    while (line[line_chars] != L'\0') ++line_chars;
    auto* command = static_cast<wchar_t*>(HeapAlloc(heap, 0, (line_chars + 1u) * sizeof(wchar_t)));
    if (command == nullptr) return ERROR_OUTOFMEMORY;
    for (DWORD i = 0; i <= line_chars; ++i) command[i] = line[i];

    STARTUPINFOW startup;
    GetStartupInfoW(&startup);
    startup.cb = sizeof(startup);
    startup.lpReserved = nullptr;

    // A nested job: Windows 8 and later accept it inside a caller's job (the test driver's, a launcher's).
    const HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
        SecureZeroMemory(&limits, sizeof(limits));
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    PROCESS_INFORMATION process;
    if (!CreateProcessW(path, command, nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &startup,
                        &process)) {
        const DWORD error = GetLastError();
        write_error("host: the CPU lacks x86-64-v3 and the x86-64-v2 build beside the program did not start\n");
        return error != 0u ? error : ERROR_FILE_NOT_FOUND;
    }
    if (job != nullptr) AssignProcessToJobObject(job, process.hProcess);
    gChild = process.hProcess;
    SetConsoleCtrlHandler(on_console_event, TRUE);
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1u;
    GetExitCodeProcess(process.hProcess, &code);
    return code;
}

}  // namespace

// No buffer security check here (compiled /GS-): the C runtime sets the cookie after this frame exists.
extern "C" DWORD nebula_host_entry(LPVOID peb) {
    if (cpu_runs_x86_64_v3()) return mainCRTStartup(peb);
    ExitProcess(run_baseline_build());
}
#endif
