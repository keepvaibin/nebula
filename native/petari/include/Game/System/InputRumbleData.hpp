#pragma once

struct RumblePattern {
    const char* mName;
    s32 mFrame;  // steps (the game's 60 updates a second)
    const u8* mPattern;  // RUMBLE_ON / RUMBLE_OFF per step, mFrame of them
};

namespace RumbleData {
    // The pattern whose name has this name's MR::getHashCode, or nullptr.
    const RumblePattern* getPattern(const char*);
};  // namespace RumbleData
