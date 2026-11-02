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
    GamePad* mPad;
    s32 mStep;
    bool mIsSuspend;
};
