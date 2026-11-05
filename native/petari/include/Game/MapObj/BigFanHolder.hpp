#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include <nebula_geometry.hpp>

class BigFan;

class BigFanHolder : public DeriveActorGroup< BigFan > {
public:
    BigFanHolder();

    void calcWindInfo(Vec3f*, const Vec3f&, f32*);
};

class BigFanFunction {
public:
    static void createBigFanHolder();
    static void calcWindInfo(Vec3f*, const Vec3f&, f32*);
    static void registerBigFan(BigFan*);
};
