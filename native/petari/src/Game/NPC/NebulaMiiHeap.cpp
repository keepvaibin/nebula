#include <RFLi_Heap.h>

#if defined(_WIN32)
extern "C" void* _aligned_malloc(size_t size, size_t alignment);
extern "C" void _aligned_free(void* block);
#else
extern "C" int posix_memalign(void** block, size_t alignment, size_t size);
extern "C" void free(void* block);
#endif

// The Mii library's blocks come from the host's allocator, aligned for the texture and vertex data they hold.
extern "C" void* NebulaMiiAlloc(u32 size) {
    const size_t bytes = (static_cast< size_t >(size) + 31u) & ~static_cast< size_t >(31u);
#if defined(_WIN32)
    return _aligned_malloc(bytes != 0 ? bytes : 32u, 32u);
#else
    void* block = nullptr;
    return posix_memalign(&block, 32u, bytes != 0 ? bytes : 32u) == 0 ? block : nullptr;
#endif
}

extern "C" void NebulaMiiFree(void* block) {
#if defined(_WIN32)
    _aligned_free(block);
#else
    free(block);
#endif
}
