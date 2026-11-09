#pragma once

#include "Game/Animation/AnmPlayer.hpp"

class BpkPlayer : public MaterialAnmPlayerBase {
public:
    BpkPlayer(const ResourceHolder*, ModelData*);

    virtual void apply(Clip*, Model*) override;
};
