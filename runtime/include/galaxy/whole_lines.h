// SPDX-License-Identifier: GPL-3.0-only
// Every line the process writes to its output reaches it whole, whichever threads write at once.
//
// A line written with std::cout or std::cerr is several writes (one for each <<, and std::cerr writes each at once),
// so two threads' lines could interleave mid-line; a reader of the log (the test harness, a player's report) then
// finds neither. install() gives std::cout, std::cerr and std::clog a buffer per thread that passes each line on in
// one write when its '\n' comes. A line with no '\n' yet waits for it, or for its thread's end.
//
// C's stdio writes each call whole: a line made of several calls is composed first (Line), then written once.
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace galaxy::whole_lines {

// Once, at the start of main, before any other thread writes.
void install() noexcept;

// One line made of parts (a list of passes, a list of counts), written in one call. Longer text is cut at the end.
class Line {
public:
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    void add(const char* format, ...) noexcept {
        if (used_ >= sizeof(text_) - 1u) {
            return;
        }
        va_list args;
        va_start(args, format);
        const int wrote = std::vsnprintf(text_ + used_, sizeof(text_) - used_, format, args);
        va_end(args);
        if (wrote > 0) {
            used_ = used_ + static_cast<std::size_t>(wrote) < sizeof(text_) - 1u ? used_ + static_cast<std::size_t>(wrote)
                                                                                 : sizeof(text_) - 1u;
        }
    }
    // The line and its '\n', in one write.
    void write(std::FILE* to) noexcept {
        text_[used_] = '\n';
        std::fwrite(text_, 1u, used_ + 1u, to);
        used_ = 0u;
    }

private:
    char text_[2048]{};
    std::size_t used_ = 0u;
};

}  // namespace galaxy::whole_lines
