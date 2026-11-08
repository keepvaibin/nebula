#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include <nebula_geometry.hpp>

class KabokuriFire;

class KabokuriFireHolder : public DeriveActorGroup< KabokuriFire > {
public:
    KabokuriFireHolder();
};

namespace KabokuriFireHolderFunc {
    void createHolder();
    KabokuriFire* getDeadMember();
    bool generateFire(const Vec3f&, const Vec3f&);
    bool generateFire(const Vec3f&, const Vec3f&, bool);
};  // namespace KabokuriFireHolderFunc
