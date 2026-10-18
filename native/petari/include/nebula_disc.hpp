#pragma once

// Disc data on the native build (TARGET_PC): helpers next to Nebula's BE<T>/BEOffset<T>/SelfRel<T> (nebula_be.hpp) for
// the console layouts those types do not cover.
//
// NEBULA_DISC(T) is T as a file lays it out: natively BEData<T>, a struct of the same layout with big-endian fields
// that converts to T where the code copies it into runtime state; T itself for CodeWarrior. Types whose fields are all
// bytes need no twin. The specializations live next to each T.
//
// NEBULA_SELFREL(T) is a 32-bit slot of a file that the code relocates in place (offset overwritten by a pointer):
// SelfRel<T> natively, T* for CodeWarrior.
#if !defined(TARGET_PC)
#define NEBULA_DISC(T) T
#define NEBULA_SELFREL(T) T*
#elif defined(__cplusplus)
#define NEBULA_SELFREL(T) SelfRel< T >
#include <revolution/types.h>
#include <revolution/mtx.h>
#include <revolution/gx/GXStruct.h>

template < typename T >
struct BEData;
#define NEBULA_DISC(T) BEData< T >

namespace nebula {
    template < typename W >
    struct BEBitsWord;
    template <>
    struct BEBitsWord< u8 > {
        static u8 swap(u8 v) { return v; }
    };
    template <>
    struct BEBitsWord< u16 > {
        static u16 swap(u16 v) { return __builtin_bswap16(v); }
    };
    template <>
    struct BEBitsWord< u32 > {
        static u32 swap(u32 v) { return __builtin_bswap32(v); }
    };
}  // namespace nebula

// Bits [Shift, Shift + Width) of a big-endian W, counted from the least significant bit of the value: the console's
// MSB-first bitfields over file bytes. A declaration `u32 a : 8; u32 b : 24;` becomes, in a union over the word,
// `BEBits< u32, 24, 8 > a; BEBits< u32, 0, 24 > b;`. Reads and assigns as an unsigned value; trivially copyable.
template < typename W, int Shift, int Width >
class BEBits {
    W mRaw;

    static W mask() { return (W)(((Width >= (int)(sizeof(W) * 8)) ? ~(W)0 : (W)(((W)1 << Width) - 1)) << Shift); }

public:
    W get() const { return (W)((nebula::BEBitsWord< W >::swap(mRaw) & mask()) >> Shift); }
    operator W() const { return get(); }

    BEBits& operator=(W v) {
        const W word = nebula::BEBitsWord< W >::swap(mRaw);
        mRaw = nebula::BEBitsWord< W >::swap((W)((word & ~mask()) | (((W)(v << Shift)) & mask())));
        return *this;
    }
    BEBits& operator|=(W v) { return *this = (W)(get() | v); }
    BEBits& operator&=(W v) { return *this = (W)(get() & v); }
};

template < int Shift, int Width >
using BEBits32 = BEBits< u32, Shift, Width >;
template < int Shift, int Width >
using BEBits16 = BEBits< u16, Shift, Width >;
template < int Shift, int Width >
using BEBits8 = BEBits< u8, Shift, Width >;

template <>
struct BEData< Vec > {
    BE< f32 > x, y, z;

    operator Vec() const {
        Vec v = {x, y, z};
        return v;
    }
};

template <>
struct BEData< S16Vec > {
    BE< s16 > x, y, z;

    operator S16Vec() const {
        S16Vec v = {x, y, z};
        return v;
    }
};

template <>
struct BEData< GXColorS10 > {
    BE< s16 > r, g, b, a;

    operator GXColorS10() const {
        GXColorS10 c = {r, g, b, a};
        return c;
    }
};

template <>
struct BEData< GXVtxAttrFmtList > {
    BE< GXAttr > attr;
    BE< GXCompCnt > cnt;
    BE< GXCompType > type;
    u8 frac;

    operator GXVtxAttrFmtList() const {
        GXVtxAttrFmtList l = {attr, cnt, type, frac};
        return l;
    }
};

template <>
struct BEData< GXVtxDescList > {
    BE< GXAttr > attr;
    BE< GXAttrType > type;

    operator GXVtxDescList() const {
        GXVtxDescList l = {attr, type};
        return l;
    }
};

// Row-major f32 matrices.
template < int Rows, int Cols >
struct BEMatrix {
    BE< f32 > m[Rows][Cols];

    void copyTo(f32 (*out)[Cols]) const {
        for (int i = 0; i < Rows; i++) {
            for (int j = 0; j < Cols; j++) {
                out[i][j] = m[i][j];
            }
        }
    }
};
template <>
struct BEData< Mtx > : BEMatrix< 3, 4 > {};
template <>
struct BEData< Mtx44 > : BEMatrix< 4, 4 > {};
template <>
struct BEData< Mtx23 > : BEMatrix< 2, 3 > {};

static_assert(sizeof(BEData< Vec >) == 12 && sizeof(BEData< GXVtxAttrFmtList >) == 16 && sizeof(BEData< Mtx >) == 48,
              "disc twins keep the console sizes");
#endif
