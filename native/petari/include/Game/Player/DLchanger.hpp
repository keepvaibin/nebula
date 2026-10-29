#pragma once

#include <JSystem/J3DGraphBase/J3DStateList.hpp>
#include <revolution/types.h>

class J3DModelX;

struct DLholder {
    J3DStateList mList;
};

class DLchanger {
public:
    // `capacity`: the changes each buffer holds.
    DLchanger(u8 count, u16 capacity) {
        mBuffers = new DLholder[count];
        mBufferCount = count;
        mCurrentBuffer = 0;
        for (u32 i = 0; i < mBufferCount; i++) {
            mBuffers[i].mList.reserve(capacity);
        }
    }

    void addDL(J3DModelX*);

    DLholder* swap() {
        mCurrentBuffer = (mCurrentBuffer + 1) % mBufferCount;
        return &mBuffers[mCurrentBuffer];
    }

    /* 0x00 */ DLholder* mBuffers;
    /* 0x04 */ u8 mBufferCount;
    /* 0x05 */ u8 mCurrentBuffer;
};
