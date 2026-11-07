#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"

class ArrowSwitchMulti;
class ArrowSwitchTarget;
class PlacementId;

class ArrowSwitchMultiHolder : public DeriveActorGroup< ArrowSwitchMulti > {
public:
    /// @brief Creates a new `ArrowSwitchMultiHolder`.
    ArrowSwitchMultiHolder();

    ArrowSwitchMulti* findSwitch(const PlacementId*);
};

namespace MR {
    void createArrowSwitchMultiHolder();
    void registerArrowSwitchMulti(ArrowSwitchMulti*);
    void registerArrowSwitchTarget(ArrowSwitchTarget*);
};  // namespace MR
