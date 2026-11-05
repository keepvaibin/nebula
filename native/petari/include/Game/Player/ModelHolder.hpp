#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>

class ModelHolder : public LiveActor {
public:
    ModelHolder(const char*, bool);

    void updateMtx(MtxPtr);
};
