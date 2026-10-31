#pragma once

#include <revolution/types.h>

class WPad;

class WPadInfoChecker {
public:
    WPadInfoChecker(WPad*);

    s32 getBattery() const;
    void startCheck() NO_INLINE;
    void reset();
    void update();

private:
    /* 0x00 */ WPad* mPad;
    /* 0x04 */ s32 mCheckInfoFrame;
    /* 0x08 */ s32 mBattery;
};
