#pragma once

#include <NebulaGpu.h>
#include <revolution/types.h>

class Color8 {
public:
    Color8() : mColor(-1) {
    }

    Color8(NebulaColor8 color) {
        set(color);
    }

    Color8(u8 r, u8 g, u8 b, u8 a) {
        set(r, g, b, a);
    }

    Color8(u32 color) {
        mColor = color;
    }

    inline operator NebulaColor8() const {
        return mGXColor;
    }

    inline operator u32() const {
        return mColor;
    }

    inline const Color8& operator=(const NebulaColor8& rOther) {
        mGXColor = rOther;
        return *this;
    }

    inline const Color8& operator=(const Color8& rOther) {
        r = rOther.r;
        g = rOther.g;
        b = rOther.b;
        a = rOther.a;
        return *this;
    }

    void set(u8 _r, u8 _g, u8 _b, u8 _a) {
        r = _r;
        g = _g;
        b = _b;
        a = _a;
    }

    void set(NebulaColor8 color) {
        NebulaColor8* const pColor = &mGXColor;
        *pColor = color;
    }

    union {
        struct {
            u8 r;
            u8 g;
            u8 b;
            u8 a;
        };
        u32 mColor;
        NebulaColor8 mGXColor;
    };
};

class Color10 {
public:
    Color10() : r(0x3FF), g(0x3FF), b(0x3FF), a(0x3FF) {
    }

    operator NebulaColor16() const {
        return mColor;
    }

    union {
        struct {
            u16 r;
            u16 g;
            u16 b;
            u16 a;
        };
        NebulaColor16 mColor;
    };
};
