#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class AstroChangeStageCube : public AreaObj {
public:
    AstroChangeStageCube(int, const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;
    virtual bool isInVolume(const Vec3f&) const override;

    u32 mDestNo;
    s32 mWaitFrame;
};
