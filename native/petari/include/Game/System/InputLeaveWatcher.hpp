#pragma once


class PlayerInput;

class InputLeaveWatcher {
public:
    InputLeaveWatcher(PlayerInput*);

    void update();
    void start();
    void stop();
    void restart();

private:
    /* 0x00 */ PlayerInput* mPad;
    /* 0x04 */ s32 mStep;
    /* 0x08 */ bool mIsSuspend;
};
