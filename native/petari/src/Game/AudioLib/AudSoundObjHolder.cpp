#include "Game/AudioLib/AudSoundObjHolder.hpp"

#include "Game/AudioLib/AudSoundObject.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/Array.hpp"

AudSoundObjHolder::AudSoundObjHolder(NebulaArena* pArena, s32 capacity)
    : mArray(new (pArena, 0) AudSoundObject*[capacity]), mCapacity(capacity), mSize(0) {
}

void AudSoundObjHolder::update() {
    for (s32 i = 0; i < mSize; i++) {
        mArray[i]->clearMapCode();
    }
}

void AudSoundObjHolder::add(AudSoundObject* pSound) {
    if (mSize == mCapacity) {
        mArray = MR::growArray(mArray, mCapacity);
        mCapacity *= 2;
    }

    mArray[mSize] = pSound;
    mSize++;
}

// Keeps the others in order; the last match goes.
void AudSoundObjHolder::remove(AudSoundObject* pSound) {
    for (s32 i = mSize - 1; i >= 0; i--) {
        if (mArray[i] == pSound) {
            for (s32 j = i; j < mSize - 1; j++) {
                mArray[j] = mArray[j + 1];
            }
            mSize--;
            return;
        }
    }
}
