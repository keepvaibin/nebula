// SPDX-License-Identifier: GPL-3.0-only
// A program the game starts for its own work and waits for (the mods' cook, the shader compiler) ends with the game,
// however the game ends (exit, crash, killed from outside): it runs in one job object of this process that kills its
// members when its last handle closes. That handle is never closed and never inherited, so it closes only when the
// system ends this process. The game itself is not in that job, so what the game starts without start_child (its own
// start-again, the crash report, the launcher) outlives it as before.
//
// The game may run inside another job (a launcher's, a test harness's, a debugger's): with breakaway allowed silently
// a child leaves that job and joins only this one; otherwise it is in both (nested jobs, Windows 8 and later).
#pragma once

#if defined(_WIN32)
#include "galaxy/windows_api.h"

#include <atomic>
#include <cstdio>
#include <string>

namespace galaxy::windows {

// The job, made on first use; null when the system refused to make it.
[[nodiscard]] inline HANDLE children_job() noexcept {
    static const HANDLE job = []() noexcept -> HANDLE {
        const HANDLE made = CreateJobObjectW(nullptr, nullptr);
        if (made == nullptr) return nullptr;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(made, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(made);
            return nullptr;
        }
        return made;
    }();
    return job;
}

// Where the one line goes that says a child could not be tied to the game (the host's log). Set before the first
// start_child; null: nothing is said.
inline std::atomic<void (*)(const char* text)>& child_note() noexcept {
    static std::atomic<void (*)(const char* text)> note{nullptr};
    return note;
}

enum class ChildStart { NotStarted, Tied, Untied };

// CreateProcessW(program, command, ..., flags) for such a program: made suspended, put in the job, then run. Untied:
// it runs, but the system refused to put it in the job (the error is said once a session through child_note), so it
// may outlive the game. NotStarted: `process` is empty and GetLastError holds why.
[[nodiscard]] inline ChildStart start_child(const wchar_t* program, std::wstring& command, DWORD flags,
                                            PROCESS_INFORMATION& process) noexcept {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    process = {};
    if (!CreateProcessW(program, command.data(), nullptr, nullptr, FALSE, flags | CREATE_SUSPENDED, nullptr, nullptr,
                        &startup, &process)) {
        process = {};
        return ChildStart::NotStarted;
    }
    const HANDLE job = children_job();
    const bool tied = job != nullptr && AssignProcessToJobObject(job, process.hProcess) != FALSE;
    const DWORD why = tied ? 0u : (job == nullptr ? ERROR_INVALID_HANDLE : GetLastError());
    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        const DWORD resume_error = GetLastError();
        TerminateProcess(process.hProcess, 1u);
        WaitForSingleObject(process.hProcess, 5000u);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        process = {};
        SetLastError(resume_error);
        return ChildStart::NotStarted;
    }
    if (tied) return ChildStart::Tied;
    static std::atomic<bool> said{false};
    if (!said.exchange(true)) {
        if (void (*const note)(const char*) = child_note().load()) {
            char text[160];
            std::snprintf(text, sizeof(text),
                          "a program started for the game could not be tied to it (error %lu): it may outlive the game",
                          static_cast<unsigned long>(why));
            note(text);
        }
    }
    return ChildStart::Untied;
}

}  // namespace galaxy::windows
#endif
