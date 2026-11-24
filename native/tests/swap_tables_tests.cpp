// SwapTables (R2.4): a form walked twice with the same swapped image registers its list once; a few distinct swaps are
// kept side by side; new texture stamps forget them all. `walk` stands for MaterialDraw::tableOf: a kept table, else the
// list built and registered (counted) and kept.
#include "Game/Render/SwapTables.hpp"

#include <cstdio>
#include <cstring>

namespace {
    int sFailures;
    unsigned sRegistered;

    void expect(bool ok, const char* pWhat) {
        if (!ok) {
            printf("FAIL %s\n", pWhat);
            sFailures++;
        }
    }

    unsigned walk(SwapTables& rTables, unsigned texture, bool coordinate, unsigned stamp, unsigned dataStamp) {
        const unsigned kept = rTables.find(0, texture, coordinate, stamp, dataStamp);
        if (kept != SwapTables::cNone) {
            return kept;
        }
        const unsigned table = 100 + sRegistered++;
        rTables.keep(0, texture, coordinate, stamp, dataStamp, table);
        return table;
    }
}  // namespace

int main() {
    SwapTables tables;
    memset(&tables, 0, sizeof(tables));  // as Material::formState clears its states

    const unsigned eyesOpen = walk(tables, 7, false, 1, 1);
    expect(walk(tables, 7, false, 1, 1) == eyesOpen && sRegistered == 1, "walked twice, the swapped list is registered once");

    const unsigned eyesShut = walk(tables, 8, false, 1, 1);
    for (int frame = 0; frame < 10; frame++) {
        expect(walk(tables, frame % 2 == 0 ? 7 : 8, false, 1, 1) == (frame % 2 == 0 ? eyesOpen : eyesShut),
               "two images swapped in turn keep their own tables");
    }
    expect(sRegistered == 2, "each distinct swap registered once");

    const unsigned screen = walk(tables, 7, true, 1, 1);
    expect(screen != eyesOpen && sRegistered == 3, "the screen-coordinate sampler makes a list of its own");

    walk(tables, 9, false, 1, 1);
    walk(tables, 10, false, 1, 1);  // a fifth swap takes the oldest's place
    expect(tables.find(0, 7, false, 1, 1) == SwapTables::cNone && tables.find(0, 10, false, 1, 1) != SwapTables::cNone,
           "past cCount swaps the oldest goes");

    const unsigned registered = sRegistered;
    walk(tables, 8, false, 2, 1);
    expect(sRegistered == registered + 1 && tables.find(0, 9, false, 2, 1) == SwapTables::cNone,
           "a new material texture stamp forgets every kept swap");
    walk(tables, 8, false, 2, 3);
    expect(sRegistered == registered + 2, "a new model data texture stamp does too");

    expect(tables.find(0, 0, false, 2, 3) == SwapTables::cNone, "no image is never kept");

    if (sFailures == 0) {
        printf("swap tables: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
