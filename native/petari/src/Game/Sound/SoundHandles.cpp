#include "Game/Sound/SoundHandles.hpp"
#include "Game/Sound/Sound.hpp"

SoundHandle* SoundHandles::getHandleSoundID(SoundID id) {
    for (int i = 0; i < mNumHandles; i++) {
        if (mHandles[i].isSoundAttached()) {
            if (mHandles[i]->mSoundID == id) {
                return &mHandles[i];
            }
        }
    }

    return nullptr;
}

SoundHandle* SoundHandles::getHandleUserData(uintptr_t addr) {
    for (int i = 0; i < mNumHandles; i++) {
        if (mHandles[i].isSoundAttached()) {
            if (mHandles[i]->getUserData() == addr) {
                return &mHandles[i];
            }
        }
    }

    return nullptr;
}

void SoundHandles::setPos(const Vec3f& pos) {
    for (int i = 0; i < mNumHandles; i++) {
        if (mHandles[i].isSoundAttached()) {
            mHandles[i].getSound()->setPos(pos);
        }
    }
}
