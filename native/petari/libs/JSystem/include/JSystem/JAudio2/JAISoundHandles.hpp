#pragma once

#include "JSystem/JAudio2/JAISound.hpp"
#include "JSystem/JGeometry/TVec.hpp"

struct JAISeqDataUser;

class JAISoundHandles {
public:
    JAISoundHandles(JAISoundHandle* handles, int handleNum) {
        mHandles = handles;
        mNumHandles = handleNum;
    };

    JAISoundHandle& operator[](int n) {
        return mHandles[n];
    }

    JAISoundHandle* getHandle(int n) {
        return &mHandles[n];
    }

    JAISound* getSound(int n) {
        return getHandle(n)->getSound();
    }

    int getNumHandles() const {
        return mNumHandles;
    }

    JAISoundHandle* getHandleSoundID(JAISoundID);
    JAISoundHandle* getHandleUserData(uintptr_t);

    void setPos(const TVec3f&);

    JAISoundHandle* mHandles;
    int mNumHandles;
};
