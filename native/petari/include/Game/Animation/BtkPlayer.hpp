#pragma once

#include "Game/Animation/AnmPlayer.hpp"

class BtkPlayer : public MaterialAnmPlayerBase {
public:
    BtkPlayer(const ResourceHolder*, J3DModelData*);

    virtual void apply(J3DAnmBase*, J3DModel*);
};
