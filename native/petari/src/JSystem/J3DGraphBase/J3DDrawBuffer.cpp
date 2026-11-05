#include "JSystem/J3DGraphBase/J3DDrawBuffer.hpp"
#include <nebula_cooked_texture.h>
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DTexture.hpp"
#include "Game/System/NebulaArena.hpp"
#include <string.h>

void J3DDrawBuffer::initialize() {
    mDrawMode = J3DDrawBufDrawMode_Head;
    mSortMode = J3DDrawBufSortMode_Mat;
    mpZMtx = nullptr;
    mpCallBackPacket = nullptr;
    mEntries = nullptr;
    mScratch = nullptr;
    mEntryCount = 0;
    mEntryRoom = 0;
}

J3DDrawBuffer::~J3DDrawBuffer() {
    delete[] mEntries;
    delete[] mScratch;
}

void J3DDrawBuffer::frameInit() {
    mEntryCount = 0;
    mpCallBackPacket = nullptr;
}

void J3DDrawBuffer::add(u64 key, J3DMatPacket* pPacket) {
    if (mEntryCount == mEntryRoom) {
        const u32 room = mEntryRoom != 0 ? mEntryRoom * 2 : 64;
        Entry* pEntries = new Entry[room];
        if (mEntryCount != 0) {
            memcpy(pEntries, mEntries, mEntryCount * sizeof(Entry));
        }
        delete[] mEntries;
        delete[] mScratch;
        mEntries = pEntries;
        mScratch = new Entry[room];
        mEntryRoom = room;
    }
    mEntries[mEntryCount].mKey = key;
    mEntries[mEntryCount].mPacket = pPacket;
    mEntryCount++;
}

int J3DDrawBuffer::entryMatSort(J3DMatPacket* pMatPacket) {
    pMatPacket->drawClear();
    pMatPacket->getShapePacket()->drawClear();

    // The same image together, wherever it was loaded, so that draw order does not depend on memory layout.
    u32 image = 0;
    if (!pMatPacket->isChanged()) {
        const u16 texNo = pMatPacket->getMaterial()->getTexNo(0);
        if (texNo != 0xFFFF) {
            const ResTIMG* pTimg = j3dSys.getTexture()->getResTIMG(texNo);
            const NebulaTexture* pRecord = nebula_texture_record(pTimg->image());
            image = pRecord != nullptr ? static_cast< u32 >(pRecord->pack_hash) : 0;
        }
        for (u32 i = 0; i < mEntryCount; i++) {
            if (static_cast< u32 >(mEntries[i].mKey >> 32) == image && mEntries[i].mPacket->isSame(pMatPacket)) {
                mEntries[i].mPacket->addShapePacket(pMatPacket->getShapePacket());
                return 0;
            }
        }
    }
    add(static_cast< u64 >(image) << 32, pMatPacket);
    return 1;
}

int J3DDrawBuffer::entryZSort(J3DMatPacket* pMatPacket) {
    pMatPacket->drawClear();
    pMatPacket->getShapePacket()->drawClear();

    Vec tmp;
    tmp.x = mpZMtx[0][3];
    tmp.y = mpZMtx[1][3];
    tmp.z = mpZMtx[2][3];

    // Far first: the key is the distance's bits turned round, in front of the place in the list.
    const f32 distance = -J3DCalcZValue(j3dSys.getViewMtx(), tmp);
    const f32 far = distance > 0.0f ? distance : 0.0f;
    add(static_cast< u64 >(0xFFFFFFFFu - __builtin_bit_cast(u32, far)) << 32, pMatPacket);
    return 1;
}

int J3DDrawBuffer::entryModelSort(J3DMatPacket* pMatPacket) {
    pMatPacket->drawClear();
    pMatPacket->getShapePacket()->drawClear();

    if (mpCallBackPacket != nullptr) {
        mpCallBackPacket->addChildPacket(pMatPacket);
        return 1;
    }

    return 0;
}

int J3DDrawBuffer::entryInvalidSort(J3DMatPacket* pMatPacket) {
    pMatPacket->drawClear();
    pMatPacket->getShapePacket()->drawClear();

    if (mpCallBackPacket != nullptr) {
        mpCallBackPacket->addChildPacket(pMatPacket->getShapePacket());
        return 1;
    }

    return 0;
}

int J3DDrawBuffer::entryNonSort(J3DMatPacket* pMatPacket) {
    pMatPacket->drawClear();
    pMatPacket->getShapePacket()->drawClear();

    add(0, pMatPacket);
    return 1;
}

J3DDrawBuffer::sortFunc J3DDrawBuffer::sortFuncTable[6] = {
    &J3DDrawBuffer::entryMatSort,   &J3DDrawBuffer::entryMatSort,  &J3DDrawBuffer::entryZSort,
    &J3DDrawBuffer::entryModelSort, &J3DDrawBuffer::entryInvalidSort, &J3DDrawBuffer::entryNonSort,
};

int J3DDrawBuffer::entryNum;

// By key; of equal keys, the one entered last first.
void J3DDrawBuffer::sort() {
    for (u32 i = 0; i < mEntryCount; i++) {
        mEntries[i].mKey |= mEntryCount - 1 - i;
    }
    Entry* pFrom = mEntries;
    Entry* pTo = mScratch;
    for (u32 width = 1; width < mEntryCount; width *= 2) {
        for (u32 start = 0; start < mEntryCount; start += width * 2) {
            const u32 middle = start + width < mEntryCount ? start + width : mEntryCount;
            const u32 end = start + width * 2 < mEntryCount ? start + width * 2 : mEntryCount;
            u32 left = start;
            u32 right = middle;
            for (u32 out = start; out < end; out++) {
                const bool takeLeft = left < middle && (right >= end || pFrom[left].mKey <= pFrom[right].mKey);
                pTo[out] = takeLeft ? pFrom[left++] : pFrom[right++];
            }
        }
        Entry* pSwap = pFrom;
        pFrom = pTo;
        pTo = pSwap;
    }
    mEntries = pFrom;
    mScratch = pTo;
}

void J3DDrawBuffer::draw() {
    sort();
    if (mDrawMode == J3DDrawBufDrawMode_Head) {
        for (u32 i = 0; i < mEntryCount; i++) {
            mEntries[i].mPacket->draw();
        }
    } else {
        for (u32 i = mEntryCount; i != 0; i--) {
            mEntries[i - 1].mPacket->draw();
        }
    }
    // Drawn again, its entries are in order already: their keys come from the same place.
    for (u32 i = 0; i < mEntryCount; i++) {
        mEntries[i].mKey &= 0xFFFFFFFF00000000ull;
    }
}
