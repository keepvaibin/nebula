#pragma once

#include "Game/System/NerveExecutor.hpp"

// Whether a dome's Prankster Comet is in orbit or hidden.
class GalaxyCometState : NerveExecutor {
public:
    // The saved state index.
    enum State {
        State_Hide = 0,
        State_Land = 1,
    };

    GalaxyCometState();

    void update();
    bool isLand() const;
    bool isHide() const;
    // Land to Hide, anything else to Land.
    void forceToNext();
    State getStateIndex() const;
    // No time-based state: always 0.
    s32 getPastSecond() const;
    // Unknown states are ignored; pastSecond is unused.
    void setStateAndPastSecond(s32 state, u16 pastSecond);

    void exeHide();
    void exeLand();

private:
    bool mIsUpdateNerve;
};
