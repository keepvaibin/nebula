#include "Game/System/AssetFolder.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/NebulaArena.hpp"

#include <cstring>

#include <nebula_game_api.h>

extern "C" {
const NebulaHostServices* nebula_host(void);
__attribute__((noreturn)) void nebula_fatal(const char* format, ...);
}

namespace {
    char lower(char c) {
        return c >= 'A' && c <= 'Z' ? static_cast< char >(c - 'A' + 'a') : c;
    }

    bool isSameName(const char* pName, const char* pPart, size_t length) {
        for (size_t i = 0; i < length; i++) {
            if (lower(pName[i]) != lower(pPart[i])) {
                return false;
            }
        }

        return pName[length] == '\0';
    }

    bool child(s32 folder, u32 index, NebulaAssetEntry* pEntry) {
        const NebulaHostServices* const host = nebula_host();
        return host->asset_child(host->user, folder, index, pEntry) != 0;
    }

    bool entryOf(s32 index, NebulaAssetEntry* pEntry) {
        const NebulaHostServices* const host = nebula_host();
        return host->asset_entry(host->user, index, pEntry) != 0;
    }
}  // namespace

AssetFolder* AssetFolder::mount(const char* pPath, NebulaArena* pHeap) {
    const s32 entry = GameAssets::find(pPath);
    if (entry < 0) {
        nebula_fatal("%s is missing: the game content was not cooked by this version", pPath);
    }

    void* const pBytes = GameAssets::load(entry, nullptr, 0, pHeap, GameAssets::ALLOC_DIRECTION_FORWARD, nullptr);
    return new (pHeap, 0) AssetFolder(entry, pBytes);
}

AssetFolder::AssetFolder(s32 entry, void* pBytes) : mEntry(entry), mBytes(static_cast< u8* >(pBytes)), mPosition(0) {
    NebulaAssetEntry self;
    if (entryOf(entry, &self)) {
        mPosition = self.position;
    }
}

void* AssetFolder::dataOf(const NebulaAssetEntry& rEntry) const {
    return rEntry.length != 0 ? mBytes + (rEntry.position - mPosition) : mBytes;
}

s32 AssetFolder::findPath(const char* pPath) const {
    s32 folder = mEntry;
    while (*pPath != '\0') {
        const char* const pSlash = strchr(pPath, '/');
        const size_t length = pSlash != nullptr ? static_cast< size_t >(pSlash - pPath) : strlen(pPath);
        if (length != 0 && !(length == 1 && pPath[0] == '.')) {
            NebulaAssetEntry entry;
            u32 i = 0;
            while (child(folder, i, &entry) && !isSameName(entry.name, pPath, length)) {
                i++;
            }
            if (!child(folder, i, &entry)) {
                return -1;
            }
            folder = entry.index;
        }
        pPath += length + (pSlash != nullptr ? 1 : 0);
    }

    return folder;
}

s32 AssetFolder::findName(s32 folder, const char* pName) const {
    const size_t length = strlen(pName);
    NebulaAssetEntry entry;
    for (u32 i = 0; child(folder, i, &entry); i++) {
        if (entry.folder == 0 && isSameName(entry.name, pName, length)) {
            return entry.index;
        }
    }

    for (u32 i = 0; child(folder, i, &entry); i++) {
        if (entry.folder != 0) {
            const s32 found = findName(entry.index, pName);
            if (found >= 0) {
                return found;
            }
        }
    }

    return -1;
}

void* AssetFolder::getResource(const char* pName) const {
    const s32 found = pName[0] == '/' ? findPath(pName) : findName(mEntry, pName);
    NebulaAssetEntry entry;
    return found >= 0 && entryOf(found, &entry) && entry.folder == 0 ? dataOf(entry) : nullptr;
}

void* AssetFolder::getResource(u32 index) const {
    Item item;
    return getItem(mEntry, index, &item) && !item.mIsFolder ? item.mData : nullptr;
}

u32 AssetFolder::findSize(s32 folder, const void* pData) const {
    NebulaAssetEntry entry;
    for (u32 i = 0; child(folder, i, &entry); i++) {
        if (entry.folder != 0) {
            const u32 size = findSize(entry.index, pData);
            if (size != 0) {
                return size;
            }
        } else if (dataOf(entry) == pData) {
            return entry.length;
        }
    }

    return 0;
}

u32 AssetFolder::getResSize(const void* pData) const {
    return findSize(mEntry, pData);
}

u32 AssetFolder::countFiles(s32 folder) const {
    u32 count = 0;
    NebulaAssetEntry entry;
    for (u32 i = 0; child(folder, i, &entry); i++) {
        count += entry.folder != 0 ? countFiles(entry.index) : 1;
    }

    return count;
}

u32 AssetFolder::countResource() const {
    return countFiles(mEntry);
}

s32 AssetFolder::findFolder(const char* pDirectory) const {
    const s32 found = findPath(pDirectory);
    NebulaAssetEntry entry;
    return found >= 0 && entryOf(found, &entry) && entry.folder != 0 ? found : -1;
}

u32 AssetFolder::countFile(const char* pDirectory) const {
    const s32 folder = findFolder(pDirectory);
    NebulaAssetEntry entry;
    return folder >= 0 && entryOf(folder, &entry) ? entry.children : 0;
}

bool AssetFolder::getItem(s32 folder, u32 index, Item* pItem) const {
    NebulaAssetEntry entry;
    if (!child(folder, index, &entry)) {
        return false;
    }

    pItem->mEntry = entry.index;
    pItem->mIsFolder = entry.folder != 0;
    pItem->mName = entry.name;
    pItem->mData = dataOf(entry);
    pItem->mSize = entry.length;
    return true;
}

const char* AssetFolder::getName() const {
    NebulaAssetEntry self;
    return entryOf(mEntry, &self) ? self.name : "";
}

AssetFolder* AssetFolder::getSubFolder(const char* pPath, NebulaArena* pHeap) const {
    const s32 folder = findFolder(pPath);
    NebulaAssetEntry entry;
    if (folder < 0 || !entryOf(folder, &entry)) {
        nebula_fatal("%s is missing: the game content was not cooked by this version", pPath);
    }

    return new (pHeap, 0) AssetFolder(folder, dataOf(entry));
}
