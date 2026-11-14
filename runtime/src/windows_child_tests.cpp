// SPDX-License-Identifier: GPL-3.0-only
// A program started with start_child ends with the program that started it, however that one ends (it exits, it
// crashes, it is killed from outside), whether that one runs in no job, in a job that lets its children break away
// silently, or in a job that does not (nested). A child started with plain CreateProcessW outlives it: the check can
// fail.
#include "galaxy/windows_child.h"
#include "galaxy/windows_paths.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <string>

namespace {

constexpr DWORD kChildSleepMs = 120000u;

// "parent <how> <file>": starts a sleeping child, writes "<child id> <tied>" to <file>, then ends as <how> says.
int run_parent(const wchar_t* how, const wchar_t* file) {
    const std::wstring self = galaxy::windows::executable_path();
    std::wstring command = L"\"" + self + L"\" child";
    PROCESS_INFORMATION process{};
    int tied = 0;
    if (std::wcscmp(how, L"plain") == 0) {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        if (!CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                            &startup, &process)) {
            return 3;
        }
    } else {
        const galaxy::windows::ChildStart started =
            galaxy::windows::start_child(self.c_str(), command, CREATE_NO_WINDOW, process);
        if (started == galaxy::windows::ChildStart::NotStarted) return 3;
        tied = started == galaxy::windows::ChildStart::Tied ? 1 : 0;
    }
    {
        const std::wstring temporary = std::wstring(file) + L".part";
        std::ofstream(temporary, std::ios::trunc) << process.dwProcessId << ' ' << tied << '\n';
        MoveFileExW(temporary.c_str(), file, MOVEFILE_REPLACE_EXISTING);
    }
    // The test holds the child before this one ends (it writes <file>.go then).
    const std::wstring go = std::wstring(file) + L".go";
    for (unsigned waited = 0u; waited < 300u && GetFileAttributesW(go.c_str()) == INVALID_FILE_ATTRIBUTES; ++waited) {
        Sleep(100u);
    }
    if (std::wcscmp(how, L"crash") == 0) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    }
    if (std::wcscmp(how, L"wait") == 0) {
        Sleep(kChildSleepMs);
    }
    // "exit" and "plain": as the game's own end does it (finish_and_exit).
    TerminateProcess(GetCurrentProcess(), 0u);
    return 0;
}

enum class Outer { None, SilentBreakaway, Nested };

struct Case {
    const wchar_t* how;
    Outer outer;
    bool child_ends;
};

const char* outer_name(Outer outer) {
    return outer == Outer::None ? "no job" : outer == Outer::SilentBreakaway ? "a job with silent breakaway" : "a job (nested)";
}

bool run_case(const Case& test, unsigned number) {
    const std::wstring self = galaxy::windows::executable_path();
    wchar_t folder[MAX_PATH];
    GetTempPathW(MAX_PATH, folder);
    const std::wstring file = std::wstring(folder) + L"windows_child_test_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                              std::to_wstring(number) + L".txt";
    DeleteFileW(file.c_str());
    std::wstring command = L"\"" + self + L"\" parent " + test.how + L" \"" + file + L"\"";

    HANDLE outer = nullptr;
    if (test.outer != Outer::None) {
        outer = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | (test.outer == Outer::SilentBreakaway ? JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK : 0u);
        if (outer == nullptr || !SetInformationJobObject(outer, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            std::printf("case %u: the outer job could not be made (error %lu)\n", number, GetLastError());
            return false;
        }
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION parent{};
    if (!CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        nullptr, &startup, &parent)) {
        std::printf("case %u: the parent did not start (error %lu)\n", number, GetLastError());
        return false;
    }
    if (outer != nullptr && !AssignProcessToJobObject(outer, parent.hProcess)) {
        std::printf("case %u: the parent could not be put in the outer job (error %lu)\n", number, GetLastError());
        TerminateProcess(parent.hProcess, 1u);
        return false;
    }
    ResumeThread(parent.hThread);
    CloseHandle(parent.hThread);

    // The child's id, once the parent wrote it.
    DWORD child_id = 0u;
    int tied = -1;
    for (unsigned waited = 0u; waited < 300u && child_id == 0u; ++waited) {
        std::ifstream in(file);
        if (!(in >> child_id >> tied)) {
            child_id = 0u;
            Sleep(100u);
        }
    }
    bool passed = true;
    const HANDLE child = child_id != 0u ? OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, child_id)
                                        : nullptr;
    if (child == nullptr) {
        std::printf("case %u: no child to watch (id %lu, error %lu)\n", number, child_id, GetLastError());
        passed = false;
    }
    const std::wstring go = file + L".go";
    std::ofstream(go, std::ios::trunc) << "go\n";
    if (child != nullptr && test.outer != Outer::None) {
        BOOL in_outer = FALSE;
        IsProcessInJob(child, outer, &in_outer);
        const bool expected = test.outer == Outer::Nested || std::wcscmp(test.how, L"plain") == 0;
        if ((in_outer != FALSE) != expected) {
            std::printf("case %u: the child is %sin the outer job\n", number, in_outer != FALSE ? "" : "not ");
            passed = false;
        }
    }
    if (std::wcscmp(test.how, L"plain") != 0 && tied != 1) {
        std::printf("case %u: start_child did not tie the child (%d)\n", number, tied);
        passed = false;
    }
    if (std::wcscmp(test.how, L"wait") == 0) {
        // Killed from outside, as a test harness ends a game at its limit.
        TerminateProcess(parent.hProcess, 1u);
    }
    if (WaitForSingleObject(parent.hProcess, 30000u) != WAIT_OBJECT_0) {
        std::printf("case %u: the parent did not end\n", number);
        TerminateProcess(parent.hProcess, 1u);
        passed = false;
    }
    CloseHandle(parent.hProcess);
    if (child != nullptr) {
        const bool ended = WaitForSingleObject(child, 10000u) == WAIT_OBJECT_0;
        if (ended != test.child_ends) {
            std::printf("case %u: the child %s after the parent ended\n", number, ended ? "ended" : "is still running");
            passed = false;
        }
        if (!ended) TerminateProcess(child, 1u);
        CloseHandle(child);
    }
    if (outer != nullptr) CloseHandle(outer);
    DeleteFileW(file.c_str());
    DeleteFileW(go.c_str());
    std::printf("case %u: parent %ls in %s: %s\n", number, test.how, outer_name(test.outer), passed ? "pass" : "FAIL");
    return passed;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 && std::wcscmp(argv[1], L"child") == 0) {
        Sleep(kChildSleepMs);
        return 0;
    }
    if (argc >= 4 && std::wcscmp(argv[1], L"parent") == 0) {
        return run_parent(argv[2], argv[3]);
    }
    const Case cases[] = {
        {L"exit", Outer::None, true},
        {L"crash", Outer::None, true},
        {L"wait", Outer::None, true},
        {L"exit", Outer::SilentBreakaway, true},
        {L"crash", Outer::SilentBreakaway, true},
        {L"wait", Outer::SilentBreakaway, true},
        {L"exit", Outer::Nested, true},
        {L"crash", Outer::Nested, true},
        {L"wait", Outer::Nested, true},
        // Without start_child the child outlives its parent: what the cook did before.
        {L"plain", Outer::None, false},
    };
    bool passed = true;
    unsigned number = 0u;
    for (const Case& test : cases) {
        passed &= run_case(test, ++number);
    }
    std::printf(passed ? "windows child tests passed\n" : "windows child tests FAILED\n");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
