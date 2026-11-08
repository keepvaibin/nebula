#pragma once

#include "Game/Screen/LayoutActor.hpp"

class SurfingGuidance : public LayoutActor {
public:
    SurfingGuidance();

    virtual void init(const TableIter&);

    void activate();
    void levelOffReady();
    void levelOffHold();
    void levelOffSuccess();
    void turnLeftReady();
    void turnLeftHold();
    void turnLeftSuccess();
    void turnRightReady();
    void turnRightHold();
    void turnRightSuccess();
    void deactivate();
    void exeFadeIn();
    void exeLevelOffReady();
    void exeLevelOffHold();
    void exeLevelOffSuccess();
    void exeTurnLeftReady();
    void exeTurnLeftHold();
    void exeTurnLeftSuccess();
    void exeTurnRightReady();
    void exeTurnRightHold();
    void exeTurnRightSuccess();
    void exeFadeOut();
};
