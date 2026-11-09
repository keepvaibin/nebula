#pragma once

#include "Game/Animation/AnmPlayer.hpp"

class BtkPlayer : public MaterialAnmPlayerBase {
public:
    BtkPlayer(const ResourceHolder*, ModelData*);

    virtual void apply(Clip*, Model*) override;
};
