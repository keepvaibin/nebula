#pragma once


class NebulaArena;
class NebulaArena;
class NebulaArena;

class HeapMemoryWatcher {
public:
    HeapMemoryWatcher();

    // What lasts as long as the game: the sound system's, the files every scene shares, saves.
    NebulaArena* getPermanentHeap() const {
        return mPermanent;
    }

    void createFileCacheHeapOnGameHeap();
    void createSceneHeapOnGameHeap();
    void setCurrentHeapToPermanent();
    void setCurrentHeapToGameHeap();
    void setCurrentHeapToSceneHeap();
    void destroySceneHeap();
    void destroyGameHeap();
    void createHeaps();
    void createGameHeap();

    NebulaArena* mPermanent;
    NebulaArena* mGameHeap;
    NebulaArena* mFileCacheHeap;
    NebulaArena* mSceneHeap;
};
