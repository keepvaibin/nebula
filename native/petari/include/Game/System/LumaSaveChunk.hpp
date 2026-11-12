#pragma once

// Mods' values in a user file's game data (contract services-save, items 2 to 5): one chunk, signature 'LUMA',
// registered last in GameDataHolder's chunk holder, so a value is written when the game saves, copied with the file
// and cleared when the file is erased or started new.
//
// The chunk's data, version 1, big-endian as the file's other chunks: u16 version, u16 mods, then per mod u8 id
// length, the id, u32 bytes of its entries, then the entries: u8 key length, the key, u32 value length, the value.
// Mods in the order their first value was set, keys in the order first set. In memory the chunk keeps exactly the
// mods' part of that form (from the first mod's id length on), so a save copies it as it is.

#include "Game/System/BinaryDataChunkHolder.hpp"

class GameDataHolder;

class LumaSaveChunk : public BinaryDataChunkBase {
public:
    // Limits (item 4): a value, one mod's entries in one file, the chunk's data (its 12-byte header not counted).
    static const u32 cValueMost = 65536;
    static const u32 cModMost = 262144;
    static const u32 cChunkMost = 1048576 - 12;
    static const u32 cKeyMost = 64;
    static const u32 cIdMost = 64;

    // What set and the kept-data calls answer.
    enum Result {
        Result_Ok,
        Result_NotFound,
        Result_Limit,      // a value or the mod's entries past their limit: nothing changed
        Result_ChunkFull,  // all mods' entries in the file past the chunk's limit: nothing changed
        Result_Memory,
    };

    // A file's chunk (its holder names the file in messages), or with a null holder one mod's values across files
    // (services-save item 7: save.global's store, its own limits, never a chunk of a holder).
    explicit LumaSaveChunk(const GameDataHolder* pHolder, u32 modMost = cModMost, u32 chunkMost = cChunkMost);
    virtual ~LumaSaveChunk();

    virtual u32 makeHeaderHashCode() const;
    virtual u32 getSignature() const;
    // Never past `size`: when the data does not fit, 0 and one Error Save.ChunkTooBig (the file saved without mods'
    // data, never corrupted).
    virtual s32 serialize(u8* pData, u32 size) const;
    // A chunk that does not parse whole is dropped and one Error Save.ChunkBroken said; 0 either way, so the file's
    // game data loads as it is (item 3).
    virtual s32 deserialize(const u8* pData, u32 size);
    virtual void initializeData();

    // The data form serialize writes (u16 version, u16 mods, the mods' blocks) read whole, saying nothing:
    // Result_Ok, Result_NotFound when it does not parse (nothing kept), Result_Memory (nothing kept).
    Result parse(const u8* pData, u32 size);

    // The data serialize writes, header excluded: the make room the save handler gives on top of the fixed part.
    u32 getDataSize() const { return mMods != 0 ? 4 + mSize : 4; }

    // `pModId` is a mod's id, `pKey` a key by the rule (the service checks both before it calls).
    // The value's length, or -1 when not set; copies min(length, room) bytes into pOut.
    s64 get(const char* pModId, const char* pKey, void* pOut, u32 room) const;
    Result set(const char* pModId, const char* pKey, const void* pValue, u32 length);
    Result remove(const char* pModId, const char* pKey);
    // A removed mod's kept data (services-mods 8, services-save 5): its entries' bytes here, 0 when none.
    u32 getModBytes(const char* pModId) const;
    // FORGET: the mod's entries go (written at the file's next save by the game).
    void forgetMod(const char* pModId);
    // takes_over (item 6): the old id's entries move to the new id, unless the new id has entries already.
    Result renameMod(const char* pOldId, const char* pNewId);

    static bool isValidKey(const char* pKey);

    // The LUMA chunk in a game-data file binary (BinaryDataChunkHolder::makeFileBinary's form): its offset and whole
    // size, header included. False when the file has none or its chunk list runs past its end.
    static bool findInFile(const u8* pFile, u32 size, u32* pAt, u32* pChunkSize);
    // Each mod's block in order: its id (not terminated) and length, and its entries' bytes.
    void forEachMod(void (*pFn)(void* pUser, const char* pId, u32 idLength, u32 bytes), void* pUser) const;

private:
    // The offset of the mod's block (its id length byte) in mData, or -1.
    s32 findMod(const char* pModId) const;
    // The offset of the entry (its key length byte) in the mod's block at modAt, or -1.
    s32 findEntry(u32 modAt, const char* pKey) const;
    u32 modBytes(u32 modAt) const;
    void setModBytes(u32 modAt, u32 bytes);
    // Replaces mData[at, at + removed) with `inserted` bytes of room (written by the caller); false when no memory.
    bool splice(u32 at, u32 removed, u32 inserted);
    void fileNumber(char (&rText)[4]) const;

    const GameDataHolder* mHolder;
    u32 mModMost;
    u32 mChunkMost;
    u8* mData;
    u32 mSize;
    u32 mRoom;
    u32 mMods;
};
