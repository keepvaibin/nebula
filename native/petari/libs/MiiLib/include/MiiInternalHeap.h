#ifndef RVL_FACE_LIBRARY_INTERNAL_HEAP_H
#define RVL_FACE_LIBRARY_INTERNAL_HEAP_H
// The Mii library's working memory: host allocations (Game/NPC/NebulaMiiHeap.cpp), 32-byte aligned, as large as
// each part needs; no work buffer or fixed-size heaps.
#ifdef __cplusplus
extern "C" {
#endif

// NULL when the host has no memory (the library reports that itself).
void* NebulaMiiAlloc(u32 size);
void NebulaMiiFree(void* block);

#ifdef __cplusplus
}
#endif
#endif
