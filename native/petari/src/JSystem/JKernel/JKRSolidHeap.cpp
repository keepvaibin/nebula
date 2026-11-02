#include "JSystem/JKernel/JKRSolidHeap.hpp"
#include "JSystem/JUtility/JUTNativeLog.hpp"
#include <new>
#include <stdint.h>

JKRSolidHeap* JKRSolidHeap::create(u32 size, JKRHeap* heap, bool useErrorHandler) {
    if (!heap) {
        heap = sRootHeap;
    }
    u32 solidHeapSize = ALIGN_NEXT(sizeof(JKRSolidHeap), 0x10);

    if (size == -1) {
        size = heap->getMaxAllocatableSize(0x10);
    }
    else {
        size = JKRNativeHeapSize(size, heap, 0x10);
    }

    u32 alignedSize = ALIGN_PREV(size, 0x10);
    if (alignedSize < solidHeapSize)
        return NULL;

    u8* mem = (u8*)JKRAllocFromHeap(heap, alignedSize, 0x10);
    void* dataPtr = mem + solidHeapSize;
    if (!mem)
        return NULL;

    return new (mem) JKRSolidHeap(dataPtr, alignedSize - solidHeapSize, heap, useErrorHandler);
}

void JKRSolidHeap::do_destroy(void) {
    JKRHeap* parent = getParent();
    if (parent) {
        this->~JKRSolidHeap();
        JKRFreeToHeap(parent, this);
    }
}

JKRSolidHeap::JKRSolidHeap(void* start, u32 size, JKRHeap* parent, bool useErrorHandler) : JKRHeap(start, size, parent, useErrorHandler) {
    mFreeSize = mSize;
    mSolidHead = (u8*)mStart;
    mSolidTail = (u8*)mEnd;
    field_0x78 = NULL;
}

JKRSolidHeap::~JKRSolidHeap(void) {
    dispose();
}

void* JKRSolidHeap::do_alloc(u32 size, int alignment) {
    lock();

    if (size < 4) {
        size = 4;
    }

    void* ptr;
    if (alignment >= 0) {
        ptr = allocFromHead(size, alignment < 4 ? 4 : alignment);
    } else {
        ptr = allocFromTail(size, -alignment < 4 ? 4 : -alignment);
    }

    unlock();
    return ptr;
}

void* JKRSolidHeap::allocFromHead(u32 size, int alignment) {
    size = ALIGN_NEXT(size, sizeof(void*));
    void* ptr = NULL;
    uintptr_t alignedStart = (alignment - 1 + (uintptr_t)mSolidHead) & ~(alignment - 1);
    u32 totalSize = size + (alignedStart - (uintptr_t)mSolidHead);
    if (totalSize <= mFreeSize) {
        ptr = (void*)alignedStart;
        mSolidHead += totalSize;
        mFreeSize -= totalSize;
    } else {
        JUTNativeLog("allocFromHead: cannot alloc memory (0x%x byte).\n", totalSize);
        if (getErrorFlag() == true) {
            callErrorHandler(this, size, alignment);
        }
    }

    return ptr;
}

void* JKRSolidHeap::allocFromTail(u32 size, int alignment) {
    size = ALIGN_NEXT(size, sizeof(void*));
    void* ptr = NULL;
    uintptr_t alignedStart = ALIGN_PREV((uintptr_t)mSolidTail - size, alignment);
    u32 totalSize = (uintptr_t)mSolidTail - (uintptr_t)alignedStart;
    if (totalSize <= mFreeSize) {
        ptr = (void*)alignedStart;
        mSolidTail -= totalSize;
        mFreeSize -= totalSize;
    } else {
        JUTNativeLog("allocFromTail: cannot alloc memory (0x%x byte).\n", totalSize);
        if (getErrorFlag() == true) {
            callErrorHandler(this, size, alignment);
        }
    }
    return ptr;
}

static void reportFreeBlock(void* ptr) {
    JUTNativeLog("free: cannot free memory block (%p)\n", ptr);
}

void JKRSolidHeap::do_freeAll(void) {
    lock();

    this->JKRHeap::callAllDisposer();
    mFreeSize = mSize;
    mSolidHead = (u8*)mStart;
    mSolidTail = (u8*)mEnd;
    field_0x78 = NULL;
    unlock();
}

void JKRSolidHeap::do_freeTail(void) {
    lock();

    if (mSolidTail != mEnd) {
        dispose(mSolidTail, mEnd);
    }

    this->mFreeSize = ((uintptr_t)mEnd - (uintptr_t)mSolidTail + mFreeSize);
    this->mSolidTail = mEnd;

    for (JKRSolidHeap::Unknown* unknown = field_0x78; unknown; unknown = unknown->mNext) {
        unknown->field_0xc = mEnd;
    }

    unlock();
}

void JKRSolidHeap::do_fillFreeArea() {
}

s32 JKRSolidHeap::do_resize(void* ptr, u32 newSize) {
    JUTNativeLog("resize: cannot resize memory block (%p: %d)\n", ptr, newSize);
    return -1;
}

s32 JKRSolidHeap::do_getSize(void* ptr) {
    JUTNativeLog("getSize: cannot get memory block size (%p)\n", ptr);
    return -1;
}

bool JKRSolidHeap::check(void) {
    lock();

    bool result = true;
    u32 calculatedSize = (mSolidHead - mStart) + mFreeSize + (mEnd - mSolidTail);
    if (calculatedSize != mSize) {
        result = false;
        JUTNativeLog("check: bad total memory block size (%08X, %08X)\n", mSize, calculatedSize);
    }

    unlock();
    return result;
}

bool JKRSolidHeap::dump(void) {
    bool result = check();

    lock();
    s32 htSize = (mSolidHead - mStart) + (mEnd - mSolidTail);
    JUTNativeLog("head %p: %08llx\n", mStart, (mSolidHead - mStart));
    JUTNativeLog("tail %p: %08llx\n", mSolidTail, (mEnd - mSolidTail));
    JUTNativeLog("%d / %d bytes (%6.2f%%) used\n", htSize, mSize, f32(htSize) / f32(mSize) * 100.0f);
    unlock();

    return result;
}

void JKRSolidHeap::state_register(JKRHeap::TState* p, u32 id) const {
    void* r28 = getState_(p);
    setState_u32ID_(p, id);
    setState_uUsedSize_(p, getUsedSize((JKRSolidHeap*)this));
    u32 r29 = (uintptr_t)mSolidHead;
    r29 += (uintptr_t)mSolidTail * 3;
    setState_u32CheckCode_(p, r29);
}

bool JKRSolidHeap::state_compare(JKRHeap::TState const& r1, JKRHeap::TState const& r2) const {
    bool result = true;
    if (r1.getCheckCode() != r2.getCheckCode()) {
        result = false;
    }

    if (r1.getUsedSize() != r2.getUsedSize()) {
        result = false;
    }

    return result;
}

s32 JKRSolidHeap::do_getTotalFreeSize(void) {
    return getFreeSize();
}

void* JKRSolidHeap::do_getMaxFreeBlock(void) {
    return mSolidHead;
}

s32 JKRSolidHeap::do_getFreeSize(void) {
    return mFreeSize;
}

u32 JKRSolidHeap::getHeapType(void) {
    return 'SLID';
}
