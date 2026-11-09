#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CinemaFrame : public LayoutActor {
public:
    CinemaFrame(bool param1);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;

    void tryScreenToFrame();
    void tryFrameToBlank();
    void tryBlankToFrame();
    void tryFrameToScreen();
    void forceToScreen();
    void forceToFrame();
    void forceToBlank();
    bool isStop() const;
    void exeScreen();
    void exeFrame();
    void exeBlank();
    void exeScreenToFrame();
    void exeFrameToBlank();
    void exeBlankToFrame();
    void exeFrameToScreen();

private:
    f32 transitionDone();
    void startTransition(const char* pAnimName);

    // Where the next transition's animation starts, 0 to 1 of its length: a transition turned back part way starts the
    // other way from the bars' place, never from its own beginning.
    f32 mStartAt;
};
