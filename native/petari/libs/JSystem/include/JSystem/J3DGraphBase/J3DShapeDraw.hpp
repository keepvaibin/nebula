#pragma once

#include <revolution.h>

class J3DShapeDraw {
public:
    u32 countVertex(u32);
    void addTexMtxIndexInDL(u32, u32, u32);
    J3DShapeDraw(u8 const*, u32);
    void draw() const;

    virtual ~J3DShapeDraw();

    u8* getDisplayList() const {
        return (u8*)mDisplayList;
    }
    u32 getDisplayListSize() const {
        return mDisplayListSize;
    }
    void setDisplayListSize(u32 size) {
        mDisplayListSize = size;
    }
#ifdef TARGET_PC
    u32 getNebulaHandle() const {
        return mNebulaHandle;
    }
#endif

private:
    /* 0x04 */ u32 mDisplayListSize;
    /* 0x08 */ void* mDisplayList;
#ifdef TARGET_PC
    // The renderer's handle for this list (__NebulaGXCallShapeDisplayList): one per shape draw, never reused.
    u32 mNebulaHandle;
#endif
};
