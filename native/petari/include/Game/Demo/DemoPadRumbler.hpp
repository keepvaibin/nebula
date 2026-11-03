#pragma once


class PadRumbleInfo {
public:
    s32 mStartFrame;
    const char* mName;
};

class DemoPadRumbler {
public:
    DemoPadRumbler(const char*);
    void update(s32);

    s32 mNumPadRumbleEntries;
    PadRumbleInfo* mPadRumbleEntries;
    s32 _8;
};
