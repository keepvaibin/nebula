#include "Game/Map/KCollision.hpp"

namespace {
    // A grid coordinate kept in 0..~mask, compared as signed (a point below the box's minimum wraps negative).
    void clampToGridBottom(u32* pValue) {
        if (static_cast< s32 >(*pValue) < 0) {
            *pValue = 0;
        }
    }

    void clampToGridTop(u32* pValue, s32 mask) {
        const s32 top = ~mask;
        if (top < static_cast< s32 >(*pValue)) {
            *pValue = top;
        }
    }
};  // namespace

bool KCollisionServer::isInsideMinMaxInLocalSpace(const V3u& rPoint) const {
    return (rPoint.x & mFile->mXMask) == 0 && (rPoint.y & mFile->mYMask) == 0 && (rPoint.z & mFile->mZMask) == 0;
}

// The box from A to B in the grid's space, cut to the grid; false when nothing of it is left.
bool KCollisionServer::outCheck(const Vec3f* pPosA, const Vec3f* pPosB, V3u* pPointA, V3u* pPointB) const {
    objectSpaceToLocalSpace(pPointA, *pPosA);
    objectSpaceToLocalSpace(pPointB, *pPosB);

    ::clampToGridBottom(&pPointA->x);
    ::clampToGridBottom(&pPointA->y);
    ::clampToGridBottom(&pPointA->z);
    ::clampToGridTop(&pPointB->x, mFile->mXMask);
    ::clampToGridTop(&pPointB->y, mFile->mYMask);
    ::clampToGridTop(&pPointB->z, mFile->mZMask);

    return !(static_cast< s32 >(pPointB->x) < static_cast< s32 >(pPointA->x) || static_cast< s32 >(pPointB->y) < static_cast< s32 >(pPointA->y) ||
             static_cast< s32 >(pPointB->z) < static_cast< s32 >(pPointA->z));
}

void KCollisionServer::objectSpaceToLocalSpace(V3u* pPoint, const Vec3f& rPos) const {
    pPoint->x = nebula_f32_to_s32_console(rPos.x - mFile->mMin.x);
    pPoint->y = nebula_f32_to_s32_console(rPos.y - mFile->mMin.y);
    pPoint->z = nebula_f32_to_s32_console(rPos.z - mFile->mMin.z);
}
