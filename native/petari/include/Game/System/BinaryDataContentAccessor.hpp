#pragma once

#include "Game/Util/MemoryUtil.hpp"
#include <JSystem/JSupport/JSUMemoryOutputStream.hpp>

class BinaryDataContentHeaderSerializer {
public:
    BinaryDataContentHeaderSerializer(u8*, u32);

    void addAttribute(const char*, u32);
    void flush();
    u32 getHeaderSize() const;
    u32 getDataSize() const;

    void* getBuffer() const {
        return mStream.mBuffer;
    }

    JSUMemoryOutputStream mStream;
    u32 mAttributeNum;
    u32 mDataSize;
};

class BinaryDataContentAccessor {
public:
    BinaryDataContentAccessor(u8* pData);

    s32 getHeaderSize() const;
    s32 getDataSize() const;
    s32 getAttributeNum() const;  // stripped
    void* getPointer(const char* pAttributeName, u8* pData) const;

    u8* mData;
};
