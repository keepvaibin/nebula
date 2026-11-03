#include "Game/System/GameAssets.hpp"
#include "Game/System/NebulaAtomic.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/System/NebulaArena.hpp"

#include <cstdlib>
#include <cstring>

#include <nebula_game_api.h>
#include <nebula_threads.h>

extern "C" {
const NebulaHostServices* nebula_host(void);
__attribute__((noreturn)) void nebula_fatal(const char* format, ...);
}

namespace {
    bool toFile(const NebulaAssetEntry& rEntry, GameAssets::File* pFile) {
        pFile->mEntry = rEntry.index;
        pFile->mLength = rEntry.length;
        return true;
    }
}  // namespace

namespace GameAssets {
    s32 find(const char* pPath) {
        const NebulaHostServices* const host = nebula_host();
        NebulaAssetEntry entry;
        return host->asset_find(host->user, pPath, &entry) != 0 ? entry.index : -1;
    }

    bool open(const char* pPath, File* pFile) {
        const NebulaHostServices* const host = nebula_host();
        NebulaAssetEntry entry;
        return host->asset_find(host->user, pPath, &entry) != 0 && toFile(entry, pFile);
    }

    bool open(s32 entry, File* pFile) {
        const NebulaHostServices* const host = nebula_host();
        NebulaAssetEntry found;
        return host->asset_entry(host->user, entry, &found) != 0 && toFile(found, pFile);
    }

    bool folderEntry(const char* pPath, u32 index, FolderEntry* pEntry) {
        const NebulaHostServices* const host = nebula_host();
        NebulaAssetEntry folder;
        NebulaAssetEntry child;
        if (host->asset_find(host->user, pPath, &folder) == 0 || folder.folder == 0 ||
            host->asset_child(host->user, folder.index, index, &child) == 0) {
            return false;
        }

        pEntry->mEntry = child.index;
        pEntry->mIsFolder = child.folder != 0;
        pEntry->mName = child.name;
        return true;
    }

    const void* map(s32 entryNum) {
        const NebulaHostServices* const host = nebula_host();
        return host->asset_map(host->user, entryNum);
    }

    s32 read(const File* pFile, void* pDst, s32 size, s32 offset) {
        if (offset < 0 || size < 0 || static_cast< u64 >(offset) + static_cast< u64 >(size) > pFile->mLength) {
            nebula_fatal("read outside file %d (length %u): %d bytes at %d", pFile->mEntry, pFile->mLength, size, offset);
        }

        const NebulaHostServices* const host = nebula_host();
        return host->asset_read(host->user, pFile->mEntry, static_cast< u64 >(offset), static_cast< u32 >(size), pDst);
    }

    // A file loaded whole is a view of the store from cMappedFrom bytes up: nearly all its pages stay as they are (a model's few
    // fixed-up pointers aside), so they cost no memory of the game's. The heap it was asked for decides when it goes.
    namespace {
        const u32 cMappedFrom = 0x2000;

        struct Mapped {
            void* mData;
            u32 mSize;
            NebulaArena* mHeap;
        };

        Mapped* sMapped;
        u32 sMappedCount;
        u32 sMappedMax;
        GameMutex sMappedLock;  // all zeros: ready before anything can run

        void release(const Mapped& rMapped) {
            const NebulaHostServices* const host = nebula_host();
            const uintptr_t start = reinterpret_cast< uintptr_t >(rMapped.mData);
            host->shape_forget(host->user, start, start + rMapped.mSize);
            host->asset_unmap(host->user, rMapped.mData, rMapped.mSize);
        }

        void* loadMapped(s32 entry, u32 size, NebulaArena* pHeap) {
            const NebulaHostServices* const host = nebula_host();
            sMappedLock.lock();
            void* pData = host->asset_map_private(host->user, entry);
            if (pData != nullptr && sMappedCount == sMappedMax) {
                sMappedMax = sMappedMax != 0 ? sMappedMax * 2 : 512;
                sMapped = static_cast< Mapped* >(realloc(sMapped, sizeof(Mapped) * sMappedMax));
            }
            if (pData != nullptr) {
                Mapped& rMapped = sMapped[sMappedCount++];
                rMapped.mData = pData;
                rMapped.mSize = size;
                rMapped.mHeap = pHeap != nullptr ? pHeap : NebulaArena::getCurrentHeap();
            }
            sMappedLock.unlock();
            return pData;
        }
    }  // namespace

    void unloadHeap(const NebulaArena* pHeap) {
        sMappedLock.lock();
        for (u32 i = 0; i < sMappedCount;) {
            if (sMapped[i].mHeap == pHeap) {
                release(sMapped[i]);
                sMapped[i] = sMapped[--sMappedCount];
            } else {
                i++;
            }
        }
        sMappedLock.unlock();
    }

    void* load(s32 entry, u8* pDst, u32 maxLength, NebulaArena* pHeap, AllocDirection direction, u32* pSize) {
        File file;
        if (!open(entry, &file)) {
            return nullptr;
        }

        if (pDst == nullptr && maxLength == 0 && file.mLength >= cMappedFrom) {
            void* const pMapped = loadMapped(entry, file.mLength, pHeap);
            if (pMapped != nullptr) {
                if (pSize != nullptr) {
                    *pSize = file.mLength;
                }
                return pMapped;
            }
        }

        const u32 size = maxLength != 0 && maxLength < file.mLength ? maxLength : file.mLength;
        const u32 blockSize = size != 0 ? (size + 31) & ~31u : 32;
        const bool allocated = pDst == nullptr;
        if (allocated) {
            pDst = static_cast< u8* >(NebulaArena::alloc(blockSize, direction == ALLOC_DIRECTION_FORWARD ? 64 : -64, pHeap));
            if (pDst == nullptr) {
                return nullptr;
            }
            memset(pDst + size, 0, blockSize - size);
        }

        if (read(&file, pDst, static_cast< s32 >(size), 0) < 0) {
            nebula_fatal("file %d cannot be read: the game's content is damaged", entry);
        }

        if (pSize != nullptr) {
            *pSize = size;
        }

        return pDst;
    }

    void* load(const char* pPath, u8* pDst, u32 maxLength, NebulaArena* pHeap, AllocDirection direction, u32* pSize) {
        const s32 entry = find(pPath);
        return entry >= 0 ? load(entry, pDst, maxLength, pHeap, direction, pSize) : nullptr;
    }
}  // namespace GameAssets
