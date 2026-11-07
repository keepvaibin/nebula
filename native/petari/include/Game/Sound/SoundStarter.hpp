#pragma once

#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundGlobal.hpp"
#include <nebula_geometry.hpp>

struct SoundStarter : public SoundGlobal< SoundStarter > {
    SoundStarter(bool setInstance);

    virtual ~SoundStarter();
    virtual bool startSound(SoundID id, SoundHandle* handlePtr, const Vec3f* posPtr) = 0;
    bool startLevelSound(SoundID id, SoundHandle* handlePtr, const Vec3f* posPtr);
};
