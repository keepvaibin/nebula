#pragma once

struct RumblePattern {
    const char* mName;
    s32 mFrame;
    u8 mPattern[180];  // RUMBLE_ON / RUMBLE_OFF per frame, mFrame of them
    u32 mHash;         // MR::getHashCode(mName)
};

namespace RumbleData {
    // The pattern of that name, or nullptr.
    const RumblePattern* getPattern(const char*);
    void initHashValue();
};  // namespace RumbleData
