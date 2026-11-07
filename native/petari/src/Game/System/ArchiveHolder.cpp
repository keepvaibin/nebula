#include "Game/System/ArchiveHolder.hpp"
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
}

ArchiveHolderArchiveEntry* ArchiveHolder::createAndAdd(AssetFolder* pFolder, NebulaArena* pHeap, const char* pArchiveName) {
    ArchiveHolderArchiveEntry* entry = new (pHeap, 0) ArchiveHolderArchiveEntry(pFolder, pHeap, pArchiveName);
    mEntries.push_back(entry);
    return entry;
}

AssetFolder* ArchiveHolder::getArchive(const char* pArchiveName) const {
    ArchiveHolderArchiveEntry* entry = findEntry(pArchiveName);
    return (entry != nullptr) ? entry->mArchive : nullptr;
}

void ArchiveHolder::getArchiveAndHeap(const char* pArchiveName, AssetFolder** pArchive, NebulaArena** pHeap) const {
    ArchiveHolderArchiveEntry* entry = findEntry(pArchiveName);

    if (entry != nullptr) {
        *pArchive = entry->mArchive;
        *pHeap = entry->mHeap;
    }
}

void ArchiveHolder::removeIfIsEqualHeap(NebulaArena* pHeap) {
    if (pHeap == nullptr) {
        return;
    }

    for (ArchiveHolderArchiveEntry** i = mEntries.begin(); i != mEntries.end();) {
        if ((*i)->mHeap == pHeap) {
            delete *i;
            mEntries.erase(i);
        } else {
            i++;
        }
    }
}

ArchiveHolderArchiveEntry* ArchiveHolder::findEntry(const char* pArchiveName) const {
    for (ArchiveHolderArchiveEntry* const* i = mEntries.begin(); i != mEntries.end(); i++) {
        if (MR::isEqualStringCase((*i)->mArchiveName, pArchiveName)) {
            return *i;
        }
    }

    return nullptr;
}
