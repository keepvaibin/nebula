#include "Game/System/BinaryDataChunkHolder.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/MemoryUtil.hpp"

namespace {
    const s32 cFirstChunkRoom = 8;
}  // namespace

BinaryDataChunkHolder::BinaryDataChunkHolder()
    : mChunks(new BinaryDataChunkBase*[cFirstChunkRoom]), mChunkRoom(cFirstChunkRoom), mNumChunks(0) {
}

void BinaryDataChunkHolder::addChunk(BinaryDataChunkBase* pChunk) {
    if (mNumChunks == mChunkRoom) {
        mChunks = MR::growArray(mChunks, mChunkRoom);
        mChunkRoom *= 2;
    }
    mChunks[mNumChunks++] = pChunk;
}

u32 BinaryDataChunkHolder::makeFileBinary(u8* pData, u32 capacity) {
    // A byte a chunk passes over reads as zero.
    MR::zeroMemory(pData, capacity);
    pData[0] = 1;
    pData[1] = mNumChunks;

    u32 offset = 4;
    for (s32 idx = 0; idx < mNumChunks; idx++) {
        offset += makeChunkData(pData + offset, capacity - offset, mChunks[idx]);
    }

    return offset;
}

bool BinaryDataChunkHolder::loadFromFileBinary(const u8* pData, u32 dataSize) {
    s32 offset = 0;

    if (dataSize < calcBinarySize(pData, dataSize)) {
        return false;
    }

    offset += 4;
    bool hashError = false;
    bool deserializeError = 0;

    for (s32 idx = 0; idx < pData[1]; idx++) {
        const u8* chunkData = &pData[offset];
        const u32 chunkSize = MR::loadSaveU32(chunkData + BinaryDataChunk::cSize);
        BinaryDataChunkBase* chunk = findFromSignature(MR::loadSaveU32(chunkData + BinaryDataChunk::cSignature));

        if (chunk != nullptr) {
            if (MR::loadSaveU32(chunkData + BinaryDataChunk::cHash) != chunk->makeHeaderHashCode()) {
                hashError = true;
            }

            s32 status = chunk->deserialize(chunkData + BinaryDataChunk::cDataOffset, chunkSize - BinaryDataChunk::cDataOffset);
            bool deserializeSuccess;
            switch (status) {
            case 0:
            case 1:
                deserializeSuccess = true;
                break;
            default:
                deserializeSuccess = false;
                break;
            }

            deserializeError |= !deserializeSuccess;
        }
        offset += chunkSize;
    }

    bool success = true;
    // An old save being converted keeps its contents whatever its hashes.
    if (hashError && !gOldSaveOrder) {
        success = false;
    }

    if (deserializeError) {
        success = false;
    }

    if (dataSize < offset) {
        success = false;
    }

    return success;
}

// The chunk's header and data at pData; returns its size, header included.
u32 BinaryDataChunkHolder::makeChunkData(u8* pData, u32 bufferSize, const BinaryDataChunkBase* pChunk) {
    const u32 size = pChunk->serialize(pData + BinaryDataChunk::cDataOffset, bufferSize - BinaryDataChunk::cDataOffset) +
                     BinaryDataChunk::cDataOffset;
    MR::storeSaveU32(pData + BinaryDataChunk::cSignature, pChunk->getSignature());
    MR::storeSaveU32(pData + BinaryDataChunk::cHash, pChunk->makeHeaderHashCode());
    MR::storeSaveU32(pData + BinaryDataChunk::cSize, size);
    return size;
}

BinaryDataChunkBase* BinaryDataChunkHolder::findFromSignature(u32 signature) const {
    for (s32 idx = 0; idx < mNumChunks; idx++) {
        BinaryDataChunkBase* chunk = mChunks[idx];

        if (chunk->getSignature() == signature) {
            return chunk;
        }
    }

    return nullptr;
}

// The file's size by its chunks' sizes; past any dataSize when a chunk does not lie in dataSize bytes.
u32 BinaryDataChunkHolder::calcBinarySize(const u8* pData, u32 dataSize) {
    const u32 cDamaged = 0xFFFFFFFF;
    if (dataSize < 4) {
        return cDamaged;
    }

    u32 size = 4;
    for (s32 idx = 0; idx < pData[1]; idx++) {
        if (dataSize - size < BinaryDataChunk::cDataOffset) {
            return cDamaged;
        }

        const u32 chunkSize = MR::loadSaveU32(&pData[size] + BinaryDataChunk::cSize);
        if (chunkSize < BinaryDataChunk::cDataOffset || chunkSize > dataSize - size) {
            return cDamaged;
        }

        size += chunkSize;
    }
    return size;
}
