#include "Game/System/InputRumbleData.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/System/NebulaInput.hpp"

namespace {
// Each pattern: its name, its length in frames, on/off per frame.
constexpr u8 cPattern0[] = {
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_ON,
};

constexpr u8 cPattern1[] = {
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,
};

constexpr u8 cPattern2[] = {
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,
};

constexpr u8 cPattern3[] = {
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,
};

constexpr u8 cPattern4[] = {
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,
};

constexpr u8 cPattern5[] = {
            RUMBLE_ON, RUMBLE_OFF, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,
};

constexpr u8 cPattern6[] = {
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON,
};

constexpr u8 cPattern7[] = {
            RUMBLE_OFF,
            RUMBLE_OFF,
            RUMBLE_OFF,
            RUMBLE_ON,
            RUMBLE_ON,
            RUMBLE_OFF,
            RUMBLE_ON,
            RUMBLE_OFF,
            RUMBLE_ON,
            RUMBLE_ON,
};

constexpr u8 cPattern8[] = {
            RUMBLE_ON,
            RUMBLE_ON,
            RUMBLE_OFF,
            RUMBLE_ON,
            RUMBLE_ON,
};

constexpr u8 cPattern9[] = {
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,
};

constexpr u8 cPattern10[] = {
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,
};

constexpr u8 cPattern11[] = {
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON,
};

constexpr u8 cPattern12[] = {
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON,
};

// A step is one of the game's 60 updates a second.
constexpr RumblePattern sRumblePatternTable[] = {
    {"最強【長】", 100, cPattern0},
    {"最強", 30, cPattern1},
    {"強", 30, cPattern2},
    {"中", 30, cPattern3},
    {"弱", 30, cPattern4},
    {"微弱", 30, cPattern5},
    {"マリオ[スリップアップ]", 25, cPattern6},
    {"マリオ[亀投げ]", 10, cPattern7},
    {"マリオ[ホッパーため]", 5, cPattern8},
    {"マリオ[変身]", 60, cPattern9},
    {"マリオ[変身解除]", 30, cPattern10},
    {"マリオ[しびれ]", 60, cPattern11},
    {"マリオ[つぶれ]", 120, cPattern12},
};

// Each pattern has a value for each of its steps.
static_assert(ARRAY_SIZE(cPattern0) == 100 && ARRAY_SIZE(cPattern1) == 30 && ARRAY_SIZE(cPattern2) == 30 && ARRAY_SIZE(cPattern3) == 30 &&
                  ARRAY_SIZE(cPattern4) == 30 && ARRAY_SIZE(cPattern5) == 30 && ARRAY_SIZE(cPattern6) == 25 && ARRAY_SIZE(cPattern7) == 10 &&
                  ARRAY_SIZE(cPattern8) == 5 && ARRAY_SIZE(cPattern9) == 60 && ARRAY_SIZE(cPattern10) == 30 && ARRAY_SIZE(cPattern11) == 60 &&
                  ARRAY_SIZE(cPattern12) == 120,
              "a pattern's step count matches its values");

constexpr u32 cPatternNum = ARRAY_SIZE(sRumblePatternTable);

// MR::getHashCode, at compile time.
constexpr u32 hashOf(const char* pStr) {
    u32 hash = 0;
    for (; *pStr != '\0'; pStr++) {
        hash = static_cast< u32 >(static_cast< s8 >(*pStr)) + hash * 31;
    }
    return hash;
}

struct PatternHashes {
    u32 mHash[cPatternNum];
};

constexpr PatternHashes makeHashes() {
    PatternHashes hashes = {};
    for (u32 i = 0; i < cPatternNum; i++) {
        hashes.mHash[i] = hashOf(sRumblePatternTable[i].mName);
    }
    return hashes;
}

constexpr PatternHashes cPatternHashes = makeHashes();

constexpr bool isEachHashDistinct() {
    for (u32 i = 0; i < cPatternNum; i++) {
        for (u32 j = i + 1; j < cPatternNum; j++) {
            if (cPatternHashes.mHash[i] == cPatternHashes.mHash[j]) {
                return false;
            }
        }
    }
    return true;
}
static_assert(isEachHashDistinct(), "a name finds one pattern, and InputRumble tells patterns apart by address");
};  // namespace

namespace RumbleData {
    const RumblePattern* getPattern(const char* pName) {
        const u32 hash = MR::getHashCode(pName);
        for (u32 i = 0; i < cPatternNum; i++) {
            if (cPatternHashes.mHash[i] == hash) {
                return &sRumblePatternTable[i];
            }
        }

        return nullptr;
    }
};  // namespace RumbleData
