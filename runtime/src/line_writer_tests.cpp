// Tests for galaxy/line_writer.h: a thread that hands text never waits on the output, even while the output is stuck
// (a full pipe, a slow disk); the text comes out in the order it was handed, across outputs; flush writes all handed
// before it and gives up at its bound; after stop, hand refuses and the caller writes itself.
//
// line_writer_tests [--without]: --without writes as the log did before the writer (on the calling thread), to show
// the fault: the caller then waits for the stuck output, and the test exits 1 as it would on that fault.
#include "galaxy/line_writer.h"
#include "galaxy/test_wait.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::printf("FAILED: %s\n", message);
    }
    return condition;
}

// An output that can be held: a write waits while it is held, as a write to a full pipe does.
struct Output {
    std::mutex mutex;
    std::condition_variable released;
    bool held = false;
    std::string text[2];
    int flushes = 0;
    std::atomic<int> writes_waiting{0};
    std::thread::id writer;  // the thread of the first write
    bool other_thread_wrote = false;

    static void write(void* user, unsigned output, const char* data, std::size_t size) {
        Output& o = *static_cast<Output*>(user);
        std::unique_lock<std::mutex> lock(o.mutex);
        if (o.writer == std::thread::id()) {
            o.writer = std::this_thread::get_id();
        } else if (o.writer != std::this_thread::get_id()) {
            o.other_thread_wrote = true;
        }
        ++o.writes_waiting;
        o.released.wait(lock, [&] { return !o.held; });
        --o.writes_waiting;
        o.text[output].append(data, size);
        o.text[output == 0u ? 1u : 0u].append("|");  // the other output sees where this one wrote: the order across both
    }
    static void flush_all(void* user) {
        Output& o = *static_cast<Output*>(user);
        std::lock_guard<std::mutex> lock(o.mutex);
        ++o.flushes;
    }
    void hold(bool on) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            held = on;
        }
        released.notify_all();
    }
};

constexpr int kLines = 2000;
// A flush that has to finish is given the test's deadline: the writer's thread may run seconds late on a busy machine.
constexpr std::chrono::milliseconds kLongBound = galaxy::kTestWaitDeadline;

}  // namespace

int main(int argc, char** argv) {
    const bool without = argc > 1 && std::strcmp(argv[1], "--without") == 0;
    bool passed = true;
    Output output;
    // A wait on a stuck output with no bound is the fault this tests for: a test that stops is a failed one. Longer
    // than all the waits of the test together (each is at most kTestWaitDeadline): it reports, it never decides.
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(180));
        std::printf("FAILED: still waiting after 180 s\n");
        std::fflush(stdout);
        std::_Exit(1);
    }).detach();
    galaxy::LineWriter writer(&Output::write, &Output::flush_all, &output);
    writer.start();

    // A flush right after a hand, the output stuck: it gives up at its bound. It never writes on its own thread
    // (it would wait on the stuck output with no bound).
    if (!without) {
        output.hold(true);
        writer.hand(1u, "s", 1u);
        // That it returns at all is what shows it gave up at its bound: a flush that waited on the stuck output would
        // not, and the watchdog above ends the test. How long after its bound it returned is no verdict.
        passed &= expect(!writer.flush(std::chrono::milliseconds(50)), "flush returned done while the output was stuck");
        output.hold(false);
        passed &= expect(writer.flush(kLongBound), "flush did not finish once the output moved");
        std::lock_guard<std::mutex> lock(output.mutex);
        output.text[0].clear();
        output.text[1].clear();
    }

    // The output is stuck. A thread hands many lines: it must finish in far less than a stuck write would take.
    output.hold(true);
    std::atomic<bool> handed{false};
    std::thread caller([&] {
        for (int i = 0; i < kLines; ++i) {
            char line[32];
            const int n = std::snprintf(line, sizeof(line), "line %d\n", i);
            if (without) {
                Output::write(&output, 0u, line, static_cast<std::size_t>(n));  // the old way: written where it is made
            } else if (!writer.hand(0u, line, static_cast<std::size_t>(n))) {
                std::printf("FAILED: hand refused while the writer runs\n");
            }
        }
        handed = true;
    });
    // A caller that waits on the output does not finish while the output is held; one that hands does at once.
    // The caller's end is waited for itself, up to the test's deadline. Without the writer (the demonstration of the
    // fault) it never comes: two seconds are enough to show it.
    (void)galaxy::test_wait([&] { return handed.load(); }, std::chrono::milliseconds(5),
                            without ? std::chrono::milliseconds(2000) : galaxy::kTestWaitDeadline);
    passed &= expect(handed.load(), "a caller handing lines waited on a stuck output");
    // flush gives up at its bound while the output stays stuck.
    if (!without) {
        passed &= expect(!writer.flush(std::chrono::milliseconds(50)), "flush returned done while the output was stuck");
    }
    output.hold(false);
    caller.join();

    // Every line, in order; then lines on both outputs keep their order across them.
    passed &= expect(without || writer.flush(kLongBound), "flush did not finish once the output moved");
    {
        std::string want;
        for (int i = 0; i < kLines; ++i) {
            want += "line " + std::to_string(i) + "\n";
        }
        std::lock_guard<std::mutex> lock(output.mutex);
        std::string got;
        for (char c : output.text[0]) {
            if (c != '|') got.push_back(c);
        }
        passed &= expect(got == want, "the lines did not come out whole and in order");
        output.text[0].clear();
        output.text[1].clear();
    }
    if (!without) {
        writer.hand(0u, "a", 1u);
        writer.hand(1u, "b", 1u);
        writer.hand(0u, "c", 1u);
        passed &= expect(writer.flush(kLongBound), "flush did not finish");
        std::lock_guard<std::mutex> lock(output.mutex);
        // Output 1 sees the '|' of a, then b, then the '|' of c: a, b, c in that order.
        passed &= expect(output.text[0] == "a|c" && output.text[1] == "|b|", "two outputs' text out of order");
        passed &= expect(output.flushes > 0, "the outputs were never flushed");
        passed &= expect(!output.other_thread_wrote, "a thread other than the writer wrote");
    }

    // After stop, hand refuses: the caller writes itself.
    writer.stop();
    passed &= expect(without || !writer.hand(0u, "x", 1u), "hand took text after stop");

    if (!passed) {
        std::printf("FAILED%s\n", without ? " (without the writer)" : "");
        return 1;
    }
    std::printf("line writer: %d lines handed while the output was stuck, in order, flushed\n", kLines);
    return 0;
}
