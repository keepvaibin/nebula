#pragma once

#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"

class AreaObj;

class AreaObjFollower : public BaseMatrixFollower {
public:
    AreaObjFollower(AreaObj*, const JMapInfoIter&);

    virtual void update();

    TPos3f mFollowMtx;
    AreaObj* mObj;
};

namespace MR {
    bool addBaseMatrixFollowerAreaObj(AreaObj*, const JMapInfoIter&);
};  // namespace MR
