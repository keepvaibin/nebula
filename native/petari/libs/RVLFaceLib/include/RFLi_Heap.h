#ifndef RVL_FACE_LIBRARY_INTERNAL_HEAP_H
#define RVL_FACE_LIBRARY_INTERNAL_HEAP_H
// The Mii library's working heaps natively: JKRExpHeaps (the game's own heaps) in the work buffer the game hands it,
// in place of the SDK's MEM expanded heaps (Game/NPC/NebulaMiiHeap.cpp).
#include <revolution/types.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct NebulaMiiHeap NebulaMiiHeap;

// A heap over [buffer, buffer + size), inside one of the game's heaps; NULL if the buffer is too small.
NebulaMiiHeap* NebulaMiiHeapCreate(void* buffer, u32 size);
void NebulaMiiHeapDestroy(NebulaMiiHeap* heap);
// NULL when the heap has no room (the library reports that itself).
void* NebulaMiiHeapAlloc(NebulaMiiHeap* heap, u32 size, s32 alignment);
void NebulaMiiHeapFree(NebulaMiiHeap* heap, void* block);
// The largest block the heap can give.
u32 NebulaMiiHeapLargestFree(NebulaMiiHeap* heap);

#ifdef __cplusplus
}
#endif
#endif
