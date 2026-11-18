// SPDX-License-Identifier: GPL-3.0-only
// The host's address services for arena_tests.cpp, as the host gives them (reserved addresses, committed on use), with
// counts the test reads: what is reserved and committed now, and every commit's size.
#include <nebula_game_api.h>

#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

uint64_t arena_test_reserved;
uint64_t arena_test_committed;
uint64_t arena_test_commit_calls;
uint64_t arena_test_odd_commits;  // commits not a whole number of MiB
// Called inside each commit when set: a test's way into the arena while one of its calls is under way.
void (*arena_test_on_commit)(void);

static void* reserve(void* user, uint64_t size) {
    (void)user;
#ifdef _WIN32
    void* const p = VirtualAlloc(NULL, (SIZE_T)size, MEM_RESERVE, PAGE_NOACCESS);
#else
    void* p = mmap(NULL, (size_t)size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        p = NULL;
    }
#endif
    if (p != NULL) {
        arena_test_reserved += size;
    }
    return p;
}

static int commit(void* user, void* start, uint64_t size) {
    (void)user;
    arena_test_commit_calls++;
    if (arena_test_on_commit != NULL) {
        arena_test_on_commit();
    }
    if (size % (1u << 20) != 0) {
        arena_test_odd_commits++;
    }
#ifdef _WIN32
    const int ok = VirtualAlloc(start, (SIZE_T)size, MEM_COMMIT, PAGE_READWRITE) != NULL;
#else
    const int ok = mprotect(start, (size_t)size, PROT_READ | PROT_WRITE) == 0;
#endif
    if (ok) {
        arena_test_committed += size;
    }
    return ok;
}

static void decommit(void* user, void* start, uint64_t size) {
    (void)user;
    (void)start;
    (void)size;
}

static void release(void* user, void* start, uint64_t size) {
    (void)user;
#ifdef _WIN32
    VirtualFree(start, 0, MEM_RELEASE);
#else
    munmap(start, (size_t)size);
#endif
    arena_test_reserved -= size;
}

static NebulaHostServices sHost;

const NebulaHostServices* nebula_host(void) {
    if (sHost.memory_reserve == NULL) {
        sHost.size = sizeof(sHost);
        sHost.version = NEBULA_GAME_API_VERSION;
        sHost.memory_reserve = reserve;
        sHost.memory_commit = commit;
        sHost.memory_decommit = decommit;
        sHost.memory_release = release;
    }
    return &sHost;
}
