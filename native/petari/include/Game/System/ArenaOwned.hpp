#pragma once

#include "Game/System/NebulaList.hpp"

class NebulaArena;

// An object the arena holding its memory destroys when that arena is disposed of.
class ArenaOwned : public NebulaListLink< ArenaOwned > {
public:
    ArenaOwned();
    virtual ~ArenaOwned();

    NebulaArena* mArena;
};
