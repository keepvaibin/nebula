#pragma once

#include "Game/Animation/AnmPlayer.hpp"

class BtpPlayer : public MaterialAnmPlayerBase {
public:
    BtpPlayer(const ResourceHolder*, J3DModelData*);

    virtual void apply(J3DAnmBase*, J3DModel*);
};
