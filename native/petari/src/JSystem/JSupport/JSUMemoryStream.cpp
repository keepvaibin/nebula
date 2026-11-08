#include "JSystem/JSupport/JSUMemoryInputStream.hpp"
#include "JSystem/JSupport/JSUMemoryOutputStream.hpp"
#include <cstring>

namespace {
    // Moves rPosition by whence and offset, kept in 0..length; returns how far it moved.
    s32 seekInBuffer(s32& rPosition, s32 length, s32 offset, JSUStreamSeekFrom whence) {
        s32 oldPosition = rPosition;
        switch (whence) {
        case SEEK_FROM_START:
            rPosition = offset;
            break;
        case SEEK_FROM_END:
            rPosition = length - offset;
            break;
        case SEEK_FROM_POSITION:
            rPosition += offset;
            break;
        }
        if (rPosition < 0) {
            rPosition = 0;
        }
        if (rPosition > length) {
            rPosition = length;
        }
        return rPosition - oldPosition;
    }
}  // namespace

void JSUMemoryInputStream::setBuffer(const void* pBuffer, s32 length) {
    mBuffer = pBuffer;
    mLength = length;
    mPosition = 0;
}

u32 JSUMemoryInputStream::readData(void* pDest, s32 length) {
    if (mPosition + length > mLength) {
        length = mLength - mPosition;
    }
    if (length > 0) {
        memcpy(pDest, (u8*)mBuffer + mPosition, length);
        mPosition += length;
    }
    return length;
}

s32 JSUMemoryInputStream::seekPos(s32 offset, JSUStreamSeekFrom whence) {
    return ::seekInBuffer(mPosition, mLength, offset, whence);
}

void JSUMemoryOutputStream::setBuffer(void* pBuffer, s32 length) {
    mBuffer = pBuffer;
    mLength = length;
    mPosition = 0;
}

s32 JSUMemoryOutputStream::writeData(const void* pSrc, s32 length) {
    if (mPosition + length > mLength) {
        length = mLength - mPosition;
    }
    if (length > 0) {
        memcpy((u8*)mBuffer + mPosition, pSrc, length);
        mPosition += length;
    }
    return length;
}

s32 JSUMemoryOutputStream::seekPos(s32 offset, JSUStreamSeekFrom whence) {
    return ::seekInBuffer(mPosition, mLength, offset, whence);
}

s32 JSUMemoryOutputStream::getLength() const {
    return mLength;
}

s32 JSUMemoryInputStream::getPosition() const {
    return mPosition;
}

s32 JSUMemoryInputStream::getLength() const {
    return mLength;
}