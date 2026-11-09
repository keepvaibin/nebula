#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class DeathArea : public AreaObj {
public:
    // DeathArea's arg 0 in the stage data: 0 kills anyone, 1 anyone but the player; the code names only 0.
    enum EDeathType { DeathType_Any, DeathType_AnyNoPlayer };

    DeathArea(int, const char*);

    virtual void init(const TableIter&);
    virtual void movement();

    virtual bool isInVolume(const Vec3f&) const;

    virtual const char* getManagerName() const {
        return "DeathArea";
    }

    s32 getDeathType() const;
};
