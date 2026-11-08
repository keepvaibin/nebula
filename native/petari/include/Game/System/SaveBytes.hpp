#pragma once

#include "Game/System/OldSave.hpp"
#include <nebula_types.h>

// A save chunk's bytes, written or read in order over the caller's buffer. A copy that runs past the end copies what fits;
// positions stay in the buffer. Writes are host order; scalar reads are host order, or the old save's while one is converted.
class SaveWriter {
public:
    SaveWriter(void* pBuffer, s32 size) : mBuffer(static_cast< u8* >(pBuffer)), mSize(size), mPosition(0) {
    }

    // Returns the bytes written.
    s32 write(const void* pSrc, s32 size) {
        if (mPosition + size > mSize) {
            size = mSize - mPosition;
        }
        if (size > 0) {
            __builtin_memcpy(mBuffer + mPosition, pSrc, size);
            mPosition += size;
        }
        return size;
    }
    void writeU8(u8 value) {
        write(&value, sizeof(value));
    }
    void writeU16(u16 value) {
        write(&value, sizeof(value));
    }

    void seekTo(s32 position) {
        mPosition = clamp(position);
    }
    void skip(s32 size) {
        mPosition = clamp(mPosition + size);
    }

    u8* getBuffer() const {
        return mBuffer;
    }
    u8* getCursor() const {
        return mBuffer + mPosition;
    }
    s32 getPosition() const {
        return mPosition;
    }
    s32 getAvailable() const {
        return mSize - mPosition;
    }

private:
    s32 clamp(s32 position) const {
        return position < 0 ? 0 : position > mSize ? mSize : position;
    }

    u8* mBuffer;
    s32 mSize;
    s32 mPosition;
};

class SaveReader {
public:
    SaveReader(const void* pBuffer, s32 size) : mBuffer(static_cast< const u8* >(pBuffer)), mSize(size), mPosition(0) {
    }

    // Returns the bytes read. Bytes and anything not a scalar below come as they are.
    s32 read(void* pDest, s32 size) {
        if (mPosition + size > mSize) {
            size = mSize - mPosition;
        }
        if (size > 0) {
            __builtin_memcpy(pDest, mBuffer + mPosition, size);
            mPosition += size;
        }
        return size;
    }
    s32 read(u16* pDest, s32 size) {
        return readScalars(pDest, size);
    }
    s32 read(s16* pDest, s32 size) {
        return readScalars(pDest, size);
    }
    s32 read(u32* pDest, s32 size) {
        return readScalars(pDest, size);
    }
    s32 read(s32* pDest, s32 size) {
        return readScalars(pDest, size);
    }
    s32 read(u64* pDest, s32 size) {
        return readScalars(pDest, size);
    }
    s32 read(s64* pDest, s32 size) {
        return readScalars(pDest, size);
    }
    s32 read(f32* pDest, s32 size) {
        return readScalars(pDest, size);
    }
    s32 read(f64* pDest, s32 size) {
        return readScalars(pDest, size);
    }
    u8 readU8() {
        u8 value = 0;
        read(&value, sizeof(value));
        return value;
    }
    u16 readU16() {
        u16 value = 0;
        read(&value, sizeof(value));
        return value;
    }

    void skip(s32 size) {
        mPosition = clamp(mPosition + size);
    }

    const u8* getCursor() const {
        return mBuffer + mPosition;
    }
    s32 getPosition() const {
        return mPosition;
    }
    s32 getAvailable() const {
        return mSize - mPosition;
    }

private:
    // Every whole element is converted, read or not (a short read leaves the rest as it was).
    template < typename T >
    s32 readScalars(T* pDest, s32 size) {
        const s32 got = read(static_cast< void* >(pDest), size);
        if (gOldSaveOrder && size % static_cast< s32 >(sizeof(T)) == 0) {
            for (s32 i = 0; i < size / static_cast< s32 >(sizeof(T)); i++) {
                pDest[i] = saveScalar(pDest[i]);
            }
        }
        return got;
    }
    s32 clamp(s32 position) const {
        return position < 0 ? 0 : position > mSize ? mSize : position;
    }

    const u8* mBuffer;
    s32 mSize;
    s32 mPosition;
};
