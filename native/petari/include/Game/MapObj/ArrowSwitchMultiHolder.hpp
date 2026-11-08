#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"

class ArrowSwitchMulti;
class ArrowSwitchTarget;
class PlacementId;

class ArrowSwitchMultiHolder : public DeriveActorGroup< ArrowSwitchMulti > {
public:
    ArrowSwitchMultiHolder();

    ArrowSwitchMulti* findSwitch(const PlacementId*);
};

namespace MR {
    void createArrowSwitchMultiHolder();
    void registerArrowSwitchMulti(ArrowSwitchMulti*);
    void registerArrowSwitchTarget(ArrowSwitchTarget*);
};  // namespace MR
