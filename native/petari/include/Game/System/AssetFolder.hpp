#pragma once

// A folder of the asset store as one block of memory. What the disc holds as an archive the cooker lays out as a folder whose
// files lie side by side, so its bytes come as a single view and each file is a place in it. The names and the order come from
// the store's index; letters match in either case.

#include <nebula_types.h>
#include <stddef.h>

class NebulaArena;
struct NebulaAssetEntry;

class AssetFolder {
public:
    struct Item {
        s32 mEntry;
        bool mIsFolder;
        const char* mName;
        void* mData;
        u32 mSize;
    };

    // The folder at pPath of the store, its bytes loaded for pHeap: fatal when the store has none.
    static AssetFolder* mount(const char* pPath, NebulaArena* pHeap);

    AssetFolder(s32 entry, void* pBytes);

    // The file at "/a/b" below this folder, or called that anywhere below it when there is no first '/'; null when none.
    void* getResource(const char* pName) const;
    // This folder's index-th entry, a file's bytes; null past the end.
    void* getResource(u32 index) const;
    u32 getResSize(const void* pData) const;
    // The files below this folder, at any depth.
    u32 countResource() const;
    // The folder at "/a/b" below this one: its entries, none when there is no such folder.
    u32 countFile(const char* pDirectory) const;
    s32 findFolder(const char* pDirectory) const;
    // The index-th entry of a folder (getEntry(), or an item's mEntry): false past the end.
    bool getItem(s32 folder, u32 index, Item* pItem) const;
    // The folder at "/a/b" below this one as an AssetFolder of its own, in the same block: fatal when there is none.
    AssetFolder* getSubFolder(const char* pPath, NebulaArena* pHeap) const;

    s32 getEntry() const {
        return mEntry;
    }

    // Its name in the store ("Kuribo.arc").
    const char* getName() const;

private:
    s32 findPath(const char* pPath) const;
    s32 findName(s32 folder, const char* pName) const;
    u32 findSize(s32 folder, const void* pData) const;
    u32 countFiles(s32 folder) const;
    void* dataOf(const NebulaAssetEntry& rEntry) const;

    s32 mEntry;
    u8* mBytes;
    u64 mPosition;
};
