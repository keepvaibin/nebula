#pragma once

#include "Game/Sound/Sound.hpp"

struct StreamSource {
    virtual s32 getStreamFileEntry(SoundID) = 0;
    virtual ~StreamSource();
};
