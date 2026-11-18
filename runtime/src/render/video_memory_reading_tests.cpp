// SPDX-License-Identifier: GPL-3.0-only
// video_memory_reading.h (P-011): the renderers' video memory read. The driver is asked once for any number of reads
// within 100 ms, again after them; at once when the OS moved the budget; an answer with no budget or a failed ask is
// no figure, and a failed ask keeps the figures before and asks again on the next read.
#include "galaxy/render/video_memory_reading.h"

#include <cstdint>
#include <cstdio>

namespace {

bool expect(bool ok, const char* what) {
    if (!ok) std::fprintf(stderr, "FAILED: %s\n", what);
    return ok;
}

struct Driver {
    int asked = 0;
    bool answers = true;
    std::uint64_t usage = 100u;
    std::uint64_t budget = 1000u;
    bool operator()(std::uint64_t& u, std::uint64_t& b) {
        ++asked;
        u = usage;
        b = budget;
        return answers;
    }
};

}  // namespace

int main() {
    using galaxy::render::VideoMemoryReading;
    bool ok = true;
    std::uint64_t usage = 0u;
    std::uint64_t budget = 0u;
    {
        // A walk per picture at 240 pictures a second: 24 reads in 100 ms ask the driver once.
        VideoMemoryReading reading;
        Driver driver;
        bool all = true;
        for (int i = 0; i < 24; ++i) {
            all &= reading.read(5000 + i * 4, false, driver, usage, budget);
        }
        ok &= expect(all && usage == 100u && budget == 1000u, "the figures are read");
        ok &= expect(driver.asked == 1, "24 reads within 100 ms ask the driver once");
        driver.usage = 200u;
        ok &= expect(reading.read(5100, false, driver, usage, budget) && usage == 200u && driver.asked == 2,
                     "100 ms on, the driver is asked again");
        driver.budget = 800u;
        ok &= expect(reading.read(5101, true, driver, usage, budget) && budget == 800u && driver.asked == 3,
                     "a budget the OS moved is read at once");
        ok &= expect(reading.budget() == 800u, "the budget last read");
        reading.forget();
        ok &= expect(reading.read(5102, false, driver, usage, budget) && driver.asked == 4, "after forget, a read asks");
    }
    {
        VideoMemoryReading reading;
        Driver driver;
        driver.answers = false;
        ok &= expect(!reading.read(0, false, driver, usage, budget) && usage == 0u && budget == 0u,
                     "a driver that never answers: no figures");
        ok &= expect(!reading.read(1, false, driver, usage, budget) && driver.asked == 2,
                     "and every read asks again until it does");
        driver.answers = true;
        driver.budget = 0u;
        ok &= expect(!reading.read(2, false, driver, usage, budget), "an answer with no budget is no figure");
        driver.budget = 1000u;
        ok &= expect(reading.read(3, false, driver, usage, budget) && budget == 1000u, "then an answer is kept");
        driver.answers = false;
        driver.usage = 999u;
        ok &= expect(reading.read(200, false, driver, usage, budget) && usage == 100u && budget == 1000u,
                     "a failed ask keeps the figures before");
        ok &= expect(reading.read(201, false, driver, usage, budget) && driver.asked == 6,
                     "and the next read asks again");
    }
    std::printf("%s\n", ok ? "video_memory_reading: every case holds" : "video_memory_reading: FAILED");
    return ok ? 0 : 1;
}
