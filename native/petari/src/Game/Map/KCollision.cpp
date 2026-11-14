#include "Game/Map/KCollision.hpp"
#include "Game/Camera/CameraPolygonCodeUtil.hpp"
#include "Game/Map/CollisionCode.hpp"
#include "Game/Map/CollisionDirector.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/MathUtil.hpp"
#include <algorithm>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_checkArrow_f1, "checkArrow.f1", 1.0e9f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkArrow_f2, "checkArrow.f2", 1.0e9f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkArrow_f3, "checkArrow.f3", 1.0e9f);
NEBULA_TUNABLE_NAMED(f32, cNum_KCHitArrow_f1, "KCHitArrow.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_KCHitArrow_f2, "KCHitArrow.f2", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_KCHitArrow_f3, "KCHitArrow.f3", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_KCHitArrow_f4, "KCHitArrow.f4", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_KCHitArrow_f5, "KCHitArrow.f5", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_KCHitArrow_f6, "KCHitArrow.f6", 0.01f);

namespace {
    // An arrow's hit part by which edge planes it lies on (bit 0-2 for plane 0-2): one, its edge; two, their vertex; else
    // the face.
    NEBULA_TUNABLE_LIST(u8, cHitPartByEdges, 8, {1, 2, 3, 5, 4, 7, 6, 1});
}  // namespace

KCollisionServer::KCollisionServer() : mFile(), mapInfo(new Table()), mMaxVertexDistance(1.0f), mModel() {
}

void KCollisionServer::init(void* pData, const void* pMapData) {
    setData(pData);

    if (pMapData != nullptr) {
        mapInfo->attach(pMapData);
    }
}

void KCollisionServer::setData(void* pData) {
    const NcolHeader* pHeader = static_cast< const NcolHeader* >(pData);
    if (pHeader->mMagic[0] != 'N' || pHeader->mMagic[1] != 'C' || pHeader->mMagic[2] != 'O' || pHeader->mMagic[3] != 'L') {
        NEBULA_PANIC("KCollisionServer: a collision file the importer did not cook");
    }
    u8* pBase = static_cast< u8* >(pData);
    mModel.mPos = reinterpret_cast< Vec3f* >(pBase + pHeader->mPosOffset);
    mModel.mNorms = reinterpret_cast< Vec3f* >(pBase + pHeader->mNormOffset);
    mModel.mPrisms = reinterpret_cast< KC_PrismData* >(pBase + pHeader->mPrismOffset);
    mModel.mOctree = reinterpret_cast< const u32* >(pBase + pHeader->mOctreeOffset);
    mModel.mLists = reinterpret_cast< const u32* >(pBase + pHeader->mListOffset);
    mModel.mTriangleNum = static_cast< s32 >(pHeader->mPrismNum);
    mModel.mThickness = pHeader->mThickness;
    mModel.mMin.set(pHeader->mMin[0], pHeader->mMin[1], pHeader->mMin[2]);
    mModel.mXMask = pHeader->mXMask;
    mModel.mYMask = pHeader->mYMask;
    mModel.mZMask = pHeader->mZMask;
    mModel.mBlockWidthShift = pHeader->mBlockWidthShift;
    mModel.mBlockXShift = pHeader->mBlockXShift;
    mModel.mBlockXYShift = pHeader->mBlockXYShift;
    mFile = &mModel;
}

void KCollisionServer::setModel(KCLFile* pModel) {
    mFile = pModel;
}

void KCollisionServer::initModel(KCLFile* pModel, const void* pMapData) {
    setModel(pModel);

    if (pMapData != nullptr) {
        mapInfo->attach(pMapData);
    }
}

bool KCollisionServer::calcFarthestVertexDistance() {
    // Also flattens near-parallel prisms and registers each prism's camera code; false when any prism has no attributes.
    s32 triCount = getTriangleNum();
    f32 maxDistanceSq = 0.0f;
    bool isAllAttributed = true;

    for (u32 i = 0; i < (u32)triCount; i++) {
        KC_PrismData* prism = &mFile->mPrisms[i + 1];
        TableIter iter = getAttributes(i);

        if (!iter.isValid()) {
            isAllAttributed = false;
        } else {
            MR::registerCameraCode(MR::getCollisionDirector()->mCode->getCameraID(iter));
        }

        if (isNearParallelNormal(prism)) {
            prism->mHeight = -MR::abs(prism->mHeight);
        } else {
            for (s32 j = 0; j < 3; j++) {
                Vec3f pos = getPos(prism, j);
                f32 distSq = pos.squared();

                if (maxDistanceSq < distSq) {
                    maxDistanceSq = distSq;
                }
            }
        }
    }

    mMaxVertexDistance = MR::sqrt(maxDistanceSq);
    return isAllAttributed;
}

KC_PrismData* KCollisionServer::checkPoint(const Vec3f* pPoint, f32 param, f32* pDist) {
    f32 maxDist = mFile->mThickness * param;
    u32 x = nebula_f32_to_s32_console(pPoint->x - mFile->mMin.x);

    if (x & mFile->mXMask) {
        return nullptr;
    }

    u32 y = nebula_f32_to_s32_console(pPoint->y - mFile->mMin.y);

    if (y & mFile->mYMask) {
        return nullptr;
    }

    u32 z = nebula_f32_to_s32_console(pPoint->z - mFile->mMin.z);

    if (z & mFile->mZMask) {
        return nullptr;
    }

    s32 shift;
    const u32* prismList = searchBlock(&shift, x, y, z);

    while (*++prismList != 0) {
        KC_PrismData* prism = &mFile->mPrisms[*prismList];
        f32 height = prism->mHeight;

        if (height <= 0.0f) {
            continue;
        }

        Vec3f* vtx = &mFile->mPos[prism->mPositionIndex];

        Vec3f dir;
        dir.x = pPoint->x - vtx->x;
        dir.y = pPoint->y - vtx->y;
        dir.z = pPoint->z - vtx->z;

        Vec3f& edgeNormal0 = mFile->mNorms[prism->mEdgeIndices[0]];

        if (dir.x * edgeNormal0.x + dir.y * edgeNormal0.y + dir.z * edgeNormal0.z > 0.0f) {
            continue;
        }

        Vec3f& edgeNormal1 = mFile->mNorms[prism->mEdgeIndices[1]];

        if (dir.x * edgeNormal1.x + dir.y * edgeNormal1.y + dir.z * edgeNormal1.z > 0.0f) {
            continue;
        }

        Vec3f& edgeNormal2 = mFile->mNorms[prism->mEdgeIndices[2]];

        if (dir.x * edgeNormal2.x + dir.y * edgeNormal2.y + dir.z * edgeNormal2.z > height) {
            continue;
        }

        Vec3f& faceNormal = mFile->mNorms[prism->mNormalIndex];
        f32 dist = -dir.x * faceNormal.x - dir.y * faceNormal.y - dir.z * faceNormal.z;

        if (dist < 0.0f) {
            continue;
        }

        if (maxDist < dist) {
            continue;
        }

        *pDist = dist;
        return prism;
    }

    return nullptr;
}

// Every prism with a height in the blocks over rPointMin..rPointMax, x fastest; a block that already covered the row before
// is skipped. Stops when visit returns true.
template < typename Visit >
void KCollisionServer::visitPrismsInBlocks(const V3u& rPointMin, const V3u& rPointMax, Visit visit) const {
    const u32* nextSkipList = nullptr;
    const u32* skipList = nullptr;
    u32 z = rPointMin.z;

    do {
        u32 y = rPointMin.y;
        s32 zStep = 1000000;

        do {
            u32 x = rPointMin.x;
            s32 yStep = 1000000;
            s32 bestRemY = 0;

            do {
                s32 shift;
                const u32* list = searchBlock(&shift, x, y, z);
                s32 blockSize = 1 << shift;
                s32 mask = blockSize - 1;
                s32 remX = blockSize - (x & mask);
                s32 remY = blockSize - (y & mask);
                s32 remZ = blockSize - (z & mask);

                if (remZ < zStep) {
                    zStep = remZ;
                }

                if (remY < yStep) {
                    yStep = remY;
                }

                if (remY > bestRemY && list[1] != 0) {
                    bestRemY = remY;
                    nextSkipList = list;
                }

                if (skipList == nullptr || list != skipList) {
                    while (*++list != 0) {
                        KC_PrismData* prism = &mFile->mPrisms[*list];

                        if (prism->mHeight <= 0.0f) {
                            continue;
                        }

                        if (visit(prism)) {
                            return;
                        }
                    }
                }

                x += remX;
            } while (x <= rPointMax.x);

            skipList = nextSkipList;
            y += yStep;
        } while (y <= rPointMax.y);

        z += zStep;
    } while (z <= rPointMax.z);
}

u32 KCollisionServer::checkArea3D(const Vec3f* pMin, const Vec3f* pMax, KC_PrismData** pOut, u32 maxCount) {
    u32 foundCount = 0;

    const Vec3f box[2] = {*pMin, *pMax};

    V3u pointMin;
    V3u pointMax;
    Vec3f queryMax;
    Vec3f queryMin;
    MR::createBoundingBox(box, 2, &queryMin, &queryMax);

    if (queryMin.x == queryMax.x) {
        queryMin.x -= 1.0f;
        queryMax.x += 1.0f;
    }

    if (queryMin.y == queryMax.y) {
        queryMin.y -= 1.0f;
        queryMax.y += 1.0f;
    }

    if (queryMin.z == queryMax.z) {
        queryMin.z -= 1.0f;
        queryMax.z += 1.0f;
    }

    if (!outCheck(&queryMin, &queryMax, &pointMin, &pointMax)) {
        return 0;
    }

    visitPrismsInBlocks(pointMin, pointMax, [&](KC_PrismData* prism) {
        KC_PrismData** end = pOut + foundCount;

        if (std::find(pOut, end, (KC_PrismData* const&)prism) != end) {
            return false;
        }

        Vec3f verts[3];
        verts[0] = getPos(prism, 0);
        verts[1] = getPos(prism, 1);
        verts[2] = getPos(prism, 2);

        Vec3f prismMin;
        Vec3f prismMax;
        MR::createBoundingBox(verts, 3, &prismMin, &prismMax);

        if (prismMax.x < queryMin.x || prismMax.y < queryMin.y || prismMax.z < queryMin.z || queryMax.x < prismMin.x ||
            queryMax.y < prismMin.y || queryMax.z < prismMin.z) {
            return false;
        }

        *end = prism;
        foundCount++;
        return foundCount == maxCount;
    });

    return foundCount;
}

u32 KCollisionServer::checkSphere(const Vec3f* pCenter, f32 radius, f32 param, u32 maxCount, KC_PrismData** pOut, f32* pDists, u8* pHitParts) {
    return checkSpherePrisms(pCenter, radius, mFile->mThickness * param, false, maxCount, pOut, pDists, pHitParts);
}

u32 KCollisionServer::checkSphereWithThickness(const Vec3f* pCenter, f32 radius, f32 param, u32 maxCount, KC_PrismData** pOut, f32* pDists, u8* pHitParts,
                                               f32 thickness) {
    return checkSpherePrisms(pCenter, radius, thickness * param, true, maxCount, pOut, pDists, pHitParts);
}

u32 KCollisionServer::checkSpherePrisms(const Vec3f* pCenter, f32 radius, f32 threshold, bool isThick, u32 maxCount, KC_PrismData** pOut, f32* pDists,
                                        u8* pHitParts) {
    V3u pointMin;
    V3u pointMax;
    Vec3f boxMax;
    Vec3f boxMin;
    boxMax.x = pCenter->x + radius;
    boxMax.y = pCenter->y + radius;
    boxMax.z = pCenter->z + radius;

    boxMin.x = pCenter->x - radius;
    boxMin.y = pCenter->y - radius;
    boxMin.z = pCenter->z - radius;

    if (!outCheck(&boxMin, &boxMax, &pointMin, &pointMax)) {
        return 0;
    }

    u32 foundCount = 0;

    visitPrismsInBlocks(pointMin, pointMax, [&](KC_PrismData* prism) {
        KC_PrismData** end = pOut + foundCount;

        if (std::find(pOut, end, (KC_PrismData* const&)prism) != end) {
            return false;
        }

        f32 dist;
        u8 hitPart;

        if (!hitSpherePrism(prism, pCenter, radius, threshold, &dist, &hitPart, isThick)) {
            return false;
        }

        // Full: nothing more is written.
        if (foundCount >= maxCount) {
            return true;
        }

        *end = prism;
        pDists[foundCount] = dist;
        pHitParts[foundCount] = hitPart;
        foundCount++;
        return foundCount >= maxCount;
    });

    return foundCount;
}

KC_PrismData* KCollisionServer::checkArrow(const Vec3f& rOrigin, const Vec3f& rDir, f32* pDists, u8* pHitParts, u32* pCount, KC_PrismData** pOut,
                                           u32 maxCount) const {
    if (rDir.x == 0.0f && rDir.y == 0.0f && rDir.z == 0.0f) {
        return nullptr;
    }

    f32 length;
    Vec3f dir(rDir);
    f32 startT = 0.0f;
    MR::separateScalarAndDirection(&length, &dir, dir);

    if (MR::isNearZero(dir)) {
        return nullptr;
    }

    Vec3f hitPoint;
    V3u cell;
    Vec3f start(rOrigin);
    start.x -= mFile->mMin.x;
    start.y -= mFile->mMin.y;
    start.z -= mFile->mMin.z;

    cell.setUsingCast(start);

    if (isInsideMinMaxInLocalSpace(cell)) {
        hitPoint.set(start);
    } else {
        Vec3f boxMax;
        boxMax.x = (f32)(u32)~mFile->mXMask;
        boxMax.y = (f32)(u32)~mFile->mYMask;
        boxMax.z = (f32)(u32)~mFile->mZMask;

        // Into the grid through the first of its faces (x, then y, then z) the ray crosses at a cell inside it.
        auto isEnteredAt = [&](f32 dirAxis, f32 startAxis, f32 boxMaxAxis) {
            if (dirAxis == 0.0f) {
                return false;
            }

            f32 edge = 0.0f < dirAxis ? 0.0f : boxMaxAxis;
            startT = (edge - startAxis) / dirAxis;

            if (!(0.0f <= startT && startT <= length)) {
                return false;
            }

            Vec3f step(dir);
            step.scale(startT);
            hitPoint.set(step);
            hitPoint += start;
            cell.setUsingCast(hitPoint);
            return isInsideMinMaxInLocalSpace(cell);
        };

        if (!isEnteredAt(dir.x, start.x, boxMax.x) && !isEnteredAt(dir.y, start.y, boxMax.y) && !isEnteredAt(dir.z, start.z, boxMax.z)) {
            return nullptr;
        }
    }

    const u32* prismList;
    u32 foundCount;
    foundCount = 0;

    s32 shift;
    s32 deltaPosX;
    s32 deltaPosY;
    s32 deltaPosZ;
    s32 deltaNegX;
    s32 deltaNegY;
    s32 deltaNegZ;
    s32* pDeltaX;
    s32* pDeltaY;
    s32* pDeltaZ;
    s32 stepX;
    s32 stepY;
    s32 stepZ;
    if (dir.x < 0.0f) {
        pDeltaX = &deltaNegX;
        stepX = -1;
    } else {
        pDeltaX = &deltaPosX;
        stepX = 1;
    }

    if (dir.y < 0.0f) {
        pDeltaY = &deltaNegY;
        stepY = -1;
    } else {
        pDeltaY = &deltaPosY;
        stepY = 1;
    }

    if (dir.z < 0.0f) {
        pDeltaZ = &deltaNegZ;
        stepZ = -1;
    } else {
        pDeltaZ = &deltaPosZ;
        stepZ = 1;
    }

    KC_PrismData* bestPrism;
    f32 accumT = startT;

    bestPrism = nullptr;

    do {
        prismList = searchBlock(&shift, cell.x, cell.y, cell.z);
        u32 blockSize = 1 << shift;
        u32 mask = blockSize - 1;

        deltaPosX = blockSize - (cell.x & mask);
        deltaPosY = blockSize - (cell.y & mask);
        deltaPosZ = blockSize - (cell.z & mask);
        deltaNegX = -(s32)(cell.x & mask);
        deltaNegY = -(s32)(cell.y & mask);
        deltaNegZ = -(s32)(cell.z & mask);

        if (*pDeltaX == 0) {
            *pDeltaX = stepX;
        }

        if (*pDeltaY == 0) {
            *pDeltaY = stepY;
        }

        if (*pDeltaZ == 0) {
            *pDeltaZ = stepZ;
        }

        f32 bestFraction = 1.0f;
        f32 dist = 1.0f;

        while (*++prismList != 0) {
            KC_PrismData* prism = &mFile->mPrisms[*prismList];

            if (prism->mHeight <= 0.0f) {
                continue;
            }

            u8 hitPart = 0;

            if (!KCHitArrow(prism, rOrigin, rDir, &dist, &hitPart)) {
                continue;
            }

            if (pOut != nullptr) {
                pDists[foundCount] = dist;
                pHitParts[foundCount] = hitPart;
                pOut[foundCount] = prism;
                foundCount++;

                if (dist < bestFraction) {
                    bestFraction = dist;
                    bestPrism = prism;
                }

                if (foundCount == maxCount) {
                    if (pCount != nullptr) {
                        *pCount = foundCount;
                    }

                    return bestPrism;
                }
            } else {
                if (!(dist < bestFraction)) {
                    continue;
                }

                bestFraction = dist;
                *pDists = dist;
                bestPrism = prism;
                *pHitParts = hitPart;
            }
        }

        if (pOut == nullptr && bestPrism != nullptr) {
            break;
        }

        f32 tX = MR::isNearZero(dir.x) ? cNum_checkArrow_f1 : (f32)*pDeltaX / dir.x;
        f32 tY = MR::isNearZero(dir.y) ? cNum_checkArrow_f2 : (f32)*pDeltaY / dir.y;
        f32 tZ = MR::isNearZero(dir.z) ? cNum_checkArrow_f3 : (f32)*pDeltaZ / dir.z;

        f32 tMin = tX;

        if (tY < tMin) {
            tMin = tY;
        }

        if (tZ < tMin) {
            tMin = tZ;
        }

        if (length - accumT <= tMin) {
            break;
        }

        Vec3f step(dir);
        step.scale(tMin);
        hitPoint += step;
        accumT += tMin;

        cell.setUsingCast(hitPoint);

        if (!isInsideMinMaxInLocalSpace(cell)) {
            break;
        }
    } while (accumT < length);

    if (pCount != nullptr) {
        *pCount = foundCount;
    }

    return bestPrism;
}

// distances: 0 the face plane, 1-3 the three edge planes (the third offset by the prism height). A one-sided test also
// misses when the centre is nearer the face plane than the edge or corner; a thick one misses only when the sphere is wholly
// behind the face.
bool KCollisionServer::hitSpherePrism(KC_PrismData* pPrism, const Vec3f* pCenter, f32 radius, f32 threshold, f32* pDist, u8* pHitPart,
                                      bool isThick) {
    f32 radiusSq = radius * radius;
    *pHitPart = 0;

    Vec3f* pCorner = &mFile->mPos[pPrism->mPositionIndex];

    Vec3f dir;
    f32 distances[4];
    dir.x = pCenter->x - pCorner->x;
    dir.y = pCenter->y - pCorner->y;
    dir.z = pCenter->z - pCorner->z;

    Vec3f* n0 = &mFile->mNorms[pPrism->mEdgeIndices[0]];
    distances[1] = dir.x * n0->x + dir.y * n0->y + dir.z * n0->z;

    if (distances[1] >= radius) {
        return false;
    }

    Vec3f* n1 = &mFile->mNorms[pPrism->mEdgeIndices[1]];
    distances[2] = dir.x * n1->x + dir.y * n1->y + dir.z * n1->z;

    if (distances[2] >= radius) {
        return false;
    }

    Vec3f* n2 = &mFile->mNorms[pPrism->mEdgeIndices[2]];
    distances[3] = dir.x * n2->x + dir.y * n2->y + dir.z * n2->z - pPrism->mHeight;

    if (distances[3] >= radius) {
        return false;
    }

    Vec3f* faceNormal = &mFile->mNorms[pPrism->mNormalIndex];
    distances[0] = dir.x * faceNormal->x + dir.y * faceNormal->y + dir.z * faceNormal->z;
    *pDist = radius - distances[0];

    if (*pDist < 0.0f) {
        return false;
    }

    const Vec3f* edgeNormals[3] = {n0, n1, n2};
    const f32* edgeDistances = &distances[1];

    // The edge plane the centre is farthest out of; the later plane on a tie.
    s32 side;

    if (distances[1] > distances[2]) {
        side = distances[1] > distances[3] ? 0 : 2;
    } else {
        side = distances[2] > distances[3] ? 1 : 2;
    }

    if (edgeDistances[side] <= 0.0f) {
        // Inside all three: the face.
        if (threshold < *pDist) {
            return false;
        }

        *pHitPart = 1;
        return true;
    }

    // Of the side plane's two neighbours, the one the centre is farther out of (the previous one on a tie).
    s32 next = side == 2 ? 0 : side + 1;
    s32 prev = side == 0 ? 2 : side - 1;
    s32 other = edgeDistances[next] > edgeDistances[prev] ? next : prev;

    const Vec3f* sideNormal = edgeNormals[side];
    const Vec3f* otherNormal = edgeNormals[other];
    f32 nn = sideNormal->x * otherNormal->x + sideNormal->y * otherNormal->y + sideNormal->z * otherNormal->z;
    u8 hitPart;

    if (nn * edgeDistances[side] > edgeDistances[other]) {
        // The side plane's edge (hit parts 2-4).
        if (!isThick && edgeDistances[side] > distances[0]) {
            return false;
        }

        *pDist = radiusSq - edgeDistances[side] * edgeDistances[side];
        hitPart = 2 + side;
    } else {
        // The corner between planes a and the one after it around the prism (hit parts 5-7).
        s32 a = other == next ? side : other;
        s32 b = a == 2 ? 0 : a + 1;
        f32 t = (nn * edgeDistances[b] - edgeDistances[a]) / (nn * nn - 1.0f);
        f32 s = edgeDistances[b] - t * nn;
        dir.x = t * edgeNormals[a]->x + s * edgeNormals[b]->x;
        dir.y = t * edgeNormals[a]->y + s * edgeNormals[b]->y;
        dir.z = t * edgeNormals[a]->z + s * edgeNormals[b]->z;

        f32 closestSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
        f32 dist = MR::sqrt(closestSq);

        if ((!isThick && dist > distances[0]) || dist >= radius) {
            return false;
        }

        *pDist = radiusSq - closestSq;
        hitPart = 5 + a;
    }

    f32 distance = MR::sqrt(*pDist);

    if (isThick && distances[0] + distance < 0.0f) {
        return false;
    }

    *pDist = distance - distances[0];

    if (*pDist < 0.0f || threshold < *pDist) {
        return false;
    }

    *pHitPart = hitPart;
    return true;
}

bool KCollisionServer::isNearParallelNormal(const KC_PrismData* pPrism) const {
    Vec3f edge0 = mFile->mNorms[pPrism->mEdgeIndices[0]];
    Vec3f edge1 = mFile->mNorms[pPrism->mEdgeIndices[1]];
    Vec3f edge2 = mFile->mNorms[pPrism->mEdgeIndices[2]];

    return MR::isSameDirection(edge0, edge1) || MR::isSameDirection(edge0, edge2) || MR::isSameDirection(edge1, edge2);
}

bool KCollisionServer::KCHitArrow(KC_PrismData* pPrism, const Vec3f& rOrigin, const Vec3f& rDir, f32* pDist, u8* pHitPart) const {
    Vec3f* pCorner = &mFile->mPos[pPrism->mPositionIndex];
    Vec3f* faceNormal = &mFile->mNorms[pPrism->mNormalIndex];

    Vec3f rel;
    NebulaVecSubtract(rOrigin, pCorner, rel);

    f32 t = NebulaVecDotProduct(rel, faceNormal);

    if (t <= 0.0f) {
        *pHitPart = 0;
        return false;
    }

    f32 dirDotFace = NebulaVecDotProduct(faceNormal, rDir);

    if (0.0f < t + dirDotFace) {
        *pHitPart = 0;
        return false;
    }

    t = t / -dirDotFace;

    Vec3f hit = rDir * t;
    hit += rel;

    bool onEdge0 = false;
    bool onEdge1 = false;
    bool onEdge2 = false;

    f32 e0 = NebulaVecDotProduct(hit, mFile->mNorms[pPrism->mEdgeIndices[0]]);

    if (cNum_KCHitArrow_f1 < e0) {
        *pHitPart = 0;
        return false;
    }

    if (0.0f <= e0 && e0 <= cNum_KCHitArrow_f2) {
        onEdge0 = true;
    }

    f32 e1 = NebulaVecDotProduct(hit, mFile->mNorms[pPrism->mEdgeIndices[1]]);

    if (cNum_KCHitArrow_f3 < e1) {
        *pHitPart = 0;
        return false;
    }

    if (0.0f <= e1 && e1 <= cNum_KCHitArrow_f4) {
        onEdge1 = true;
    }

    f32 e2 = NebulaVecDotProduct(hit, mFile->mNorms[pPrism->mEdgeIndices[2]]);

    if (cNum_KCHitArrow_f5 + pPrism->mHeight < e2) {
        *pHitPart = 0;
        return false;
    }

    if (0.0f <= e2 && e2 <= cNum_KCHitArrow_f6) {
        onEdge2 = true;
    }

    *pDist = t;

    *pHitPart = cHitPartByEdges[onEdge0 | onEdge1 << 1 | onEdge2 << 2];
    return true;
}

s32 KCollisionServer::toIndex(const KC_PrismData* pPrism) const {
    return pPrism - (mFile->mPrisms + 1);
}

Vec3f* KCollisionServer::getFaceNormal(const KC_PrismData* pPrism) const {
    return &mFile->mNorms[pPrism->mNormalIndex];
}

Vec3f* KCollisionServer::getEdgeNormal1(const KC_PrismData* pPrism) const {
    return &mFile->mNorms[pPrism->mEdgeIndices[0]];
}

Vec3f* KCollisionServer::getEdgeNormal2(const KC_PrismData* pPrism) const {
    return &mFile->mNorms[pPrism->mEdgeIndices[1]];
}

Vec3f* KCollisionServer::getEdgeNormal3(const KC_PrismData* pPrism) const {
    return &mFile->mNorms[pPrism->mEdgeIndices[2]];
}

Vec3f* KCollisionServer::getNormal(u32 index) const {
    return &mFile->mNorms[index];
}

void KCollisionServer::calXvec(const Vec3f* pVecA, const Vec3f* pVecB, Vec3f* pDst) const {
    pDst->x = pVecA->z * pVecB->y - pVecA->y * pVecB->z;
    pDst->y = pVecA->x * pVecB->z - pVecA->z * pVecB->x;
    pDst->z = pVecA->y * pVecB->x - pVecA->x * pVecB->y;
}

Vec3f KCollisionServer::getPos(const KC_PrismData* pPrism, int vertexIndex) const {
    switch (vertexIndex) {
    case 0: {
        Vec3f* pos = &mFile->mPos[pPrism->mPositionIndex];

        return Vec3f(pos->x, pos->y, pos->z);
    }
    // From vertex 0 along edge plane 1's edge (vertex 1) or edge plane 0's (vertex 2) to edge plane 2, mHeight out.
    case 1:
    case 2: {
        const Vec3f* pos = &mFile->mPos[pPrism->mPositionIndex];
        const Vec3f* edge2 = &mFile->mNorms[pPrism->mEdgeIndices[2]];
        const Vec3f* face = &mFile->mNorms[pPrism->mNormalIndex];

        Vec3f finalPos;

        if (vertexIndex == 1) {
            calXvec(&mFile->mNorms[pPrism->mEdgeIndices[1]], face, &finalPos);
        } else {
            calXvec(face, &mFile->mNorms[pPrism->mEdgeIndices[0]], &finalPos);
        }

        f32 sideLength = pPrism->mHeight / (finalPos.x * edge2->x + finalPos.y * edge2->y + finalPos.z * edge2->z);

        finalPos.x = pos->x + sideLength * finalPos.x;
        finalPos.y = pos->y + sideLength * finalPos.y;
        finalPos.z = pos->z + sideLength * finalPos.z;

        return Vec3f(finalPos.x, finalPos.y, finalPos.z);
    }
    default:
        return Vec3f(0.0f, 0.0f, 0.0f);
    }
}

KC_PrismData* KCollisionServer::getPrismData(u32 index) const {
    return &mFile->mPrisms[1 + index];
}

s32 KCollisionServer::getTriangleNum() const {
    return mFile->mTriangleNum;
}

TableIter KCollisionServer::getAttributes(u32 index) const {
    KC_PrismData* prism = &mFile->mPrisms[1 + index];

    return TableIter(mapInfo, prism->mAttribute);
}

const u32* KCollisionServer::searchBlock(s32* pShift, const u32& rX, const u32& rY, const u32& rZ) const {
    const KCLFile* file = mFile;
    const u32* block = file->mOctree;
    s32 blockWidthShift = *pShift = file->mBlockWidthShift;

    s32 xyShift = file->mBlockXYShift;
    s32 xShift = file->mBlockXShift;
    u32 slot = 0;
    // -1 for both: one root block (DynamicCollisionObj).
    if (xyShift >= 0 && xShift >= 0) {
        slot = ((rZ >> blockWidthShift) << xyShift) | ((rY >> blockWidthShift) << xShift) | (rX >> blockWidthShift);
    }

    u32 word;
    while (((word = block[slot]) & 0x80000000u) == 0) {
        block += word;
        (*pShift)--;
        s32 shift = *pShift;

        slot = (((rZ >> shift) & 1) << 2) | (((rY >> shift) & 1) << 1) | ((rX >> shift) & 1);
    }

    return file->mLists + (word & 0x7FFFFFFFu);
}

void KCollisionServer::V3u::setUsingCast(const Vec3f& rPos) {
    x = nebula_f32_to_s32_console(rPos.x);
    y = nebula_f32_to_s32_console(rPos.y);
    z = nebula_f32_to_s32_console(rPos.z);
}
