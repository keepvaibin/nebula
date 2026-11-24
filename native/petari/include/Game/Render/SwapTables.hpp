#pragma once

// The texture tables of the draws that swap one map's image (PlayerModel's swapped images, MaterialDraw's screen-coordinate
// picture): a form keeps the tables of its last cCount distinct swaps, each list built and registered once (FM 2.10), while
// the material's and its model data's textures stand (their stamps). A texture id is never given again once freed, and a
// table lives while its textures do, so a kept table stays right for its swap. Header only:
// native/tests/swap_tables_tests.cpp.

#include <stdint.h>

struct SwapTables {
    static const uint32_t cCount = 4;
    static const uint32_t cNone = 0xFFFFFFFFu;

    struct Swap {
        uint32_t mTexture;  // 0: empty
        uint32_t mTable;
        uint8_t mMap;
        uint8_t mCoordinate;
    };

    // The table kept for `texture` in `map` (with the screen-coordinate sampler or not), or cNone.
    uint32_t find(uint32_t map, uint32_t texture, bool coordinate, uint32_t stamp, uint32_t dataStamp) const {
        if (texture == 0 || stamp != mStamp || dataStamp != mDataStamp) {
            return cNone;
        }
        for (uint32_t i = 0; i < cCount; i++) {
            const Swap& rSwap = mSwaps[i];
            if (rSwap.mTexture == texture && rSwap.mMap == map && (rSwap.mCoordinate != 0) == coordinate) {
                return rSwap.mTable;
            }
        }
        return cNone;
    }

    // Keeps `table` for the swap, in place of the oldest; new stamps forget every swap first.
    void keep(uint32_t map, uint32_t texture, bool coordinate, uint32_t stamp, uint32_t dataStamp, uint32_t table) {
        if (texture == 0) {
            return;
        }
        if (stamp != mStamp || dataStamp != mDataStamp) {
            for (uint32_t i = 0; i < cCount; i++) {
                mSwaps[i].mTexture = 0;
            }
            mStamp = stamp;
            mDataStamp = dataStamp;
            mNext = 0;
        }
        Swap& rSwap = mSwaps[mNext];
        rSwap.mTexture = texture;
        rSwap.mTable = table;
        rSwap.mMap = static_cast< uint8_t >(map);
        rSwap.mCoordinate = coordinate ? 1 : 0;
        mNext = static_cast< uint8_t >((mNext + 1) % cCount);
    }

    // All zero is empty (Material::formState clears its states with memset).
    Swap mSwaps[cCount];
    uint32_t mStamp;
    uint32_t mDataStamp;
    uint8_t mNext;
};
