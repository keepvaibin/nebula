#include "Game/Map/CollisionChanges.hpp"

#if defined(NEBULA_PLAIN_PATHS)
namespace {
    // The last cKept changes, at their count modulo cKept. A query whose line was checked more changes ago queries again.
    constexpr u64 cKept = 64;

    struct Change {
        f32 mX;
        f32 mY;
        f32 mZ;
        f32 mRadius;
    };

    Change sChanges[cKept];
    u64 sCount = 0;

    // A number that is neither infinite nor NaN (both give NaN here).
    inline bool isFiniteValue(f64 value) {
        return value - value == 0.0;
    }

    inline f64 absolute(f64 value) {
        return value < 0.0 ? -value : value;
    }
}  // namespace

namespace CollisionChanges {
    void note(const Vec3f& rCenter, f32 radius) {
        sChanges[sCount % cKept] = {rCenter.x, rCenter.y, rCenter.z, radius};
        sCount++;
    }

    u64 count() {
        return sCount;
    }

    // The game's line tests are made in floats: a sphere they find touching the line is at most a few parts in ten
    // million of the sizes involved past it. A change counts as reaching the line when its sphere comes within a part
    // in ten thousand of those sizes, plus one unit, of the segment: far wider, so it only sends more queries again.
    bool isLineUnchanged(u64 since, const Vec3f& rStart, const Vec3f& rLine) {
        if (sCount - since > cKept) {
            return false;
        }

        const f64 startX = rStart.x;
        const f64 startY = rStart.y;
        const f64 startZ = rStart.z;
        const f64 lineX = rLine.x;
        const f64 lineY = rLine.y;
        const f64 lineZ = rLine.z;
        const f64 lineSquared = lineX * lineX + lineY * lineY + lineZ * lineZ;
        const f64 reach = absolute(startX) + absolute(startY) + absolute(startZ) + absolute(lineX) + absolute(lineY) + absolute(lineZ);
        if (!isFiniteValue(reach) || !isFiniteValue(lineSquared)) {
            return false;
        }

        for (u64 k = since; k != sCount; k++) {
            const Change& rChange = sChanges[k % cKept];
            const f64 radius = rChange.mRadius;
            const f64 centerX = rChange.mX;
            const f64 centerY = rChange.mY;
            const f64 centerZ = rChange.mZ;
            const f64 size = absolute(centerX) + absolute(centerY) + absolute(centerZ);
            if (!(radius >= 0.0) || !isFiniteValue(radius) || !isFiniteValue(size)) {
                return false;
            }

            const f64 toX = centerX - startX;
            const f64 toY = centerY - startY;
            const f64 toZ = centerZ - startZ;
            f64 along = lineSquared > 0.0 ? (toX * lineX + toY * lineY + toZ * lineZ) / lineSquared : 0.0;
            along = along < 0.0 ? 0.0 : (along > 1.0 ? 1.0 : along);
            const f64 offX = toX - along * lineX;
            const f64 offY = toY - along * lineY;
            const f64 offZ = toZ - along * lineZ;
            const f64 near = radius + 1e-4 * (radius + size + reach) + 1.0;
            if (offX * offX + offY * offY + offZ * offZ <= near * near) {
                return false;
            }
        }

        return true;
    }
}  // namespace CollisionChanges
#endif
