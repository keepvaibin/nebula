#pragma once


class NebulaArena;
class NebulaArena;
class NebulaArena;

class HeapMemoryWatcher {
public:
    HeapMemoryWatcher();

    NebulaArena* getAudSystemHeap() const {
        return mAudSystemHeap;
    }

    void createFileCacheHeapOnGameHeap();
    void createSceneHeapOnGameHeap();
    void adjustStationedHeaps();
    void setCurrentHeapToStationedHeap();
    void setCurrentHeapToGameHeap();
    void setCurrentHeapToSceneHeap();
    void destroySceneHeap();
    void destroyGameHeap();
    void createHeaps();
    void createGameHeap();
    void checkRestMemory();

    // used in GameSystemObjHolder::initAudio()
    NebulaArena* getAudSystemHeap() {
        return mAudSystemHeap;
    }

    NebulaArena* mStationedHeap;
    NebulaArena* mGameHeap;
    NebulaArena* mFileCacheHeap;
    NebulaArena* mSceneHeap;
    NebulaArena* mAudSystemHeap;
};
