#pragma once

#include "Game/System/OldSave.hpp"
#include "JSystem/JSupport/JSUIosBase.hpp"

class JSUInputStream : public JSUIosBase {
public:
    JSUInputStream() : JSUIosBase() {
    }

    virtual ~JSUInputStream();
    virtual s32 getAvailable() const = 0;
    virtual s32 skip(s32);
    virtual u32 readData(void*, s32) = 0;

    s32 read(void*, s32);

    // Scalars: host order, or an old save's while one is converted.
    template < typename T >
    typename JSUScalar< T >::Result read(T* pData, s32 size) {
        const s32 got = read(static_cast< void* >(pData), size);
        if (gOldSaveOrder && size % (s32)sizeof(T) == 0) {
            for (s32 i = 0; i < size / (s32)sizeof(T); i++) {
                pData[i] = saveScalar(pData[i]);
            }
        }
        return got;
    }

    inline u8 readU8() {
        u8 ret;
        read(&ret, sizeof(u8));
        return ret;
    }

    inline u16 readU16() {
        u16 ret;
        read(&ret, sizeof(u16));
        return ret;
    }

    inline u32 readU32() {
        u32 ret;
        read(&ret, sizeof(u32));
        return ret;
    }

    // TODO: probably a lot of other helpers for different types
};
