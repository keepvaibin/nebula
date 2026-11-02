#pragma once


class PlayerInput;

class InputInfoChecker {
public:
    InputInfoChecker(PlayerInput*);

    s32 getBattery() const;
    void startCheck();
    void reset();
    void update();

private:
    /* 0x00 */ PlayerInput* mPad;
    /* 0x04 */ s32 mCheckInfoFrame;
    /* 0x08 */ s32 mBattery;
};
