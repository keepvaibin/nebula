#include "Game/System/ArchiveHolder.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/Util.hpp"
#include <cstring>

ArchiveHolderArchiveEntry::ArchiveHolderArchiveEntry(AssetFolder* pFolder, NebulaArena* pHeap, const char* pArchiveName)
    : mHeap(pHeap), mArchive(pFolder), mArchiveName(nullptr) {
    s32 len = strlen(pArchiveName) + 1;
    mArchiveName = new (pHeap, 0) char[len];
    MR::copyString(mArchiveName, pArchiveName, len);
}

ArchiveHolderArchiveEntry::~ArchiveHolderArchiveEntry() {
    delete mArchive;
    delete mArchiveName;
}

ArchiveHolder::ArchiveHolder() {
    mEntries.init(0x180);
    mMutex.init();
}

ArchiveHolderArchiveEntry* ArchiveHolder::createAndAdd(AssetFolder* pFolder, NebulaArena* pHeap, const char* pArchiveName) {
    ArchiveHolderArchiveEntry* entry = new (pHeap, 0) ArchiveHolderArchiveEntry(pFolder, pHeap, pArchiveName);
    GameMutex* mutex = &mMutex;
    mutex->lock();

    mEntries.push_back(entry);

    mutex->unlock();
    return entry;
}

AssetFolder* ArchiveHolder::getArchive(const char* pArchiveName) const {
    mMutex.lock();
    ArchiveHolderArchiveEntry* entry = findEntry(pArchiveName);
    mMutex.unlock();
    return (entry != nullptr) ? entry->mArchive : nullptr;
}

void ArchiveHolder::getArchiveAndHeap(const char* pArchiveName, AssetFolder** pArchive, NebulaArena** pHeap) const {
    mMutex.lock();
    ArchiveHolderArchiveEntry* entry = findEntry(pArchiveName);
    mMutex.unlock();

    if (entry != nullptr) {
        *pArchive = entry->mArchive;
        *pHeap = entry->mHeap;
    }
}

void ArchiveHolder::removeIfIsEqualHeap(NebulaArena* pHeap) {
    if (pHeap == nullptr) {
        return;
    }

    mMutex.lock();
    for (ArchiveHolderArchiveEntry** i = mEntries.begin(); i != mEntries.end();) {
        if ((*i)->mHeap == pHeap) {
            delete *i;
            mEntries.erase(i);
        } else {
            i++;
        }
    }
    mMutex.unlock();
}

ArchiveHolderArchiveEntry* ArchiveHolder::findEntry(const char* pArchiveName) const {
    for (ArchiveHolderArchiveEntry* const* i = mEntries.begin(); i != mEntries.end(); i++) {
        if (MR::isEqualStringCase((*i)->mArchiveName, pArchiveName)) {
            return *i;
        }
    }

    return nullptr;
}
