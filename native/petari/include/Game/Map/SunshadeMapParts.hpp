#pragma once

#include "Game/MapObj/GeneralMapParts.hpp"

class SunshadeMapParts : public GeneralMapParts {
public:
    SunshadeMapParts(const char*);

    virtual void init(const TableIter&);
    virtual void calcAnim();

    CollisionParts* mCollision;
};

namespace MR {
    NameObj* createSunshadeMapParts(const char*);
};  // namespace MR
