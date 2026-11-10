#include "Game/MapObj/DynamicCollisionObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionDirector.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/KCollision.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_createCollision_f1, "createCollision.f1", 40.f);

DynamicCollisionObj::DynamicCollisionObj(const char* pName) : LiveActor(pName) {
}

void DynamicCollisionObj::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToScene(this, MR::MovementType_Enemy, MR::CalcAnimType_Enemy, MR::DrawBufferType_None, MR::DrawType_FlexibleSphere);

    mKCLFile = nullptr;
}

void DynamicCollisionObj::syncCollision() {
    updateCollisionHeader();
    updateTriangle();
    mParts->mServer->calcFarthestVertexDistance();
    mParts->updateBoundingSphereRange(mScale);
}

void DynamicCollisionObj::updateTriangle() {
    if (mKCLFile == nullptr) {
        return;
    }

    s32 nrmidxa, nrmidxb, nrmidxc;
    s32 prismidx;
    s32 normalidx = 0;

    for (s32 i = 0; i < mTriangleNum; i++) {
        Vec3f Vertex1(mPositions[mIndices[i].mIndex[0]]);
        Vec3f Vertex2(mPositions[mIndices[i].mIndex[1]]);
        Vec3f Vertex3(mPositions[mIndices[i].mIndex[2]]);

        Vec3f a(Vertex3);
        a -= Vertex1;
        Vec3f SaveForLater(a);
        Vec3f b(Vertex2);
        b -= Vertex1;
        Vec3f c(Vertex3);
        c -= Vertex2;
        MR::normalizeOrZero(&a);
        MR::normalizeOrZero(&b);
        MR::normalizeOrZero(&c);
        Vec3f faceNormal = b.cross(a);
        MR::normalizeOrZero(&faceNormal);

        Vec3f edgeNormalA = (-a).cross(faceNormal);
        Vec3f edgeNormalB = b.cross(faceNormal);
        Vec3f edgeNormalC = c.cross(faceNormal);

        MR::normalizeOrZero(&edgeNormalA);
        MR::normalizeOrZero(&edgeNormalB);
        MR::normalizeOrZero(&edgeNormalC);

        mKCLFile->mPos[i] = Vertex1;
        mKCLFile->mNorms[normalidx + 0] = faceNormal;
        nrmidxa = normalidx + 1;
        mKCLFile->mNorms[nrmidxa] = edgeNormalA;
        nrmidxb = normalidx + 2;
        mKCLFile->mNorms[nrmidxb] = edgeNormalB;
        nrmidxc = normalidx + 3;
        mKCLFile->mNorms[nrmidxc] = edgeNormalC;

        prismidx = i + 1;
        KC_PrismData* prism = &mKCLFile->mPrisms[prismidx];
        MR::vecKillElement(SaveForLater, c, &SaveForLater);

        prism->mHeight = SaveForLater.length();
        prism->mPositionIndex = i;
        u32 nrmidx = normalidx;
        prism->mNormalIndex = nrmidx;
        nrmidx += 4;
        prism->mEdgeIndices[0] = nrmidxa;
        prism->mEdgeIndices[1] = nrmidxb;
        prism->mEdgeIndices[2] = nrmidxc;
        prism->mAttribute = prismidx;
        normalidx = nrmidx;
    }
}

void DynamicCollisionObj::updateCollisionHeader() {
    // Compute bounding box and extent
    Vec3f min, max;
    MR::createBoundingBox(this->mPositions, this->mPositionNum, &min, &max);
    Vec3f extent(max);
    extent -= min;

    // Initialize masks with extent
    u32 masks[3];
    masks[0] = (s32)extent.x;
    masks[1] = (s32)extent.y;
    masks[2] = (s32)extent.z;
    if (!masks[0]) {
        masks[0] = 1;
    }

    if (!masks[1]) {
        masks[1] = 1;
    }

    if (!masks[2]) {
        masks[2] = 1;
    }

    // Find highest number of bits to shift
    u32 max_entropy = 0;
    for (u32 component = 0; component < 3; component++) {
        u32 bits_sel = 1u << 31;
        u32 mask_sel = 0xFFFFFFFF;
        u32 mask = masks[component];
        u32 i = 0;

        for (u32 bit = 0; bit < 32; bit++) {
            if ((mask & bits_sel)) {
                masks[component] = ~mask_sel;
                if (max_entropy < i) {
                    max_entropy = i;
                }

                break;
            }

            bits_sel >>= 1;
            mask_sel >>= 1;
            i++;
        }
    }

    // Update header fields
    u32 bit_shift = 33 - max_entropy;
    u32 area_y_width_mask = masks[1];
    this->mKCLFile->mXMask = masks[0];
    u32 area_z_width_mask = masks[2];
    this->mKCLFile->mYMask = area_y_width_mask;
    this->mKCLFile->mZMask = area_z_width_mask;
    this->mKCLFile->mBlockWidthShift = bit_shift;
    this->mKCLFile->mMin = min;
}

void DynamicCollisionObj::createCollision() {
    s32 triangleNum = mTriangleNum;
    mKCLFile = new KCLFile();
    mKCLFile->mPos = new Vec3f[triangleNum];
    mKCLFile->mNorms = new Vec3f[triangleNum * 4];
    mKCLFile->mPrisms = new KC_PrismData[triangleNum + 1];
    // One leaf (list 0) holding every prism, from the last.
    u32* octree = new u32[1];
    octree[0] = 1u << 31;
    mKCLFile->mOctree = octree;
    u32* list = new u32[triangleNum + 2];
    list[0] = 0;  // not read
    s32 count = triangleNum;
    for (s32 i = 0; i <= triangleNum; i++) {
        list[i + 1] = count--;
    }
    mKCLFile->mLists = list;

    mKCLFile->mTriangleNum = triangleNum;
    mKCLFile->mThickness = cNum_createCollision_f1;
    mKCLFile->mBlockXShift = -1;
    mKCLFile->mBlockXYShift = -1;
    updateCollisionHeader();
    updateTriangle();

    mParts = new CollisionParts();
    Pos3f posmtx;
    NebulaMtxTrans(posmtx, mPosition.x, mPosition.y, mPosition.z);
    mParts->init(posmtx, getSensor("body"), mKCLFile, nullptr, CollisionCategory_Map, true);
    MR::validateCollisionParts(mParts);
    mParts->mServer->calcFarthestVertexDistance();
    mParts->updateBoundingSphereRange(Vec3f(mScale));
}
