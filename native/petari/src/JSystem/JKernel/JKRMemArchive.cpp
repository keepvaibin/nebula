#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JKernel/JKRAram.hpp"
#include "JSystem/JKernel/JKRDecomp.hpp"
#include "JSystem/JKernel/JKRDvdRipper.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include "JSystem/JUtility/JUTException.hpp"
#include <revolution.h>
#include <cstring>

JKRMemArchive::JKRMemArchive() {
}

JKRMemArchive::JKRMemArchive(s32 entryNum, EMountDirection mountDir) : JKRArchive(entryNum, MOUNT_MODE_MEM) {
    mIsMounted = false;
    mMountDir = mountDir;

    if (!open(entryNum, mountDir)) {
        return;
    }

    mLoaderType = RARC_MAGIC;
    mLoaderName = mStringTable + mDirs->mNameOffset;

    prependVolumeList(&mLoaderLink);

    mIsMounted = true;
}

JKRMemArchive::~JKRMemArchive() {
    if (mIsMounted == true) {
        if (_6C && mHeader != nullptr) {
            JKRHeap::free(mHeader, mHeap);
        }

        removeVolumeList(&mLoaderLink);
        mIsMounted = false;
    }
}

void JKRMemArchive::fixedInit(s32 entryNum) {
    mIsMounted = false;
    mMountMode = MOUNT_MODE_MEM;
    _34 = 1;
    _58 = 2;
    mHeap = JKRHeap::sCurrentHeap;
    mEntryNum = entryNum;

    if (sCurrentVolume != nullptr) {
        return;
    }

    sCurrentVolume = this;
    sCurrentDirID = 0;
}

bool JKRMemArchive::mountFixed(void* a1, JKRMemBreakFlag breakFlag) {
#ifdef TARGET_PC
    // The archive's identity is its address, as the console's cached address (0x80000000 | physical): never a disc
    // entry number, and unique per archive.
    const s32 identity = static_cast< s32 >(0x80000000u | OSCachedToPhysical(a1));
#else
    const s32 identity = reinterpret_cast< s32 >(a1);
#endif
    if (check_mount_already(identity) != nullptr) {
        return false;
    }

    fixedInit(identity);

    if (!open(a1, 0xFFFF, breakFlag)) {
        return false;
    }

    SDIDirEntry* firstDir = mDirs;
    char* stringTable = mStringTable;

    mLoaderType = RARC_MAGIC;
    mLoaderName = stringTable + firstDir->mNameOffset;

    prependVolumeList(&mLoaderLink);

    mIsMounted = true;
    _6C = breakFlag == JKR_MEM_BREAK_FLAG_1;

    return true;
}

bool JKRMemArchive::open(s32 entryNum, EMountDirection mountDir) {
    mHeader = nullptr;
    mInfoBlock = nullptr;
    mFileDataStart = nullptr;
    mDirs = nullptr;
    mFiles = nullptr;
    mStringTable = nullptr;
    _6C = false;
    mMountDir = mountDir;

    if (mountDir == MOUNT_DIRECTION_1) {
        u32 size;

        void* pData = JKRDvdRipper::loadToMainRAM(entryNum, nullptr, EXPAND_SWITCH_UNKNOWN1, 0, mHeap, JKRDvdRipper::ALLOC_DIRECTION_FORWARD, 0,
                                                  reinterpret_cast< int* >(&_5C), &size);

        mHeader = reinterpret_cast< RarcHeader* >(pData);

        if (pData != nullptr) {
            DCInvalidateRange(pData, size);
        }
    } else {
        u32 size;

        void* pData = JKRDvdRipper::loadToMainRAM(entryNum, nullptr, EXPAND_SWITCH_UNKNOWN1, 0, mHeap, JKRDvdRipper::ALLOC_DIRECTION_BACKWARD, 0,
                                                  reinterpret_cast< int* >(&_5C), &size);

        mHeader = reinterpret_cast< RarcHeader* >(pData);

        if (pData != nullptr) {
            DCInvalidateRange(pData, size);
        }
    }

    if (mHeader == nullptr) {
        mMountMode = MOUNT_MODE_0;
    } else {
        mInfoBlock = reinterpret_cast< RarcInfoBlock* >(reinterpret_cast< u8* >(mHeader) + mHeader->mHeaderSize);
        mDirs = reinterpret_cast< SDIDirEntry* >(&reinterpret_cast< u8* >(mInfoBlock)[mInfoBlock->mDirOffset]);
        mFiles = reinterpret_cast< SDIFileEntry* >(&reinterpret_cast< u8* >(mInfoBlock)[mInfoBlock->mFileOffset]);
        mStringTable = reinterpret_cast< char* >(&reinterpret_cast< u8* >(mInfoBlock)[mInfoBlock->mStringTableOffset]);
        mFileDataStart = reinterpret_cast< u8* >(mHeader->mFileDataOffset + (reinterpret_cast< uintptr_t >(mHeader) + mHeader->mHeaderSize));
        _6C = true;
    }

    return mMountMode != MOUNT_MODE_0;
}

bool JKRMemArchive::open(void* pData, u32 a2, JKRMemBreakFlag breakFlag) {
    mHeader = reinterpret_cast< RarcHeader* >(pData);
    mInfoBlock = reinterpret_cast< RarcInfoBlock* >(reinterpret_cast< u8* >(mHeader) + mHeader->mHeaderSize);
    mDirs = reinterpret_cast< SDIDirEntry* >(&reinterpret_cast< u8* >(mInfoBlock)[mInfoBlock->mDirOffset]);
    mFiles = reinterpret_cast< SDIFileEntry* >(&reinterpret_cast< u8* >(mInfoBlock)[mInfoBlock->mFileOffset]);
    mStringTable = reinterpret_cast< char* >(&reinterpret_cast< u8* >(mInfoBlock)[mInfoBlock->mStringTableOffset]);
    mFileDataStart = reinterpret_cast< u8* >(mHeader->mFileDataOffset + (reinterpret_cast< uintptr_t >(mHeader) + mHeader->mHeaderSize));
    _6C = breakFlag == JKR_MEM_BREAK_FLAG_1;
    mHeap = JKRHeap::findFromRoot(pData);
    _5C = 0;

    return true;
}

void* JKRMemArchive::fetchResource(SDIFileEntry* pFile, u32* pSize) {
    if (fileData(pFile) == nullptr) {
        fileData(pFile) = mFileDataStart + pFile->mDataOffset;
    }

    if (pSize != nullptr) {
        *pSize = pFile->mDataSize;
    }

    return fileData(pFile);
}

void* JKRMemArchive::fetchResource(void* pData, u32 dataSize, SDIFileEntry* pFile, u32* pSize) {
    u32 size = pFile->mDataSize;

    if (size > dataSize) {
        size = dataSize;
    }

    if (fileData(pFile) != nullptr) {
        memcpy(pData, fileData(pFile), size);
    } else {
        s32 compression;

        if ((pFile->mFlag & FILE_FLAG_COMPRESSED) == 0) {
            compression = JKR_COMPRESSION_NONE;
        } else if ((pFile->mFlag & FILE_FLAG_IS_YAZ0) != 0) {
            compression = JKR_COMPRESSION_SZS;
        } else {
            compression = JKR_COMPRESSION_SZP;
        }

        size = fetchResource_subroutine(mFileDataStart + pFile->mDataOffset, size, reinterpret_cast< u8* >(pData), dataSize, compression);
    }

    if (pSize != nullptr) {
        *pSize = size;
    }

    return pData;
}

void JKRMemArchive::removeResourceAll() {
    if (mInfoBlock == nullptr) {
        return;
    }

    if (mMountMode == MOUNT_MODE_MEM) {
        return;
    }

    SDIFileEntry* current = mFiles;

    for (s32 i = 0; i < mInfoBlock->mNrFiles; i++) {
        if (fileData(current) != nullptr) {
            fileData(current) = nullptr;
        }
    }
}

bool JKRMemArchive::removeResource(void* pResource) {
    SDIFileEntry* file = findPtrResource(pResource);

    if (file == nullptr) {
        return false;
    }

    fileData(file) = nullptr;
    return true;
}

s32 JKRMemArchive::fetchResource_subroutine(unsigned char* pSrc, u32 srcSize, unsigned char* pDst, u32 dstSize, int compression) {
    switch (compression) {
    case JKR_COMPRESSION_NONE:
        if (srcSize > dstSize) {
            srcSize = dstSize;
        }

        memcpy(pDst, pSrc, srcSize);

        return srcSize;
    case JKR_COMPRESSION_SZP:
    case JKR_COMPRESSION_SZS: {
        u32 size = JKRDecompExpandSize(pSrc);

        if (size > dstSize) {
            size = dstSize;
        }

        JKRDecomp::orderSync(pSrc, pDst, size, 0);
        return size;
    }
    default:
        JUTException::panic(__FILE__, 723, "??? bad sequence\n");
        break;
    }

    return 0;
}

u32 JKRMemArchive::getExpandedResSize(const void* pResource) const {
    SDIFileEntry* file = findPtrResource(pResource);

    if (file == nullptr) {
        return -1;
    }

    if ((file->mFlag & FILE_FLAG_COMPRESSED) == 0) {
        return getResSize(pResource);
    }

    return JKRDecompExpandSize(reinterpret_cast< u8* >(const_cast< void* >(pResource)));
}
