#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"

class PoltaGroundRock;

class PoltaGroundRockHolder : public DeriveActorGroup< PoltaGroundRock > {
public:
    PoltaGroundRockHolder();

    void breakAll();
};
