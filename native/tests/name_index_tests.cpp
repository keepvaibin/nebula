// MR::NameIndexOf: first name kept, case folding, first of a hash (findHash), growth and rename keep probe order.
#include "Game/Util/NameIndex.hpp"
#include "Game/Util/HashUtil.hpp"

#include <cstdio>
#include <cstring>

// StringUtil.cpp's two compares, as they are there.
namespace MR {
    bool isEqualString(const char* pStr1, const char* pStr2) {
        return strcmp(pStr1, pStr2) == 0;
    }

    bool isEqualStringCase(const char* pStr1, const char* pStr2) {
        return ::nebula_stricmp(pStr1, pStr2) == 0;
    }
}  // namespace MR

namespace {
    int sFailures;

    void expect(bool ok, const char* pWhat) {
        if (!ok) {
            printf("FAIL %s\n", pWhat);
            sFailures++;
        }
    }

    const char* const cNames[] = {"SwitchArea", "Water", "WaterArea", "CubeCamera", "LightArea", "MirrorArea", "DeathArea",
                                  "RestartCube", "ChangeBgmCube", "SoundEmitterCube", "PlaneModeCube", "PipeModeCube"};
    const u32 cCount = sizeof(cNames) / sizeof(cNames[0]);
}  // namespace

int main() {
    // Exact names; a later equal name is refused and the first keeps its value.
    MR::NameIndex exact;
    exact.reset(4);
    for (u32 i = 0; i < cCount; i++) {
        expect(exact.add(cNames[i], i), "add new name");
    }
    expect(!exact.add("Water", 99), "equal name refused");
    expect(exact.count() == cCount, "count after refusal");
    for (u32 i = 0; i < cCount; i++) {
        expect(exact.find(cNames[i]) == i, "find each name (grown from 4)");
    }
    expect(exact.find("water") == MR::NameIndex::cNoValue, "exact is case-sensitive");
    expect(exact.find("Wat") == MR::NameIndex::cNoValue, "a prefix is not a name");

    // Case folded: any case finds it, and an equal name in another case is refused.
    MR::NameIndexOf< MR::NameKeyCase > folded;
    folded.reset(cCount);
    for (u32 i = 0; i < cCount; i++) {
        folded.add(cNames[i], i);
    }
    expect(folded.find("WATERAREA") == 2, "case-folded find");
    expect(!folded.add("cubecamera", 50), "case-folded equal refused");

    // Names sharing one hash: findHash gives the first added, find tells them apart, before and after growth.
    MR::NameIndex shared;
    shared.reset(2);
    const u32 hash = 0x12345u;
    expect(shared.add("a", hash, 10), "shared hash a");
    expect(shared.add("b", hash, 11), "shared hash b");
    expect(shared.add("c", hash, 12), "shared hash c");
    expect(shared.findHash(hash) == 10, "findHash first of a hash");
    for (u32 i = 0; i < 40; i++) {
        static char sExtra[40][8];
        snprintf(sExtra[i], sizeof(sExtra[i]), "x%u", i);
        shared.add(sExtra[i], i * 0x9E3779B9u, 100 + i);
    }
    expect(shared.findHash(hash) == 10, "findHash first of a hash after growth");
    expect(shared.find("b", hash) == 11 && shared.find("c", hash) == 12, "shared hash names apart after growth");

    // Rename keeps the value; the old name is gone and the rest of its probe run is still found.
    expect(shared.rename("a", "z"), "rename");
    expect(shared.find("a", hash) == MR::NameIndex::cNoValue, "old name gone");
    expect(shared.find("z") == 10, "new name keeps value");
    expect(shared.find("b", hash) == 11 && shared.find("c", hash) == 12, "run intact after removal");
    expect(shared.findHash(hash) == 11, "first remaining of the hash");
    expect(!shared.rename("b", "c"), "rename onto a held name refused");

    // Empty: nothing found, no table needed.
    MR::NameIndex empty;
    expect(empty.find("Water") == MR::NameIndex::cNoValue && empty.findHash(1) == MR::NameIndex::cNoValue, "empty index");

    if (sFailures == 0) {
        printf("name index: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
