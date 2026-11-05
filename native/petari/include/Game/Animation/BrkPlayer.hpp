#pragma once

#include "Game/Animation/AnmPlayer.hpp"

class BrkPlayer : public MaterialAnmPlayerBase {
public:
    BrkPlayer(const ResourceHolder*, J3DModelData*);

    virtual void apply(J3DAnmBase*, J3DModel*);
};
