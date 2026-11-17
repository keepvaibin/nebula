// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/whole_lines.h"

#include <cstdio>
#include <iostream>
#include <streambuf>
#include <string>

namespace galaxy::whole_lines {
namespace {

// The process's two outputs; std::clog shares stderr's.
enum Output : unsigned { Out, Err, Outputs };

std::FILE* file_of(unsigned output) noexcept {
    return output == Out ? stdout : stderr;
}

void emit(unsigned output, const char* data, std::size_t size) noexcept {
    // One fwrite: stdio holds the stream's lock for the whole call, so no other thread's write lands inside it.
    std::fwrite(data, 1u, size, file_of(output));
    if (output == Err) {
        std::fflush(stderr);
    }
}

// Set when this thread's unfinished lines are gone (its end; the main thread's comes before the program's statics are
// destroyed): a line written after that goes out as it comes. A bool needs no destruction, so it stays readable.
thread_local bool t_pending_gone = false;

// One thread's unfinished lines, one per output: written whole as each '\n' comes, the rest at the thread's end.
struct Pending {
    std::string text[Outputs];
    ~Pending() {
        t_pending_gone = true;
        for (unsigned output = 0; output < Outputs; ++output) {
            if (!text[output].empty()) {
                text[output].push_back('\n');
                emit(output, text[output].data(), text[output].size());
            }
        }
    }
};

Pending& pending() {
    thread_local Pending lines;
    return lines;
}

class LineBuffer final : public std::streambuf {
public:
    explicit LineBuffer(unsigned output) : output_(output) {}

protected:
    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) {
            return traits_type::not_eof(ch);
        }
        const char c = traits_type::to_char_type(ch);
        put(&c, 1u);
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        put(s, static_cast<std::size_t>(n));
        return n;
    }

    // A flush (std::cerr flushes after every <<, std::endl after its '\n') writes nothing unfinished: a line goes out
    // when it ends.
    int sync() override {
        return 0;
    }

private:
    void put(const char* s, std::size_t n) {
        if (t_pending_gone) {
            emit(output_, s, n);
            return;
        }
        std::string& line = pending().text[output_];
        std::size_t start = 0u;
        for (std::size_t i = 0u; i < n; ++i) {
            if (s[i] != '\n') {
                continue;
            }
            if (line.empty()) {
                emit(output_, s + start, i + 1u - start);
            } else {
                line.append(s + start, i + 1u - start);
                emit(output_, line.data(), line.size());
                line.clear();
            }
            start = i + 1u;
        }
        line.append(s + start, n - start);
    }

    unsigned output_;
};

}  // namespace

void install() noexcept {
    // Never destroyed: the standard streams outlive every static, and a thread may still write while the process ends.
    static LineBuffer* const out = new LineBuffer(Out);
    static LineBuffer* const err = new LineBuffer(Err);
    std::cout.rdbuf(out);
    std::cerr.rdbuf(err);
    std::clog.rdbuf(err);
}

}  // namespace galaxy::whole_lines
