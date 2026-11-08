#pragma once

struct RumblePattern {
    const char* mName;
    s32 mFrame;
    u8 mPattern[180];  // RUMBLE_ON / RUMBLE_OFF per frame, mFrame of them
};

namespace RumbleData {
    // The pattern whose name has this name's MR::getHashCode, or nullptr.
    const RumblePattern* getPattern(const char*);
};  // namespace RumbleData
