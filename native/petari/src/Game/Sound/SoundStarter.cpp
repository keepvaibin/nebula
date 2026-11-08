#include "Game/Sound/SoundStarter.hpp"
#include "Game/Sound/SoundHandles.hpp"

SoundStarter::SoundStarter(bool setInstance) : SoundGlobal< SoundStarter >(setInstance) {
}

SoundStarter::~SoundStarter() {
}

// A level sound lives one frame at a time: the same sound on the handle is kept for one more.
bool SoundStarter::startLevelSound(SoundID id, SoundHandle* handlePtr, const Vec3f* posPtr) {
    if (handlePtr->isSoundAttached() && (*handlePtr)->getID() == id) {
        (*handlePtr)->updateLifeTime(1);
        if (posPtr != nullptr) {
            (*handlePtr)->setPos(*posPtr);
        }
        return true;
    }

    const bool isStarted = startSound(id, handlePtr, posPtr);
    if (isStarted && handlePtr->isSoundAttached()) {
        (*handlePtr)->setLifeTime(1, false);
    }
    return isStarted;
}
