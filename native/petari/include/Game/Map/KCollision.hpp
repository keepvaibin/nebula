#pragma once

#include "Game/Util/JMapInfo.hpp"
#include <JSystem/JGeometry/TVec.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class JMapInfo;

struct KC_PrismData {
    f32 mHeight;
    u16 mPositionIndex;
    u16 mNormalIndex;
    u16 mEdgeIndices[3];
    u16 mAttribute;
};

class Fxyz {
public:
    inline Fxyz() {
    }

    Fxyz& operator=(const Fxyz&);

    f32 x, y, z;
};

// A collision model natively: its arrays and octree, and its scalars. KCollisionServer::setData makes one from a
// cooked .kcl (KCLHeader); DynamicCollisionObj builds its own (KCollisionServer::setModel).
struct KCLFile {
    TVec3f* mPos;
    TVec3f* mNorms;
    KC_PrismData* mPrisms;  // 1-based
    void* mOctree;
    f32 mThickness;
    TVec3f mMin;
    s32 mXMask;
    s32 mYMask;
    s32 mZMask;
    s32 mBlockWidthShift;
    s32 mBlockXShift;
    s32 mBlockXYShift;
    s32 mTriangleNum;
};

// A .kcl's header as the importer cooks it (host order, crates/nebula-import cook/native_data.rs): the arrays'
// offsets from the file's start, then the scalars.
struct KCLHeader {
    u32 mPosOffset;
    u32 mNormOffset;
    u32 mPrismOffset;
    u32 mOctreeOffset;
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

        void setUsingCast(const TVec3f&);

        u32 x;
        u32 y;
        u32 z;
    };

    KCollisionServer();

    void init(void*, const void*);
    void setData(void*);
    // A model the game built (no file): DynamicCollisionObj.
    void setModel(KCLFile*);
    void initModel(KCLFile*, const void*);
    bool calcFarthestVertexDistance();
    KC_PrismData* checkPoint(Fxyz*, f32, f32*);
    u32 checkArea3D(Fxyz*, Fxyz*, KC_PrismData**, u32);
    u32 checkSphere(Fxyz*, f32, f32, u32, KC_PrismData**, f32*, u8*);
    u32 checkSphereWithThickness(Fxyz*, f32, f32, u32, KC_PrismData**, f32*, u8*, f32);
    KC_PrismData* checkArrow(const TVec3f&, const TVec3f&, f32*, u8*, u32*, KC_PrismData**, u32) const;
    bool KCHitSphere(KC_PrismData*, Fxyz*, f32, f32, f32*, u8*);
    bool KCHitSphereWithThickness(KC_PrismData*, Fxyz*, f32, f32, f32*, u8*, f32);
    bool isNearParallelNormal(const KC_PrismData*) const;
    bool KCHitArrow(KC_PrismData*, const TVec3f&, const TVec3f&, f32*, u8*) const;
    s32 toIndex(const KC_PrismData*) const;
    TVec3f* getFaceNormal(const KC_PrismData*) const;
    TVec3f* getEdgeNormal1(const KC_PrismData*) const;
    TVec3f* getEdgeNormal2(const KC_PrismData*) const;
    TVec3f* getEdgeNormal3(const KC_PrismData*) const;
    TVec3f* getNormal(u32) const;
    void calXvec(const Fxyz*, const Fxyz*, Fxyz*) const;
    TVec3f getPos(const KC_PrismData*, int) const;
    KC_PrismData* getPrismData(u32) const;
    s32 getTriangleNum() const;
    JMapInfoIter getAttributes(u32) const;
    s32* searchBlock(s32*, const u32&, const u32&, const u32&) const;
    bool isInsideMinMaxInLocalSpace(const V3u&) const;
    bool outCheck(const TVec3f*, const TVec3f*, V3u*, V3u*) const;
    void objectSpaceToLocalSpace(V3u*, const TVec3f&) const;

    KCLFile* mFile;
    JMapInfo* mapInfo;
    f32 mMaxVertexDistance;
    KCLFile mModel;  // the view of a cooked file
};
