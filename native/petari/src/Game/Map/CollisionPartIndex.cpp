#include "Game/Map/CollisionPartIndex.hpp"

#if defined(NEBULA_PLAIN_PATHS)
#include "Game/Map/CollisionCategorizedKeeper.hpp"
#include "Game/Map/CollisionParts.hpp"

namespace {
    // The first grid's cell; each grid's is twice the last's.
    const f32 cFirstCell = 256.0f;
    // A cell's place on an axis is held to 20 bits.
    const s32 cAxisLimit = 1 << 19;
    // A grid whose cells a query's box meets past this many gives the query all its parts.
    const s64 cMaxQueryCells = 64;

    bool isNumber(f32 value) {
        return value == value && value - value == 0.0f;
    }

    f32 cellSizeOf(s32 level) {
        return cFirstCell * static_cast< f32 >(1 << level);
    }

    s32 cellOf(f32 value, f32 cellSize) {
        const f32 at = value / cellSize;
        if (!(at > static_cast< f32 >(-cAxisLimit))) {
            return -cAxisLimit;
        }
        if (!(at < static_cast< f32 >(cAxisLimit - 1))) {
            return cAxisLimit - 1;
        }
        s32 cell = static_cast< s32 >(at);
        if (static_cast< f32 >(cell) > at) {
            cell--;
        }
        return cell;
    }

    u64 keyOf(s32 level, s32 x, s32 y, s32 z) {
        // Never 0: an empty cell's key.
        return (static_cast< u64 >(level + 1) << 60) | (static_cast< u64 >(x + cAxisLimit) << 40) | (static_cast< u64 >(y + cAxisLimit) << 20) |
               static_cast< u64 >(z + cAxisLimit);
    }

    u32 hashOf(u64 key) {
        return static_cast< u32 >((key * 0x9E3779B97F4A7C15ull) >> 32);
    }

    // A part's place and grid: the first whose cell is at least as wide as the part's box, its radius out by more than
    // the float rounding of the queries' own tests. False (every query takes it) for a place or radius that is not a
    // number, a radius below 0, or a box wider than the last grid's cell.
    bool partPlace(CollisionParts* pParts, Vec3f* pPlace, s32* pLevel) {
        *pPlace = pParts->getTrans();
        const f32 radius = pParts->mBoundingRadius;
        if (!isNumber(pPlace->x) || !isNumber(pPlace->y) || !isNumber(pPlace->z) || !isNumber(radius) || radius < 0.0f) {
            return false;
        }

        const f32 width = 2.0f * (radius + radius * 1.0e-4f + 1.0f);
        for (s32 level = 0; level < CollisionPartIndex::cLevels; level++) {
            if (width <= cellSizeOf(level)) {
                *pLevel = level;
                return true;
            }
        }
        return false;
    }
}  // namespace

CollisionPartIndex::CollisionPartIndex()
    : mCells(), mCellMask(0), mEntries(), mLevelOf(), mByLevel(), mLevelStart(), mAlways(), mAlwaysCount(0), mBuiltChanges(0), mBuiltCount(0),
      mBuilt(false), mSeenChanges(0), mSeenCount(-1), mQuietQueries(0) {
}

CollisionPartIndex::Cell* CollisionPartIndex::find(u64 key) const {
    for (u32 at = hashOf(key) & mCellMask;; at = (at + 1) & mCellMask) {
        Cell& rCell = const_cast< Cell& >(mCells[at]);
        if (rCell.mKey == key) {
            return &rCell;
        }
        if (rCell.mKey == 0) {
            return nullptr;
        }
    }
}

CollisionPartIndex::Cell* CollisionPartIndex::findOrAdd(u64 key) {
    u32 at = hashOf(key) & mCellMask;
    while (mCells[at].mKey != 0 && mCells[at].mKey != key) {
        at = (at + 1) & mCellMask;
    }
    Cell& rCell = mCells[at];
    if (rCell.mKey == 0) {
        rCell.mKey = key;
        rCell.mStart = 0;
        rCell.mCount = 0;
    }
    return &rCell;
}

void CollisionPartIndex::build(const CollisionZone& rZone) {
    const s32 count = rZone.mParts.size();
    const u32 room = static_cast< u32 >(count > 0 ? count : 1);
    s32* const pLevelOf = mLevelOf.room(room);
    s32* const pAlways = mAlways.room(room);
    s32* const pByLevel = mByLevel.room(room);
    s32* const pEntries = mEntries.room(room);

    // Each part's grid; the grids' starts in mByLevel.
    u32 perLevel[cLevels] = {};
    mAlwaysCount = 0;
    for (s32 i = 0; i < count; i++) {
        Vec3f place;
        s32 level;
        if (partPlace(rZone.mParts[i], &place, &level)) {
            pLevelOf[i] = level;
            perLevel[level]++;
        } else {
            pLevelOf[i] = -1;
            pAlways[mAlwaysCount++] = i;
        }
    }
    u32 start = 0;
    for (s32 level = 0; level < cLevels; level++) {
        mLevelStart[level] = start;
        start += perLevel[level];
        perLevel[level] = 0;
    }
    mLevelStart[cLevels] = start;

    // The cells: a table twice the parts at least, each cell's parts counted, given their start, then filled in list
    // order.
    u32 size = 64;
    while (size < start * 2u) {
        size *= 2;
    }
    Cell* const pCells = mCells.room(size);
    for (u32 i = 0; i < size; i++) {
        pCells[i].mKey = 0;
    }
    mCellMask = size - 1;

    for (int pass = 0; pass < 2; pass++) {
        for (s32 i = 0; i < count; i++) {
            const s32 level = pLevelOf[i];
            if (level < 0) {
                continue;
            }
            const Vec3f place(rZone.mParts[i]->getTrans());
            const f32 cellSize = cellSizeOf(level);
            Cell* const pCell = findOrAdd(keyOf(level, cellOf(place.x, cellSize), cellOf(place.y, cellSize), cellOf(place.z, cellSize)));
            if (pass == 1) {
                pEntries[pCell->mStart + pCell->mCount] = i;
                pByLevel[mLevelStart[level] + perLevel[level]++] = i;
            }
            pCell->mCount++;
        }

        if (pass == 0) {
            u32 cellStart = 0;
            for (u32 i = 0; i < size; i++) {
                if (pCells[i].mKey != 0) {
                    pCells[i].mStart = cellStart;
                    cellStart += pCells[i].mCount;
                    pCells[i].mCount = 0;
                }
            }
        }
    }
}

bool CollisionPartIndex::gather(const CollisionZone& rZone, u32 changes, const Vec3f& rMin, const Vec3f& rMax, u64* pBits) {
    const s32 count = rZone.mParts.size();
    if (count < cFewParts) {
        return false;
    }
    if (!isNumber(rMin.x) || !isNumber(rMin.y) || !isNumber(rMin.z) || !isNumber(rMax.x) || !isNumber(rMax.y) || !isNumber(rMax.z)) {
        return false;
    }

    if (mSeenChanges != changes || mSeenCount != count) {
        mSeenChanges = changes;
        mSeenCount = count;
        mQuietQueries = 0;
    } else if (mQuietQueries < cQuietQueries) {
        mQuietQueries++;
    }

    if (!mBuilt || mBuiltChanges != changes || mBuiltCount != count) {
        if (mQuietQueries < cQuietQueries) {
            return false;
        }
        build(rZone);
        mBuilt = true;
        mBuiltChanges = changes;
        mBuiltCount = count;
    }

    for (u32 i = 0; i < mAlwaysCount; i++) {
        const u32 place = static_cast< u32 >(mAlways[i]);
        pBits[place / 64u] |= 1ull << (place % 64u);
    }

    for (s32 level = 0; level < cLevels; level++) {
        const u32 first = mLevelStart[level];
        const u32 last = mLevelStart[level + 1];
        if (first == last) {
            continue;
        }

        // A part of this grid lies within half a cell of its place, so its place is within half a cell of the box.
        const f32 cellSize = cellSizeOf(level);
        const f32 half = 0.5f * cellSize;
        const s32 lowX = cellOf(rMin.x - half, cellSize), highX = cellOf(rMax.x + half, cellSize);
        const s32 lowY = cellOf(rMin.y - half, cellSize), highY = cellOf(rMax.y + half, cellSize);
        const s32 lowZ = cellOf(rMin.z - half, cellSize), highZ = cellOf(rMax.z + half, cellSize);
        const s64 cells = (highX >= lowX && highY >= lowY && highZ >= lowZ)
                              ? static_cast< s64 >(highX - lowX + 1) * (highY - lowY + 1) * (highZ - lowZ + 1)
                              : 0;
        if (cells > cMaxQueryCells || cells > static_cast< s64 >(last - first)) {
            // More cells than parts: the grid's parts, every one.
            for (u32 i = first; i < last; i++) {
                const u32 place = static_cast< u32 >(mByLevel[i]);
                pBits[place / 64u] |= 1ull << (place % 64u);
            }
            continue;
        }

        for (s32 x = lowX; x <= highX; x++) {
            for (s32 y = lowY; y <= highY; y++) {
                for (s32 z = lowZ; z <= highZ; z++) {
                    const Cell* const pCell = find(keyOf(level, x, y, z));
                    if (pCell == nullptr) {
                        continue;
                    }
                    for (u32 i = 0; i < pCell->mCount; i++) {
                        const u32 place = static_cast< u32 >(mEntries[pCell->mStart + i]);
                        pBits[place / 64u] |= 1ull << (place % 64u);
                    }
                }
            }
        }
    }

#if defined(NEBULA_COLLISION_INDEX_VERIFY)
    // A build that checks the index: every part whose box meets the query's is among those given.
    for (s32 i = 0; i < count; i++) {
        CollisionParts* const pParts = rZone.mParts[i];
        const Vec3f place(pParts->getTrans());
        const f32 radius = pParts->mBoundingRadius;
        const bool meets = !(place.x < rMin.x - radius || rMax.x + radius < place.x || place.y < rMin.y - radius ||
                             rMax.y + radius < place.y || place.z < rMin.z - radius || rMax.z + radius < place.z);
        if (meets && (pBits[static_cast< u32 >(i) / 64u] & (1ull << (static_cast< u32 >(i) % 64u))) == 0) {
            NEBULA_PANIC("collision index left out part %d of zone %d", i, rZone.mZoneID);
        }
    }
#endif
    return true;
}

CollisionPartWalk::CollisionPartWalk(CollisionZone* pZone, const Vec3f& rMin, const Vec3f& rMax)
    : mCount(pZone->mParts.size()), mAt(0), mWords((static_cast< u32 >(mCount) + 63u) / 64u), mWord(0), mCurrent(0), mIndexed(false),
      mBits(mWords > 0 ? mWords : 1u) {
    if (mCount < CollisionPartIndex::cFewParts) {
        return;
    }

    for (u32 i = 0; i < mWords; i++) {
        mBits[i] = 0;
    }
    mIndexed = pZone->mIndex.gather(*pZone, pZone->mChanges, rMin, rMax, &mBits[0]);
    if (mIndexed) {
        mCurrent = mBits[0];
    }
}
#endif
