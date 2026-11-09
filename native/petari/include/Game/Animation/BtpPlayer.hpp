#pragma once

#include "Game/Animation/AnmPlayer.hpp"

class BtpPlayer : public MaterialAnmPlayerBase {
public:
    BtpPlayer(const ResourceHolder*, ModelData*);

    virtual void apply(Clip*, Model*) override;
};
