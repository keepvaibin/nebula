#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class SunshadeMapHolder : public NameObj {
public:
    /// @brief Creates a new `SunshadeMapHolder`.
    SunshadeMapHolder();

    Vec3f _C;
};

namespace MR {
    bool isInShadeFromTheSun(const Vec3f&, f32);
    void createSunshadeMapHolder();
};  // namespace MR
