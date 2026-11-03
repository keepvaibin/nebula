#pragma once

#include "Game/Util/Array.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/System/AssetFolder.hpp"

class ArchiveHolderArchiveEntry {
public:
    /// @brief Creates a new `ArchiveHolderArchiveEntry`.
    ArchiveHolderArchiveEntry(AssetFolder*, NebulaArena*, const char*);

    /// @brief Destroys the `ArchiveHolderArchiveEntry`.
    ~ArchiveHolderArchiveEntry();

    AssetFolder* mArchive;
    NebulaArena* mHeap;
    char* mArchiveName;
};

class ArchiveHolder {
public:
    /// @brief Creates a new `ArchiveHolder`.
    ArchiveHolder();

    /// @brief Creates and adds a new archive entry.
    /// @param pFolder The archive.
    /// @param pHeap The heap to allocate the archive in.
    /// @param pArchiveName The name of the archive.
    /// @return The pointer to the created entry.
    ArchiveHolderArchiveEntry* createAndAdd(AssetFolder*, NebulaArena*, const char*);

    /// @brief Gets the archive for the given archive name.
    /// @param pArchiveName The name of the archive.
    /// @return The pointer to the found archive, or `nullptr` if not found.
    AssetFolder* getArchive(const char*) const;

    /// @brief Gets the archive and heap for the given archive name.
    /// @param pArchiveName The name of the archive.
    /// @param pArchive The output pointer for the archive.
    /// @param pHeap The output pointer for the heap.
    void getArchiveAndHeap(const char*, AssetFolder**, NebulaArena**) const;

    /// @brief Removes entries that match the given heap.
    /// @param pHeap The heap to compare.
    void removeIfIsEqualHeap(NebulaArena*);

    /// @brief Finds the entry with the given archive name.
    /// @param pArchiveName The name of the archive.
    /// @return The pointer to the found entry, or `nullptr` if not found.
    ArchiveHolderArchiveEntry* findEntry(const char*) const;

    MR::Vector< MR::AssignableArray< ArchiveHolderArchiveEntry* > > mEntries;
    mutable GameMutex mMutex;
};
