#pragma once


class JKRExpHeap;
class JKRHeap;
class JKRSolidHeap;

class HeapMemoryWatcher {
public:
    HeapMemoryWatcher();

    JKRSolidHeap* getAudSystemHeap() const {
        return mAudSystemHeap;
    }

    void createFileCacheHeapOnGameHeap(u32);
    void createSceneHeapOnGameHeap();
    void adjustStationedHeaps();
    void setCurrentHeapToStationedHeap();
    void setCurrentHeapToGameHeap();
    void setCurrentHeapToSceneHeap();
    void destroySceneHeap();
    void destroyGameHeap();
    void createHeaps();
    void createGameHeap();
    static void memoryErrorCallback(void*, u32, int);
    void checkRestMemory();

    // used in GameSystemObjHolder::initAudio()
    JKRSolidHeap* getAudSystemHeap() {
        return mAudSystemHeap;
    }

    void* mSoundMemory;  // the root of the wave archives' places in their sections (JASKernel::setupSoundHeap)
    JKRExpHeap* mStationedHeap;
    JKRExpHeap* mGameHeap;
    JKRHeap* mFileCacheHeap;
    JKRSolidHeap* mSceneHeap;
    JKRExpHeap* mHomeButtonLayoutHeap;
    JKRSolidHeap* mAudSystemHeap;
};
