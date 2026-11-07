#pragma once

#include "Game/Sound/Sound.hpp"
#include <nebula_geometry.hpp>

class SoundHandles {
public:
    SoundHandles(SoundHandle* handles, int handleNum) {
        mHandles = handles;
        mNumHandles = handleNum;
    };

    SoundHandle& operator[](int n) {
        return mHandles[n];
    }

    SoundHandle* getHandle(int n) {
        return &mHandles[n];
    }

    Sound* getSound(int n) {
        return getHandle(n)->getSound();
    }

    int getNumHandles() const {
        return mNumHandles;
    }

    SoundHandle* getHandleSoundID(SoundID);
    SoundHandle* getHandleUserData(uintptr_t);

    void setPos(const Vec3f&);

    SoundHandle* mHandles;
    int mNumHandles;
};
