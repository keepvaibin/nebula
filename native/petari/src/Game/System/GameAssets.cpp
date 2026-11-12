#include "Game/Render/RenderTexture.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/NebulaArena.hpp"

#include <cstdlib>
#include <cstring>

#include <nebula_game_api.h>

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

    s32 findGame(const char* pPath) {
        const NebulaHostServices* const host = nebula_host();
        NebulaAssetEntry entry;
        return host->asset_find_game(host->user, pPath, &entry) != 0 ? entry.index : -1;
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
        if (host->asset_find(host->user, pPath, &folder) == 0 || folder.folder == 0) {
            return false;
        }
        // The folder's own, then what mods add to it.
        if (host->asset_child(host->user, folder.index, index, &child) == 0 &&
            (index < folder.children || host->asset_added_child(host->user, pPath, index - folder.children, &child) == 0)) {
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

        // An image read over is a new image.
        const uintptr_t at = reinterpret_cast< uintptr_t >(pDst);
        RenderTexture::forget(at, at + static_cast< uintptr_t >(size));
        const NebulaHostServices* const host = nebula_host();
        return host->asset_read(host->user, pFile->mEntry, static_cast< u64 >(offset), static_cast< u32 >(size), pDst);
    }

    // A file loaded whole is a view of the store from cMappedFrom bytes up: nearly all its pages stay as they are (a model's few
    // fixed-up pointers aside), so they cost no memory of the game's. The arena it was asked for decides when it goes.
    namespace {
        const u32 cMappedFrom = 0x2000;

        struct Mapped {
            void* mData;
            u32 mSize;
            NebulaArena* mArena;
        };

        Mapped* sMapped;
        u32 sMappedCount;
        u32 sMappedMax;
        u32 sLastView;  // viewArena's last hit

        void release(const Mapped& rMapped) {
            const NebulaHostServices* const host = nebula_host();
            const uintptr_t start = reinterpret_cast< uintptr_t >(rMapped.mData);
            RenderTexture::forget(start, start + rMapped.mSize);
            host->asset_unmap(host->user, rMapped.mData, rMapped.mSize);
        }

        void* loadMapped(s32 entry, u32 size, NebulaArena* pArena) {
            const NebulaHostServices* const host = nebula_host();
            void* pData = host->asset_map_private(host->user, entry);
            if (pData != nullptr && sMappedCount == sMappedMax) {
                sMappedMax = sMappedMax != 0 ? sMappedMax * 2 : 512;
                sMapped = static_cast< Mapped* >(realloc(sMapped, sizeof(Mapped) * sMappedMax));
            }
            if (pData != nullptr) {
                Mapped& rMapped = sMapped[sMappedCount++];
                rMapped.mData = pData;
                rMapped.mSize = size;
                rMapped.mArena = pArena != nullptr ? pArena : NebulaArena::current();
            }
            return pData;
        }
    }  // namespace

    namespace {
        struct Held {
            u32 mHash;
            const char* mPath;
            AssetFolder* mFolder;
            NebulaArena* mArena;
            bool mGame;  // the game's own copy under a mod's
        };

        Held* sHeld;
        u32 sHeldCount;
        u32 sHeldMax;

        char lower(char c) {
            return c >= 'A' && c <= 'Z' ? static_cast< char >(c - 'A' + 'a') : c;
        }

        u32 hashPath(const char* pPath) {
            u32 hash = 2166136261u;
            for (; *pPath != '\0'; pPath++) {
                hash = (hash ^ static_cast< u8 >(lower(*pPath))) * 16777619u;
            }
            return hash;
        }

        bool isSamePath(const char* pA, const char* pB) {
            for (; *pA != '\0' && lower(*pA) == lower(*pB); pA++, pB++) {
            }
            return *pA == *pB;
        }
    }  // namespace

    NebulaArena* viewArena(const void* p) {
        // A model's images lie in one view: the last view found is tried first.
        const uintptr_t at = reinterpret_cast< uintptr_t >(p);
        const auto holds = [at](const Mapped& rMapped) {
            const uintptr_t start = reinterpret_cast< uintptr_t >(rMapped.mData);
            return at >= start && at - start < rMapped.mSize;
        };
        if (sLastView < sMappedCount && holds(sMapped[sLastView])) {
            return sMapped[sLastView].mArena;
        }
        for (u32 i = 0; i < sMappedCount; i++) {
            if (holds(sMapped[i])) {
                sLastView = i;
                return sMapped[i].mArena;
            }
        }
        return nullptr;
    }

    void unloadArena(const NebulaArena* pArena) {
        for (u32 i = 0; i < sMappedCount;) {
            if (sMapped[i].mArena == pArena) {
                release(sMapped[i]);
                sMapped[i] = sMapped[--sMappedCount];
            } else {
                i++;
            }
        }
        for (u32 i = 0; i < sHeldCount;) {
            if (sHeld[i].mArena == pArena) {
                sHeld[i] = sHeld[--sHeldCount];
            } else {
                i++;
            }
        }
    }

    AssetFolder* findArchive(const char* pPath, NebulaArena** ppArena, bool game) {
        const u32 hash = hashPath(pPath);
        for (u32 i = 0; i < sHeldCount; i++) {
            if (sHeld[i].mHash == hash && sHeld[i].mGame == game && isSamePath(sHeld[i].mPath, pPath)) {
                if (ppArena != nullptr) {
                    *ppArena = sHeld[i].mArena;
                }
                return sHeld[i].mFolder;
            }
        }
        return nullptr;
    }

    void holdArchive(const char* pPath, AssetFolder* pFolder, NebulaArena* pArena, bool game) {
        if (sHeldCount == sHeldMax) {
            sHeldMax = sHeldMax != 0 ? sHeldMax * 2 : 256;
            sHeld = static_cast< Held* >(realloc(sHeld, sizeof(Held) * sHeldMax));
        }
        const u32 length = static_cast< u32 >(strlen(pPath));
        char* const pCopy = static_cast< char* >(NebulaArena::alloc(length + 1, 4, pArena));
        memcpy(pCopy, pPath, length + 1);
        sHeld[sHeldCount++] = {hashPath(pPath), pCopy, pFolder, pArena, game};
    }

    void* load(s32 entry, u8* pDst, u32 maxLength, NebulaArena* pArena, u32* pSize) {
        File file;
        if (!open(entry, &file)) {
            return nullptr;
        }

        if (pDst == nullptr && maxLength == 0 && file.mLength >= cMappedFrom) {
            void* const pMapped = loadMapped(entry, file.mLength, pArena);
            if (pMapped != nullptr) {
                if (pSize != nullptr) {
                    *pSize = file.mLength;
                }
                return pMapped;
            }
        }

        const u32 size = maxLength != 0 && maxLength < file.mLength ? maxLength : file.mLength;
        const bool allocated = pDst == nullptr;
        if (allocated) {
            pDst = static_cast< u8* >(NebulaArena::alloc(size + 1, 16, pArena));
            if (pDst == nullptr) {
                return nullptr;
            }
            pDst[size] = 0;
        }

        if (read(&file, pDst, static_cast< s32 >(size), 0) < 0) {
            nebula_fatal("file %d cannot be read: the game's content is damaged", entry);
        }

        if (pSize != nullptr) {
            *pSize = size;
        }

        return pDst;
    }

    void* load(const char* pPath, u8* pDst, u32 maxLength, NebulaArena* pArena, u32* pSize) {
        const s32 entry = find(pPath);
        return entry >= 0 ? load(entry, pDst, maxLength, pArena, pSize) : nullptr;
    }
}  // namespace GameAssets
