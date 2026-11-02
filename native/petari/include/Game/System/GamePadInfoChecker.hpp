#pragma once


class GamePad;

class GamePadInfoChecker {
public:
    GamePadInfoChecker(GamePad*);

    s32 getBattery() const;
    void startCheck();
    void reset();
    void update();

private:
    /* 0x00 */ GamePad* mPad;
    /* 0x04 */ s32 mCheckInfoFrame;
    /* 0x08 */ s32 mBattery;
};
