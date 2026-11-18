#pragma once

#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class CollisionZone;

// A zone's parts by place, the plain module's (P-129). Every query of the keeper takes a part only when the part's
// bound box (its place, out by its bounding radius on each axis) meets the query's box: a point, a ball's box, a line's
// box. The index keeps the parts in grids by size: a part goes to the grid whose cells are at least as wide as its box,
// into the one cell that holds its place, so a query looks in at most two cells an axis in each grid that holds parts.
// It gives a query the parts in those cells as bits by the parts' places in the zone's list, so the query takes them in
// list order as the walk of every part did; the query's own tests then decide as before. Parts with a place or radius
// that is not a number, or a radius below 0, are given to every query. It is made again when the zone's list changed or
// one of its parts moved or changed its radius (CollisionZone::mChanges).
class CollisionPartIndex {
public:
    CollisionPartIndex();

    // A zone with fewer parts is walked whole.
    static const s32 cFewParts = 48;
    // Queries a zone's parts must keep still for before the index is made again.
    static const u32 cQuietQueries = 2;
    // The grids: cells of 256 units, then each twice the last.
    static const s32 cLevels = 16;

    // Sets in pBits (a bit a part, (count + 63) / 64 words, all clear) every part whose box may meet [rMin, rMax];
    // false when the index does not serve the query (few parts, parts that changed since the last queries, or a box
    // that is not a number), when the caller takes every part. `changes` is the zone's count of changes.
    bool gather(const CollisionZone& rZone, u32 changes, const Vec3f& rMin, const Vec3f& rMax, u64* pBits);

private:
    struct Cell {
        u64 mKey;  // 0: empty
        u32 mStart;
        u32 mCount;
    };

    void build(const CollisionZone& rZone);
    Cell* find(u64 key) const;
    Cell* findOrAdd(u64 key);

    MR::GrowBuffer< Cell > mCells;
    u32 mCellMask;
    MR::GrowBuffer< s32 > mEntries;  // part places, cell by cell
    MR::GrowBuffer< s32 > mLevelOf;  // each part's grid, -1: given to every query
    MR::GrowBuffer< s32 > mByLevel;  // part places, grid by grid
    u32 mLevelStart[cLevels + 1];
    MR::GrowBuffer< s32 > mAlways;   // part places every query takes
    u32 mAlwaysCount;
    u32 mBuiltChanges;
    s32 mBuiltCount;
    bool mBuilt;
    // The zone's count of changes at the last query, and how many queries since saw it unchanged: an index is made
    // again only once the parts have kept still for cQuietQueries queries, so parts that change between every query
    // cost a walk, not a build, each.
    u32 mSeenChanges;
    s32 mSeenCount;
    u32 mQuietQueries;
};

// The places of a zone's parts a query takes, in list order: the index's when it serves the query, else every place.
class CollisionPartWalk {
public:
    CollisionPartWalk(CollisionZone* pZone, const Vec3f& rMin, const Vec3f& rMax);
    CollisionPartWalk(const CollisionPartWalk&) = delete;
    CollisionPartWalk& operator=(const CollisionPartWalk&) = delete;

    // The next place, -1 after the last.
    s32 next() {
        if (!mIndexed) {
            return mAt < mCount ? mAt++ : -1;
        }

        while (mCurrent == 0) {
            if (++mWord >= mWords) {
                return -1;
            }
            mCurrent = mBits[mWord];
        }

        const s32 place = static_cast< s32 >(mWord * 64u + static_cast< u32 >(__builtin_ctzll(mCurrent)));
        mCurrent &= mCurrent - 1;
        return place;
    }

private:
    s32 mCount;
    s32 mAt;
    u32 mWords;
    u32 mWord;
    u64 mCurrent;
    bool mIndexed;
    MR::LocalArray< u64, 32 > mBits;
};
