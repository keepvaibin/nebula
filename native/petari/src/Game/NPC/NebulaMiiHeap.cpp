#include <RFLi_Heap.h>

extern "C" void* malloc(size_t size);
extern "C" void free(void* block);

// The Mii library's blocks come from the host's allocator, aligned to 32 bytes for the texture and vertex data they
// hold: the allocation is over-sized by the alignment and a pointer, and the block's start keeps the allocation's
// address just before it.
extern "C" void* NebulaMiiAlloc(u32 size) {
    const size_t extra = 32u + sizeof(void*);
    void* raw = malloc(static_cast< size_t >(size) + extra);
    if (raw == nullptr) {
        return nullptr;
    }
    const size_t start = (reinterpret_cast< size_t >(raw) + extra) & ~static_cast< size_t >(31u);
    reinterpret_cast< void** >(start)[-1] = raw;
    return reinterpret_cast< void* >(start);
}

extern "C" void NebulaMiiFree(void* block) {
    if (block != nullptr) {
        free(static_cast< void** >(block)[-1]);
    }
}
