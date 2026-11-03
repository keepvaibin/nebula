#pragma once

#include "Game/System/GameThreads.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/System/NebulaArena.hpp"
#include "JSystem/JSupport/JSUList.hpp"

class NebulaArena;
class NebulaArena;

extern NebulaArena* JASDram;

class JASGenericMemPool {
public:
    JASGenericMemPool();
    ~JASGenericMemPool();

    void newMemPool(u32, int);
    void* alloc(u32);

    void free(void*, u32);

    u32 getFreeMemCount() const {
        return mFreeMemCount;
    }

    u32 getTotalMemCount() const {
        return mTotalMemCount;
    }

    u32 getUsedMemCount() const {
        return mUsedMemCount;
    }

    void* _0;
    u32 mFreeMemCount;
    u32 mTotalMemCount;
    u32 mUsedMemCount;
};

namespace JASThreadingModel {
    template < typename T >
    class AudioExcluded {
    public:
        struct Lock {
            Lock(const T&) {
                mEnabled = GameThreads::excludeAudio();
            }
            ~Lock() {
                GameThreads::restoreAudio(mEnabled);
            }

            u32 mEnabled;
        };
    };

    template < typename T >
    struct ObjectLevelLockable : public GameMutex {
        ObjectLevelLockable() {
            this->init();
        }

        struct Lock {
            Lock(T const& mutex) {
                mMutex = (T*)&mutex;
                mMutex->lock();
            }

            ~Lock() {
                mMutex->unlock();
            }

            T* mMutex;
        };
    };

    template < typename T >
    struct SingleThreaded {
        struct Lock {
            Lock(const T&) {
            }
        };
    };
}  // namespace JASThreadingModel

template < typename T >
class JASMemPool : public JASGenericMemPool {
public:
    void newMemPool(int n) {
        typename JASThreadingModel::SingleThreaded< JASMemPool< T > >::Lock lock(*this);
        JASGenericMemPool::newMemPool(sizeof(T), n);
    }

    void* alloc(u32 n) {
        typename JASThreadingModel::SingleThreaded< JASMemPool< T > >::Lock lock(*this);
        return JASGenericMemPool::alloc(n);
    }

    void free(void* ptr, u32 n) {
        typename JASThreadingModel::SingleThreaded< JASMemPool< T > >::Lock lock(*this);
        JASGenericMemPool::free(ptr, n);
    }

    u32 getFreeMemCount() const {
        typename JASThreadingModel::SingleThreaded< JASMemPool< T > >::Lock lock(*this);
        return JASGenericMemPool::getFreeMemCount();
    }

    u32 getTotalMemCount() const {
        typename JASThreadingModel::SingleThreaded< JASMemPool< T > >::Lock lock(*this);
        return JASGenericMemPool::getTotalMemCount();
    }
};

namespace JASKernel {
    NebulaArena* getSystemHeap();
};  // namespace JASKernel

template < u32 ChunkSize, template < typename > class T >
class JASMemChunkPool : public T< JASMemChunkPool< ChunkSize, T > >::ObjectLevelLockable {
public:
    struct MemoryChunk {
        MemoryChunk(MemoryChunk* nextChunk) {
            mNextChunk = nextChunk;
            mUsedSize = 0;
            mChunks = 0;
        }

        bool checkArea(const void* ptr) const {
            return mBuffer <= (const u8*)ptr && (const u8*)ptr < mBuffer + ChunkSize;
        }

        MemoryChunk* getNextChunk() {
            return mNextChunk;
        }

        void* alloc(u32 size) {
            u8* rv = mBuffer + mUsedSize;
            mUsedSize += size;
            mChunks++;
            return rv;
        }

        void free(void* mem) {
            mChunks--;
        }

        bool isEmpty() const {
            return mChunks == 0;
        }

        void setNextChunk(MemoryChunk* chunk) {
            mNextChunk = chunk;
        }

        u32 getFreeSize() const {
            return ChunkSize - mUsedSize;
        }

        void revive() {
            mUsedSize = 0;
        }

        MemoryChunk* mNextChunk;
        u32 mUsedSize;
        u32 mChunks;
        u8 mBuffer[ChunkSize];
    };

    JASMemChunkPool() {
        mChunk = nullptr;
        createNewChunk();
    }

    bool createNewChunk() {
        if (mChunk != nullptr && mChunk->isEmpty()) {
            mChunk->revive();
            return true;
        }
        MemoryChunk* pMVar4 = mChunk;
        mChunk = new (JASKernel::getSystemHeap(), 0) MemoryChunk(pMVar4);
        if (mChunk != nullptr) {
            return true;
        }

        mChunk = new (NebulaArena::getSystemHeap(), 0) MemoryChunk(pMVar4);
        if (mChunk != nullptr) {
            return true;
        }

        mChunk = pMVar4;
        return false;
    }

    void* alloc(u32 size) {
        typename T< JASMemChunkPool< ChunkSize, T > >::Lock lock(*this);
        u32 freeSize = mChunk->getFreeSize();
        if (freeSize < size) {
            if (ChunkSize < size) {
                return nullptr;
            }
            if (createNewChunk() == 0) {
                return nullptr;
            }
        }
        return mChunk->alloc(size);
    }

    void free(void* ptr) {
        typename T< JASMemChunkPool< ChunkSize, T > >::Lock lock(*this);
        MemoryChunk* chunk = mChunk;
        MemoryChunk* prevChunk = nullptr;
        while (chunk != nullptr) {
            if (chunk->checkArea(ptr)) {
                chunk->free(ptr);
                if (chunk != mChunk && chunk->isEmpty()) {
                    MemoryChunk* nextChunk = chunk->getNextChunk();
                    delete chunk;
                    prevChunk->setNextChunk(nextChunk);
                }
                return;
            }
            prevChunk = chunk;
            chunk = chunk->getNextChunk();
        }
    }

    MemoryChunk* mChunk;
};

namespace JASKernel {
    void setupRootHeap(NebulaArena*, u32);
    NebulaArena* getSystemHeap();
    JASMemChunkPool< 1024, JASThreadingModel::ObjectLevelLockable >* getCommandHeap();
    extern NebulaArena* sSystemHeap;
    extern JASMemChunkPool< 1024, JASThreadingModel::ObjectLevelLockable >* sCommandHeap;
};  // namespace JASKernel

template < typename T >
class JASPoolAllocObject {
public:
    static void* operator new(size_t size) {
        return memPool_.alloc(size);
    }

    static void operator delete(void* addr, size_t size) {
        memPool_.free(addr, size);
    }

    static void newMemPool(int n) {
        memPool_.newMemPool(n);
    }

    static JASMemPool< T > memPool_;
};

template < typename T >
JASMemPool< T > JASPoolAllocObject< T >::memPool_;

template < typename T >
class JASMemPool_MultiThreaded : public JASGenericMemPool {
public:
    void newMemPool(int n) {
        typename JASThreadingModel::AudioExcluded< JASMemPool_MultiThreaded< T > >::Lock lock(*this);
        JASGenericMemPool::newMemPool(sizeof(T), n);
    }

    void* alloc(u32 size) {
        typename JASThreadingModel::AudioExcluded< JASMemPool_MultiThreaded >::Lock lock(*this);
        return JASGenericMemPool::alloc(size);
    }
    void free(void* addr, u32 size) {
        typename JASThreadingModel::AudioExcluded< JASMemPool_MultiThreaded >::Lock lock(*this);
        JASGenericMemPool::free(addr, size);
    }
};

template < typename T >
class JASPoolAllocObject_MultiThreaded {
public:
    static void* operator new(size_t size) {
        return memPool_.alloc(size);
    }

    static void operator delete(void* addr, size_t size) {
        memPool_.free(addr, size);
    }

    static void newMemPool(int n) {
        memPool_.newMemPool(n);
    }

    static JASMemPool_MultiThreaded< T > memPool_;
};

template < typename T >
JASMemPool_MultiThreaded< T > JASPoolAllocObject_MultiThreaded< T >::memPool_;
