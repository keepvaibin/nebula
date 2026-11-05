#pragma once

#include "Game/Animation/AnmPlayer.hpp"

class BpkPlayer : public MaterialAnmPlayerBase {
public:
    BpkPlayer(const ResourceHolder*, J3DModelData*);

    virtual void apply(J3DAnmBase*, J3DModel*);
};
