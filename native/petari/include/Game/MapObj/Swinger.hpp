#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class Swinger {
public:
    Swinger(const Vec3f*, MtxPtr, f32, f32, f32, const Vec3f*);

    void update();
    void accel(const Vec3f&);
    void updateSwingMtx(const Vec3f&);

    const Vec3f* _0;
    Pos3f* _4;
    Vec3f _8;
    Vec3f mAcceleration;
    Vec3f _20;
    f32 _2C;
    f32 _30;
    f32 _34;
    const Vec3f* _38;
    Vec3f _3C;
    Vec3f _48;
    Vec3f _54;
    Pos3f _60;
};
