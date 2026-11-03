#pragma once

// Reading an old save: a version 2 file holds its numbers with the most significant byte first. This is on while one is
// read (SaveDataHandler), and it is written back in host order. It is the game's one place that knows another byte order.

inline bool gOldSaveOrder = false;

// A scalar of the save being read, in host order.
template < typename T >
inline T saveScalar(T value) {
    if (!gOldSaveOrder) {
        return value;
    }

    unsigned char bytes[sizeof(T)];
    __builtin_memcpy(bytes, &value, sizeof(T));
    for (unsigned long i = 0; i < sizeof(T) / 2; i++) {
        const unsigned char swapped = bytes[i];
        bytes[i] = bytes[sizeof(T) - 1 - i];
        bytes[sizeof(T) - 1 - i] = swapped;
    }
    __builtin_memcpy(&value, bytes, sizeof(T));
    return value;
}
