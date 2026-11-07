#pragma once

#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"

class AreaObj;

class AreaObjFollower : public BaseMatrixFollower {
public:
    AreaObjFollower(AreaObj*, const TableIter&);

    virtual void update();

    Pos3f mFollowMtx;
    AreaObj* mObj;
};

namespace MR {
    bool addBaseMatrixFollowerAreaObj(AreaObj*, const TableIter&);
};  // namespace MR
