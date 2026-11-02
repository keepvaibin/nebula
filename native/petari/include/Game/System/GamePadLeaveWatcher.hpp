#pragma once


class GamePad;

class GamePadLeaveWatcher {
public:
    GamePadLeaveWatcher(GamePad*);

    void update();
    void start();
    void stop();
    void restart();

private:
    /* 0x00 */ GamePad* mPad;
    /* 0x04 */ s32 mStep;
    /* 0x08 */ bool mIsSuspend;
};
