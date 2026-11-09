#pragma once

#include "Game/Animation/AnmPlayer.hpp"

class BrkPlayer : public MaterialAnmPlayerBase {
public:
    BrkPlayer(const ResourceHolder*, ModelData*);

    virtual void apply(Clip*, Model*) override;
};
