#pragma once

#include <revolution/types.h>
#include "Game/System/SaveBinary.hpp"

class BinaryDataChunkBase {
public:
    virtual u32 makeHeaderHashCode() const = 0;
    virtual u32 getSignature() const = 0;
    virtual s32 serialize(u8*, u32) const = 0;
    virtual s32 deserialize(const u8*, u32) = 0;
    virtual void initializeData() = 0;
};

// One chunk in a user file of GameData.bin: its signature, header hash and size (header included), then its data;
// the numbers big-endian by the file format, read and written field by field (SaveBinary.hpp).
namespace BinaryDataChunk {
    const u32 cSignature = 0x0;
    const u32 cHash = 0x4;
    const u32 cSize = 0x8;
    const u32 cDataOffset = 0xC;
};  // namespace BinaryDataChunk

class BinaryDataChunkHolder {
public:
    BinaryDataChunkHolder(u32 dataSize, int maxChunks);

    void addChunk(BinaryDataChunkBase* pChunk);
    u32 makeFileBinary(u8*, u32);
    bool loadFromFileBinary(const u8* pData, u32 dataSize);
    static u32 makeChunkData(u8* pData, u32 dataSize, const BinaryDataChunkBase*);
    BinaryDataChunkBase* findFromSignature(u32 signature) const;
    static u32 calcBinarySize(const u8* pData);

    /* 0x00 */ BinaryDataChunkBase** mChunks;
    /* 0x04 */ s32 mMaxChunks;
    /* 0x08 */ s32 mNumChunks;
    /* 0x0C */ void* mData;
    /* 0x10 */ u32 mBufferSize;
};
