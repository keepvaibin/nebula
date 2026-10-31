#pragma once

#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

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

private:
    /* 0x04 */ u32 mDisplayListSize;
    /* 0x08 */ void* mDisplayList;

public:
    // The position matrix places its corners read (bit k: place k; 0: none of their own), set when it is registered
    // (J3DNativeShape::registerShape).
    u16 mNebulaPlaces = 0;
};
