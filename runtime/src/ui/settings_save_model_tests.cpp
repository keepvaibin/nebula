#include "galaxy/ui/settings_save_model.h"
#include "galaxy/test_wait.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared save model must not include Windows.h
#endif

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
int writes = 0, failures = 0;
bool fail = true, throws = false;
bool runtime_writer() {
    ++writes;
    if (throws) throw std::runtime_error("write failure");
    return !fail;
}
bool render_writer() { ++writes; return true; }
void failed() { ++failures; }
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

// The watch: how often it looked, on which thread, and whether it ever looked during a save.
std::atomic<int> looks{0};
std::atomic<bool> writing{false}, looked_while_writing{false}, looked_on_caller{false};
std::thread::id caller;
galaxy::ui::SettingsSaveModel* g_watched = nullptr;  // held for the process: its thread is never joined
bool slow_writer() {
    writing = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    writing = false;
    return true;
}
void look() {
    if (writing) looked_while_writing = true;
    if (std::this_thread::get_id() == caller) looked_on_caller = true;
    ++looks;
}
// The watch's thread runs below the normal priority: its looks and saves are waited for, up to the test's deadline.
template <class Done> bool wait_for(Done done) {
    return galaxy::test_wait(done, std::chrono::milliseconds(2));
}
}

int main() {
    try {
        galaxy::ui::SettingsSaveModel model(runtime_writer, render_writer, failed);
        check(model.write() && writes == 0, "clean model does not write");
        model.dirty = true;
        check(!model.write() && model.dirty && model.error, "failed write retained for retry");
        check(writes == 2 && failures == 1, "both writers attempted, one notification");
        check(!model.write() && failures == 1, "repeated failure notification suppressed");
        fail = false;
        check(model.write() && !model.dirty && !model.error, "successful retry clears error");
        model.dirty = true; throws = true;
        check(!model.write() && model.dirty && failures == 2 && writes == 8, "writer exception retains dirty state and attempts other writer");
        throws = false;
        model.save_now();
        check(!model.dirty && !model.pending(), "save-now flushes unqueued changes");

        // The watch looks on the model's thread, again and again, and never while a save is written: the caller (the
        // render thread in the game) only starts it.
        // Lives as long as the process, as the game's does: its thread is never joined.
        g_watched = new galaxy::ui::SettingsSaveModel(slow_writer, render_writer, failed);
        galaxy::ui::SettingsSaveModel& watched = *g_watched;
        caller = std::this_thread::get_id();
        watched.watch(look, std::chrono::milliseconds(5));
        check(wait_for([] { return looks >= 3; }), "the watch looks again and again");
        check(!looked_on_caller, "the watch never looks on the thread that started it");
        for (int i = 0; i < 5; ++i) {
            watched.dirty = true;
            watched.queue();
            check(wait_for([&] { return !watched.pending(); }), "a queued save finishes");
        }
        const int before = looks;
        check(wait_for([&] { return looks > before; }), "the watch goes on after saves");
        check(!looked_while_writing, "the watch never looks during a save");
        std::cout << "settings save model: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
