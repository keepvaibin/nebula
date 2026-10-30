#include <RFLi_Heap.h>

#include <JSystem/JKernel/JKRExpHeap.hpp>

// A NebulaMiiHeap is a JKRExpHeap: created without the out-of-memory handler, so a full heap returns NULL to the Mii
// library as the SDK's heaps did.

// The buffer's own heap is the parent; it may be one outside the root heap's range (the MEM2 heaps), which only the
// whole tree's search finds.
extern "C" NebulaMiiHeap* NebulaMiiHeapCreate(void* buffer, u32 size) {
    return reinterpret_cast< NebulaMiiHeap* >(JKRExpHeap::create(buffer, size, JKRHeap::findFromRoot(buffer), false));
}

extern "C" void NebulaMiiHeapDestroy(NebulaMiiHeap* heap) {
    if (heap != nullptr) {
        JKRHeap::destroy(reinterpret_cast< JKRExpHeap* >(heap));
    }
}

extern "C" void* NebulaMiiHeapAlloc(NebulaMiiHeap* heap, u32 size, s32 alignment) {
    return reinterpret_cast< JKRExpHeap* >(heap)->alloc(size, alignment);
}

extern "C" void NebulaMiiHeapFree(NebulaMiiHeap* heap, void* block) {
    reinterpret_cast< JKRExpHeap* >(heap)->free(block);
}

// The largest free block less what aligning its start may take: JKR heaps align every block to at least 16 bytes
// natively, so a request this size succeeds at any alignment up to 16 (the SDK's heaps reported the same, for their
// alignment).
extern "C" u32 NebulaMiiHeapLargestFree(NebulaMiiHeap* heap) {
    const s32 largest = reinterpret_cast< JKRExpHeap* >(heap)->getFreeSize();
    return largest > 16 ? static_cast< u32 >(largest - 16) & ~3u : 0;
}
