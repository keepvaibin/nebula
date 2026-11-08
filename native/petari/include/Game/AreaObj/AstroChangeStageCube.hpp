#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class AstroChangeStageCube : public AreaObj {
public:
    AstroChangeStageCube(int, const char*);

    virtual void init(const TableIter&);
    virtual void movement();
    virtual bool isInVolume(const Vec3f&) const;

    u32 mDestNo;
    s32 mWaitFrame;
};
