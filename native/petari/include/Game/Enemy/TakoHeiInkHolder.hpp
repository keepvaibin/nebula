#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include <nebula_geometry.hpp>

class TakoHeiInk;

class TakoHeiInkHolder : public DeriveActorGroup< TakoHeiInk > {
public:
    TakoHeiInkHolder();
};

namespace MR {
    void createTakoHeiInkHolder();
    bool spurtTakoHeiInk(const Vec3f&, const Vec3f&);
};  // namespace MR
