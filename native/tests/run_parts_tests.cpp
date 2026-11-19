// SPDX-License-Identifier: GPL-3.0-only
// The benchmark run screen's part names (Game/Screen/NebulaRunParts.hpp): each part the host sends has its own name;
// a part past the table has none, not another part's (P-324).
#include "Game/Screen/NebulaRunParts.hpp"

#include <stdio.h>
#include <string.h>

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
    expect(strcmp(NebulaRunParts::name(NEBULA_BENCHMARK_PART_AGAIN), "FIRST STAGE AGAIN") == 0, "the last part's name");
    bool distinct = true;
    for (u32 a = 0; a < NebulaRunParts::cCount; a++) {
        for (u32 b = a + 1; b < NebulaRunParts::cCount; b++) {
            distinct = distinct && strcmp(NebulaRunParts::name(a), NebulaRunParts::name(b)) != 0;
        }
    }
    expect(distinct, "each part its own name");
    expect(NebulaRunParts::name(NebulaRunParts::cCount) == nullptr, "a part past the table has no name");
    expect(NebulaRunParts::name(0xFFFFFFFFu) == nullptr, "the largest part has no name");
    if (sFailures != 0) {
        printf("%d run parts test failures\n", sFailures);
        return 1;
    }
    printf("run parts tests passed\n");
    return 0;
}
