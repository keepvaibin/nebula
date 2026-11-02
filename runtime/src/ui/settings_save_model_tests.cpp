#include "galaxy/ui/settings_save_model.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared save model must not include Windows.h
#endif

#include <iostream>
#include <stdexcept>

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
        std::cout << "settings save model: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
