#include "Game/System/InputRumbleData.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/System/NebulaInput.hpp"

static RumblePattern gRumblePatternTable[] = {
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
    {},
};

namespace RumbleData {
    u32 getTableSize() {
        return ARRAY_SIZE(gRumblePatternTable) - 1;
    }

    RumblePattern* getData(u16 index) {
        return &gRumblePatternTable[index];
    }

    const RumblePattern* getPattern(const char* pName) {
        u32 hash = MR::getHashCode(pName);

        for (u16 i = 0; i < getTableSize(); i++) {
            if (getData(i)->mHash == hash) {
                return &gRumblePatternTable[i];
            }
        }

        return nullptr;
    }

    void initHashValue() {
        for (u32 i = 0; i < getTableSize(); i++) {
            RumblePattern* pPattern = getData(i);

            pPattern->mHash = MR::getHashCode(pPattern->mName);
        }
    }

    void checkHashCollision() {
    }
};  // namespace RumbleData
