#include "JSystem/JKernel/JKRDvdRipper.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JKernel/JKRDvdFile.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include "JSystem/JUtility/JUTException.hpp"
#include "Game/System/NebulaFrameClock.hpp"

bool JKRDvdRipper::errorRetry = true;

void JKRDvdRipper::panicIfCompressed(const void* pData, const char* pName) {
    const u8* const magic = static_cast< const u8* >(pData);
    if (magic[0] == 'Y' && magic[1] == 'a' && (magic[2] == 'z' || magic[2] == 'y') && magic[3] == '0') {
        JUTException::panic_f(__FILE__, __LINE__, "%s: a compressed file, not cooked (the importer expands them)", pName);
    }
}

void* JKRDvdRipper::loadToMainRAM(char const* name, u8* dst, u32 dstLength, JKRHeap* heap, JKRDvdRipper::EAllocDirection allocDirection, u32 offset,
                                  u32* pSize) {
    JKRDvdFile file;

    if (!file.open(name)) {
        return nullptr;
    }

    return JKRDvdToMainRam(&file, dst, dstLength, heap, allocDirection, offset, pSize);
}

void* JKRDvdRipper::loadToMainRAM(s32 entryNumber, u8* dst, u32 dstLength, JKRHeap* heap, JKRDvdRipper::EAllocDirection allocDirection, u32 offset,
                                  u32* pSize) {
    JKRDvdFile file;
    if (!file.open(entryNumber)) {
        return nullptr;
    }
    return JKRDvdToMainRam(&file, dst, dstLength, heap, allocDirection, offset, pSize);
}

void* JKRDvdRipper::loadToMainRAM(JKRDvdFile* dvdFile, u8* dst, u32 dstLength, JKRHeap* heap, JKRDvdRipper::EAllocDirection allocDirection, u32 offset,
                                  u32* pSize) {
    bool hasAllocated = false;

    const s32 fileSizeAligned = ALIGN_NEXT(dvdFile->getFileSize(), 32);
    s32 readSize = fileSizeAligned - offset;
    if (dstLength != 0 && dstLength < readSize) {
        readSize = dstLength;
    }

    if (dst == nullptr) {
        dst = reinterpret_cast< u8* >(JKRAllocFromHeap(heap, readSize, allocDirection == ALLOC_DIRECTION_FORWARD ? 32 : -32));
        hasAllocated = true;
    }
    if (dst == nullptr) {
        return nullptr;
    }

    while (true) {
        int readBytes = NebulaDisc::read(dvdFile->getFile(), dst, readSize, static_cast< s32 >(offset));
        if (readBytes >= 0) {
            break;
        }

        if (readBytes == -3 || !errorRetry) {
            if (hasAllocated) {
                JKRFree(dst);
            }
            return nullptr;
        }
        NebulaFrameClock::waitFrame();
    }

    if (offset == 0) {
        panicIfCompressed(dst, "JKRDvdRipper");
    }
    if (pSize) {
        *pSize = readSize;
    }
    return dst;
}
