// NebulaOnce (P-183): the work runs once, and a thread that asks while another does it waits for its end.
#include "Game/System/NebulaOnce.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
    int sFailures;

    void expect(bool ok, const char* pWhat) {
        if (!ok) {
            printf("FAIL %s\n", pWhat);
            sFailures++;
        }
    }
}  // namespace

int main() {
    // A second thread asks while the first is inside the work: it does not run the work again, and it returns only
    // once the work has ended, with what the work wrote seen.
    {
        static NebulaOnce once;
        std::atomic<int> runs{0};
        std::atomic<bool> inside{false};
        std::atomic<bool> asked{false};
        int written = 0;  // plain: the once's release and acquire must carry it
        std::thread first([&] {
            once.run([&] {
                runs.fetch_add(1);
                inside.store(true);
                while (!asked.load()) {
                    std::this_thread::yield();
                }
                // The second thread is in run() by now (it asked just before); the work holds it a little longer.
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                written = 42;
            });
        });
        while (!inside.load()) {
            std::this_thread::yield();
        }
        asked.store(true);
        once.run([&] { runs.fetch_add(1); });
        expect(runs.load() == 1, "the work runs once when a second thread asks during it");
        expect(written == 42, "the second thread returns after the work, and sees what it wrote");
        first.join();
        expect(once.isDone(), "done after the work");
    }

    // Done: a later call runs nothing.
    {
        static NebulaOnce once;
        int runs = 0;
        expect(!once.isDone(), "a zero NebulaOnce is not done");
        once.run([&] { runs++; });
        once.run([&] { runs++; });
        expect(runs == 1 && once.isDone(), "a second call after the work runs nothing");
    }

    // Eight threads at once: one runs the work, all see the whole of what it wrote.
    for (int round = 0; round < 200; ++round) {
        static NebulaOnce onces[200];
        static int tables[200][256];
        NebulaOnce& once = onces[round];
        int* table = tables[round];
        std::atomic<int> runs{0};
        std::atomic<int> wrong{0};
        std::atomic<bool> go{false};
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&] {
                while (!go.load()) {
                }
                once.run([&] {
                    runs.fetch_add(1);
                    for (int i = 0; i < 256; ++i) {
                        table[i] = i + 1;
                    }
                });
                for (int i = 0; i < 256; ++i) {
                    if (table[i] != i + 1) {
                        wrong.fetch_add(1);
                        break;
                    }
                }
            });
        }
        go.store(true);
        for (std::thread& thread : threads) {
            thread.join();
        }
        expect(runs.load() == 1, "eight threads: the work runs once");
        expect(wrong.load() == 0, "eight threads: each sees the whole table");
    }

    if (sFailures == 0) {
        printf("once_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
