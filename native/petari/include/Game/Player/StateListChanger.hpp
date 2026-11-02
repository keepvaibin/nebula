#pragma once

#include <JSystem/J3DGraphBase/J3DStateList.hpp>

class J3DModelX;

struct StateListHolder {
    J3DStateList mList;
};

class StateListChanger {
public:
    // `capacity`: the changes each buffer holds.
    StateListChanger(u8 count, u16 capacity) {
        mBuffers = new StateListHolder[count];
        mBufferCount = count;
        mCurrentBuffer = 0;
        for (u32 i = 0; i < mBufferCount; i++) {
            mBuffers[i].mList.reserve(capacity);
        }
    }

    void addList(J3DModelX*);

    StateListHolder* swap() {
        mCurrentBuffer = (mCurrentBuffer + 1) % mBufferCount;
        return &mBuffers[mCurrentBuffer];
    }

    /* 0x00 */ StateListHolder* mBuffers;
    /* 0x04 */ u8 mBufferCount;
    /* 0x05 */ u8 mCurrentBuffer;
};
