#pragma once

#include "Game/MapObj/InvisiblePolygonObj.hpp"

class TransparentWall : public InvisiblePolygonObj {
public:
    TransparentWall(const char*);

    virtual ~TransparentWall();
    virtual void init(const TableIter&);
    virtual void control();

    s32 mPassFlags;
    bool mIsPassable;
};
