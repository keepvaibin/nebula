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
    PlayerInput* mPad;
    s32 mCheckInfoFrame;
    s32 mBattery;
};
