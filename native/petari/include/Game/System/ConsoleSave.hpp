#pragma once

// Reading an old save: a version 2 file, written in the console's byte order (a Wii save, a v1 install's), is read once with
// this on (SaveDataHandler) and written back in host order. It is the game's one place that knows another byte order.

inline bool gSaveConsoleOrder = false;

// A scalar of the save being read, in host order.
template < typename T >
inline T saveScalar(T value) {
    if (!gSaveConsoleOrder) {
        return value;
    }

    if constexpr (sizeof(T) == 2) {
        unsigned short v;
        __builtin_memcpy(&v, &value, 2);
        v = __builtin_bswap16(v);
        __builtin_memcpy(&value, &v, 2);
    } else if constexpr (sizeof(T) == 4) {
        unsigned int v;
        __builtin_memcpy(&v, &value, 4);
        v = __builtin_bswap32(v);
        __builtin_memcpy(&value, &v, 4);
    } else if constexpr (sizeof(T) == 8) {
        unsigned long long v;
        __builtin_memcpy(&v, &value, 8);
        v = __builtin_bswap64(v);
        __builtin_memcpy(&value, &v, 8);
    }

    return value;
}
