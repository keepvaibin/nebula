#pragma once


#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>
#include <stdint.h>

class J3DMatPacket;
class J3DDrawBuffer;
class J3DMaterial;
class J3DModel;
class J3DMtxBuffer;
class J3DShape;

// A material whose packet never joins another's.
const u32 J3DMaterialID_Alone = 1u << 31;

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
    virtual void draw();

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
    bool isLocked() const {
        return checkFlag(1);
    }

public:
    u32 mFlags;
};

class J3DShapePacket : public J3DDrawPacket {
public:
    J3DShapePacket();
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
    J3DModel* mpModel;
    // What it draws: the model data's material, or the model's own copy.
    J3DMaterial* mpMaterial;
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

    void setInitShapePacket(J3DShapePacket* packet) {
        mpInitShapePacket = packet;
    }
    void setMaterialID(u32 id) {
        mMaterialID = id;
    }
    BOOL isChanged() {
        return mMaterialID & J3DMaterialID_Alone;
    }

    virtual ~J3DMatPacket();
    virtual int entry(J3DDrawBuffer*);
    virtual void draw();

public:
    J3DShapePacket* mpInitShapePacket;
    J3DShapePacket* mpShapePacket;
    J3DMaterial* mpMaterial;
    u32 mMaterialID;
};
