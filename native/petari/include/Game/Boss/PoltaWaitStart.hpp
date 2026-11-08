#pragma once

#include "Game/Boss/PoltaActionBase.hpp"

class PoltaWaitStart : public PoltaActionBase {
public:
    PoltaWaitStart(Polta*);

    virtual void appear();
};
