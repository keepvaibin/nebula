#include "Game/System/GalaxyCometState.hpp"
#include "Game/LiveActor/Nerve.hpp"

namespace {
    NEW_NERVE(GalaxyCometStateHide, GalaxyCometState, Hide);
    NEW_NERVE(GalaxyCometStateLand, GalaxyCometState, Land);
};  // namespace

GalaxyCometState::GalaxyCometState() : NerveExecutor("GalaxyCometState"), mIsUpdateNerve(true) {
    initNerve(GET_NERVE_ANON(GalaxyCometStateLand));
}

void GalaxyCometState::update() {
    if (!mIsUpdateNerve) {
        return;
    }

    updateNerve();
}

bool GalaxyCometState::isLand() const {
    return isNerve(GET_NERVE_ANON(GalaxyCometStateLand));
}

bool GalaxyCometState::isHide() const {
    return isNerve(GET_NERVE_ANON(GalaxyCometStateHide));
}

void GalaxyCometState::forceToNext() {
    if (isLand()) {
        setNerve(GET_NERVE_ANON(GalaxyCometStateHide));
    } else {
        setNerve(GET_NERVE_ANON(GalaxyCometStateLand));
    }
}

GalaxyCometState::State GalaxyCometState::getStateIndex() const {
    return isLand() ? State_Land : State_Hide;
}

s32 GalaxyCometState::getPastSecond() const {
    return 0;
}

void GalaxyCometState::setStateAndPastSecond(s32 state, u16 /*pastSecond*/) {
    if (state == State_Hide) {
        setNerve(GET_NERVE_ANON(GalaxyCometStateHide));
    } else if (state == State_Land) {
        setNerve(GET_NERVE_ANON(GalaxyCometStateLand));
    }
}

void GalaxyCometState::exeHide() {
}

void GalaxyCometState::exeLand() {
}
