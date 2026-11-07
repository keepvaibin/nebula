#pragma once

#include "Game/Sound/SoundControls.hpp"
#include "Game/Sound/SoundEngine.hpp"

class PlayerLink;

// One of a sound's tracks or channels, moved on its own.
struct SoundChild {
    void init();
    // A sequence's numbered track: its values each frame (the audio side keeps them only that long).
    void mixOut(PlayerLink* pLink, const TrackPath& rPath);
    void calc();

    SoundChild() {
        init();
    }

    SoundMove* getMoveParams() {
        return &mMove;
    }

    SoundMove mMove;
    SoundParams mParams;
};
