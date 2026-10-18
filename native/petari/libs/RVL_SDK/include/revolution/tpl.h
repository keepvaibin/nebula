#ifndef TPL_H
#define TPL_H

#include "revolution/gx.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(TARGET_PC) && defined(__cplusplus)
// Natively the structs are the file's layout: big-endian fields, and 32-bit slots that TPLBind turns from file offsets
// into self-relative pointers in place (the console wrote 32-bit pointers there).
#define TPL_BE(T) BE< T >
#define TPL_PTR(T) SelfRel< T >
#elif defined(TARGET_PC)
#define TPL_BE(T) T
#define TPL_PTR(T) s32
#else
#define TPL_BE(T) T
#define TPL_PTR(T) T*
#endif

typedef struct {
    TPL_BE(u16) numEntries;
    u8 unpacked;
    u8 _4;
    TPL_BE(GXTlutFmt) format;
    TPL_PTR(char) data;
} TPLClutHeader, *TPLClutHeaderPtr;

typedef struct {
    TPL_BE(u16) height;
    TPL_BE(u16) width;
    TPL_BE(u32) format;
    TPL_PTR(char) data;
    TPL_BE(GXTexWrapMode) wrapS;
    TPL_BE(GXTexWrapMode) wrapT;
    TPL_BE(GXTexFilter) minFilter;
    TPL_BE(GXTexFilter) magFilter;
    TPL_BE(f32) LODBias;
    u8 edgeLODEnable;
    u8 minLOD;
    u8 maxLOD;
    u8 unpacked;
} TPLHeader, *TPLHeaderPtr;

typedef struct {
    TPL_PTR(TPLHeader) textureHeader;
    TPL_PTR(TPLClutHeader) CLUTHeader;
} TPLDescriptor, *TPLDescriptorPtr;

typedef struct {
    TPL_BE(u32) versionNumber;
    TPL_BE(u32) numDescriptors;
    TPL_PTR(TPLDescriptor) descriptorArray;
} TPLPalette, *TPLPalettePtr;

void TPLBind(TPLPalettePtr);
TPLDescriptorPtr TPLGet(TPLPalettePtr, u32);

#ifdef __cplusplus
}
#endif

#endif // TPL_H
