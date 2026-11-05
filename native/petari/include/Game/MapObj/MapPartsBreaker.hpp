#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"
#include <nebula_geometry.hpp>

class MapPartsBreaker : MapPartsFunction {
public:
    void exeWait();
    void exeReactToTornado();
    void exeBreak();

    u32 _18;
    Vec3f _1C;
    Vec3f _28;
    f32 _34;
    u8 _38;
};
