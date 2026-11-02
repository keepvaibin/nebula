#pragma once


struct RumblePattern {
    const char* mName;
    s32 mFrame;
    u8 mPattern[180];
    u32 mHash;
};

namespace RumbleData {
    const RumblePattern* getPattern(const char*);
    void initHashValue();
    void checkHashCollision();
    u32 getTableSize();
    RumblePattern* getData(u16);
};  // namespace RumbleData
