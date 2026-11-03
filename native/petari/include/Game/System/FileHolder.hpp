#pragma once

#include "Game/Util/Array.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/System/GameThreads.hpp"

class NebulaArena;

class FileHolderFileEntry {
public:
    /// @brief Creates a new `FileHolderFileEntry`.
    /// @param pFilePath The path to the file.
    /// @param pHeap The heap used for allocation.
    /// @param pContext The context data for the file.
    FileHolderFileEntry(const char*, NebulaArena*, void*);

    /// @brief Destroys the `FileHolderFileEntry`.
    ~FileHolderFileEntry();

    /// @brief Waits until the file read is done.
    void waitReadDone();

    /// @brief Sets the context data for the file.
    /// @param pContext The context data.
    void setContext(void*);

    s32 mEntryNum;
    void* mContext;
    NebulaArena* mHeap;
    bool mDone;
    GameMessageQueue mQueue;
    void* mMessage;
    bool mContextSet;
};

class FileHolder {
public:
    /// @brief Creates a new `FileHolder`.
    FileHolder();

    /// @brief Adds a new file entry.
    /// @param pFilePath The path to the file.
    /// @param pHeap The heap used for allocation.
    /// @param pContext The context data for the file.
    FileHolderFileEntry* add(const char*, NebulaArena*, void*);

    /// @brief Checks if a file entry exists.
    /// @param pFilePath The path to the file.
    /// @return `true` if the file entry exists, `false` otherwise.
    bool isExist(const char*) const;

    /// @brief Gets the context data for the given file.
    /// @param pFilePath The path to the file.
    void* getContext(const char*) const;

    /// @brief Removes file entries that match the given heap.
    /// @param pHeap The heap to compare.
    void removeIfIsEqualHeap(NebulaArena*);

    /// @brief Removes the file entry for the given file.
    /// @param pFilePath The path to the file.
    /// @return The pointer to the removed entry.
    FileHolderFileEntry** removeFile(const char*);

    /// @brief Finds the file entry for the given file.
    /// @param pFilePath The path to the file.
    /// @return The pointer to the found entry, or `nullptr` if not found.
    FileHolderFileEntry* findEntry(const char*) const;

    MR::Vector< MR::AssignableArray< FileHolderFileEntry* > > mEntries;
};
