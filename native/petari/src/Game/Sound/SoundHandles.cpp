#include "Game/Sound/SoundHandles.hpp"
#include "Game/Sound/Sound.hpp"

SoundHandle* SoundHandles::getHandleSoundID(SoundID id) {
    for (s32 i = 0; i < mNumHandles; i++) {
        if (mHandles[i].isSoundAttached() && mHandles[i]->mSoundID == id) {
            return &mHandles[i];
        }
    }

    return nullptr;
}

SoundHandle* SoundHandles::getHandleUserData(uintptr_t addr) {
    for (s32 i = 0; i < mNumHandles; i++) {
        if (mHandles[i].isSoundAttached() && mHandles[i]->getUserData() == addr) {
            return &mHandles[i];
        }
    }

    return nullptr;
}

void SoundHandles::setPos(const Vec3f& pos) {
    for (s32 i = 0; i < mNumHandles; i++) {
        if (mHandles[i].isSoundAttached()) {
            mHandles[i].getSound()->setPos(pos);
        }
    }
}
