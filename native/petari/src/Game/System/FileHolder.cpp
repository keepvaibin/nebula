#include "Game/System/FileHolder.hpp"
#include "Game/Util.hpp"
#include "Game/System/GameAssets.hpp"

FileHolderFileEntry::FileHolderFileEntry(const char* pFilePath, NebulaArena* pHeap, void* pContext)
    : mEntryNum(GameAssets::find(pFilePath)), mContext(nullptr), mHeap(pHeap), mContextSet(true) {
    if (pContext) {
        mContextSet = false;
        mContext = pContext;
    }
}

FileHolderFileEntry::~FileHolderFileEntry() {
    if (mContextSet && (mContext != nullptr)) {
        delete (u8*)mContext;
    }
}

FileHolder::FileHolder() {
    mEntries.init(0x180);
}

FileHolderFileEntry* FileHolder::add(const char* pFilePath, NebulaArena* pHeap, void* pContext) {
    FileHolderFileEntry* pEntry = new (pHeap, 0) FileHolderFileEntry(pFilePath, pHeap, pContext);
    mEntries.push_back(pEntry);
    return pEntry;
}

bool FileHolder::isExist(const char* pFilePath) const {
    return findEntry(pFilePath);
}

void* FileHolder::getContext(const char* pFilePath) const {
    return findEntry(pFilePath)->mContext;
}

void FileHolder::removeIfIsEqualHeap(NebulaArena* pHeap) {
    if (pHeap == nullptr) {
        return;
    }

    for (FileHolderFileEntry** i = mEntries.begin(); i != mEntries.end();) {
        if ((*i)->mHeap == pHeap) {
            delete *i;
            mEntries.erase(i);
        } else {
            i++;
        }
    }
}

FileHolderFileEntry** FileHolder::removeFile(const char* pFilePath) {
    FileHolderFileEntry** p;
    FileHolderFileEntry* pEntry = findEntry(pFilePath);

    for (p = mEntries.begin(); p != mEntries.end() && *p != pEntry; p++)
        ;
    delete *p;

    return mEntries.erase(p);
}

FileHolderFileEntry* FileHolder::findEntry(const char* pFilePath) const {
    s32 entryNum = GameAssets::find(pFilePath);
    for (FileHolderFileEntry* const* pEntry = mEntries.begin(); pEntry != mEntries.end(); pEntry++) {
        if (entryNum == (*pEntry)->mEntryNum) {
            return *pEntry;
        }
    }

    return nullptr;
}
