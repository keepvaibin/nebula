#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DDrawBuffer;
class J3DPacket;
class J3DDrawPacket;
class J3DMatPacket;
class J3DShapePacket;

inline f32 J3DCalcZValue(MtxPtr m, Vec v) {
    return (v.z * m[2][2] + v.x * m[2][0]) + (m[2][3] + v.y * m[2][1]);
}

enum J3DDrawBufDrawMode {
    J3DDrawBufDrawMode_Head,
    J3DDrawBufDrawMode_Tail,

    J3DDrawBufDrawMode_MAX,
};

enum J3DDrawBufSortMode {
    J3DDrawBufSortMode_Mat,
    J3DDrawBufSortMode_MatAnm,
    J3DDrawBufSortMode_Z,
    J3DDrawBufSortMode_Model,
    J3DDrawBufSortMode_Invalid,
    J3DDrawBufSortMode_Non,

    J3DDrawBufSortMode_MAX,
};

// A frame's material packets as a flat list, as long as it needs to be, in the order the sort mode asks for when drawn.
class J3DDrawBuffer {
public:
    typedef int (J3DDrawBuffer::*sortFunc)(J3DMatPacket*);

    J3DDrawBuffer() {
        initialize();
    }
    ~J3DDrawBuffer();
    void initialize();
    void frameInit();
    int entryMatSort(J3DMatPacket*);
    int entryZSort(J3DMatPacket*);
    int entryModelSort(J3DMatPacket*);
    int entryInvalidSort(J3DMatPacket*);
    int entryNonSort(J3DMatPacket*);
    void draw();

    int getSortMode() {
        return mSortMode;
    }

    void setNonSort() {
        mSortMode = J3DDrawBufSortMode_Non;
    }
    void setZSort() {
        mSortMode = J3DDrawBufSortMode_Z;
    }
    void setZMtx(MtxPtr mtx) {
        mpZMtx = mtx;
    }

    struct Entry {
        u64 mKey;
        J3DMatPacket* mPacket;
    };

    u32 mDrawMode;
    u32 mSortMode;
    MtxPtr mpZMtx;
    J3DPacket* mpCallBackPacket;

    static sortFunc sortFuncTable[6];
    static int entryNum;

private:
    void add(u64 key, J3DMatPacket* pPacket);
    void sort();

    Entry* mEntries;
    Entry* mScratch;
    u32 mEntryCount;
    u32 mEntryRoom;
};
