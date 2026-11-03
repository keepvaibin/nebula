#pragma once

#include "Game/Screen/LayoutActor.hpp"

class TalkTextFormer;

class MovieSubtitles : public LayoutActor {
public:
    MovieSubtitles(const char*, s32);

    virtual void appear();

    void exeWait();
    void exeTalk();
    void exeTalkWait();

private:
    TalkTextFormer* mFormerText;
    s32 mAppearTime;
};
