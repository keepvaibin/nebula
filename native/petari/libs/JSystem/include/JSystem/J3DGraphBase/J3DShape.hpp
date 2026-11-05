#pragma once

#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DMaterial;
class J3DShapeMtx;
class J3DShapeDraw;
class J3DVertexData;
class J3DDrawMtxData;

enum J3DShpFlag {
    J3DShpFlag_Visible = 0x0001,
    J3DShpFlag_Hidden = 0x0010,
    J3DShpFlag_EnableLod = 0x0100,
};

// The matrix of a group of one, in its model's draw matrices.
class J3DShapeMtx {
public:
    J3DShapeMtx(u16 useMtxIndex) : mUseMtxIndex(useMtxIndex) {
    }

    virtual ~J3DShapeMtx() {
    }

    virtual bool isMulti() const {
        return false;
    }
    virtual u16 getUseMtxNum() const {
        return 1;
    }
    virtual u16 getUseMtxIndex(u16) const {
        return mUseMtxIndex;
    }

protected:
    u16 mUseMtxIndex;
};

class J3DShape {
public:
    J3DShape() {
        initialize();
    }

    void initialize();
    // Each corner carries a texture matrix place for coordinate `coord`: `valueBase` (0 position matrices, 30 texture
    // matrices, in their ids) plus its position matrix's id.
    void calcNBTScale(Vec const&, f32 (*)[3][3], f32 (*)[3][3]);
    u16 countBumpMtxNum() const;
    void initVtxAttr();
    void loadPreDrawSetting() const;
    // The packet's: what its user data loads before the draw (the post-transform of a skinned shape's coordinates).
    // The draw itself once its material is sent: from its model's block of matrices. `pCallback`: called before the draw.
    void drawGroups(void (*pCallback)(J3DShape*) = nullptr) const;

    virtual void draw() const;
    virtual void drawFast() const;
    virtual void simpleDraw() const;
    virtual void simpleDrawCache() const;

    void loadCurrentMtx() const;

    void onFlag(u32 flag) {
        mFlags |= flag;
    }
    void offFlag(u32 flag) {
        mFlags &= ~flag;
    }
    bool checkFlag(u32 flag) const {
        return (mFlags & flag) ? true : false;
    }

    void setMaterial(J3DMaterial* pMaterial) {
        mMaterial = pMaterial;
    }

    void setDrawMtxDataPointer(J3DDrawMtxData* pMtxData) {
        mDrawMtxData = pMtxData;
    }

    void setVertexDataPointer(J3DVertexData* pVtxData) {
        mVertexData = pVtxData;
    }

    // Its groups' geometry and its model's arrays registered with the renderer before its first draw.
    void prepareGeometry() const {
        if (!mNebulaRegistered) {
            J3DNativeShape::registerShape(const_cast< J3DShape* >(this));
        }
    }
    void show() {
        offFlag(J3DShpFlag_Visible);
    }
    void hide() {
        onFlag(J3DShpFlag_Visible);
    }

    void setCurrentViewNoPtr(u32* pViewNoPtr) {
        mCurrentViewNo = pViewNoPtr;
    }


    void setDrawMtx(Mtx** pDrawMtx) {
        mDrawMtx = pDrawMtx;
    }
    void setNrmMtx(Mtx33** pNrmMtx) {
        mNrmMtx = pNrmMtx;
    }
    bool getNBTFlag() const {
        return mHasNBT;
    }
    u32 getBumpMtxOffset() const {
        return mBumpMtxOffset;
    }
    void setBumpMtxOffset(u32 offset) {
        mBumpMtxOffset = offset;
    }
    J3DMaterial* getMaterial() const {
        return mMaterial;
    }
    u16 getIndex() const {
        return mIndex;
    }
    u32 getMtxGroupNum() const {
        return mMtxGroupNum;
    }
    J3DShapeDraw* getShapeDraw(u16 idx) {
        return mShapeDraw[idx];
    }
    J3DShapeMtx* getShapeMtx(u16 idx) {
        return mShapeMtx[idx];
    }
    Vec* getMin() {
        return &mMin;
    }
    Vec* getMax() {
        return &mMax;
    }

    friend struct J3DShapeFactory;
    friend class J3DJointTree;

    J3DMaterial* mMaterial;
    u16 mIndex;
    u16 mMtxGroupNum;
    u32 mFlags;
    f32 mRadius;
    Vec mMin;
    Vec mMax;
    bool mNebulaRegistered;
    bool mNebulaWhole;  // its groups registered as one geometry (J3DNativeShape::registerWhole)
    bool mHasNBT;
    J3DShapeMtx** mShapeMtx;
    J3DShapeDraw** mShapeDraw;
    bool mHasPNMTXIdx;
    J3DVertexData* mVertexData;
    J3DDrawMtxData* mDrawMtxData;
    Mtx** mDrawMtx;
    Mtx33** mNrmMtx;
    u32* mCurrentViewNo;
    u32 mBumpMtxOffset;
};
