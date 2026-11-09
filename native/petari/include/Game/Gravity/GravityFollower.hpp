#pragma once

#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"

class GlobalGravityObj;
class PlanetGravity;

class GravityFollower : public BaseMatrixFollower {
public:
    GravityFollower(GlobalGravityObj* pGravityObj, const TableIter& rIter);

    virtual void setGravityFollowHost(const NameObj* pHost) override;
    virtual void update() override;

    PlanetGravity* mGravity;
};

namespace MR {
    bool addBaseMatrixFollowerGravity(GlobalGravityObj* pGravityObj, const TableIter& rIter);
};  // namespace MR
