#include "Game/Sound/SoundChild.hpp"
#include "Game/Sound/PlayerLink.hpp"

void SoundChild::init() {
    mMove.init();
    mParams.init();
}

void SoundChild::mixOut(PlayerLink* pLink, const TrackPath& rPath) {
    mParams = mMove.mParams;
    pLink->setTrackSound(rPath, &mParams);
}

void SoundChild::calc() {
    mMove.calc();
}
