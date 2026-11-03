#pragma once

#include <NebulaGpu.h>

struct JPAEmitterWorkData;

struct JPAExTexShapeData {
    // Common header.
    u8 mMagic[4];
    u32 mSize;

    u32 mFlags;
    f32 mIndTexMtx[2][3];
    s8 mExpScale;
    s8 mIndTexIdx;
    s8 mSecTexIdx;
};

class JPAExTexShape {
public:
    JPAExTexShape(u8 const*);

    // The resource's matrix.
    void getIndTexMtx(f32 (*pMtx)[3]) const {
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 3; j++) {
                pMtx[i][j] = mpData->mIndTexMtx[i][j];
            }
        }
    }
    s8 getExpScale() const {
        return mpData->mExpScale;
    }
    u8 getIndTexIdx() const {
        return mpData->mIndTexIdx;
    }
    u8 getSecTexIdx() const {
        return mpData->mSecTexIdx;
    }
    bool isUseIndirect() const {
        return !!(mpData->mFlags & 0x01);
    }
    BOOL isUseSecTex() const {
        return (mpData->mFlags & 0x0100);
    }

public:
    const JPAExTexShapeData* mpData;
};

void JPALoadExTex(JPAEmitterWorkData*);
