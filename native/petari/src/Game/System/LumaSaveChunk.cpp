#include "Game/System/LumaSaveChunk.hpp"

#include "Game/System/GameDataHolder.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/SaveBinary.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include <luma_modlog.h>
#include <nebula_format.h>

namespace {
    const u32 cSignature = 0x4C554D41;  // 'LUMA'
    const u16 cVersion = 1;
    const u32 cCodeBroken = 180;   // Save.ChunkBroken
    const u32 cCodeTooBig = 184;   // Save.ChunkTooBig

    // fnv1a32 of "LUMA" "1" (item 3): the header hash stays the same in every version (the version is in the data).
    u32 headerHash() {
        u32 hash = 2166136261u;
        for (const char* p = "LUMA1"; *p != '\0'; p++) {
            hash ^= static_cast< u8 >(*p);
            hash *= 16777619u;
        }
        return hash;
    }

    u32 textLength(const char* pText) {
        u32 length = 0;
        while (pText[length] != '\0') {
            length++;
        }
        return length;
    }

    bool sameBytes(const u8* pA, const char* pB, u32 length) {
        for (u32 i = 0; i < length; i++) {
            if (pA[i] != static_cast< u8 >(pB[i])) {
                return false;
            }
        }
        return true;
    }

    // A mod id's characters (luma.md, the validator's id_ok: a-z 0-9 - .), 1 to 64 of them. The full id rule is the
    // validator's; this only keeps a chunk from naming what no mod can be.
    bool validIdBytes(const u8* pId, u32 length) {
        if (length == 0 || length > LumaSaveChunk::cIdMost) {
            return false;
        }
        for (u32 i = 0; i < length; i++) {
            const u8 c = pId[i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) {
                return false;
            }
        }
        return true;
    }

    // A key's name (item 4): 1 to 64 of a-z 0-9 _ - .
    bool validNameBytes(const u8* pName, u32 length) {
        if (length == 0 || length > LumaSaveChunk::cKeyMost) {
            return false;
        }
        for (u32 i = 0; i < length; i++) {
            const u8 c = pName[i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
                return false;
            }
        }
        return true;
    }

    // A key (items 4 and 5): a name, or "<mod id>/<name>", a value kept for that mod (forgetting the mod removes it
    // from every mod's values).
    bool validKeyBytes(const u8* pKey, u32 length) {
        for (u32 i = 0; i < length; i++) {
            if (pKey[i] == '/') {
                return validIdBytes(pKey, i) && validNameBytes(pKey + i + 1, length - i - 1);
            }
        }
        return validNameBytes(pKey, length);
    }

    // The length of the mod id a key is kept for ("<mod id>/<name>"), 0 for a key of a name alone.
    u32 forIdLength(const u8* pKey, u32 length) {
        for (u32 i = 0; i < length; i++) {
            if (pKey[i] == '/') {
                return i;
            }
        }
        return 0;
    }

    // One Error in the mod log about a file's chunk (no mod's: the data of every mod in it).
    void sayFile(u32 code, const char* pRow, const char* pFile) {
        LumaModLogEntry entry = {};
        entry.size = sizeof(entry);
        entry.source = LUMA_MODLOG_CODE;
        entry.level = LUMA_MODLOG_ERROR;
        entry.code = code;
        entry.mod = "";
        entry.file = "";
        entry.names[0] = "file";
        entry.values[0] = pFile;
        entry.count = 1;
        luma::modlog::add(entry, pRow);
    }
}  // namespace

LumaSaveChunk::LumaSaveChunk(const GameDataHolder* pHolder, u32 modMost, u32 chunkMost)
    : mHolder(pHolder), mModMost(modMost), mChunkMost(chunkMost), mData(nullptr), mSize(0), mRoom(0), mMods(0),
      mNewer(false) {
}

LumaSaveChunk::~LumaSaveChunk() {
    delete[] mData;
}

u32 LumaSaveChunk::makeHeaderHashCode() const {
    return headerHash();
}

u32 LumaSaveChunk::getSignature() const {
    return cSignature;
}

s32 LumaSaveChunk::serialize(u8* pData, u32 size) const {
    const u32 needed = getDataSize();
    if (needed > size) {
        char file[4];
        fileNumber(file);
        sayFile(cCodeTooBig, "Save.ChunkTooBig", file);
        return 0;
    }
    if (mNewer) {
        MR::copyMemory(pData, mData, mSize);
        return static_cast< s32 >(mSize);
    }
    MR::storeSaveU16(pData, cVersion);
    MR::storeSaveU16(pData + 2, static_cast< u16 >(mMods));
    if (mSize != 0) {
        MR::copyMemory(pData + 4, mData, mSize);
    }
    return static_cast< s32 >(needed);
}

s32 LumaSaveChunk::deserialize(const u8* pData, u32 size) {
    initializeData();
    // A file saved when the data did not fit (serialize gave 0): no mods' data, nothing broken.
    if (size == 0) {
        return 0;
    }
    // Broken: said once. No memory for mods' data: the values are not there this time. Either way the file's game
    // data loads.
    if (parse(pData, size) == Result_NotFound) {
        char file[4];
        fileNumber(file);
        sayFile(cCodeBroken, "Save.ChunkBroken", file);
    }
    return 0;
}

LumaSaveChunk::Result LumaSaveChunk::parse(const u8* pData, u32 size) {
    initializeData();
    // A later version's data: kept whole, so this game's next save writes it back as it was (never dropped).
    if (size >= 4 && size - 4 <= mChunkMost && MR::loadSaveU16(pData) > cVersion) {
        if (!splice(0, 0, size)) {
            initializeData();
            return Result_Memory;
        }
        MR::copyMemory(mData, pData, size);
        mNewer = true;
        return Result_Ok;
    }
    bool good = size >= 4 && size - 4 <= mChunkMost && MR::loadSaveU16(pData) == cVersion;
    const u32 mods = good ? MR::loadSaveU16(pData + 2) : 0;
    u32 at = 4;
    for (u32 m = 0; good && m < mods; m++) {
        // The mod's id, unique in the chunk.
        if (size - at < 1) {
            good = false;
            break;
        }
        const u32 idLength = pData[at];
        if (size - at - 1 < idLength + 4 || !validIdBytes(pData + at + 1, idLength)) {
            good = false;
            break;
        }
        for (u32 seen = 4; seen < at; ) {
            const u32 seenLength = pData[seen];
            if (seenLength == idLength && sameBytes(pData + seen + 1, reinterpret_cast< const char* >(pData + at + 1), idLength)) {
                good = false;
                break;
            }
            seen += 1 + seenLength + 4 + MR::loadSaveU32(pData + seen + 1 + seenLength);
        }
        if (!good) {
            break;
        }
        const u32 bytes = MR::loadSaveU32(pData + at + 1 + idLength);
        const u32 entries = at + 1 + idLength + 4;
        if (bytes > mModMost || bytes > size - entries) {
            good = false;
            break;
        }
        // Its entries fill exactly `bytes`, each key valid and unique in the mod, each value at most 64 KiB.
        u32 e = entries;
        while (good && e < entries + bytes) {
            const u32 keyLength = pData[e];
            if (entries + bytes - e < 1 + keyLength + 4 || !validKeyBytes(pData + e + 1, keyLength)) {
                good = false;
                break;
            }
            const u32 valueLength = MR::loadSaveU32(pData + e + 1 + keyLength);
            if (valueLength > cValueMost || valueLength > entries + bytes - (e + 1 + keyLength + 4)) {
                good = false;
                break;
            }
            for (u32 seen = entries; seen < e; ) {
                const u32 seenLength = pData[seen];
                if (seenLength == keyLength &&
                    sameBytes(pData + seen + 1, reinterpret_cast< const char* >(pData + e + 1), keyLength)) {
                    good = false;
                    break;
                }
                seen += 1 + seenLength + 4 + MR::loadSaveU32(pData + seen + 1 + seenLength);
            }
            e += 1 + keyLength + 4 + valueLength;
        }
        at = entries + bytes;
    }
    if (good && at != size) {
        good = false;
    }

    if (!good) {
        return Result_NotFound;
    }
    if (size > 4) {
        if (!splice(0, 0, size - 4)) {
            initializeData();
            return Result_Memory;
        }
        MR::copyMemory(mData, pData + 4, size - 4);
    }
    mMods = mods;
    return Result_Ok;
}

void LumaSaveChunk::initializeData() {
    mSize = 0;
    mMods = 0;
    mNewer = false;
}

bool LumaSaveChunk::findInFile(const u8* pFile, u32 size, u32* pAt, u32* pChunkSize) {
    if (pFile == nullptr || size < 4) {
        return false;
    }
    u32 at = 4;
    for (u32 i = 0; i < pFile[1]; i++) {
        if (size - at < BinaryDataChunk::cDataOffset) {
            return false;
        }
        const u32 chunkSize = MR::loadSaveU32(pFile + at + BinaryDataChunk::cSize);
        if (chunkSize < BinaryDataChunk::cDataOffset || chunkSize > size - at) {
            return false;
        }
        if (MR::loadSaveU32(pFile + at + BinaryDataChunk::cSignature) == cSignature) {
            *pAt = at;
            *pChunkSize = chunkSize;
            return true;
        }
        at += chunkSize;
    }
    return false;
}

void LumaSaveChunk::forEachMod(void (*pFn)(void* pUser, const char* pId, u32 idLength, u32 bytes), void* pUser) const {
    if (mNewer) {
        return;
    }
    for (u32 at = 0; at < mSize; at += 1 + mData[at] + 4 + modBytes(at)) {
        if (modBytes(at) != 0) {
            pFn(pUser, reinterpret_cast< const char* >(mData + at + 1), mData[at], modBytes(at));
        }
    }
}

bool LumaSaveChunk::isValidKey(const char* pKey) {
    return pKey != nullptr && validKeyBytes(reinterpret_cast< const u8* >(pKey), textLength(pKey));
}

s32 LumaSaveChunk::findMod(const char* pModId) const {
    if (mNewer) {
        return -1;  // get, remove, getModBytes, forgetMod and renameMod find nothing in a later version's data
    }
    const u32 length = textLength(pModId);
    for (u32 at = 0; at < mSize; at += 1 + mData[at] + 4 + modBytes(at)) {
        if (mData[at] == length && sameBytes(mData + at + 1, pModId, length)) {
            return static_cast< s32 >(at);
        }
    }
    return -1;
}

u32 LumaSaveChunk::modBytes(u32 modAt) const {
    return MR::loadSaveU32(mData + modAt + 1 + mData[modAt]);
}

void LumaSaveChunk::setModBytes(u32 modAt, u32 bytes) {
    MR::storeSaveU32(mData + modAt + 1 + mData[modAt], bytes);
}

s32 LumaSaveChunk::findEntry(u32 modAt, const char* pKey) const {
    const u32 length = textLength(pKey);
    const u32 first = modAt + 1 + mData[modAt] + 4;
    const u32 end = first + modBytes(modAt);
    for (u32 at = first; at < end; at += 1 + mData[at] + 4 + MR::loadSaveU32(mData + at + 1 + mData[at])) {
        if (mData[at] == length && sameBytes(mData + at + 1, pKey, length)) {
            return static_cast< s32 >(at);
        }
    }
    return -1;
}

bool LumaSaveChunk::splice(u32 at, u32 removed, u32 inserted) {
    const u32 size = mSize - removed + inserted;
    if (size > mRoom) {
        u32 room = mRoom != 0 ? mRoom : 256;
        while (room < size) {
            room *= 2;
        }
        u8* const pData = new u8[room];
        if (pData == nullptr) {
            return false;
        }
        if (at != 0) {
            MR::copyMemory(pData, mData, at);
        }
        if (mSize > at + removed) {
            MR::copyMemory(pData + at + inserted, mData + at + removed, mSize - at - removed);
        }
        delete[] mData;
        mData = pData;
        mRoom = room;
    } else if (inserted != removed && mSize > at + removed) {
        // Overlapping move: memmove's direction, byte by byte from the safe end.
        const u32 tail = mSize - at - removed;
        u8* const pFrom = mData + at + removed;
        u8* const pTo = mData + at + inserted;
        if (pTo < pFrom) {
            for (u32 i = 0; i < tail; i++) {
                pTo[i] = pFrom[i];
            }
        } else {
            for (u32 i = tail; i > 0; i--) {
                pTo[i - 1] = pFrom[i - 1];
            }
        }
    }
    mSize = size;
    return true;
}

s64 LumaSaveChunk::get(const char* pModId, const char* pKey, void* pOut, u32 room) const {
    const s32 modAt = findMod(pModId);
    if (modAt < 0) {
        return -1;
    }
    const s32 at = findEntry(static_cast< u32 >(modAt), pKey);
    if (at < 0) {
        return -1;
    }
    const u32 keyLength = mData[at];
    const u32 length = MR::loadSaveU32(mData + at + 1 + keyLength);
    const u32 copied = length < room ? length : room;
    if (copied != 0) {
        MR::copyMemory(pOut, mData + at + 1 + keyLength + 4, copied);
    }
    return length;
}

LumaSaveChunk::Result LumaSaveChunk::set(const char* pModId, const char* pKey, const void* pValue, u32 length) {
    if (mNewer) {
        return Result_NotFound;  // the services answer LUMA_E_ABSENT before they call
    }
    if (length > cValueMost) {
        return Result_Limit;
    }
    const u32 keyLength = textLength(pKey);
    const u32 idLength = textLength(pModId);
    const u32 entrySize = 1 + keyLength + 4 + length;
    s32 modAt = findMod(pModId);
    const s32 oldAt = modAt >= 0 ? findEntry(static_cast< u32 >(modAt), pKey) : -1;
    const u32 oldSize = oldAt >= 0 ? 1 + keyLength + 4 + MR::loadSaveU32(mData + oldAt + 1 + keyLength) : 0;
    const u32 modNow = modAt >= 0 ? modBytes(static_cast< u32 >(modAt)) : 0;
    const u32 modAfter = modNow - oldSize + entrySize;
    const u32 blockHead = modAt >= 0 ? 0 : 1 + idLength + 4;
    if (modAfter > mModMost) {
        return Result_Limit;
    }
    if (mSize - oldSize + entrySize + blockHead > mChunkMost - 4 || (modAt < 0 && mMods >= 0xFFFF)) {
        return Result_ChunkFull;
    }

    if (modAt < 0) {
        // A new mod's block at the end: its id and 0 bytes of entries.
        const u32 at = mSize;
        if (!splice(at, 0, blockHead)) {
            return Result_Memory;
        }
        mData[at] = static_cast< u8 >(idLength);
        MR::copyMemory(mData + at + 1, pModId, idLength);
        MR::storeSaveU32(mData + at + 1 + idLength, 0);
        modAt = static_cast< s32 >(at);
        mMods++;
    }

    // The entry where the old one was (the key keeps its place), else at the end of the mod's block.
    const u32 blockEnd = static_cast< u32 >(modAt) + 1 + idLength + 4 + modBytes(static_cast< u32 >(modAt));
    const u32 at = oldAt >= 0 ? static_cast< u32 >(oldAt) : blockEnd;
    if (!splice(at, oldSize, entrySize)) {
        return Result_Memory;
    }
    mData[at] = static_cast< u8 >(keyLength);
    MR::copyMemory(mData + at + 1, pKey, keyLength);
    MR::storeSaveU32(mData + at + 1 + keyLength, length);
    if (length != 0) {
        MR::copyMemory(mData + at + 1 + keyLength + 4, pValue, length);
    }
    setModBytes(static_cast< u32 >(modAt), modAfter);
    return Result_Ok;
}

LumaSaveChunk::Result LumaSaveChunk::remove(const char* pModId, const char* pKey) {
    const s32 modAt = findMod(pModId);
    const s32 at = modAt >= 0 ? findEntry(static_cast< u32 >(modAt), pKey) : -1;
    if (at < 0) {
        return Result_NotFound;
    }
    const u32 keyLength = mData[at];
    const u32 entrySize = 1 + keyLength + 4 + MR::loadSaveU32(mData + at + 1 + keyLength);
    const u32 modAfter = modBytes(static_cast< u32 >(modAt)) - entrySize;
    (void)splice(static_cast< u32 >(at), entrySize, 0);  // shrinking never allocates
    if (modAfter == 0) {
        // A mod with no values left has no block: the chunk holds only mods with data.
        (void)splice(static_cast< u32 >(modAt), 1 + mData[modAt] + 4, 0);
        mMods--;
    } else {
        setModBytes(static_cast< u32 >(modAt), modAfter);
    }
    return Result_Ok;
}

u32 LumaSaveChunk::getModBytes(const char* pModId) const {
    const s32 modAt = findMod(pModId);
    return modAt >= 0 ? modBytes(static_cast< u32 >(modAt)) : 0;
}

u32 LumaSaveChunk::entrySize(u32 at) const {
    return 1 + mData[at] + 4 + MR::loadSaveU32(mData + at + 1 + mData[at]);
}

bool LumaSaveChunk::isKeptFor(u32 at, const char* pModId, u32 idLength) const {
    return forIdLength(mData + at + 1, mData[at]) == idLength && sameBytes(mData + at + 1, pModId, idLength);
}

u32 LumaSaveChunk::getForBytes(const char* pModId) const {
    if (mNewer) {
        return 0;
    }
    const u32 idLength = textLength(pModId);
    u32 bytes = 0;
    for (u32 modAt = 0; modAt < mSize; modAt += 1 + mData[modAt] + 4 + modBytes(modAt)) {
        const u32 first = modAt + 1 + mData[modAt] + 4;
        const u32 end = first + modBytes(modAt);
        for (u32 at = first; at < end; at += entrySize(at)) {
            if (isKeptFor(at, pModId, idLength)) {
                bytes += entrySize(at);
            }
        }
    }
    return bytes;
}

void LumaSaveChunk::forEachFor(void (*pFn)(void* pUser, const char* pId, u32 idLength, u32 bytes), void* pUser) const {
    if (mNewer) {
        return;
    }
    for (u32 modAt = 0; modAt < mSize; modAt += 1 + mData[modAt] + 4 + modBytes(modAt)) {
        const u32 first = modAt + 1 + mData[modAt] + 4;
        const u32 end = first + modBytes(modAt);
        for (u32 at = first; at < end; at += entrySize(at)) {
            const u32 idLength = forIdLength(mData + at + 1, mData[at]);
            if (idLength != 0) {
                pFn(pUser, reinterpret_cast< const char* >(mData + at + 1), idLength, entrySize(at));
            }
        }
    }
}

void LumaSaveChunk::forgetMod(const char* pModId) {
    if (mNewer) {
        return;
    }
    const s32 ownAt = findMod(pModId);
    if (ownAt >= 0) {
        (void)splice(static_cast< u32 >(ownAt), 1 + mData[ownAt] + 4 + modBytes(static_cast< u32 >(ownAt)), 0);
        mMods--;
    }
    // Then what every other mod keeps for it ("<id>/" keys); a mod left with no values has no block.
    const u32 idLength = textLength(pModId);
    for (u32 modAt = 0; modAt < mSize;) {
        const u32 first = modAt + 1 + mData[modAt] + 4;
        u32 end = first + modBytes(modAt);
        for (u32 at = first; at < end;) {
            const u32 size = entrySize(at);
            if (isKeptFor(at, pModId, idLength)) {
                (void)splice(at, size, 0);  // shrinking never allocates
                setModBytes(modAt, modBytes(modAt) - size);
                end -= size;
            } else {
                at += size;
            }
        }
        if (modBytes(modAt) == 0) {
            (void)splice(modAt, 1 + mData[modAt] + 4, 0);
            mMods--;
        } else {
            modAt = end;
        }
    }
}

LumaSaveChunk::Result LumaSaveChunk::renameMod(const char* pOldId, const char* pNewId) {
    const s32 modAt = findMod(pOldId);
    if (modAt < 0) {
        return Result_NotFound;
    }
    if (findMod(pNewId) >= 0) {
        return Result_Limit;  // the new id has its own values: nothing moves (item 6 moves only into an empty id)
    }
    const u32 oldLength = mData[modAt];
    const u32 newLength = textLength(pNewId);
    if (mSize - oldLength + newLength > mChunkMost - 4) {
        return Result_ChunkFull;
    }
    if (!splice(static_cast< u32 >(modAt) + 1, oldLength, newLength)) {
        return Result_Memory;
    }
    mData[modAt] = static_cast< u8 >(newLength);
    MR::copyMemory(mData + modAt + 1, pNewId, newLength);
    return Result_Ok;
}

void LumaSaveChunk::fileNumber(char (&rText)[4]) const {
    nebula_snprintf(rText, sizeof(rText), "%d", mHolder != nullptr ? static_cast< int >(mHolder->mUser) : 0);
}
