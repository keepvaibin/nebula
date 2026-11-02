#include "JSystem/JKernel/JKRDvdArchive.hpp"
#include "JSystem/JKernel/JKRArchive.hpp"
#include "JSystem/JKernel/JKRDvdFile.hpp"
#include "JSystem/JKernel/JKRDvdRipper.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include "JSystem/JUtility/JUTException.hpp"
#include <revolution/types.h>
#include <cstdlib>
#include <cstring>
#include <mem.h>

JKRDvdArchive::JKRDvdArchive(s32 entryNum, EMountDirection mountDir) : JKRArchive(entryNum, MOUNT_MODE_DVD) {
    mMountDir = mountDir;

    if (!open(entryNum)) {
        return;
    }

    mLoaderType = RARC_MAGIC;
    mLoaderName = mStringTable + mDirs->mNameOffset;

    prependVolumeList(&mLoaderLink);

    mIsMounted = true;
}

JKRDvdArchive::~JKRDvdArchive() {
    if (mIsMounted == true) {
        if (mInfoBlock != nullptr) {
            SDIFileEntry* entry = mFiles;
            for (int i = 0; i < mInfoBlock->mNrFiles; i++) {
                if (fileData(entry) != nullptr) {
                    JKRFreeToHeap(mHeap, fileData(entry));
                }

                entry++;
            }

            JKRFreeToHeap(mHeap, mInfoBlock);
            mInfoBlock = nullptr;
        }

        if (mFile != nullptr) {
            delete mFile;
        }

        removeVolumeList(&mLoaderLink);
        mIsMounted = false;
    }
}

bool JKRDvdArchive::open(s32 entryNum) {
    mInfoBlock = nullptr;
    _64 = 0;
    mDirs = nullptr;
    mFiles = nullptr;
    mStringTable = nullptr;

    mFile = new (JKRHeap::getSystemHeap(), 0) JKRDvdFile(entryNum);
    if (mFile == nullptr) {
        mMountMode = MOUNT_MODE_0;
        return false;
    }

    RarcHeader* header = reinterpret_cast< RarcHeader* >(JKRHeap::getSystemHeap()->alloc(32, 32));
    if (header == nullptr) {
        mMountMode = MOUNT_MODE_0;
    } else {
        JKRDvdRipper::loadToMainRAM(entryNum, reinterpret_cast< u8* >(header), 32, nullptr, JKRDvdRipper::ALLOC_DIRECTION_FORWARD, 0, nullptr);
        panicIfNotCooked(header);

        int alignment = mMountDir == MOUNT_DIRECTION_1 ? 32 : -32;
        mInfoBlock = reinterpret_cast< RarcInfoBlock* >(JKRHeap::alloc(header->mFileDataOffset, alignment, mHeap));
        if (mInfoBlock == nullptr) {
            mMountMode = MOUNT_MODE_0;
        } else {
            JKRDvdRipper::loadToMainRAM(entryNum, reinterpret_cast< u8* >(mInfoBlock), header->mFileDataOffset, nullptr,
                                        JKRDvdRipper::ALLOC_DIRECTION_FORWARD, 32, nullptr);

            mDirs = reinterpret_cast< SDIDirEntry* >(reinterpret_cast< u8* >(mInfoBlock) + mInfoBlock->mDirOffset);
            mFiles = reinterpret_cast< SDIFileEntry* >(reinterpret_cast< u8* >(mInfoBlock) + mInfoBlock->mFileOffset);
            mStringTable = reinterpret_cast< char* >(mInfoBlock) + mInfoBlock->mStringTableOffset;

            _64 = header->mHeaderSize + header->mFileDataOffset;
        }
    }

    if (header != nullptr) {
        JKRHeap::getSystemHeap()->free(header);
    }

    if (mMountMode == MOUNT_MODE_0) {
        if (mFile != nullptr) {
            delete mFile;
        }

        return false;
    }

    return true;
}

void* JKRDvdArchive::fetchResource(JKRArchive::SDIFileEntry* pEntry, u32* pSize) {
    u32 emergency_pointer;
    if (pSize == nullptr) {
        pSize = &emergency_pointer;
    }

    panicIfCompressedEntry(pEntry);

    if (fileData(pEntry) == nullptr) {
        const u32 size = ALIGN_NEXT(pEntry->mDataSize, 32);
        u8* const out = static_cast< u8* >(mHeap->alloc(size, 0x20, mHeap));
        JKRDvdRipper::loadToMainRAM(mEntryNum, out, size, nullptr, JKRDvdRipper::ALLOC_DIRECTION_FORWARD, _64 + pEntry->mDataOffset, nullptr);
        *pSize = size;
        fileData(pEntry) = out;
    } else {
        *pSize = pEntry->mDataSize;
    }

    return fileData(pEntry);
}

void* JKRDvdArchive::fetchResource(void* pArg1, u32 arg2, JKRArchive::SDIFileEntry* pEntry, u32* pSize) {
    u32 fetchedSize = pEntry->mDataSize;

    panicIfCompressedEntry(pEntry);

    if (fileData(pEntry) == nullptr) {
        fetchedSize = ALIGN_NEXT(pEntry->mDataSize, 32);
        const u32 alignedDestination = ALIGN_PREV(arg2, 32);
        if (fetchedSize > alignedDestination) {
            fetchedSize = alignedDestination;
        }

        JKRDvdRipper::loadToMainRAM(mEntryNum, static_cast< u8* >(pArg1), fetchedSize, nullptr, JKRDvdRipper::ALLOC_DIRECTION_FORWARD,
                                    _64 + pEntry->mDataOffset, nullptr);
    } else {
        if (fetchedSize > arg2) {
            fetchedSize = arg2;
        }

        JKRHeap::copyMemory(pArg1, fileData(pEntry), fetchedSize);
    }

    if (pSize != nullptr) {
        *pSize = fetchedSize;
    }

    return pArg1;
}
