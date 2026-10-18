#pragma once

#include "JSystem/JSupport/JSUIosBase.hpp"

class JSUOutputStream : public JSUIosBase {
public:
    JSUOutputStream() {
    }

    virtual ~JSUOutputStream() ATTRIBUTE_WEAK;
    virtual s32 skip(s32, s8);
    virtual s32 writeData(const void*, s32) = 0;

    s32 write(const void*, s32);

#ifdef TARGET_PC
    // Big-endian scalars (JSUBigEndianScalar); a size that is not a whole number of elements writes the bytes as they are.
    template < typename T >
    typename JSUBigEndianScalar< T >::Result write(const T* pData, s32 size) {
        if (size % (s32)sizeof(T) != 0) {
            return write(static_cast< const void* >(pData), size);
        }

        s32 written = 0;
        for (s32 i = 0; i < size / (s32)sizeof(T); i++) {
            const BE< T > value(pData[i]);
            written += write(static_cast< const void* >(&value), sizeof(T));
        }
        return written;
    }
#endif

    inline void writeU8(u8 val) {
        write(&val, sizeof(u8));
    }

    inline void writeU16(u16 val) {
        write(&val, sizeof(u16));
    }

    inline void writeU32(u32 val) {
        write(&val, sizeof(u32));
    }

    // TODO: probably a lot of other helpers for different types
};
