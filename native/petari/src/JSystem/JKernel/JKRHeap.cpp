#include "JSystem/JKernel/JKRHeap.hpp"
#include "JSystem/JUtility/JUTException.hpp"
#include <revolution/os/OSBootInfo.h>

JKRHeap* JKRHeap::sSystemHeap;
JKRHeap* JKRHeap::sCurrentHeap;
JKRHeap* JKRHeap::sRootHeap;
JKRErrorHandler JKRHeap::mErrorHandler;

static bool byte_806B70B8;

void* JKRHeap::mCodeStart;
void* JKRHeap::mCodeEnd;
void* JKRHeap::mUserRamStart;
void* JKRHeap::mUserRamEnd;
u32 JKRHeap::mMemorySize;

static bool byte_806B26D8 = true;
// The DSP's ARAM-in-MEM2 base; natively the MEM2 arena address HeapMemoryWatcher sets before the DSP boots.
uintptr_t ARALT_AramStartAdr = 0x90000000;

#ifdef NEBULA_ASAN_HEAPS
// AddressSanitizer test builds: a heap's free space and block headers are poisoned and each block it hands out is
// unpoisoned, so the game's overflows and uses after free inside its own heaps are reported. The heaps themselves are
// built uninstrumented (CMakeLists.txt).
#include <sanitizer/asan_interface.h>

static void heapPoison(const void* p, s32 n) {
    if (p != nullptr && n > 0) {
        __asan_poison_memory_region(p, (size_t)n);
    }
}

static void heapUnpoison(const void* p, s32 n) {
    if (p != nullptr && n > 0) {
        __asan_unpoison_memory_region(p, (size_t)n);
    }
}
#endif

JKRHeap::JKRHeap(void* data, u32 size, JKRHeap* parent, bool error) : JKRDisposer(), mChildTree(this), mDisposerList() {
    OSInitMutex(&mMutex);
    mSize = size;
    mStart = (u8*)data;
    mEnd = (u8*)data + size;
#ifdef NEBULA_ASAN_HEAPS
    heapPoison(mStart, (s32)mSize);
#endif

    if (parent == nullptr) {
        JKRHeap::sSystemHeap = this;
        JKRHeap::sCurrentHeap = this;
    } else {
        parent->mChildTree.appendChild(&mChildTree);

        if (JKRHeap::sSystemHeap == JKRHeap::sRootHeap) {
            JKRHeap::sSystemHeap = this;
        }

        if (JKRHeap::sCurrentHeap == JKRHeap::sRootHeap) {
            JKRHeap::sCurrentHeap = this;
        }
    }

    mErrorFlag = error;

    if (mErrorFlag == true && mErrorHandler == nullptr) {
        mErrorHandler = JKRDefaultMemoryErrorRoutine;
    }

    _3C = byte_806B26D8;
    _3D = byte_806B70B8;
    _69 = false;
}

JKRHeap::~JKRHeap() {
    mChildTree.getParent()->removeChild(&mChildTree);
    JSUTree< JKRHeap >* nextRootHeap = sRootHeap->mChildTree.getFirstChild();

    if (sCurrentHeap == this)
        sCurrentHeap = !nextRootHeap ? sRootHeap : nextRootHeap->getObject();

    if (sSystemHeap == this)
        sSystemHeap = !nextRootHeap ? sRootHeap : nextRootHeap->getObject();
}

#ifdef TARGET_PC
u32 JKRNativeHeapSize(u32 size, JKRHeap* pParent, int alignment) {
    if (size == 0xFFFFFFFF || size > 0x7FFFFFFF) {
        return size;
    }
    const u32 room = pParent->getMaxAllocatableSize(alignment);
    return size * 2 <= room ? size * 2 : size;
}
#endif

bool JKRHeap::initArena(char** memory, u32* size, int maxHeaps) {
    void *ramStart, *ramEnd, *arenaStart;

    void* arenaLo = OSGetArenaLo();
    void* arenaHi = OSGetArenaHi();

    OSReport("original arenaLo = %p arenaHi = %p\n", arenaLo, arenaHi);

    if (arenaLo == arenaHi) {
        return false;
    }

    arenaStart = OSInitAlloc(arenaLo, arenaHi, maxHeaps);
    OSBootInfo* code = (OSBootInfo*)OSPhysicalToCached(0);
    ramStart = (void*)(((uintptr_t)arenaStart + 31) & ~(uintptr_t)31);
    ramEnd = (void*)((uintptr_t)arenaHi & ~(uintptr_t)31);

    JKRHeap::mCodeStart = code;
    JKRHeap::mCodeEnd = ramStart;
    JKRHeap::mUserRamStart = ramStart;
    JKRHeap::mUserRamEnd = ramEnd;
    JKRHeap::mMemorySize = code->memorySize;

    OSSetArenaLo(ramEnd);
    OSSetArenaHi(ramEnd);

    *memory = (char*)ramStart;
    *size = (u32)((uintptr_t)ramEnd - (uintptr_t)ramStart);
    return true;
}

JKRHeap* JKRHeap::becomeSystemHeap() {
    JKRHeap* sys = sSystemHeap;
    sSystemHeap = this;
    return sys;
}

JKRHeap* JKRHeap::becomeCurrentHeap() {
    JKRHeap* cur = sCurrentHeap;
    sCurrentHeap = this;
    return cur;
}

void JKRHeap::destroy(JKRHeap* pHeap) {
    pHeap->do_destroy();
}

void* JKRHeap::alloc(u32 size, int align, JKRHeap* pHeap) {
    if (pHeap != nullptr) {
        return pHeap->alloc(size, align);
    }

    if (JKRHeap::sCurrentHeap != nullptr) {
        return JKRHeap::sCurrentHeap->alloc(size, align);
    }

    return nullptr;
}

#ifdef TARGET_PC
// At least 16-byte aligned natively, as a native allocator's memory is: the compiler takes operator new's memory to be
// (aligned SSE stores zero new objects), and 8-byte members never straddle cache lines. Negative alignments allocate
// from a heap's tail.
static inline int nativeAlignment(int align) {
    if (align >= 0) {
        return align < 16 ? 16 : align;
    }
    return align > -16 ? -16 : align;
}

void* JKRHeap::alloc(u32 size, int align) {
#ifdef NEBULA_ASAN_HEAPS
    void* p = do_alloc(size, nativeAlignment(align));
    if (p != nullptr) {
        const s32 block = do_getSize(p);
        heapUnpoison(p, block > 0 ? block : (s32)size);
    }
    return p;
#else
    return do_alloc(size, nativeAlignment(align));
#endif
}
#else
void* JKRHeap::alloc(u32 size, int align) {
    return do_alloc(size, align);
}
#endif

void JKRHeap::free(void* pData, JKRHeap* pHeap) {
    if (!pHeap) {
        pHeap = findFromRoot(pData);

        if (!pHeap) {
            return;
        }
    }

#ifdef NEBULA_ASAN_HEAPS
    pHeap->free(pData);
#else
    pHeap->do_free(pData);
#endif
}

void JKRHeap::free(void* pData) {
#ifdef NEBULA_ASAN_HEAPS
    // A solid heap frees nothing block by block.
    const s32 block = getHeapType() != 'SLID' && pData != nullptr ? do_getSize(pData) : 0;
    do_free(pData);
    heapPoison(pData, block);
#else
    do_free(pData);
#endif
}

void JKRHeap::callAllDisposer() {
    while (mDisposerList.mHead != nullptr) {
        reinterpret_cast< JKRDisposer* >(mDisposerList.mHead->mData)->~JKRDisposer();
    }
}

void JKRHeap::freeAll() {
    do_freeAll();
#ifdef NEBULA_ASAN_HEAPS
    heapPoison(mStart, (s32)mSize);
#endif
}

void JKRHeap::freeTail() {
    do_freeTail();
}

s32 JKRHeap::resize(void* pData, u32 size) {
#ifdef NEBULA_ASAN_HEAPS
    const s32 before = do_getSize(pData);
    const s32 result = do_resize(pData, size);
    const s32 after = do_getSize(pData);
    if (after > 0) {
        heapUnpoison(pData, after);
        if (before > after) {
            heapPoison((u8*)pData + after, before - after);
        }
    }
    return result;
#else
    return do_resize(pData, size);
#endif
}

s32 JKRHeap::getFreeSize() {
    return do_getFreeSize();
}

void* JKRHeap::getMaxFreeBlock() {
    return do_getMaxFreeBlock();
}

s32 JKRHeap::getTotalFreeSize() {
    return do_getTotalFreeSize();
}

JKRHeap* JKRHeap::findFromRoot(void* pData) {
    JKRHeap* root = sRootHeap;

    if (root == nullptr) {
        return nullptr;
    }

    if ((void*)root->mStart <= pData && pData < (void*)root->mEnd) {
        return root->find(pData);
    }

    return root->findAllHeap(pData);
}

JKRHeap* JKRHeap::find(void* pData) const {
    if (mStart <= pData && pData < mEnd) {
        const JSUTree< JKRHeap >& tree = mChildTree;

        if (tree.getNumChildren() != 0) {
            for (JSUTreeIterator< JKRHeap > iterator(mChildTree.getFirstChild()); iterator != mChildTree.getEndChild(); ++iterator) {
                JKRHeap* result = iterator->find(pData);

                if (result) {
                    return result;
                }
            }
        }

        return const_cast< JKRHeap* >(this);
    }

    return nullptr;
}

JKRHeap* JKRHeap::findAllHeap(void* ptr) const {
    if (mChildTree.getNumChildren() != 0) {
        for (JSUTreeIterator< JKRHeap > iterator(mChildTree.getFirstChild()); iterator != mChildTree.getEndChild(); ++iterator) {
            JKRHeap* heap = iterator->findAllHeap(ptr);

            if (heap != nullptr) {
                return heap;
            }
        }
    }

    if (mStart <= ptr && ptr < mEnd) {
        return const_cast< JKRHeap* >(this);
    }

    return nullptr;
}

void JKRHeap::dispose_subroutine(uintptr_t start, uintptr_t end) {
    JSUListIterator< JKRDisposer > it(mDisposerList.getFirst());
    JSUListIterator< JKRDisposer > last_it;

    JSULink< JKRDisposer >* link;
    while ((link = it.mLink) != nullptr) {
        JKRDisposer* disp = link->getObject();

        if (reinterpret_cast< void* >(start) <= disp && disp < reinterpret_cast< void* >(end)) {
            disp->~JKRDisposer();

            if (last_it == nullptr) {
                it = mDisposerList.getFirst();
            } else {
                it = last_it;
                it++;
            }
        } else {
            last_it = it;
            it++;
        }
    }
}

bool JKRHeap::dispose(void* ptr, u32 size) {
    uintptr_t begin = (uintptr_t)ptr;
    uintptr_t end = (uintptr_t)ptr + size;
    dispose_subroutine(begin, end);
    return false;
}

void JKRHeap::dispose(void* begin, void* end) {
    dispose_subroutine((uintptr_t)begin, (uintptr_t)end);
}

void JKRHeap::dispose() {
    const JSUList< JKRDisposer >& list = mDisposerList;
    JSUListIterator< JKRDisposer > iterator;

    while (list.getFirst() != list.getEnd()) {
        iterator = list.getFirst();
        iterator->~JKRDisposer();
    }
}

void JKRHeap::copyMemory(void* pDst, void* pSrc, u32 size) {
    u32 count = (size + 3) / 4;
    u32* dst_32 = (u32*)pDst;
    u32* src_32 = (u32*)pSrc;

    while (count > 0) {
        *dst_32 = *src_32;
        dst_32++;
        src_32++;
        count--;
    }
}

void JKRDefaultMemoryErrorRoutine(void* pHeap, u32 size, int alignment) {
    JUTException::panic(__FILE__, 0x355, "abort\n");
}

JKRErrorHandler JKRHeap::setErrorHandler(JKRErrorHandler errorHandler) {
    JKRErrorHandler prev = JKRHeap::mErrorHandler;

    if (!errorHandler) {
        errorHandler = JKRDefaultMemoryErrorRoutine;
    }

    JKRHeap::mErrorHandler = errorHandler;
    return prev;
}

void* operator new(JKRNewSize size) {
    return JKRHeap::alloc(size, 4, nullptr);
}

void* operator new(JKRNewSize size, int align) {
    return JKRHeap::alloc(size, align, nullptr);
}

void* operator new(JKRNewSize size, JKRHeap* pHeap, int align) {
    return JKRHeap::alloc(size, align, pHeap);
}

void* operator new[](JKRNewSize size) {
    return JKRHeap::alloc(size, 4, nullptr);
}

void* operator new[](JKRNewSize size, int align) {
    return JKRHeap::alloc(size, align, nullptr);
}

void* operator new[](JKRNewSize size, JKRHeap* pHeap, int align) {
    return JKRHeap::alloc(size, align, pHeap);
}

void operator delete(void* pData) {
    JKRHeap::free(pData, nullptr);
}

void operator delete[](void* pData) {
    JKRHeap::free(pData, nullptr);
}

void JKRHeap::state_register(TState*, u32) const {
    return;
}

bool JKRHeap::state_compare(const TState& lhs, const TState& rhs) const {
    return lhs.mCheckCode == rhs.mCheckCode;
}

void JKRHeap::state_dump(const TState&) const {
    return;
}

void JKRHeap::setAltAramStartAdr(uintptr_t addr) {
    ARALT_AramStartAdr = addr;
}

uintptr_t JKRHeap::getAltAramStartAdr() {
    return ARALT_AramStartAdr;
}

s32 JKRHeap::do_changeGroupID(u8) {
    return 0;
}

u8 JKRHeap::do_getCurrentGroupId() {
    return 0;
}

bool JKRHeap::dump_sort() {
    return true;
}
