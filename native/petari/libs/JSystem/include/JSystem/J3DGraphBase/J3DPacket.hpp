#pragma once


#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>
#include <stdint.h>

class J3DMatPacket;
class J3DDrawBuffer;
class J3DMaterial;
class J3DMaterialAnm;
class J3DModel;
class J3DMtxBuffer;
class J3DShape;
class J3DTexMtx;
class J3DTexMtxObj;
class J3DTexture;

enum J3DDiffFlag {
    J3DDiffFlag_MatColor = 0x1,
    J3DDiffFlag_ColorChan = 0x2,
    J3DDiffFlag_AmbColor = 0x4,
    J3DDiffFlag_TexGen = 0x1000,
    J3DDiffFlag_TevReg = 0x1000000,
    J3DDiffFlag_KonstColor = 0x2000000,  // is this right?
    J3DDiffFlag_TexCoordScale = 0x4000000,
    J3DDiffFlag_TevStageIndirect = 0x8000000,
    J3DDiffFlag_Fog = 0x10000000,
    J3DDiffFlag_Blend = 0x20000000,
    J3DDiffFlag_Unk40000000 = 0x40000000,
    J3DDiffFlag_Changed = 1u << 31,
};

// A material's typed snapshot (J3DNativeMaterial): made with its owner
// (a material, a material packet, an instance's shape packet), refreshed where J3D built or patched the list.
class J3DMaterialSnapshot {
public:
    J3DMaterialSnapshot() {
        mpNebulaMaterial = nullptr;
        mNebulaCaptured = false;
        mNebulaDiffFlags = 0;
    }

    J3DError newSnapshot();

    struct NebulaMaterialParts* mpNebulaMaterial;
    bool mNebulaCaptured;
    u32 mNebulaDiffFlags;
};

class J3DPacket {
public:
    J3DPacket() {
        mpNextPacket = NULL;
        mpFirstChild = NULL;
        mpUserArea = NULL;
    }

    virtual int entry(J3DDrawBuffer*);
    // Pure (the DOL has no vtable of this base; every packet defines it).
    virtual void draw() = 0;
    virtual ~J3DPacket() {
    }

    void addChildPacket(J3DPacket*);

    J3DPacket* getNextPacket() const {
        return mpNextPacket;
    }
    void setNextPacket(J3DPacket* i_packet) {
        mpNextPacket = i_packet;
    }

    void drawClear() {
        mpNextPacket = NULL;
        mpFirstChild = NULL;
    }

    void* getUserArea() const {
        return mpUserArea;
    }
    void setUserArea(uintptr_t area) {
        mpUserArea = (void*)area;
    }

public:
    J3DPacket* mpNextPacket;
    J3DPacket* mpFirstChild;
    void* mpUserArea;
};

class J3DDrawPacket : public J3DPacket {
public:
    J3DDrawPacket();
    ~J3DDrawPacket();
    J3DError newSnapshot();
    virtual void draw();

    J3DMaterialSnapshot* getSnapshot() {
        return mpSnapshot;
    }

    void setSnapshot(J3DMaterialSnapshot* pObj) {
        mpSnapshot = pObj;
    }

    bool isValidSnapshot() const {
        return mpSnapshot;
    }

    enum {
        LOCKED = 0x01,
    };

    bool checkFlag(u32 flag) const {
        return (mFlags & flag) ? true : false;
    }
    inline void onFlag(u32 flag) {
        mFlags |= flag;
    }
    inline void offFlag(u32 flag) {
        mFlags &= ~flag;
    }
    void lock() {
        onFlag(LOCKED);
    }
    void unlock() {
        offFlag(LOCKED);
    }
    J3DTexMtxObj* getTexMtxObj() {
        return mpTexMtxObj;
    }
    bool isLocked() const {
        return checkFlag(1);
    }

public:
    u32 mFlags;
    char unk_0x14[0x20 - 0x14];
    J3DMaterialSnapshot* mpSnapshot;
    J3DTexMtxObj* mpTexMtxObj;
};

class J3DShapePacket : public J3DDrawPacket {
public:
    J3DShapePacket();
    J3DError newDifferedSnapshot(u32);
    void prepareDraw() const;
    void drawFast();

    virtual ~J3DShapePacket();
    virtual void draw();

    void setShape(J3DShape* pShape) {
        mpShape = pShape;
    }

    void setModel(J3DModel* pModel) {
        mpModel = pModel;
    }

    void setMtxBuffer(J3DMtxBuffer* pMtxBuffer) {
        mpMtxBuffer = pMtxBuffer;
    }
    void setBaseMtxPtr(Mtx* pMtx) {
        mpBaseMtxPtr = pMtx;
    }

    J3DShape* getShape() const {
        return mpShape;
    }
    J3DModel* getModel() const {
        return mpModel;
    }
    Mtx* getBaseMtxPtr() const {
        return mpBaseMtxPtr;
    }

public:
    J3DShape* mpShape;
    J3DMtxBuffer* mpMtxBuffer;
    Mtx* mpBaseMtxPtr;
    u32 mDiffFlag;
    J3DModel* mpModel;
};

class J3DMatPacket : public J3DDrawPacket {
public:
    J3DMatPacket();
    void addShapePacket(J3DShapePacket*);
    bool isSame(J3DMatPacket*) const;

    J3DMaterial* getMaterial() const {
        return mpMaterial;
    }
    J3DShapePacket* getShapePacket() const {
        return mpShapePacket;
    }
    void setShapePacket(J3DShapePacket* packet) {
        mpShapePacket = packet;
    }

    void setMaterial(J3DMaterial* pMaterial) {
        mpMaterial = pMaterial;
    }

    void setTexture(J3DTexture* pTexture) {
        mpTexture = pTexture;
    }

    void setInitShapePacket(J3DShapePacket* packet) {
        mpInitShapePacket = packet;
    }
    void setMaterialID(u32 id) {
        mDiffFlag = id;
    }
    void setMaterialAnmID(J3DMaterialAnm* materialAnm) {
        mpMaterialAnm = materialAnm;
    }
    BOOL isChanged() {
        return mDiffFlag & J3DDiffFlag_Changed;
    }
    bool isEnabled_Diff() {
        return mpInitShapePacket->getSnapshot() != NULL;
    }

    virtual ~J3DMatPacket();
    virtual int entry(J3DDrawBuffer*);
    virtual void draw();

public:
    J3DShapePacket* mpInitShapePacket;
    J3DShapePacket* mpShapePacket;
    J3DMaterial* mpMaterial;
    u32 mDiffFlag;
    J3DTexture* mpTexture;
    J3DMaterialAnm* mpMaterialAnm;
};
