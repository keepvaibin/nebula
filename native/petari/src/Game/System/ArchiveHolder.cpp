#include "Game/System/ArchiveHolder.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/Util.hpp"
#include <cstring>

ArchiveHolderArchiveEntry::ArchiveHolderArchiveEntry(void* pData, JKRHeap* pHeap, const char* pArchiveName)
    : mHeap(pHeap), mArchive(nullptr), mArchiveName(nullptr) {
    JKRMemArchive* archive = new (pHeap, 0) JKRMemArchive();
    archive->mountFixed(pData, JKR_MEM_BREAK_FLAG_0);
    mArchive = archive;

    s32 len = strlen(pArchiveName) + 1;
    mArchiveName = new (pHeap, 0) char[len];
    MR::copyString(mArchiveName, pArchiveName, len);
}

ArchiveHolderArchiveEntry::~ArchiveHolderArchiveEntry() {
    mArchive->unmount();
    delete mArchiveName;
}

ArchiveHolder::ArchiveHolder() {
    mEntries.init(0x180);
    mMutex.init();
}

ArchiveHolderArchiveEntry* ArchiveHolder::createAndAdd(void* pData, JKRHeap* pHeap, const char* pArchiveName) {
    ArchiveHolderArchiveEntry* entry = new (pHeap, 0) ArchiveHolderArchiveEntry(pData, pHeap, pArchiveName);
    GameMutex* mutex = &mMutex;
    mutex->lock();

    mEntries.push_back(entry);

    mutex->unlock();
    return entry;
}

JKRMemArchive* ArchiveHolder::getArchive(const char* pArchiveName) const {
    mMutex.lock();
    ArchiveHolderArchiveEntry* entry = findEntry(pArchiveName);
    mMutex.unlock();
    return (entry != nullptr) ? entry->mArchive : nullptr;
}

void ArchiveHolder::getArchiveAndHeap(const char* pArchiveName, JKRArchive** pArchive, JKRHeap** pHeap) const {
    mMutex.lock();
    ArchiveHolderArchiveEntry* entry = findEntry(pArchiveName);
    mMutex.unlock();

    if (entry != nullptr) {
        *pArchive = entry->mArchive;
        *pHeap = entry->mHeap;
    }
}

void ArchiveHolder::removeIfIsEqualHeap(JKRHeap* pHeap) {
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
