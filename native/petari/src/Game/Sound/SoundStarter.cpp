#include "Game/Sound/SoundStarter.hpp"
#include "Game/Sound/SoundHandles.hpp"

SoundStarter::SoundStarter(bool setInstance) : SoundGlobal< SoundStarter >(setInstance) {
}

SoundStarter::~SoundStarter() {
}

bool SoundStarter::startLevelSound(SoundID id, SoundHandle* handlePtr, const Vec3f* posPtr) {
    SoundHandle* handlePtr2 = handlePtr;
    if (handlePtr2->isSoundAttached()) {
        if ((*handlePtr2)->getID() == id) {
            (*handlePtr2)->updateLifeTime(1);
            if (posPtr != nullptr) {
                (*handlePtr2)->setPos(*posPtr);
            }
            return true;
        }
    }

    bool rt = startSound(id, handlePtr, posPtr);
    if (rt && handlePtr2->isSoundAttached()) {
        (*handlePtr2)->setLifeTime(1, false);
    }
    return rt;
}
