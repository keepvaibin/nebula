#include "JSystem/JKernel/JKRArchive.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include <cstring>
#include <ctype.h>

u32 JKRArchive::sCurrentDirID;

JKRArchive::JKRArchive() {
    mIsMounted = false;
    mMountDir = MOUNT_DIRECTION_1;
}

JKRArchive::JKRArchive(s32 entryNum, EMountMode mountMode) {
    mIsMounted = false;
    mMountMode = mountMode;
    _34 = 1;
    _58 = 1;
    mHeap = JKRHeap::findFromRoot(this);

    if (mHeap == nullptr) {
        mHeap = JKRHeap::sCurrentHeap;
    }

    mEntryNum = entryNum;

    if (sCurrentVolume == nullptr) {
        sCurrentDirID = 0;
        sCurrentVolume = this;
    }
}

JKRArchive::~JKRArchive() {
    freeFileDataTable();
}

void JKRArchive::allocFileDataTable() const {
    // Host memory: the console spent no game heap on these pointers.
    mFileDataTable = static_cast< void** >(::calloc(mInfoBlock->mNrFiles, sizeof(void*)));
}

void JKRArchive::freeFileDataTable() {
    ::free(mFileDataTable);
    mFileDataTable = nullptr;
}

void JKRArchive::panicIfNotCooked(const void* pHeader) {
    if (pHeader != nullptr && *static_cast< const u32* >(pHeader) == 'CRAR') {  // "RARC" as the disc has it
        NEBULA_PANIC("JKRArchive: an archive not in host order (the importer cooks it)");
    }
}

void JKRArchive::panicIfCompressedEntry(const SDIFileEntry* pEntry) const {
    if ((pEntry->mFlag & FILE_FLAG_COMPRESSED) != 0) {
        NEBULA_PANIC("%s: a compressed member, not cooked (the importer expands them)", mStringTable + pEntry->mNameOffset);
    }
}

bool JKRArchive::isSameName(CArcName& rName, u32 nameOffset, u16 hash) const {
    if (rName.mHash != hash) {
        return false;
    }

    return strcmp(mStringTable + nameOffset, rName.mName) == 0;
}

JKRArchive::SDIDirEntry* JKRArchive::findResType(u32 a1) const {
    SDIDirEntry* current = mDirs;
    for (u32 i = 0; i < mInfoBlock->mNrDirs; i++) {
        if (current->mID == a1) {
            return current;
        }

        current++;
    }

    return nullptr;
}

JKRArchive::SDIDirEntry* JKRArchive::findDirectory(const char* name, u32 directoryId) const {
    if (name == NULL) {
        return mDirs + directoryId;
    }

    CArcName arcName(&name, '/');
    SDIDirEntry* dirEntry = mDirs + directoryId;
    SDIFileEntry* fileEntry = mFiles + dirEntry->mFirstFileIndex;

    for (int i = 0; i < dirEntry->mNrFiles; i++) {
        if (isSameName(arcName, fileEntry->mNameOffset, fileEntry->mHash)) {
            if ((fileEntry->mFlag) & 2) {
                return findDirectory(name, fileEntry->mDataOffset);
            }
            break;
        }
        fileEntry++;
    }

    return NULL;
}

JKRArchive::SDIFileEntry* JKRArchive::findTypeResource(u32 a1, const char* pName) const {
    if (a1 != 0) {
        CArcName name;
        name.store(pName);

        SDIDirEntry* dir = findResType(a1);

        if (dir != nullptr) {
            SDIFileEntry* current = &mFiles[dir->mFirstFileIndex];

            for (s32 i = 0; i < dir->mNrFiles; i++) {
                if (isSameName(name, current->mNameOffset, current->mHash)) {
                    return current;
                }

                current++;
            }
        }
    }

    return nullptr;
}

JKRArchive::SDIFileEntry* JKRArchive::findFsResource(const char* name, u32 directoryId) const {
    if (name) {
        CArcName arcName(&name, '/');
        SDIDirEntry* dirEntry = mDirs + directoryId;
        SDIFileEntry* fileEntry = mFiles + dirEntry->mFirstFileIndex;

        for (int i = 0; i < dirEntry->mNrFiles; i++) {
            if (isSameName(arcName, fileEntry->mNameOffset, fileEntry->mHash)) {
                if ((fileEntry->mFlag) & 2) {
                    return findFsResource(name, fileEntry->mDataOffset);
                }

                if (name == NULL) {
                    return fileEntry;
                }

                return NULL;
            }
            fileEntry++;
        }
    }

    return NULL;
}

JKRArchive::SDIFileEntry* JKRArchive::findIdxResource(u32 index) const {
    if (index < mInfoBlock->mNrFiles) {
        return &mFiles[index];
    }

    return nullptr;
}

JKRArchive::SDIFileEntry* JKRArchive::findNameResource(const char* pName) const {
    SDIFileEntry* current = mFiles;

    CArcName name;
    name.store(pName);
    for (s32 i = 0; i < mInfoBlock->mNrFiles; i++) {
        if (isSameName(name, current->mNameOffset, current->mHash)) {
            return current;
        }

        current++;
    }

    return nullptr;
}

JKRArchive::SDIFileEntry* JKRArchive::findPtrResource(const void* pResource) const {
    SDIFileEntry* current = mFiles;
    for (s32 i = 0; i < mInfoBlock->mNrFiles; i++) {
        if (fileData(current) == pResource) {
            return current;
        }

        current++;
    }

    return nullptr;
}

JKRArchive::SDIFileEntry* JKRArchive::findIdResource(u16 fileID) const {
    if (fileID != 0xFFFF) {
        SDIFileEntry* current = mFiles;
        SDIFileEntry* indexed = &mFiles[fileID];

        if (indexed->mFileID == fileID && (indexed->mFlag & FILE_FLAG_FILE) != 0) {
            return indexed;
        }

        for (s32 i = 0; i < mInfoBlock->mNrFiles; i++) {
            if (current->mFileID == fileID && (current->mFlag & FILE_FLAG_FILE) != 0) {
                return current;
            }

            current++;
        }
    }

    return nullptr;
}

void JKRArchive::CArcName::store(const char* name) {
    mHash = 0;
    s32 length = 0;
    while (*name) {
        s32 ch = tolower(static_cast< s8 >(*name));
        mHash = ch + mHash * 3;
        if (length < ARRAY_SIZE(mName)) {
            mName[length++] = ch;
        }
        name++;
    }

    mLength = static_cast< u16 >(length);
    mName[length] = 0;
}

const char* JKRArchive::CArcName::store(const char* name, char endChar) {
    mHash = 0;
    s32 length = 0;
    while (*name && *name != endChar) {
        s32 lch = tolower(static_cast< s8 >(*name));
        mHash = lch + mHash * 3;
        if (length < ARRAY_SIZE(mName)) {
            mName[length++] = lch;
        }
        name++;
    }

    mLength = static_cast< u16 >(length);
    mName[length] = 0;

    if (*name == 0) {
        return NULL;
    }
    return name + 1;
}

