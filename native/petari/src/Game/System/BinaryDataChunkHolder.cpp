#include "Game/System/BinaryDataChunkHolder.hpp"
#include "Game/Util/MemoryUtil.hpp"

BinaryDataChunkHolder::BinaryDataChunkHolder(u32 bufferSize, int maxChunks)
    : mChunks(nullptr), mMaxChunks(0), mNumChunks(0), mData(nullptr), mBufferSize(bufferSize) {
    mData = new u8[mBufferSize];
    MR::zeroMemory(mData, mBufferSize);
    mChunks = new BinaryDataChunkBase*[maxChunks];
    mMaxChunks = maxChunks;
}

void BinaryDataChunkHolder::addChunk(BinaryDataChunkBase* pChunk) {
    s32 numChunks = mNumChunks;
    mNumChunks++;
    mChunks[numChunks] = pChunk;
}

u32 BinaryDataChunkHolder::makeFileBinary(u8* pData, u32 a2) {
    s32 offset = 0;

    MR::zeroMemory(pData, 4);
    pData[0] = 1;
    pData[1] = mNumChunks;

    offset += 4;
    for (s32 idx = 0; idx < mNumChunks; idx++) {
        MR::zeroMemory(mData, mBufferSize);

        u8* chunkData = static_cast< u8* >(mData);
        const u32 chunkSize = makeChunkData(chunkData, mBufferSize, mChunks[idx]);

        MR::copyMemory(&pData[offset], chunkData, chunkSize);
        offset += chunkSize;
    }

    return offset;
}

bool BinaryDataChunkHolder::loadFromFileBinary(const u8* pData, u32 dataSize) {
    s32 offset = 0;

    if (dataSize < calcBinarySize(pData)) {
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
    if (hashError) {
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

u32 BinaryDataChunkHolder::calcBinarySize(const u8* pData) {
    s32 size = 4;
    for (s32 idx = 0; idx < pData[1]; idx++) {
        size += MR::loadSaveU32(&pData[size] + BinaryDataChunk::cSize);
    }
    return size;
}
