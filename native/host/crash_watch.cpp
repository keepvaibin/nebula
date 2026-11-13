// SPDX-License-Identifier: GPL-3.0-only
// The crash watcher (crash_watch.h).
#include "crash_watch.h"

#if defined(_WIN32)
#include <dbghelp.h>
#include <processsnapshot.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <system_error>

namespace galaxy::crash_watch {
namespace {

// The memory the game and its watcher share: the request, then the answer.
struct Block {
    DWORD version;
    DWORD thread;      // the crashed thread
    ULONG64 pointers;  // its EXCEPTION_POINTERS, an address in the game's memory (0: none)
    DWORD type;        // MINIDUMP_TYPE
    LONG written;      // the answer: 1 written, 0 not
    DWORD error;       // why not (a Windows error code)
    DWORD snapshot;    // PssCaptureSnapshot's answer: 0, the dump read a snapshot; else why not (it read the live process)
    wchar_t path[1024];
};
constexpr DWORD kVersion = 2u;

// The game's side.
HANDLE g_process = nullptr;  // the watcher
HANDLE g_ask = nullptr;
HANDLE g_done = nullptr;
HANDLE g_ready = nullptr;  // set once the watcher has the game's process open
Block* g_block = nullptr;
wchar_t g_folder[1024];  // where dumps go, kept from the start (no heap at the crash)

void name_of(wchar_t* out, std::size_t size, const wchar_t* base, const wchar_t* what) {
    std::swprintf(out, size, L"%ls-%ls", base, what);
}

// Appends `text` at *at in out (size characters), always ending it.
void append(wchar_t* out, std::size_t size, std::size_t* at, const wchar_t* text) noexcept {
    for (; *text != 0 && *at + 1 < size; ++text) out[(*at)++] = *text;
    out[*at] = 0;
}

// MiniDumpWriteDump given a process snapshot: it reads the snapshot (S_FALSE to IsProcessSnapshotCallback).
BOOL CALLBACK snapshot_callback(PVOID, const PMINIDUMP_CALLBACK_INPUT input, PMINIDUMP_CALLBACK_OUTPUT output) {
    if (input->CallbackType == IsProcessSnapshotCallback) {
        output->Status = S_FALSE;
    }
    return TRUE;
}

// The dump of the game's process as it is now, read from a snapshot: its address space cloned (copy on write), its
// threads and their contexts, its handles. The game's other threads run on while the dump is written, a driver's among
// them; read live, a fault in a graphics driver stopped the dump at ERROR_PARTIAL_COPY with nothing in it (p0479
// kit-replay, p0480 3511: its memory freed while it was read), MiniDumpIgnoreInaccessibleMemory or not. The clone
// breaks away from the game's job when the job allows it: PSS_CREATE_BREAKAWAY_OPTIONAL is taken only with
// PSS_CREATE_BREAKAWAY (alone, the system refuses every snapshot with ERROR_INVALID_PARAMETER: until intm10 no dump
// read one, p0482 3206's driver fault among them). No snapshot: the live process, and *snapshot_error says why.
BOOL write_dump(decltype(&MiniDumpWriteDump) write, HANDLE game, DWORD pid, HANDLE file, MINIDUMP_TYPE type,
                MINIDUMP_EXCEPTION_INFORMATION* exception, DWORD* snapshot_error) {
    HPSS snapshot = nullptr;
    const auto flags = static_cast<PSS_CAPTURE_FLAGS>(
        PSS_CAPTURE_VA_CLONE | PSS_CAPTURE_HANDLES | PSS_CAPTURE_HANDLE_NAME_INFORMATION | PSS_CAPTURE_HANDLE_BASIC_INFORMATION |
        PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION | PSS_CAPTURE_HANDLE_TRACE | PSS_CAPTURE_THREADS | PSS_CAPTURE_THREAD_CONTEXT |
        PSS_CAPTURE_THREAD_CONTEXT_EXTENDED | PSS_CREATE_BREAKAWAY | PSS_CREATE_BREAKAWAY_OPTIONAL | PSS_CREATE_USE_VM_ALLOCATIONS |
        PSS_CREATE_RELEASE_SECTION);
    *snapshot_error = PssCaptureSnapshot(game, flags, CONTEXT_ALL, &snapshot);
    if (*snapshot_error != ERROR_SUCCESS) {
        return write(game, pid, file, type, exception, nullptr, nullptr);
    }
    MINIDUMP_CALLBACK_INFORMATION callback{snapshot_callback, nullptr};
    const BOOL ok = write(reinterpret_cast<HANDLE>(snapshot), pid, file, type, exception, nullptr, &callback);
    const DWORD error = ok ? 0u : GetLastError();
    PssFreeSnapshot(GetCurrentProcess(), snapshot);
    SetLastError(error);
    return ok;
}

}  // namespace

int run(int argc, char** argv) {
    if (argc < 4) {
        return 2;
    }
    const DWORD pid = static_cast<DWORD>(std::strtoul(argv[2], nullptr, 10));
    wchar_t base[96];
    std::swprintf(base, sizeof(base) / sizeof(base[0]), L"%hs", argv[3]);
    wchar_t name[128];
    name_of(name, 128, base, L"block");
    const HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
    name_of(name, 128, base, L"ask");
    const HANDLE ask = OpenEventW(SYNCHRONIZE, FALSE, name);
    name_of(name, 128, base, L"done");
    const HANDLE done = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    name_of(name, 128, base, L"ready");
    const HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    // What MiniDumpWriteDump reads of another process: its memory, threads and handles; PROCESS_CREATE_PROCESS for the
    // snapshot's clone of its address space (write_dump).
    const HANDLE game = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE | PROCESS_CREATE_PROCESS | SYNCHRONIZE,
                                    FALSE, pid);
    auto* const block = mapping != nullptr ? static_cast<Block*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Block)))
                                           : nullptr;
    const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    using WriteFn = decltype(&MiniDumpWriteDump);
    const auto write = dbghelp != nullptr ? reinterpret_cast<WriteFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    if (block == nullptr || ask == nullptr || done == nullptr || ready == nullptr || game == nullptr || write == nullptr ||
        block->version != kVersion) {
        return 3;
    }
    // Idle until needed: below the game, so it never takes the game's time.
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    SetEvent(ready);
    const HANDLE waits[2] = {game, ask};
    for (;;) {
        const DWORD woke = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (woke != WAIT_OBJECT_0 + 1) {
            return 0;  // the game ended (or the wait failed): nothing left to watch
        }
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
        block->path[sizeof(block->path) / sizeof(block->path[0]) - 1] = 0;
        const std::filesystem::path path(block->path);
        std::error_code ignored;
        std::filesystem::create_directories(path.parent_path(), ignored);
        const HANDLE file = CreateFileW(block->path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        BOOL ok = FALSE;
        DWORD error = 0;
        DWORD snapshot = ERROR_NOT_READY;  // no file: no dump tried
        if (file == INVALID_HANDLE_VALUE) {
            error = GetLastError();
        } else {
            MINIDUMP_EXCEPTION_INFORMATION exception{block->thread,
                                                     reinterpret_cast<EXCEPTION_POINTERS*>(static_cast<std::uintptr_t>(block->pointers)),
                                                     TRUE};
            ok = write_dump(write, game, pid, file, static_cast<MINIDUMP_TYPE>(block->type), block->pointers != 0 ? &exception : nullptr,
                            &snapshot);
            // MiniDumpWriteDump's failure is an HRESULT in the last error.
            error = ok ? 0u : GetLastError();
            CloseHandle(file);
        }
        block->error = error;
        block->snapshot = snapshot;
        block->written = ok ? 1 : 0;
        SetEvent(done);
    }
}

void start(const std::filesystem::path& folder) {
    std::size_t at = 0;
    g_folder[0] = 0;
    append(g_folder, sizeof(g_folder) / sizeof(g_folder[0]), &at, std::filesystem::absolute(folder).wstring().c_str());
    // Objects named for this process and start (a restarted game is a new process, a new name).
    const DWORD pid = GetCurrentProcessId();
    char base_narrow[96];
    std::snprintf(base_narrow, sizeof(base_narrow), "Local\\NebulaCrashWatch-%lu-%llu", pid,
                  static_cast<unsigned long long>(GetTickCount64()));
    wchar_t base[96];
    std::swprintf(base, sizeof(base) / sizeof(base[0]), L"%hs", base_narrow);
    wchar_t name[128];
    name_of(name, 128, base, L"block");
    const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Block), name);
    name_of(name, 128, base, L"ask");
    g_ask = CreateEventW(nullptr, FALSE, FALSE, name);
    name_of(name, 128, base, L"done");
    g_done = CreateEventW(nullptr, FALSE, FALSE, name);
    name_of(name, 128, base, L"ready");
    g_ready = CreateEventW(nullptr, TRUE, FALSE, name);
    g_block = mapping != nullptr ? static_cast<Block*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Block))) : nullptr;
    if (g_block == nullptr || g_ask == nullptr || g_done == nullptr || g_ready == nullptr) {
        return;
    }
    g_block->version = kVersion;
    wchar_t exe[MAX_PATH * 2];
    const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    if (length == 0 || length >= MAX_PATH * 2) {
        return;
    }
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --crash-watch " + std::to_wstring(pid) + L" " + base;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // No handles inherited (a test's output pipe stays the game's alone) and no window.
    if (CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                       nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        g_process = process.hProcess;
    }
}

Result write_dump(DWORD thread, EXCEPTION_POINTERS* pointers, unsigned type, wchar_t* path, std::size_t size, DWORD* error,
                  DWORD* snapshot, DWORD wait_ms, const wchar_t* kind) noexcept {
    *error = 0;
    *snapshot = ERROR_NOT_READY;
    if (size != 0) path[0] = 0;
    if (g_process == nullptr || g_block == nullptr || WaitForSingleObject(g_process, 0) != WAIT_TIMEOUT ||
        WaitForSingleObject(g_ready, 0) != WAIT_OBJECT_0) {
        return Result::kNoWatcher;
    }
    // The file name is fixed width (the kind, a date, a time and a process id); the folder is any length.
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t file[64];
    std::swprintf(file, sizeof(file) / sizeof(file[0]), L"%.16ls-%04u%02u%02u-%02u%02u%02u-%lu.dmp", kind, now.wYear, now.wMonth,
                  now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
    const std::size_t room = sizeof(g_block->path) / sizeof(g_block->path[0]);
    std::size_t at = 0;
    g_block->path[0] = 0;
    append(g_block->path, room, &at, g_folder);
    append(g_block->path, room, &at, L"\\");
    append(g_block->path, room, &at, file);
    g_block->thread = thread;
    g_block->pointers = reinterpret_cast<std::uintptr_t>(pointers);
    g_block->type = type;
    g_block->written = 0;
    g_block->error = 0;
    g_block->snapshot = ERROR_NOT_READY;
    std::size_t copied = 0;
    if (size != 0) {
        path[0] = 0;
        append(path, size, &copied, g_block->path);
    }
    SetEvent(g_ask);
    const HANDLE waits[2] = {g_done, g_process};
    const DWORD woke = WaitForMultipleObjects(2, waits, FALSE, wait_ms);
    if (woke != WAIT_OBJECT_0) {
        *error = woke == WAIT_TIMEOUT ? WAIT_TIMEOUT : ERROR_PROCESS_ABORTED;
        return Result::kFailed;
    }
    *error = g_block->error;
    *snapshot = g_block->snapshot;
    return g_block->written != 0 ? Result::kWritten : Result::kFailed;
}

}  // namespace galaxy::crash_watch
#endif
