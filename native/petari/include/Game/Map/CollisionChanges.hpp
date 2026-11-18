#pragma once

#include <nebula_geometry.hpp>

#if defined(NEBULA_PLAIN_PATHS)
// The plain module's log of changes to collision a line query can see (P-140): a valid part that moved, turned, was
// scaled or made invalid, a part put in or taken out of a zone, a zone whose bounds changed. Each is noted as the
// spheres it covered before and after; a query whose line no noted sphere since reaches gets the answer it got then.
namespace CollisionChanges {
    // A sphere a change covered. A radius that is negative or not finite, or a centre that is not finite, stands for
    // everywhere.
    void note(const Vec3f& rCenter, f32 radius);
    // The changes noted so far.
    u64 count();
    // Whether every change noted since `since` (a count()) is far from the segment from rStart along rLine. False when
    // one may reach it, or when more were noted since than the log keeps.
    bool isLineUnchanged(u64 since, const Vec3f& rStart, const Vec3f& rLine);
}  // namespace CollisionChanges
#endif
