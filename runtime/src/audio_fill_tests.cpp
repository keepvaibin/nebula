// SPDX-License-Identifier: GPL-3.0-only
// audio_fill.h: the Windows output's fill (P-072) and SDL's chunked pull (P-037).
#include "galaxy/audio_fill.h"

#include <cstdio>
#include <vector>

namespace {
int failures = 0;
void expect(bool value, const char* what) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}
}  // namespace

int main() {
    using galaxy::host::ChunkedPull;
    using galaxy::host::OutputFill;
    constexpr std::int64_t kPeriod = 1000;  // clock units per engine period

    // The fill: two periods at the start, a period more for a late wake or a slow render, never back down.
    OutputFill fill;
    fill.open(480u, 4800u);
    expect(fill.fill() == 960u, "starts at two periods");
    expect(fill.wake(kPeriod, kPeriod, 0u) == 960u, "the first wake fills an empty device");
    expect(fill.underruns() == 0u, "the first wake is no under-run");
    expect(fill.wake(kPeriod, kPeriod, 960u) == 0u, "a full device takes nothing");
    expect(fill.wake(kPeriod * 2, kPeriod, 0u) == 1440u && fill.fill() == 1440u, "a late wake raises a period");
    expect(fill.underruns() == 1u, "a wake that finds the device empty is an under-run");
    for (int second = 0; second < 600; ++second) {
        (void)fill.wake(kPeriod, kPeriod, fill.fill());
    }
    expect(fill.fill() == 1440u, "quiet minutes do not lower the fill again");
    fill.rendered(kPeriod, kPeriod);
    fill.rendered(kPeriod, kPeriod);
    fill.rendered(kPeriod, kPeriod);
    expect(fill.fill() == 1920u, "slow renders raise it to four periods at most");
    fill.open(480u, 4800u);
    expect(fill.fill() == 960u && fill.underruns() == 1u, "a device opened again starts over; under-runs stay counted");
    fill.open(480u, 600u);
    expect(fill.fill() == 600u, "never more than the device's buffer");

    // The pull: any number of frames from one buffer, made before the device starts.
    ChunkedPull pull(256u, 2u);
    const float* const buffer = pull.data();
    std::vector<std::uint32_t> rendered;
    std::uint32_t put = 0u;
    bool same = true;
    pull.pull(1000u,
              [&](float* out, std::uint32_t n) {
                  same = same && out == buffer;
                  for (std::uint32_t i = 0; i < n * 2u; ++i) out[i] = static_cast<float>(put + i / 2u);
                  rendered.push_back(n);
              },
              [&](const float* out, std::uint32_t n) {
                  same = same && out[0] == static_cast<float>(put);
                  put += n;
              });
    expect(same && pull.data() == buffer, "every chunk in the one buffer, put as rendered");
    expect(rendered.size() == 4u && rendered[0] == 256u && rendered[3] == 232u && put == 1000u,
           "chunks of at most 256 frames, 1000 in all");

    if (failures == 0) std::printf("audio fill and pull tests passed\n");
    return failures == 0 ? 0 : 1;
}
