#pragma once

#include <revolution/types.h>
#include <stdint.h>

template < typename T >
inline T* JSUConvertOffsetToPtr(const void* ptr, uintptr_t offset) {
    return offset == 0 ? NULL : (T*)((intptr_t)ptr + (intptr_t)offset);
}

template < typename T >
inline T* JSUConvertOffsetToPtr(const void* ptr, const void* offset) {
    return offset == NULL ? NULL : reinterpret_cast< T* >(reinterpret_cast< intptr_t >(ptr) + reinterpret_cast< intptr_t >(offset));
}

#ifdef TARGET_PC
// The file's offset fields that the console declared as pointers (NEBULA_OFFSET).
template < typename T, typename U >
inline T* JSUConvertOffsetToPtr(const void* ptr, const BEOffset< U >& offset) {
    return offset.isNull() ? NULL : (T*)((const u8*)ptr + offset.offset());
}

// Such a field's offset as an integer, where the console cast the pointer-typed field.
#define JSU_OFFSET_VALUE(field) ((uintptr_t)(field).offset())
#else
#define JSU_OFFSET_VALUE(field) ((uintptr_t)(field))
#endif

inline u8 JSULoNibble(u8 value) {
    return value & 0x0f;
}

inline u8 JSUHiNibble(u8 value) {
    return value >> 4;
}

inline u8 JSULoByte(u16 value) {
    return value & 0xff;
}

inline u8 JSUHiByte(u16 value) {
    return value >> 8;
}

inline u16 JSUHiHalf(u32 value) {
    return (value >> 16);
}

inline u16 JSULoHalf(u32 value) {
    return value;
}
