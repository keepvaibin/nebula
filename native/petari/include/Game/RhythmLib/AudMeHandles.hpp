#pragma once

#include "Game/RhythmLib/AudMePlayer.hpp"
#include "Game/Sound/SoundHandles.hpp"

class AudMe;

class AudMeHandle {
public:
    AudMeHandle() : mMe() {};

    ~AudMeHandle() {
        releaseMe();
    }

    void releaseMe();

    bool isMeAttached() const {
        return mMe != nullptr;
    }

    AudMe* getMe() {
        return mMe;
    }

    AudMe* operator->() const {
        return mMe;
    }

    AudMe* mMe;
};

class AudMeHandles {
public:
    AudMeHandles(AudMeHandle*, int);

    AudMeHandle* getHandleMeID(u32);
    AudMeHandle* getFreeHandle();

    AudMeHandle* getHandle(int index) {
        return &mHandles[index];
    }

    AudMe* getMe(int n) {
        return getHandle(n)->getMe();
    }

    int getNumHandles() const {
        return mNumHandles;
    }

    AudMeHandle* mHandles;
    int mNumHandles;
};
