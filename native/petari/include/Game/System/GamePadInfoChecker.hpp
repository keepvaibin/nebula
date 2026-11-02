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
    GamePad* mPad;
    s32 mCheckInfoFrame;
    s32 mBattery;
};
