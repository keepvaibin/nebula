#pragma once

#include "Game/Util/Table.hpp"
#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class Table;

struct KC_PrismData {
    f32 mHeight;
    u32 mPositionIndex;
    u32 mNormalIndex;
    u32 mEdgeIndices[3];
    u32 mAttribute;
};

// A collision model: its arrays and octree, and its scalars. KCollisionServer::setData makes one from a cooked .kcl
// (NcolHeader); DynamicCollisionObj builds its own (KCollisionServer::setModel).
// Octree words: below 2^31, the offset in words from its block's first word of a block of eight children; with the top bit
// set, the index in mLists of a list's first word, which is not read (the list is the prism numbers after it, to a 0).
struct KCLFile {
    Vec3f* mPos;
    Vec3f* mNorms;
    KC_PrismData* mPrisms;  // 1-based
    const u32* mOctree;
    const u32* mLists;
    f32 mThickness;
    Vec3f mMin;
    s32 mXMask;
    s32 mYMask;
    s32 mZMask;
    s32 mBlockWidthShift;
    s32 mBlockXShift;
    s32 mBlockXYShift;
    s32 mTriangleNum;
};

// A .kcl as the importer cooks it (crates/nebula-import cook/kcl.rs): offsets from the file's start, counts, scalars.
struct NcolHeader {
    char mMagic[4];  // "NCOL"
    u32 mPosOffset;
    u32 mPosNum;
    u32 mNormOffset;
    u32 mNormNum;
    u32 mPrismOffset;
    u32 mPrismNum;  // entry 0 not counted
    u32 mOctreeOffset;
    u32 mListOffset;
    u32 mListNum;
    f32 mThickness;
    f32 mMin[3];
    s32 mXMask;
    s32 mYMask;
    s32 mZMask;
    s32 mBlockWidthShift;
    s32 mBlockXShift;
    s32 mBlockXYShift;
};

class KCollisionServer {
public:
    class V3u {
    public:
        inline V3u() {
        }

        void setUsingCast(const Vec3f&);

        u32 x;
        u32 y;
        u32 z;
    };

    KCollisionServer();

    void init(void* pData, const void* pMapData);
    void setData(void* pData);
    // A model the game built (no file): DynamicCollisionObj.
    void setModel(KCLFile* pModel);
    void initModel(KCLFile* pModel, const void* pMapData);
    bool calcFarthestVertexDistance();
    KC_PrismData* checkPoint(const Vec3f* pPoint, f32 param, f32* pDist);
    u32 checkArea3D(const Vec3f* pMin, const Vec3f* pMax, KC_PrismData** pOut, u32 maxCount);
    // A hit part (HitInfo::mHitPart): 1 the face, 2-4 the edge of edge plane 0-2 (vertices 0-2, 0-1, 1-2), 5-7 vertex 0-2
    // (where edge planes 0-1, 1-2, 2-0 meet).
    u32 checkSphere(const Vec3f* pCenter, f32 radius, f32 param, u32 maxCount, KC_PrismData** pOut, f32* pDists, u8* pHitParts);
    u32 checkSphereWithThickness(const Vec3f* pCenter, f32 radius, f32 param, u32 maxCount, KC_PrismData** pOut, f32* pDists, u8* pHitParts,
                                 f32 thickness);
    KC_PrismData* checkArrow(const Vec3f& rOrigin, const Vec3f& rDir, f32* pDists, u8* pHitParts, u32* pCount, KC_PrismData** pOut,
                             u32 maxCount) const;
    u32 checkSpherePrisms(const Vec3f* pCenter, f32 radius, f32 threshold, bool isThick, u32 maxCount, KC_PrismData** pOut, f32* pDists,
                          u8* pHitParts);
    bool hitSpherePrism(KC_PrismData* pPrism, const Vec3f* pCenter, f32 radius, f32 threshold, f32* pDist, u8* pHitPart, bool isThick);
    // Defined and used in KCollision.cpp only.
    template < typename Visit >
    void visitPrismsInBlocks(const V3u& rPointMin, const V3u& rPointMax, Visit visit) const;
    bool isNearParallelNormal(const KC_PrismData* pPrism) const;
    bool KCHitArrow(KC_PrismData* pPrism, const Vec3f& rOrigin, const Vec3f& rDir, f32* pDist, u8* pHitPart) const;
    s32 toIndex(const KC_PrismData*) const;
    Vec3f* getFaceNormal(const KC_PrismData*) const;
    Vec3f* getEdgeNormal1(const KC_PrismData*) const;
    Vec3f* getEdgeNormal2(const KC_PrismData*) const;
    Vec3f* getEdgeNormal3(const KC_PrismData*) const;
    Vec3f* getNormal(u32) const;
    void calXvec(const Vec3f*, const Vec3f*, Vec3f*) const;
    Vec3f getPos(const KC_PrismData*, int) const;
    KC_PrismData* getPrismData(u32) const;
    s32 getTriangleNum() const;
    TableIter getAttributes(u32) const;
    // The list (its unread first word) of the leaf holding the point; *pShift becomes the leaf's width shift.
    const u32* searchBlock(s32* pShift, const u32&, const u32&, const u32&) const;
    bool isInsideMinMaxInLocalSpace(const V3u&) const;
    bool outCheck(const Vec3f*, const Vec3f*, V3u*, V3u*) const;
    void objectSpaceToLocalSpace(V3u*, const Vec3f&) const;

    KCLFile* mFile;
    Table* mapInfo;
    f32 mMaxVertexDistance;
    KCLFile mModel;  // the view of a cooked file
};
