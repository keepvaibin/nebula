#pragma once

#include <revolution/gx.h>

class JPAEmitterWorkData;

struct JPAExTexShapeData {
    // Common header.
    /* 0x00 */ u8 mMagic[4];
    /* 0x04 */ NEBULA_BE(u32) mSize;

    /* 0x08 */ NEBULA_BE(u32) mFlags;
    /* 0x0C */ NEBULA_BE(f32) mIndTexMtx[2][3];
    /* 0x24 */ s8 mExpScale;
    /* 0x25 */ s8 mIndTexIdx;
    /* 0x26 */ s8 mSecTexIdx;
};  // Size: 0x28

class JPAExTexShape {
public:
    JPAExTexShape(u8 const*);

#ifdef TARGET_PC
    // A native copy of the resource's big-endian matrix, for GXSetIndTexMtx.
    void getIndTexMtx(f32 (*pMtx)[3]) const {
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 3; j++) {
                pMtx[i][j] = mpData->mIndTexMtx[i][j];
            }
        }
    }
#else
    const f32* getIndTexMtx() const {
        return &mpData->mIndTexMtx[0][0];
    }
#endif
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
