#include "Game/System/InputRumbleData.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/System/NebulaInput.hpp"

namespace {
// Each pattern: its name, its length in frames, on/off per frame.
constexpr RumblePattern sRumblePatternTable[] = {
    {
        "最強【長】",
        100,
        {
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
        },
    },
    {
        "最強",
        30,
        {
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,
        },
    },
    {
        "強",
        30,
        {
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,
        },
    },
    {
        "中",
        30,
        {
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,
        },
    },
    {
        "弱",
        30,
        {
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,
        },
    },
    {
        "微弱",
        30,
        {
            RUMBLE_ON, RUMBLE_OFF, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,
        },
    },
    {
        "マリオ[スリップアップ]",
        25,
        {
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF, RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON,
        },
    },
    {
        "マリオ[亀投げ]",
        10,
        {
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
        },
    },
    {
        "マリオ[ホッパーため]",
        5,
        {
            RUMBLE_ON,
            RUMBLE_ON,
            RUMBLE_OFF,
            RUMBLE_ON,
            RUMBLE_ON,
        },
    },
    {
        "マリオ[変身]",
        60,
        {
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,
        },
    },
    {
        "マリオ[変身解除]",
        30,
        {
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_OFF,
        },
    },
    {
        "マリオ[しびれ]",
        60,
        {
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,
            RUMBLE_ON, RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON, RUMBLE_ON,
            RUMBLE_OFF,   RUMBLE_ON, RUMBLE_OFF,   RUMBLE_ON,
        },
    },
    {
        "マリオ[つぶれ]",
        120,
        {
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
        },
    },
};

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
