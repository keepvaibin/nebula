#pragma once

// A number at a byte position of cooked data or of a tag's units: it may lie on any boundary, and nothing else is told it is a
// number there. Replaces a cast and a dereference.

template < typename T >
inline T readBytes(const void* p) {
    T value;
    __builtin_memcpy(&value, p, sizeof(value));
    return value;
}

template < typename T >
inline void writeBytes(void* p, T value) {
    __builtin_memcpy(p, &value, sizeof(value));
}
