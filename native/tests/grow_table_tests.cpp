// SPDX-License-Identifier: GPL-3.0-only
// The kernel's growing table (Game/System/LumaGrowTable.hpp, N-3008): entries are zeroed and never move as it grows
// past many directories; only the next entry can be reserved; and a reader on another thread, given entries through a
// count stored with release, reads each as its writer left it while the writer keeps growing the table (the thread
// sanitizer build checks the order).
#include "Game/System/LumaGrowTable.hpp"

#include "test_support.h"

#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#include <process.h>
extern "C" __declspec(dllimport) unsigned long __stdcall WaitForSingleObject(void*, unsigned long);
extern "C" __declspec(dllimport) int __stdcall CloseHandle(void*);
#else
#include <pthread.h>
#endif

namespace {
    int sFailures;

    void expect(bool ok, const char* pWhat) {
        if (!ok) {
            printf("FAIL %s\n", pWhat);
            sFailures++;
        }
    }

    struct Entry {
        uint32_t index;
        uint32_t check;
        uint8_t pad[40];
    };

    const uint32_t kMany = 200000u;  // 3125 chunks of 64: the directory is replaced ten times

    void grows_and_never_moves() {
        luma::GrowTable<Entry> table;
        static Entry* sAddress[kMany];
        bool zeroed = true;
        bool stays = true;
        for (uint32_t i = 0; i < kMany; i++) {
            Entry* const pEntry = table.reserve(i);
            if (pEntry == nullptr) {
                expect(false, "reserve gives every next entry");
                return;
            }
            zeroed = zeroed && pEntry->index == 0u && pEntry->check == 0u;
            pEntry->index = i;
            pEntry->check = i * 2654435761u;
            sAddress[i] = pEntry;
        }
        for (uint32_t i = 0; i < kMany; i++) {
            stays = stays && &table.at(i) == sAddress[i] && table.at(i).index == i && table.at(i).check == i * 2654435761u;
        }
        expect(zeroed, "a new entry is zeroed");
        expect(stays, "no entry moves as the table grows");
        expect(table.reserved() == kMany, "reserved counts the entries");
        expect(table.reserve(5u) == &table.at(5u), "reserving an entry again gives the same one");
        expect(table.reserve(kMany + 1u) == nullptr && table.reserved() == kMany, "an entry past the next is refused, nothing changed");
    }

    // The writer reserves, fills, then publishes the count; the reader reads every published entry.
    luma::GrowTable<Entry, 2u> sShared;  // chunks of 4: a new chunk or directory every few entries
    uint32_t sPublished;
    uint32_t sReaderBad;
    const uint32_t kShared = 100000u;

#if defined(_WIN32)
    unsigned __stdcall reader(void*) {
#else
    void* reader(void*) {
#endif
        uint32_t seen = 0;
        while (seen < kShared) {
            const uint32_t n = __atomic_load_n(&sPublished, __ATOMIC_ACQUIRE);
            for (uint32_t i = seen; i < n; i++) {
                const Entry& rEntry = sShared.at(i);
                if (rEntry.index != i || rEntry.check != ~i) {
                    sReaderBad++;
                }
            }
            if (n > 0u) {
                // An entry read before stays as it was.
                const Entry& rFirst = sShared.at(0u);
                if (rFirst.index != 0u || rFirst.check != ~0u) {
                    sReaderBad++;
                }
            }
            seen = n;
        }
        return 0;
    }

    void reader_on_another_thread() {
#if defined(_WIN32)
        const uintptr_t thread = _beginthreadex(nullptr, 0, reader, nullptr, 0, nullptr);
        expect(thread != 0u, "the reader thread starts");
#else
        pthread_t thread;
        expect(pthread_create(&thread, nullptr, reader, nullptr) == 0, "the reader thread starts");
#endif
        for (uint32_t i = 0; i < kShared; i++) {
            Entry* const pEntry = sShared.reserve(i);
            if (pEntry == nullptr) {
                expect(false, "reserve gives every next entry while read");
                __atomic_store_n(&sPublished, kShared, __ATOMIC_RELEASE);
                break;
            }
            pEntry->index = i;
            pEntry->check = ~i;
            __atomic_store_n(&sPublished, i + 1u, __ATOMIC_RELEASE);
        }
#if defined(_WIN32)
        WaitForSingleObject(reinterpret_cast<void*>(thread), 0xFFFFFFFFu);
        CloseHandle(reinterpret_cast<void*>(thread));
#else
        pthread_join(thread, nullptr);
#endif
        expect(sReaderBad == 0u, "a reader on another thread reads each published entry as written");
    }
}  // namespace

int main() {
    grows_and_never_moves();
    reader_on_another_thread();
    if (sFailures != 0) {
        printf("%d failed\n", sFailures);
        return 1;
    }
    printf("grow table: all passed\n");
    return 0;
}
