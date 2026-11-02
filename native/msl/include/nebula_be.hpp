// The console's byte order for data whose layout the console's files and hardware define (docs/NATIVE_PORT.md,
// "Byte order"): structs laid over loaded file bytes, saves, and everything the GPU reads. Natively such fields are
// BE<T> (a big-endian value, swapped on each access). NEBULA_BE(T) in a struct reads as the plain type for CodeWarrior.
#ifndef NEBULA_BE_HPP
#define NEBULA_BE_HPP

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace nebula {

template < size_t N >
struct BEWord;
template <>
struct BEWord< 1 > {
    typedef uint8_t Type;
    static Type swap(Type v) { return v; }
};
template <>
struct BEWord< 2 > {
    typedef uint16_t Type;
    static Type swap(Type v) { return __builtin_bswap16(v); }
};
template <>
struct BEWord< 4 > {
    typedef uint32_t Type;
    static Type swap(Type v) { return __builtin_bswap32(v); }
};
template <>
struct BEWord< 8 > {
    typedef uint64_t Type;
    static Type swap(Type v) { return __builtin_bswap64(v); }
};

}  // namespace nebula

// A T stored big-endian: integers, enums, f32, f64. Trivially constructible and copyable, the size and alignment of
// T, so it can be laid over file bytes and sit in unions.
template < typename T >
class BE {
    typedef nebula::BEWord< sizeof(T) > Word;
    typename Word::Type mRaw;

    static typename Word::Type toRaw(T v) {
        typename Word::Type w;
        memcpy(&w, &v, sizeof(T));
        return Word::swap(w);
    }

public:
    BE() = default;
    BE(T v) : mRaw(toRaw(v)) {}

    T get() const {
        const typename Word::Type w = Word::swap(mRaw);
        T v;
        memcpy(&v, &w, sizeof(T));
        return v;
    }
    void set(T v) { mRaw = toRaw(v); }
    operator T() const { return get(); }

    BE& operator=(T v) {
        set(v);
        return *this;
    }
    BE& operator+=(T v) { return *this = T(get() + v); }
    BE& operator-=(T v) { return *this = T(get() - v); }
    BE& operator*=(T v) { return *this = T(get() * v); }
    BE& operator/=(T v) { return *this = T(get() / v); }
    BE& operator|=(T v) { return *this = T(get() | v); }
    BE& operator&=(T v) { return *this = T(get() & v); }
    BE& operator^=(T v) { return *this = T(get() ^ v); }
    BE& operator<<=(int n) { return *this = T(get() << n); }
    BE& operator>>=(int n) { return *this = T(get() >> n); }
    BE& operator++() { return *this = T(get() + 1); }
    BE& operator--() { return *this = T(get() - 1); }
    T operator++(int) {
        const T v = get();
        set(T(v + 1));
        return v;
    }
    T operator--(int) {
        const T v = get();
        set(T(v - 1));
        return v;
    }

    // The bytes as the file has them.
    typename Word::Type raw() const { return mRaw; }
};

#define NEBULA_BE(T) BE< T >

static_assert(sizeof(BE< uint32_t >) == 4 && alignof(BE< uint32_t >) == 4, "BE<u32> lays over 4 file bytes");
static_assert(sizeof(BE< uint16_t >) == 2 && sizeof(BE< float >) == 4, "BE<T> is the size of T");

#endif
